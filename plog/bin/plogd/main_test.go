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
var ctxID uint64

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

func hello(t testing.TB, conn *plogproto.Writer, ctxtype plogproto.CtxType, pID uint64, appname ...string) uint64 {
	cID := atomic.AddUint64(&ctxID, 1)
	hello := plogproto.OpenContext{Ctxtype: &ctxtype, ParentCtxId: &pID, Key: appname}
	err := conn.SendOpen(cID, &hello)
	if err != nil {
		t.Fatal(err)
	}
	return cID
}

func goodbye(t testing.TB, conn *plogproto.Writer, cID uint64) {
	err := conn.SendClose(cID)
	if err != nil {
		t.Fatal(err)
	}
}

func TestHelloGoodbye(t *testing.T) {
	conn := testConnect()
	defer conn.Close()

	cID := hello(t, conn, plogproto.CtxType_log, 0, "trans")
	goodbye(t, conn, cID)
}

func TestBrokenPipe(t *testing.T) {
	conn := testConnect()
	defer conn.Close()
}

func TestSinglePublish(t *testing.T) {
	conn := testConnect()
	defer conn.Close()

	cID := hello(t, conn, plogproto.CtxType_log, 0, "trans")

	if err := conn.SendKeyValue(cID, "INFO", []byte(`"foo"`)); err != nil {
		t.Fatal(err)
	}
	checkLog(t, "trans", "INFO", `"foo"`)

	goodbye(t, conn, cID)
}

func TestSubprog(t *testing.T) {
	conn := testConnect()
	defer conn.Close()

	cID := hello(t, conn, plogproto.CtxType_log, 0, "test+subtest")

	if err := conn.SendKeyValue(cID, "INFO", []byte(`"foo"`)); err != nil {
		t.Fatal(err)
	}
	expectLog(t, []string{"---", "test", "INFO", `"foo"`, `subprog: "subtest"`})

	goodbye(t, conn, cID)
}

func TestDict(t *testing.T) {
	lconn := testConnect()
	defer lconn.Close()

	lID := hello(t, lconn, plogproto.CtxType_log, 0, "trans")

	tID := hello(t, lconn, plogproto.CtxType_dict, lID, "transaction")

	if err := lconn.SendKeyValue(tID, "cmd", []byte(`"transinfo"`)); err != nil {
		t.Fatal(err)
	}

	goodbye(t, lconn, lID)
	goodbye(t, lconn, tID)

	checkLog(t, "trans", "transaction", `{"cmd":"transinfo"}`)
}

func TestList(t *testing.T) {
	lconn := testConnect()
	defer lconn.Close()

	lID := hello(t, lconn, plogproto.CtxType_log, 0, "trans")

	tID := hello(t, lconn, plogproto.CtxType_list, lID, "connections")

	pub := plogproto.PlogMessage{Value: []byte(`"1"`)}
	if err := lconn.SendMessage(tID, &pub); err != nil {
		t.Fatal(err)
	}
	pub.Value = []byte(`"2"`)
	if err := lconn.SendMessage(tID, &pub); err != nil {
		t.Fatal(err)
	}

	goodbye(t, lconn, lID)
	goodbye(t, lconn, tID)

	checkLog(t, "trans", "connections", `["1","2"]`)
}

func TestTransaction(t *testing.T) {
	lconn := testConnect()
	defer lconn.Close()

	lID := hello(t, lconn, plogproto.CtxType_log, 0, "trans")

	if err := lconn.SendKeyValue(lID, "INFO", []byte(`"incoming connection"`)); err != nil {
		t.Fatal(err)
	}
	checkLog(t, "trans", "INFO", `"incoming connection"`)

	tID := hello(t, lconn, plogproto.CtxType_dict, lID, "transaction")

	lconn.SendKeyValue(tID, "remote_addr", []byte(`"::1"`))
	lconn.SendKeyValue(tID, "control", []byte(`true`))
	lconn.SendKeyValue(tID, "tptr", []byte(`"0xba7390"`))

	tdID := hello(t, lconn, plogproto.CtxType_list, tID, "debug")

	iID := hello(t, lconn, plogproto.CtxType_list, tID, "input")
	lconn.SendKeyValue(iID, "", []byte(`"cmd:transaction"`))
	lconn.SendKeyValue(iID, "", []byte(`"backends:1"`))
	lconn.SendKeyValue(iID, "", []byte(`"commit:1"`))
	lconn.SendKeyValue(iID, "", []byte(`"end"`))
	goodbye(t, lconn, iID)

	lconn.SendKeyValue(tdID, "", []byte(`"verify_parameters: phase 0, pending 0"`))
	lconn.SendKeyValue(tdID, "", []byte(`"starting validator v_bool for transinfo"`))
	lconn.SendKeyValue(tdID, "", []byte(`"ending validator for transinfo"`))

	lconn.SendKeyValue(tID, "info", []byte(`["Logging temporarily disabled."]`))

	goodbye(t, lconn, tdID)
	goodbye(t, lconn, tID)

	// Looks like dicts are sorted, so this should work. Not that pretty though.
	checkLog(t, "trans", "transaction", `{"control":true,"debug":["verify_parameters: phase 0, pending 0","starting validator v_bool for transinfo","ending validator for transinfo"],"info":["Logging temporarily disabled."],"input":["cmd:transaction","backends:1","commit:1","end"],"remote_addr":"::1","tptr":"0xba7390"}`)

	lconn.SendKeyValue(lID, "INFO", []byte(`"command thread event dispatch exited because events depleted"`))
	checkLog(t, "trans", "INFO", `"command thread event dispatch exited because events depleted"`)

	goodbye(t, lconn, lID)
}

func TestSimpleState(t *testing.T) {
	sconn := testConnect()
	defer sconn.Close()
	sID := hello(t, sconn, plogproto.CtxType_state, 0, "trans")

	sconn.SendKeyValue(sID, "test", []byte(`"fest"`))
	sconn.SendKeyValue(sID, "test", []byte(`"new"`))

	goodbye(t, sconn, sID)

	checkLog(t, "trans", "state", `{"test":"new"}`)
}

func TestDeepState(t *testing.T) {
	sconn := testConnect()
	defer sconn.Close()
	sID := hello(t, sconn, plogproto.CtxType_state, 0, "trans")

	sconn.SendKeyValue(sID, "test", []byte(`{"a":"b"}`))
	sconn.SendKeyValue(sID, "test", []byte(`{"b":["c"], "c":"d"}`))
	sconn.SendKeyValue(sID, "test", []byte(`{"b":"e"}`))
	sconn.SendKeyValue(sID, "test", []byte(`{"a":null}`))
	sconn.SendKeyValue(sID, "rest", []byte(`"nope"`))
	sconn.SendKeyValue(sID, "fest", []byte(`"yep"`))
	sconn.SendKeyValue(sID, "rest", []byte(`null`))

	goodbye(t, sconn, sID)

	checkLog(t, "trans", "state", `{"fest":"yep","test":{"b":"e","c":"d"}}`)
}

func TestStateSessionsInterrupted(t *testing.T) {
	sconn := testConnect()
	sID := hello(t, sconn, plogproto.CtxType_state, 0, "trans")

	dID := hello(t, sconn, plogproto.CtxType_dict, sID, "foo")

	sconn.SendKeyValue(dID, "a", []byte(`"b"`))
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
	sconn.SendKeyValue(sID, "foo", []byte(`{"a":"c"}`))

	lID := hello(t, sconn, plogproto.CtxType_list, dID, "bar")

	sconn.SendKeyValue(lID, "", []byte(`"d"`))
	sconn.SendKeyValue(dID, "bar", []byte(`"e"`))

	goodbye(t, sconn, sID)
	sconn.Close()

	// lists overwrite, dicts merge.
	// state propogates immediately so a:c overwrites the previously sent a:b
	checkLog(t, "trans", "state", `{"foo":{"@interrupted":true,"a":"c","bar":["d"]}}`)
}

func TestCounterState(t *testing.T) {
	sconn := testConnect()
	defer sconn.Close()
	sID := hello(t, sconn, plogproto.CtxType_count, 0, "test", "counters", "foo", "bar")

	sconn.SendKeyValue(sID, "name", []byte(`"bar"`))
	sconn.SendKeyValue(sID, "test", []byte(`2`))
	sconn.SendKeyValue(sID, "test", []byte(`1`))
	sconn.SendKeyValue(sID, "test", []byte(`-2`))

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

	goodbye(t, sconn, sID)

	checkLog(t, "test", "state", `{"counters":{"foo":{}}}`)
}

type countWriter struct {
	N   int
	cnt int
	ch  chan bool
}

func newCountWriter(N int) *countWriter {
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
	cw := newCountWriter(b.N)
	dataStore.Output = &ioWriter{cw}

	conn := testConnect()
	defer conn.Close()

	cID := hello(b, conn, plogproto.CtxType_log, 0, "trans")
	key := "foo"
	pub := &plogproto.PlogMessage{Key: &key, Value: []byte(`"bar"`)}

	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		conn.SendMessage(cID, pub)
	}

	<-cw.ch

	b.StopTimer()

	goodbye(b, conn, cID)
}

func BenchmarkLogSearchQuery(b *testing.B) {
	savedOutput := dataStore.Output
	defer func() { dataStore.Output = savedOutput }()
	cw := newCountWriter(b.N)
	dataStore.Output = &ioWriter{cw}

	lconn := testConnect()
	defer lconn.Close()

	lID := hello(b, lconn, plogproto.CtxType_log, 0, "search")

	S := func(s string) *string { return &s }
	squery := plogproto.Plog{
		Open: &plogproto.OpenContext{Ctxtype: plogproto.CtxType_dict.Enum(), ParentCtxId: &lID, Key: []string{"query"}},
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
		cid := atomic.AddUint64(&ctxID, 1)
		squery.CtxId = &cid
		lconn.Send(&squery)
	}

	<-cw.ch

	b.StopTimer()

	goodbye(b, lconn, lID)
}
