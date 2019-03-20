// Copyright 2018 Schibsted

package etcd

import (
	"context"
	"crypto/tls"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"reflect"
	"testing"
	"time"

	"github.com/schibsted/sebase/core/pkg/sd/sdr"
	"github.com/schibsted/sebase/util/pkg/slog"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

const (
	getReply = `{
	"action":"get",
	"node":{
		"dir":true,
		"nodes":[{
			"key":"/service",
			"dir":true,
			"nodes":[{
				"key":"/service/search",
				"dir":true,
				"nodes":[{
					"key":"/service/search/asearch",
					"dir":true,
					"nodes":[{
						"key":"/service/search/asearch/foo",
						"dir":true,
						"expiration":"2015-09-03T15:42:25.907485522Z",
						"ttl":25,
						"nodes":[{
							"key":"/service/search/asearch/foo/config",
							"value":"*.*.cost=1000\n*.*.name=::1\n*.*.port=8081\nlocal.*.cost=2\n",
							"modifiedIndex":4431,
							"createdIndex":4431
						},{
							"key":"/service/search/asearch/foo/health",
							"value":"up",
							"modifiedIndex":4371,
							"createdIndex":4371
						}],
						"modifiedIndex":4369,
						"createdIndex":4369
					}],
					"modifiedIndex":9,
					"createdIndex":9
				}],
				"modifiedIndex":5,
				"createdIndex":5
			}],
			"modifiedIndex":5,
			"createdIndex":5
		}]
	}
}`
	delReply = `{
	"action":"delete",
	"node":{
		"key":"/service/search/asearch/foo",
		"expiration":"2015-09-03T15:42:25.907485522Z",
		"modifiedIndex":5,
		"createdIndex":5
	}
}`
)

func init() {
	slog.Debug.SetLogPrintf()
}

type handler struct {
	tb        testing.TB
	method    string
	path      string
	query     string
	reply     string
	reported  bool
	waitCh    chan struct{}
	single500 bool
}

func (h *handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if h.single500 {
		h.single500 = false
		http.Error(w, "Temp error", 500)
		return
	}
	if !h.reported {
		if r.Method != h.method {
			h.tb.Errorf("Method mismatch, %s != %s", r.Method, h.method)
			h.reported = true
		}
		if r.URL.Path != h.path {
			h.tb.Errorf("Path mismatch, %s != %s", r.URL.Path, h.path)
			h.reported = true
		}
		if r.URL.RawQuery != h.query {
			h.tb.Errorf("Query string mismatch, %s != %s", r.URL.RawQuery, h.query)
			h.reported = true
		}
		// Reset to default watch query.
		h.method = "GET"
		h.path = "/v2/keys/service/"
		h.query = "recursive=true&wait=true&waitIndex=4432"
	}

	if len(r.URL.Query()["wait"]) > 0 {
		select {
		case <-h.waitCh:
		case <-time.After(5 * time.Second):
			h.tb.Errorf("Timed out waiting to send reply.")
		}
	}

	io.WriteString(w, h.reply)
}

func waitForIt(tb testing.TB, conn sdr.SourceConn, msgs []sdr.Message) {
	for _, expected := range msgs {
		select {
		case got := <-conn.Channel():
			if !reflect.DeepEqual(got, expected) {
				tb.Errorf("Bad message, %#v != %#v", got, expected)
			}
		case <-time.After(3 * time.Second):
			tb.Errorf("Timed out waiting for %#v", expected)
			return
		}
	}
}

func TestConnRefused(t *testing.T) {
	conf := &bconf.Node{}
	conf.Add("sd.etcd_url")("http://localhost:2")
	watcher, err := (&etcdSourceType{}).SdrSourceSetup(conf, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer watcher.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 1*time.Second)
	defer cancel()
	_, err = watcher.Connect(ctx, "search/asearch", nil)
	if err != context.DeadlineExceeded {
		t.Fatal(err)
	}
}

func TestTlsError(t *testing.T) {
	l, err := net.Listen("tcp", ":0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	tl := tls.NewListener(l, &tls.Config{})
	go func() {
		for {
			c, err := tl.Accept()
			if err != nil {
				return
			}
			c.(*tls.Conn).Handshake()
			c.Close()
		}
	}()
	defer tl.Close()

	conf := &bconf.Node{}
	conf.Add("sd.etcd_url")("https://" + l.Addr().String())
	watcher, err := (&etcdSourceType{}).SdrSourceSetup(conf, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer watcher.Close()

	_, err = watcher.Connect(context.TODO(), "search/asearch", nil)
	if _, ok := err.(*url.Error); !ok {
		t.Fatal(err)
	}
}

func TestUpdateDelete(t *testing.T) {
	h := &handler{
		tb:     t,
		method: "GET",
		path:   "/v2/keys/service/search/asearch",
		query:  "recursive=true",
		reply:  getReply,
		waitCh: make(chan struct{}),
	}
	s := httptest.NewServer(h)
	defer s.Close()
	defer close(h.waitCh)

	conf := &bconf.Node{}
	conf.Add("sd.etcd_url")("http://" + s.Listener.Addr().String())
	watcher, err := (&etcdSourceType{}).SdrSourceSetup(conf, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer watcher.Close()

	// Error on the initial GET to check for retries.
	h.single500 = true
	conn, err := watcher.Connect(context.TODO(), "search/asearch", nil)
	if err != nil {
		t.Fatal(err)
	}

	waitForIt(t, conn, []sdr.Message{
		{Index: 4431, Type: sdr.Update, HostKey: "foo", Key: "config", Value: "*.*.cost=1000\n*.*.name=::1\n*.*.port=8081\nlocal.*.cost=2\n"},
		{Index: 4431, Type: sdr.Update, HostKey: "foo", Key: "health", Value: "up"},
		{Index: 4431, Type: sdr.EndOfBatch},
	})

	h.reply = delReply
	h.waitCh <- struct{}{}

	waitForIt(t, conn, []sdr.Message{
		{Index: 4431, Type: sdr.Delete, HostKey: "foo"},
		{Index: 4431, Type: sdr.EndOfBatch},
	})
}
