package main

import (
	"context"
	"encoding/json"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"text/template"
	"time"

	"github.com/olivere/elastic/v7"
	"github.com/olivere/elastic/v7/config"
	"github.com/schibsted/sebase/plog/pkg/plogd"
	"github.com/schibsted/sebase/util/pkg/slog"
)

// NewOutput is the plugin entrypoint creating the output writer. The query
// string is passed on to config.Parse except for these keys:
//
// url: Used as the Elasticsearch URL in config.Parse call.
//
// nofail: If true, failing the initial connection to elasticsearch is
// nonfatal. It will be logged and tried again later.
//
// index-template: If set, defines a golang template to use to generate the
// index name. Gets the plogd.LogMessage struct as input.
//
// index-time-layout: If set, this is passed to time.Time.Format using the
// message timestamp. The value is then appended to the index name.
// If index, index-template and index-time-layout are all empty then the
// latter is set to "plog-2006.01.02".
//
// string-key, number-key, null-key, bool-key, array-key, object-key:
// Since Elasticsearch might auto-create indexes for messages using the
// same key for different type of values can create conflicts. Therefore
// the JSON type is used as the default key ("string", "number", etc.) These
// keys can be used to change the defaults.
//
// flatten-objects: If true, objects are not logged using the object-key
// but rather flattened into the top level object. The envelope keys will be
// part of the same object. Defaults to false.
func NewOutput(qs url.Values, fragment string) (plogd.OutputWriter, error) {
	var err error
	w := defaultWriter
	esurl, err := url.Parse(qs.Get("url"))
	if err != nil {
		return nil, err
	}
	delete(qs, "url")

	if err := w.configure(qs); err != nil {
		return nil, err
	}

	esurl.RawQuery = qs.Encode()
	w.conf, err = config.Parse(esurl.String())
	if err != nil {
		return nil, err
	}
	if w.conf.Index == "" && w.indexTemplate == nil && w.indexTimeLayout == "" {
		w.indexTimeLayout = "plog-2006.01.02"
	}

	err = w.initClient()
	if err != nil {
		if !w.nofail {
			return nil, err
		}
		slog.Error("Failed initial connection to Elasticsearch", "error", err)
		w.client = nil
	}
	return &w, nil
}

func (w *OutputWriter) initClient() error {
	var err error
	w.client, err = elastic.NewClientFromConfig(w.conf)
	if err != nil {
		return err
	}
	if w.conf.Errorlog == "" {
		elastic.SetErrorLog(slog.Error)(w.client)
	}
	version, err := w.client.ElasticsearchVersion(w.conf.URL)
	if err != nil {
		return err
	}
	varr := strings.Split(version, ".")
	w.versionMajor, _ = strconv.Atoi(varr[0])
	var ctx context.Context
	ctx, w.cancel = context.WithCancel(context.Background())
	// Use StopBackoff to avoid hang in commit. The bulk processor
	// will still wait and retry, but with the stop channel active.
	w.bulker, err = elastic.NewBulkProcessorService(w.client).FlushInterval(1 * time.Second).Backoff(elastic.StopBackoff{}).Do(ctx)
	if err != nil {
		w.cancel()
		w.cancel = nil
	}
	return err
}

// OutputWriter is a plogd output writer for Elasticsearch
type OutputWriter struct {
	conf                          *config.Config
	indexTemplate                 *template.Template
	indexTimeLayout               string
	flattenObjects                bool
	stringKey, numberKey, nullKey string
	boolKey, arrayKey, objectKey  string

	nofail    bool
	failQueue []plogd.LogMessage

	connectLock  sync.Mutex
	client       *elastic.Client
	bulker       *elastic.BulkProcessor
	cancel       context.CancelFunc
	versionMajor int
}

var defaultWriter = OutputWriter{
	stringKey: "string",
	numberKey: "number",
	nullKey:   "null",
	boolKey:   "bool",
	arrayKey:  "array",
	objectKey: "object",
}

// configure parses the local keys from qs as specified in the NewOutputWriter
// documentation, except for url which is handled there only.
func (w *OutputWriter) configure(qs url.Values) error {
	for k, ptr := range map[string]*string{
		"string-key": &w.stringKey,
		"number-key": &w.numberKey,
		"null-key":   &w.nullKey,
		"bool-key":   &w.boolKey,
		"array-key":  &w.arrayKey,
		"object-key": &w.objectKey,
	} {
		if qs.Get(k) != "" {
			*ptr = qs.Get(k)
		}
		delete(qs, k)
	}

	if qs.Get("flatten-objects") != "" {
		var err error
		w.flattenObjects, err = strconv.ParseBool(qs.Get("flatten-objects"))
		if err != nil {
			return err
		}
	}
	delete(qs, "flatten-objects")

	tmpl := qs.Get("index-template")
	delete(qs, "index-template")
	if tmpl != "" {
		var err error
		w.indexTemplate, err = template.New("index").Parse(tmpl)
		if err != nil {
			return err
		}
	}
	w.indexTimeLayout = qs.Get("index-time-layout")
	delete(qs, "index-time-layout")

	if v := qs.Get("nofail"); v != "" {
		var err error
		w.nofail, err = strconv.ParseBool(v)
		if err != nil {
			return err
		}
	}
	delete(qs, "nofail")

	return nil
}

// WriteMessage queues the message in the Elasticsearch processor which runs
// asynchronously. If there isn't any connection to elasticsearch yet because
// of nofail, the message is queued in the writer instead waiting for the
// connection to succeed.
func (w *OutputWriter) WriteMessage(ctx context.Context, message plogd.LogMessage) {
	if w.client == nil {
		var err error
		w.connectLock.Lock()
		if w.client == nil {
			err = w.initClient()
		}
		w.connectLock.Unlock()
		if err != nil {
			w.failQueue = append(w.failQueue, message)
			slog.CtxError(ctx, "Connecting to Elasticsearch failed", "error", err)
			return
		}
		for _, msg := range w.failQueue {
			w.WriteMessage(ctx, msg)
		}
		w.failQueue = nil
	}
	req := elastic.NewBulkIndexRequest()
	req.Index(w.index(ctx, &message))
	if w.versionMajor < 7 {
		req.Type("_doc")
	}
	req.Doc(w.message(ctx, &message))

	w.bulker.Add(req)
}

// index determines the index name to use based on config and message.
func (w *OutputWriter) index(ctx context.Context, message *plogd.LogMessage) string {
	var index strings.Builder
	if w.indexTemplate != nil {
		err := w.indexTemplate.Execute(&index, &message)
		if err != nil {
			slog.CtxError(ctx, "Failed to run index template", "error", err)
			index.Reset()
			index.WriteString(w.conf.Index)
		}
	} else {
		index.WriteString(w.conf.Index)
	}
	if w.indexTimeLayout != "" {
		index.WriteString(message.Timestamp.Format(w.indexTimeLayout))
	}
	return index.String()
}

// message determins the message to use based on config and input message.
func (w *OutputWriter) message(ctx context.Context, message *plogd.LogMessage) map[string]interface{} {
	msg := message.ToMap()
	msgkey := "message"
	switch payload := msg["message"].(type) {
	case json.RawMessage:
		if len(payload) == 0 {
			return msg
		}
		switch payload[0] {
		case '{':
			if w.flattenObjects {
				var m map[string]interface{}
				err := json.Unmarshal(msg["message"].(json.RawMessage), &m)
				if err != nil {
					slog.CtxError(ctx, "Failed to unmarshal message", "error", err)
				}
				return w.flattenObject(msg, m)
			}
			msgkey = w.objectKey
		case '[':
			msgkey = w.arrayKey
		case '"':
			msgkey = w.stringKey
		case 'n':
			msgkey = w.nullKey
		case 't', 'f':
			msgkey = w.boolKey
		default:
			msgkey = w.numberKey
		}
	case map[string]interface{}:
		if w.flattenObjects {
			return w.flattenObject(msg, payload)
		}
		msgkey = w.objectKey
	case []interface{}:
		msgkey = w.arrayKey
	case string:
		msgkey = w.stringKey
	case nil:
		msgkey = w.nullKey
	case bool:
		msgkey = w.boolKey
	default:
		msgkey = w.numberKey
	}
	if msgkey != "message" {
		msg[msgkey] = msg["message"]
		delete(msg, "message")
	}
	return msg
}

// flattenObject flattens msg into m. m is assumed to have come from
// msg["message"] which is skipped.
func (w *OutputWriter) flattenObject(msg, m map[string]interface{}) map[string]interface{} {
	if m == nil {
		return msg
	}
	for k, v := range msg {
		if k == "message" {
			continue
		}
		m[k] = v
	}
	return m
}

// Close flushes and stops the writer bulk processor.
func (w *OutputWriter) Close() error {
	w.connectLock.Lock()
	defer w.connectLock.Unlock()
	if w.bulker == nil {
		return nil
	}
	w.bulker.Flush()
	w.cancel()
	return w.bulker.Close()
}
