package plog

import (
	"context"

	"github.com/schibsted/sebase/util/pkg/slog"
)

type ctxKeyLogger struct{}

// ContextLogger is the interface used for values stored in ContextWithLogger.
// Typically a *Plog or other Logger value.
type ContextLogger interface {
	Log(key string, value interface{}) error
}

// ContextLoggerFunc lets use use a function value as a ContextLogger.
type ContextLoggerFunc func(key string, value interface{}) error

// Log calls f. It implements the ContextLogger interface.
func (f ContextLoggerFunc) Log(key string, value interface{}) error {
	return f(key, value)
}

// ContextWithLogger stores l in ctx so that it can be used by the Ctx
// functions. If l is a *Plog it can also be retrieved with CtxPlog.
// Also consider using slog.ContextWithFilter as another way to modify logging
// via contexts.
func ContextWithLogger(ctx context.Context, l ContextLogger) context.Context {
	return context.WithValue(ctx, ctxKeyLogger{}, l)
}

// Ctx returns a Logger using the ContextLogger stored in ctx. If there's no
// ContextLogger stored, it returns a Logger pointing to Default or the
// fallback logger. The returned Logger will also pass the ctx parameter to
// slog.CtxKVsMap, applying any context kv filters.
func Ctx(ctx context.Context) Logger {
	fallback := false
	var farr []map[string]interface{}
	l, _ := ctx.Value(ctxKeyLogger{}).(ContextLogger)
	if l == nil {
		l = Default
		fallback = true
	} else if f, ok := l.(*fielder); ok {
		farr = f.fields
		l = f.logger
		fallback = f.fallback
	}
	f := newFielder(farr, nil, l, fallback)
	f.cctx = ctx
	return f
}

// CtxPlog returns the plog stored with ContextWithLogger, if the latest value
// stored was a *Plog, otherwise it returns Default.
func CtxPlog(ctx context.Context) *Plog {
	plog, _ := ctx.Value(ctxKeyLogger{}).(*Plog)
	if plog != nil {
		return plog
	}
	return Default
}

// Ctx returns a TypeLogger using the ContextLogger stored in ctx. If there's
// no ContextLogger stored, it returns a TypeLogger pointing to Default or the
// fallback logger. The returned TypeLogger will also pass the ctx parameter to
// slog.CtxKVsMap, applying any context kv filters.
// If the log level is disabled then a TypeLogger discarding logs is returned.
func (l Level) Ctx(ctx context.Context) TypeLogger {
	f := Ctx(ctx).(*fielder)
	if l > SetupLevel {
		f.logger = nil
		f.fallback = false
	}
	return &typeFielder{f, l.Code()}
}

// Ctx returns a Logger that will pass the ctx parameter to slog.CtxKVsMap.
func (plog *Plog) Ctx(ctx context.Context) Logger {
	f := newFielder(nil, nil, plog, false)
	f.cctx = ctx
	return f
}

// Ctx returns a new Logger that will pass the ctx parameter to slog.CtxKVsMap.
func (f *fielder) Ctx(ctx context.Context) Logger {
	f = newFielder(f.fields, nil, f.logger, f.fallback)
	f.cctx = ctx
	return f
}

// Ctx returns a new TypeLogger that will pass the ctx parameter to slog.CtxKVsMap.
func (f *typeFielder) Ctx(ctx context.Context) TypeLogger {
	f = &typeFielder{newFielder(f.fields, nil, f.logger, f.fallback), f.key}
	f.cctx = ctx
	return f
}

// CtxMsg logs a message together with a JSON dictionary from the variadic
// arguments, which are parsed by slog.CtxKVsMap. Logs to the logger stored in
// ctx, if any. Otherwise to Default or FallbackWriter if Default is nil.
// This function does not return errors. If json encoding fails it
// converts to a string and tries again, adding a "log-error" key.
func CtxMsg(ctx context.Context, key, msg string, kvs ...interface{}) {
	m := slog.CtxKVsMap(ctx, kvs...)
	m["msg"] = msg
	l, _ := ctx.Value(ctxKeyLogger{}).(ContextLogger)
	if l == nil {
		l = ContextLoggerFunc(Log)
	}
	errWrap(l.Log, key, m)
}

// CtxMsg logs a human readable message with a JSON dictionary from the
// variadic arguments, which are parsed with slog.CtxKVsMap.
// This function does not return errors. If json encoding fails it
// converts to a string and tries again, adding a "log-error" key.
func (plog *Plog) CtxMsg(ctx context.Context, key, msg string, kvs ...interface{}) {
	if plog == nil {
		return
	}
	m := slog.CtxKVsMap(ctx, kvs...)
	m["msg"] = msg
	errWrap(plog.Log, key, m)
}

func (f *fielder) CtxMsg(ctx context.Context, key, msg string, kvs ...interface{}) {
	if f.logger == nil && !f.fallback {
		return
	}
	m := slog.CtxKVsMap(ctx, kvs...)
	m["msg"] = msg
	errWrap(f.Log, key, m)
}

// CtxMsg will call the package level CtxMsg if l is an enabled level.
// It will use l.Code() as the type key.
func (l Level) CtxMsg(ctx context.Context, msg string, kvs ...interface{}) {
	if l <= SetupLevel {
		CtxMsg(ctx, l.Code(), msg, kvs...)
	}
}

func (f *typeFielder) CtxMsg(ctx context.Context, msg string, kvs ...interface{}) {
	f.fielder.CtxMsg(ctx, f.key, msg, kvs...)
}
