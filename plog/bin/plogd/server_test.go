// Copyright 2018 Schibsted

package main

import (
	"bytes"
	"io/ioutil"
	"net/http"
	"net/url"
	"os"
	"testing"

	"github.com/schibsted/sebase/plog/internal/pkg/plogproto"
)

type testWriter struct {
	headers http.Header
	code    int
	bytes.Buffer
}

func (w *testWriter) Header() http.Header {
	if w.headers == nil {
		w.headers = make(http.Header)
	}
	return w.headers
}

func (w *testWriter) WriteHeader(code int) {
	w.code = code
}

func TestServerQuery(t *testing.T) {
	sconn := testConnect()
	defer sconn.Close()
	sId := hello(t, sconn, plogproto.CtxType_state, 0, "teststate")
	dataStore.testStatePing = make(chan struct{})
	sconn.SendKeyValue(sId, "test", []byte(`"fest"`))
	<-dataStore.testStatePing
	dataStore.testStatePing = nil

	s := &server{dataStore: &dataStore}
	w := testWriter{}
	req := http.Request{URL: &url.URL{Path: "/state/teststate"}}
	s.queryState(&w, &req)

	if w.Buffer.String() != `{"value":{"test":"fest"}}` {
		t.Errorf("Expected %v, got %v, code %v", `{"value":{"test":"fest"}}`, w.Buffer.String(), w.code)
	}

	req.URL.Path = "/state/teststate.test"
	w = testWriter{}
	s.queryState(&w, &req)

	if w.Buffer.String() != `{"value":"fest"}` {
		t.Errorf("Expected %v, got %v, code %v", `{"value":"fest"}`, w.Buffer.String(), w.code)
	}

	goodbye(t, sconn, sId)
	checkLog(t, "teststate", "state", `{"test":"fest"}`)

	w.Buffer.Reset()
	w.code = 0
	s.queryState(&w, &req)

	if w.code != 404 {
		t.Errorf("Expected http status 404, got %v", w.code)
	}
}

func TestRotate(t *testing.T) {
	f, err := ioutil.TempFile("", "plogd-test")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	defer os.Remove(f.Name())
	jf := &jsonFileWriter{path: f.Name()}
	s := &server{fileWriter: jf}
	req := http.Request{URL: &url.URL{Path: "/rotate"}}
	w := testWriter{}
	s.rotate(&w, &req)

	if w.code != 200 && w.code != 0 { // 0 => unset, defaults to 200
		t.Errorf("Got bad HTTP status %d", w.code)
	}
	if jf.w == nil || jf.Closer == nil {
		t.Errorf("Writer or Closer were unset. Got %#v", jf)
	}
	defer jf.Close()
	finfo, err := f.Stat()
	if err != nil {
		t.Fatal(err)
	}
	jfinfo, err := jf.w.(*os.File).Stat()
	if err != nil {
		t.Fatal(err)
	}
	if !os.SameFile(finfo, jfinfo) {
		t.Errorf("Rotate opened wrong file. Got %#v", jfinfo)
	}
}
