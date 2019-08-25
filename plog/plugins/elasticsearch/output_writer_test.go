package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"reflect"
	"testing"
	"time"

	"github.com/schibsted/sebase/plog/pkg/plogd"
	"github.com/schibsted/sebase/util/pkg/slog"
)

type TestBulkAction struct {
	Index *TestBulkIndex
}

type TestBulkIndex struct {
	Index string `json:"_index"`
	Type  string `json:"_type"`
}

func TestOutputWriter(t *testing.T) {
	expectedAction := &TestBulkAction{
		Index: &TestBulkIndex{
			Index: time.Now().Format("plog-2006.01.02"),
			Type:  "_doc",
		},
	}
	ts := time.Now().Truncate(time.Second)
	expectedDoc := map[string]interface{}{
		"@timestamp": ts.Format(time.RFC3339),
		"prog":       "testprog",
		"type":       "test",
		"string":     "test-msg",
	}
	bulkdone := make(chan struct{})
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/":
			io.WriteString(w, `{}`)
		case "/_bulk":
			decoder := json.NewDecoder(r.Body)
			var a TestBulkAction
			if err := decoder.Decode(&a); err != nil {
				http.Error(w, err.Error(), 500)
				return
			}
			if !reflect.DeepEqual(&a, expectedAction) {
				t.Errorf("Bad action: %+v", a.Index)
			}
			var doc map[string]interface{}
			if err := decoder.Decode(&doc); err != nil {
				http.Error(w, err.Error(), 500)
				return
			}
			if !reflect.DeepEqual(doc, expectedDoc) {
				t.Errorf("Bad doc: %#v != %#v", doc, expectedDoc)
			}
			fmt.Fprintf(w, `
{"took":543,"errors":false,"items":[{"index":{"_index":"%s","_type":"_doc","_id":"CfGkrWwB6nj95KrkBboj","_version":1,"result":"created","_shards":{"total":2,"successful":1,"failed":0},"_seq_no":0,"_primary_term":1,"status":201}}]}
`, a.Index.Index)
			close(bulkdone)
		default:
			panic(r)
		}
	}))
	defer srv.Close()

	qs := make(url.Values)
	qs.Set("url", srv.URL)
	if testing.Verbose() {
		qs.Set("tracelog", "/dev/stderr")
	}
	qs.Set("sniff", "false")
	ow, err := NewOutput(qs, "")
	if err != nil {
		t.Fatal(err)
	}
	defer ow.Close()
	w := ow.(*OutputWriter)

	w.WriteMessage(slog.Error, plogd.LogMessage{
		Timestamp: ts,
		Prog:      "testprog",
		Type:      "test",
		Message:   "test-msg",
	})
	<-bulkdone
}
