// Copyright 2018 Schibsted

package plog

import (
	"fmt"
	"net"
	"os"
	"testing"

	"github.com/schibsted/sebase/plog/internal/pkg/plogproto"
)

var testN int
var testPlogCh chan plogproto.Plog
var testSock string

func init() {
	l, err := net.Listen("tcp", ":0")
	if err != nil {
		panic(err)
	}
	port := l.Addr().(*net.TCPAddr).Port
	testSock = fmt.Sprintf("tcp://localhost:%v", port)
	pl := plogproto.Listener{l, false}
	go func() {
		for {
			conn, err := pl.Accept()
			if err != nil {
				panic(err)
			}
			var plog plogproto.Plog
			for ; testN > 0; testN-- {
				err := conn.Receive(&plog)
				if err != nil {
					panic(err)
				}
				testPlogCh <- plog
			}
			close(testPlogCh)
			conn.Close()
		}
	}()
}

func TestDefaultPlog(t *testing.T) {
	defer Setup("test", "debug")
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Setenv("PLOG_SOCKET", "/")
	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 2

	Setup("test", "debug")
	ctx := Default
	if ctx == nil {
		t.Error("Expected non-nil ctx")
	}
	ctx.Log("mykey", "myvalue")
	<-testPlogCh
	v := <-testPlogCh
	if v.Msg[0].String() != `key:"mykey" value:"\"myvalue\"" ` {
		t.Errorf("didn't get mykey:myvalue logged, got %s", v.Msg[0].String())
	}
}

func TestLogMap(t *testing.T) {
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Unsetenv("PLOG_SOCKET")

	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 2
	ctx := NewPlogLog("testapp")
	defer ctx.Close()
	ctx.Log("test", map[string]bool{"foo": true})
	<-testPlogCh
	v := <-testPlogCh
	if v.Msg[0].String() != `key:"test" value:"{\"foo\":true}" ` {
		t.Errorf("didn't get test map logged, got %s", v.Msg[0].String())
	}
}

func TestNewLogInt(t *testing.T) {
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Unsetenv("PLOG_SOCKET")

	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 2
	ctx := NewPlogLog("testapp")
	defer ctx.Close()
	v := <-testPlogCh
	if v.Open.String() != `ctxtype:log key:"testapp" ` {
		t.Errorf("Didn't get log hello, got %s", v.Open.String())
	}
	ctx.Log("foo", 42)
	v = <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"42" ` {
		t.Errorf("Didn't get foo:42 logged, got %s", v.Msg[0].String())
	}
}

func TestNewStateBool(t *testing.T) {
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Unsetenv("PLOG_SOCKET")

	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 2
	ctx := NewPlogState("testapp")
	defer ctx.Close()
	v := <-testPlogCh
	if v.Open.String() != `ctxtype:state key:"testapp" ` {
		t.Errorf("Didn't get state hello, got %s", v.Open.String())
	}
	ctx.Log("foo", false)
	v = <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"false" ` {
		t.Errorf("Didn't get foo:false logged, got %s", v.Msg[0].String())
	}
}

func TestNewCountJSON(t *testing.T) {
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Unsetenv("PLOG_SOCKET")

	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 2
	ctx := NewPlogCount("testapp", "test", "path")
	defer ctx.Close()
	v := <-testPlogCh
	if v.Open.String() != `ctxtype:count key:"testapp" key:"test" key:"path" ` {
		t.Errorf("Didn't get state hello, got %s", v.Open.String())
	}
	ctx.LogJSON("foo", []byte(`[{},null,true]`))
	v = <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"[{},null,true]" ` {
		t.Errorf("Didn't get foo:false logged, got %s", v.Msg[0].String())
	}
}

func TestOpenDict(t *testing.T) {
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Unsetenv("PLOG_SOCKET")

	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 2
	ctx := NewPlogLog("testapp")
	defer ctx.Close()
	<-testPlogCh
	dctx := ctx.OpenDict("foo")
	defer dctx.Close()
	v := <-testPlogCh
	pid := uint64(1) // Fixed value for simpler testing
	v.Open.ParentCtxId = &pid
	if v.Open.String() != `ctxtype:dict key:"foo" parent_ctx_id:1 ` {
		t.Errorf("Didn't get dict hello, got %s", v.Open.String())
	}
}

func TestOpenList(t *testing.T) {
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Unsetenv("PLOG_SOCKET")

	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 2
	ctx := NewPlogLog("testapp")
	defer ctx.Close()
	<-testPlogCh
	dctx := ctx.OpenList("foo")
	defer dctx.Close()
	v := <-testPlogCh
	pid := uint64(1) // Fixed value for simpler testing
	v.Open.ParentCtxId = &pid
	if v.Open.String() != `ctxtype:list key:"foo" parent_ctx_id:1 ` {
		t.Errorf("Didn't get list hello, got %s", v.Open.String())
	}
}

func TestOpenListOfDicts(t *testing.T) {
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Unsetenv("PLOG_SOCKET")

	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 2
	ctx := NewPlogLog("testapp")
	defer ctx.Close()
	<-testPlogCh
	dctx := ctx.OpenListOfDicts("foo")
	defer dctx.Close()
	v := <-testPlogCh
	pid := uint64(1) // Fixed value for simpler testing
	v.Open.ParentCtxId = &pid
	if v.Open.String() != `ctxtype:list_of_dicts key:"foo" parent_ctx_id:1 ` {
		t.Errorf("Didn't get list hello, got %s", v.Open.String())
	}
}
