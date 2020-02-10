package plog

import (
	"context"
	"os"
	"reflect"
	"testing"

	"github.com/schibsted/sebase/plog/internal/pkg/plogproto"
	"github.com/schibsted/sebase/util/pkg/slog"
)

type testContextLogger struct {
	key   string
	value interface{}
}

func (l *testContextLogger) Log(key string, value interface{}) error {
	l.key = key
	l.value = value
	return nil
}

func TestCtx(t *testing.T) {
	ctx := context.Background()
	tl := &testContextLogger{}
	ctx = ContextWithLogger(ctx, tl)
	ctx = slog.ContextWithFilter(ctx, func(kvs []interface{}) []interface{} {
		return append(kvs, "test", "x")
	})
	l := Ctx(ctx)
	l.LogMsg("foo", "bar")
	exp := &testContextLogger{"foo", map[string]interface{}{"msg": "bar", "test": "x"}}
	if !reflect.DeepEqual(tl, exp) {
		t.Errorf("LogMsg failed, got %#v", tl)
	}

	Info.Ctx(ctx).Msg("foo")
	exp = &testContextLogger{"INFO", map[string]interface{}{"msg": "foo", "test": "x"}}
	if !reflect.DeepEqual(tl, exp) {
		t.Errorf("Info.Ctx.Msg failed, got %#v", tl)
	}

	testStderr.Reset()
	l = Ctx(context.Background())
	l.LogMsg("foo", "bar")
	if testStderr.String() != `test.foo: {"msg":"bar"}`+"\n" {
		t.Errorf("nil LogMsg failed, got %q", testStderr.String())
	}
	testStderr.Reset()
	ctxl := ContextWithLogger(ctx, l.With("foo", "bar"))
	Ctx(ctxl).LogMsg("type", "msg")
	if testStderr.String() != `test.type: {"foo":"bar","msg":"msg","test":"x"}`+"\n" {
		t.Errorf("fielder LogMsg failed, got %q", testStderr.String())
	}
}

func TestLoggerCtx(t *testing.T) {
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Unsetenv("PLOG_SOCKET")

	ctx := context.Background()
	ctxwith := slog.ContextWithFilter(ctx, func(kvs []interface{}) []interface{} {
		return append(kvs, "test", "y")
	})

	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 5

	plog := NewPlogLog("testapp")
	defer plog.Close()
	plog.Ctx(ctxwith).LogMsg("foo", "bar")
	<-testPlogCh
	v := <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"{\"msg\":\"bar\",\"test\":\"y\"}" ` {
		t.Errorf("plog.Ctx.LogMsg failed, got %s", v.Msg[0].String())
	}
	plog.Ctx(ctxwith).Ctx(ctx).LogMsg("foo", "bar")
	v = <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"{\"msg\":\"bar\"}" ` {
		t.Errorf("plog.Ctx.Ctx.LogMsg failed, got %s", v.Msg[0].String())
	}
	plog.Type("foo").Ctx(ctxwith).Msg("bar")
	v = <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"{\"msg\":\"bar\",\"test\":\"y\"}" ` {
		t.Errorf("plog.Type.Ctx.Msg failed, got %s", v.Msg[0].String())
	}
	plog.Type("foo").Ctx(ctxwith).Ctx(ctx).Msg("bar")
	v = <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"{\"msg\":\"bar\"}" ` {
		t.Errorf("plog.Type.Ctx.Ctx.Msg failed, got %s", v.Msg[0].String())
	}
}

func TestCtxMsg(t *testing.T) {
	testStderr.Reset()
	ctx := context.Background()
	ctx = slog.ContextWithFilter(ctx, func(kvs []interface{}) []interface{} {
		return append(kvs, "test", "z")
	})
	CtxMsg(ctx, "foo", "bar")
	if testStderr.String() != `test.foo: {"msg":"bar","test":"z"}`+"\n" {
		t.Errorf("CtxMsg failed, got %q", testStderr.String())
	}
	testStderr.Reset()
	Info.CtxMsg(ctx, "foo")
	if testStderr.String() != `test.INFO: {"msg":"foo","test":"z"}`+"\n" {
		t.Errorf("Info.CtxMsg failed, got %q", testStderr.String())
	}
}

func TestLoggerCtxMsg(t *testing.T) {
	os.Setenv("PLOG_SOCKET", testSock)
	defer os.Unsetenv("PLOG_SOCKET")

	ctx := context.Background()
	ctxwith := slog.ContextWithFilter(ctx, func(kvs []interface{}) []interface{} {
		return append(kvs, "test", "w")
	})

	testPlogCh = make(chan plogproto.Plog, 2)
	testN = 5

	plog := NewPlogLog("testapp")
	defer plog.Close()
	plog.CtxMsg(ctxwith, "foo", "bar")
	<-testPlogCh
	v := <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"{\"msg\":\"bar\",\"test\":\"w\"}" ` {
		t.Errorf("plog.CtxMsg failed, got %s", v.Msg[0].String())
	}
	plog.Ctx(ctxwith).CtxMsg(ctx, "foo", "bar")
	v = <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"{\"msg\":\"bar\"}" ` {
		t.Errorf("plog.Ctx.CtxMsg failed, got %s", v.Msg[0].String())
	}
	plog.Type("foo").CtxMsg(ctxwith, "bar")
	v = <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"{\"msg\":\"bar\",\"test\":\"w\"}" ` {
		t.Errorf("plog.Type.Ctx.Msg failed, got %s", v.Msg[0].String())
	}
	plog.Type("foo").Ctx(ctxwith).CtxMsg(ctx, "bar")
	v = <-testPlogCh
	if v.Msg[0].String() != `key:"foo" value:"{\"msg\":\"bar\"}" ` {
		t.Errorf("plog.Type.Ctx.Ctx.Msg failed, got %s", v.Msg[0].String())
	}
}
