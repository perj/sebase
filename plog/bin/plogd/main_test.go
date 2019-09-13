// Copyright 2018 Schibsted

package main

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"runtime"
	"sync/atomic"
	"testing"
	"time"

	"github.com/schibsted/sebase/plog/internal/pkg/plogproto"
)

type testConn struct {
	r io.ReadCloser
	d *json.Decoder
	w io.WriteCloser
}

func (conn testConn) Close() {
	if conn.r != nil {
		conn.r.Close()
	}
	conn.w.Close()
}

var sessionStore SessionStorage
var dataStore DataStorage
var logScanner *bufio.Scanner
var ctxId uint64

func init() {
	logR, logW := io.Pipe()
	dataStore.Output = filterOutput(&ioWriter{logW}, "subprog")
	logScanner = bufio.NewScanner(logR)
}

func testConnect() *plogproto.Writer {
	inputR, inputW := io.Pipe()

	go func() {
		r := plogproto.NewReader(inputR, true)
		handleConnection(context.TODO(), &sessionStore, &dataStore, r)
		inputR.Close()
	}()

	return plogproto.NewWriter(inputW, true)
}

func checkFatal(t testing.TB, err error) {
	if err != nil {
		t.Fatal(err)
	}
}

func expectLog(t testing.TB, expect []string) {
	for _, expected := range expect {
		if !logScanner.Scan() {
			t.Fatal(logScanner.Err())
		}
		if logScanner.Text() != expected {
			_, file, line, _ := runtime.Caller(2)
			t.Error(file, ":", line, ":", logScanner.Text(), "!=", expected)
		}
	}
}

func checkLog(t testing.TB, prog, key, value string) {
	expectLog(t, []string{"---", prog, key, value})
}

func checkLogAnyValue(t *testing.T, prog, level string, values []string) int {
	expectLog(t, []string{"---", prog, level})

	if !logScanner.Scan() {
		t.Fatal(logScanner.Err())
	}
	v := logScanner.Text()

	for idx, candidate := range values {
		if v == candidate {
			return idx
		}
	}
	_, file, line, _ := runtime.Caller(1)
	t.Error(file, ":", line, ":", logScanner.Text(), "not one of", values)
	return -1
}

func decode(t testing.TB, d *json.Decoder) []byte {
	var msg interface{}
	err := d.Decode(&msg)
	checkFatal(t, err)
	data, err := json.Marshal(msg)
	checkFatal(t, err)
	return data
}

func hello(t testing.TB, conn *plogproto.Writer, ctxtype plogproto.CtxType, pId uint64, appname ...string) uint64 {
	cId := atomic.AddUint64(&ctxId, 1)
	hello := plogproto.OpenContext{Ctxtype: &ctxtype, ParentCtxId: &pId, Key: appname}
	err := conn.SendOpen(cId, &hello)
	if err != nil {
		t.Fatal(err)
	}
	return cId
}

func goodbye(t testing.TB, conn *plogproto.Writer, cId uint64) {
	err := conn.SendClose(cId)
	if err != nil {
		t.Fatal(err)
	}
}

func TestHelloGoodbye(t *testing.T) {
	conn := testConnect()
	defer conn.Close()

	cId := hello(t, conn, plogproto.CtxType_log, 0, "trans")
	goodbye(t, conn, cId)
}

func TestBrokenPipe(t *testing.T) {
	conn := testConnect()
	defer conn.Close()
}

func TestSinglePublish(t *testing.T) {
	conn := testConnect()
	defer conn.Close()

	cId := hello(t, conn, plogproto.CtxType_log, 0, "trans")

	if err := conn.SendKeyValue(cId, "INFO", []byte(`"foo"`)); err != nil {
		t.Fatal(err)
	}
	checkLog(t, "trans", "INFO", `"foo"`)

	goodbye(t, conn, cId)
}

func TestSubprog(t *testing.T) {
	conn := testConnect()
	defer conn.Close()

	cId := hello(t, conn, plogproto.CtxType_log, 0, "test+subtest")

	if err := conn.SendKeyValue(cId, "INFO", []byte(`"foo"`)); err != nil {
		t.Fatal(err)
	}
	expectLog(t, []string{"---", "test", "INFO", `"foo"`, `subprog: "subtest"`})

	goodbye(t, conn, cId)
}

func TestDict(t *testing.T) {
	lconn := testConnect()
	defer lconn.Close()

	lId := hello(t, lconn, plogproto.CtxType_log, 0, "trans")

	tId := hello(t, lconn, plogproto.CtxType_dict, lId, "transaction")

	if err := lconn.SendKeyValue(tId, "cmd", []byte(`"transinfo"`)); err != nil {
		t.Fatal(err)
	}

	goodbye(t, lconn, lId)
	goodbye(t, lconn, tId)

	checkLog(t, "trans", "transaction", `{"cmd":"transinfo"}`)
}

func TestList(t *testing.T) {
	lconn := testConnect()
	defer lconn.Close()

	lId := hello(t, lconn, plogproto.CtxType_log, 0, "trans")

	tId := hello(t, lconn, plogproto.CtxType_list, lId, "connections")

	pub := plogproto.PlogMessage{Value: []byte(`"1"`)}
	if err := lconn.SendMessage(tId, &pub); err != nil {
		t.Fatal(err)
	}
	pub.Value = []byte(`"2"`)
	if err := lconn.SendMessage(tId, &pub); err != nil {
		t.Fatal(err)
	}

	goodbye(t, lconn, lId)
	goodbye(t, lconn, tId)

	checkLog(t, "trans", "connections", `["1","2"]`)
}

func TestTransaction(t *testing.T) {
	lconn := testConnect()
	defer lconn.Close()

	lId := hello(t, lconn, plogproto.CtxType_log, 0, "trans")

	if err := lconn.SendKeyValue(lId, "INFO", []byte(`"incoming connection"`)); err != nil {
		t.Fatal(err)
	}
	checkLog(t, "trans", "INFO", `"incoming connection"`)

	tId := hello(t, lconn, plogproto.CtxType_dict, lId, "transaction")

	lconn.SendKeyValue(tId, "remote_addr", []byte(`"::1"`))
	lconn.SendKeyValue(tId, "control", []byte(`true`))
	lconn.SendKeyValue(tId, "tptr", []byte(`"0xba7390"`))

	tdId := hello(t, lconn, plogproto.CtxType_list, tId, "debug")

	iId := hello(t, lconn, plogproto.CtxType_list, tId, "input")
	lconn.SendKeyValue(iId, "", []byte(`"cmd:transaction"`))
	lconn.SendKeyValue(iId, "", []byte(`"backends:1"`))
	lconn.SendKeyValue(iId, "", []byte(`"commit:1"`))
	lconn.SendKeyValue(iId, "", []byte(`"end"`))
	goodbye(t, lconn, iId)

	lconn.SendKeyValue(tdId, "", []byte(`"verify_parameters: phase 0, pending 0"`))
	lconn.SendKeyValue(tdId, "", []byte(`"starting validator v_bool for transinfo"`))
	lconn.SendKeyValue(tdId, "", []byte(`"ending validator for transinfo"`))

	lconn.SendKeyValue(tId, "info", []byte(`["Logging temporarily disabled."]`))

	goodbye(t, lconn, tdId)
	goodbye(t, lconn, tId)

	// Looks like dicts are sorted, so this should work. Not that pretty though.
	checkLog(t, "trans", "transaction", `{"control":true,"debug":["verify_parameters: phase 0, pending 0","starting validator v_bool for transinfo","ending validator for transinfo"],"info":["Logging temporarily disabled."],"input":["cmd:transaction","backends:1","commit:1","end"],"remote_addr":"::1","tptr":"0xba7390"}`)

	lconn.SendKeyValue(lId, "INFO", []byte(`"command thread event dispatch exited because events depleted"`))
	checkLog(t, "trans", "INFO", `"command thread event dispatch exited because events depleted"`)

	goodbye(t, lconn, lId)
}

func TestSimpleState(t *testing.T) {
	sconn := testConnect()
	defer sconn.Close()
	sId := hello(t, sconn, plogproto.CtxType_state, 0, "trans")

	sconn.SendKeyValue(sId, "test", []byte(`"fest"`))
	sconn.SendKeyValue(sId, "test", []byte(`"new"`))

	goodbye(t, sconn, sId)

	checkLog(t, "trans", "state", `{"test":"new"}`)
}

func TestDeepState(t *testing.T) {
	sconn := testConnect()
	defer sconn.Close()
	sId := hello(t, sconn, plogproto.CtxType_state, 0, "trans")

	sconn.SendKeyValue(sId, "test", []byte(`{"a":"b"}`))
	sconn.SendKeyValue(sId, "test", []byte(`{"b":["c"], "c":"d"}`))
	sconn.SendKeyValue(sId, "test", []byte(`{"b":"e"}`))
	sconn.SendKeyValue(sId, "test", []byte(`{"a":null}`))
	sconn.SendKeyValue(sId, "rest", []byte(`"nope"`))
	sconn.SendKeyValue(sId, "fest", []byte(`"yep"`))
	sconn.SendKeyValue(sId, "rest", []byte(`null`))

	goodbye(t, sconn, sId)

	checkLog(t, "trans", "state", `{"fest":"yep","test":{"b":"e","c":"d"}}`)
}

func TestStateSessionsInterrupted(t *testing.T) {
	sconn := testConnect()
	sId := hello(t, sconn, plogproto.CtxType_state, 0, "trans")

	dId := hello(t, sconn, plogproto.CtxType_dict, sId, "foo")

	sconn.SendKeyValue(dId, "a", []byte(`"b"`))
	// Race condition here, wait for a:b to appear in state.
	done := false
	for !done {
		one := make(chan struct{})
		b, err := dataStore.CallbackState("trans", func(state map[string]interface{}) {
			foo := state["foo"]
			if foo != nil {
				done = foo.(map[string]interface{})["a"] == "b"
			}
			close(one)
		})
		if !b {
			t.Fatal(err)
		}
		<-one
	}
	sconn.SendKeyValue(sId, "foo", []byte(`{"a":"c"}`))

	lId := hello(t, sconn, plogproto.CtxType_list, dId, "bar")

	sconn.SendKeyValue(lId, "", []byte(`"d"`))
	sconn.SendKeyValue(dId, "bar", []byte(`"e"`))

	goodbye(t, sconn, sId)
	sconn.Close()

	// lists overwrite, dicts merge.
	// state propogates immediately so a:c overwrites the previously sent a:b
	checkLog(t, "trans", "state", `{"foo":{"@interrupted":true,"a":"c","bar":["d"]}}`)
}

func TestCounterState(t *testing.T) {
	sconn := testConnect()
	defer sconn.Close()
	sId := hello(t, sconn, plogproto.CtxType_count, 0, "test", "counters", "foo", "bar")

	sconn.SendKeyValue(sId, "name", []byte(`"bar"`))
	sconn.SendKeyValue(sId, "test", []byte(`2`))
	sconn.SendKeyValue(sId, "test", []byte(`1`))
	sconn.SendKeyValue(sId, "test", []byte(`-2`))

	dataStore.dumpEmpty = true

	// There's a race against the writes here. Loop until ok.
	for {
		dataStore.ProgStore["test"].sendState <- ""
		idx := checkLogAnyValue(t, "test", "state", []string{
			`{"counters":{"foo":{"bar":{"name":"bar","test":1}}}}`,
			`{"counters":{"foo":{"bar":{"name":"bar","test":2}}}}`,
			`{"counters":{"foo":{"bar":{"name":"bar","test":3}}}}`,
			`{"counters":{"foo":{"bar":{"name":"bar"}}}}`,
			`{}`,
		})
		if idx <= 0 {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}

	dataStore.dumpEmpty = false

	goodbye(t, sconn, sId)

	checkLog(t, "test", "state", `{"counters":{"foo":{}}}`)
}

type countWriter struct {
	N   int
	cnt int
	ch  chan bool
}

func NewCountWriter(N int) *countWriter {
	return &countWriter{N * 4, 0, make(chan bool)}
}

func (w *countWriter) Write(data []byte) (int, error) {
	w.cnt++
	if w.cnt >= w.N {
		close(w.ch)
	}
	return len(data), nil
}

func BenchmarkLogSimple(b *testing.B) {
	savedOutput := dataStore.Output
	defer func() { dataStore.Output = savedOutput }()
	cw := NewCountWriter(b.N)
	dataStore.Output = &ioWriter{cw}

	conn := testConnect()
	defer conn.Close()

	cId := hello(b, conn, plogproto.CtxType_log, 0, "trans")
	key := "foo"
	pub := &plogproto.PlogMessage{Key: &key, Value: []byte(`"bar"`)}

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		conn.SendMessage(cId, pub)
	}

	<-cw.ch

	b.StopTimer()

	goodbye(b, conn, cId)
}

func BenchmarkLogSearchQuery(b *testing.B) {
	savedOutput := dataStore.Output
	defer func() { dataStore.Output = savedOutput }()
	cw := NewCountWriter(b.N)
	dataStore.Output = &ioWriter{cw}

	lconn := testConnect()
	defer lconn.Close()

	lId := hello(b, lconn, plogproto.CtxType_log, 0, "search")

	S := func(s string) *string { return &s }
	squery := plogproto.Plog{
		Open: &plogproto.OpenContext{Ctxtype: plogproto.CtxType_dict.Enum(), ParentCtxId: &lId, Key: []string{"query"}},
		Msg: []*plogproto.PlogMessage{
			{Key: S("remote_addr"), Value: []byte(`"::ffff:127.0.0.1"`)},
			{Key: S("input"), Value: []byte(`"J0 print_parse:2 indonly:brown,grown attrind:quick "`)},
			{Key: S("cleaned"), Value: []byte(`"J0 print_parse:2 indonly:brown,grown attrind:quick "`)},
			{Key: S("tot_bytes"), Value: []byte(`418`)},
			{Key: S("ndocs"), Value: []byte(`2`)},
			{Key: S("exec_time_us"), Value: []byte(`240`)},
		},
		Close: func(b bool) *bool { return &b }(true),
	}

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		cid := atomic.AddUint64(&ctxId, 1)
		squery.CtxId = &cid
		lconn.Send(&squery)
	}

	<-cw.ch

	b.StopTimer()

	goodbye(b, lconn, lId)
}
