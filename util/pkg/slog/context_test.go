package slog

import (
	"context"
	"reflect"
	"testing"
)

func TestContext(t *testing.T) {
	ctx := context.Background()
	ctx = ContextWithFilter(ctx, func(kvs []interface{}) []interface{} {
		return append(kvs, "x", "y")
	})
	m := CtxKVsMap(ctx)
	exp := map[string]interface{}{"x": "y"}
	if !reflect.DeepEqual(m, exp) {
		t.Errorf("CtxKVsMap failed, got %v", exp)
	}

	defer func(l Logger) {
		Error = l
	}(Error)
	Error = func(msg string, kvs ...interface{}) {
		m = KVsMap(kvs...)
		m["msg"] = msg
	}
	CtxError(ctx, "errmsg")
	exp["msg"] = "errmsg"
	if !reflect.DeepEqual(m, exp) {
		t.Errorf("CtxError failed, got %v", exp)
	}
}
