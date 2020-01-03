// Copyright 2020 Schibsted

package plog

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/schibsted/sebase/util/pkg/slog"
)

// Fields is now just an alias for slog.KV. It's used to add key-value pairs to
// logs.
type Fields = slog.KV

// TypeLogger can be used to log with preset fields. Additional
// Fields can also be added via the With method or passed together
// with a message to LogMsg.
//
// The Level type in this package implements this interface.
// See that type for more documenation about the functions here.
//
// Methods might be added to this interface without increasing the
// major version.
type TypeLogger interface {
	With(kvs ...interface{}) TypeLogger

	Msg(msg string)
	Msgf(format string, value ...interface{})

	LogMsg(msg string, kvs ...interface{})

	Print(value ...interface{})
	Printf(format string, value ...interface{})
	Fatal(value ...interface{})
	Fatalf(format string, value ...interface{})
	Panic(value ...interface{})
	Panicf(format string, value ...interface{})
}

// Logger is an interface with functions for level based logging. Plog
// context conform to this interface, as well as the WithFields return value.
// See the Plog type for documentation about the functions in this interface.
//
// Methods might be added to this interface without increasing the
// major version.
type Logger interface {
	With(kvs ...interface{}) Logger

	Type(key string) TypeLogger

	Log(key string, value interface{}) error
	LogDict(key string, kvs ...interface{}) error
	LogMsg(key string, msg string, kvs ...interface{})

	LevelPrint(lvl Level, value ...interface{})
	LevelPrintf(lvl Level, format string, value ...interface{})
	Emergency(value ...interface{})
	Emergencyf(format string, value ...interface{})
	Alert(value ...interface{})
	Alertf(format string, value ...interface{})
	Critical(value ...interface{})
	Criticalf(format string, value ...interface{})
	Error(value ...interface{})
	Errorf(format string, value ...interface{})
	Warning(value ...interface{})
	Warningf(format string, value ...interface{})
	Notice(value ...interface{})
	Noticef(format string, value ...interface{})
	Info(value ...interface{})
	Infof(format string, value ...interface{})
	Debug(value ...interface{})
	Debugf(format string, value ...interface{})
}

// WithFields can be used to create Loggers with preset fields.
// If the Log function on the returned interface is used with
// a map[string]interface{} value then the fields are merged with that map,
// otherwise the value is put in the "msg" key in a dictionary with the
// fields. LogDict and LogMsg are wrappers for Log with a map[string]interface{}.
// The latter adds the msg argument with the "msg" key.
//
// This function logs to Default if non-nil otherwise to FallbackWriter.
//
// WithFields(...).Info(x) is equal to Info.LogMsg(x, ...)
// WithFields(...).Type("INFO").Msg(x) is also the same.
//
// The fields map is stored by reference so don't modify it after this call.
// Each call to With and WithFields creates a new logger so don't call it
// more than necessary.
func WithFields(f Fields) Logger {
	return newFielder(nil, f, Default, true)
}

// With creates a Logger in a similar way as WithFields, but uses the
// alternating keys and values given by kvs instead. The full details
// of how kvs is parsed is described in the slog.KVsMap function, but
// the basic version is to alternate keys and values.
func With(kvs ...interface{}) Logger {
	return newFielder(nil, slog.KVsMap(kvs...), Default, true)
}

// WithFields on a specific plog context. See top level WithFields for more
// info.
// Unlike the package level function, this one does not use the FallbackWriter
// but simply discards instead if plog is nil.
func (plog *Plog) WithFields(f Fields) Logger {
	return newFielder(nil, f, plog, false)
}

// With creates a Logger with the key-values. See the package level function
// for more information. kvs is parsed via slog.KVsMap.
// Unlike the package level function, this one does not use the FallbackWriter
// but simply discards instead if plog is nil.
func (plog *Plog) With(kvs ...interface{}) Logger {
	return newFielder(nil, slog.KVsMap(kvs...), plog, false)
}

func (f *fielder) With(kvs ...interface{}) Logger {
	return newFielder(f.fields, slog.KVsMap(kvs...), f.Ctx, f.fallback)
}

func newFielder(farr []map[string]interface{}, f Fields, ctx *Plog, fallback bool) *fielder {
	newfarr := make([]map[string]interface{}, len(farr)+1)
	if farr != nil {
		copy(newfarr, farr)
	}
	if f != nil {
		newfarr[len(farr)] = f
	} else {
		newfarr = newfarr[:len(farr)]
	}
	return &fielder{newfarr, ctx, fallback}
}

type fielder struct {
	fields   []map[string]interface{}
	Ctx      *Plog
	fallback bool
}

func (f *fielder) flatten(m map[string]interface{}) map[string]interface{} {
	ret := make(map[string]interface{}, len(m)+len(f.fields))
	for _, f := range f.fields {
		for k, v := range f {
			ret[k] = v
		}
	}
	for k, v := range m {
		ret[k] = v
	}
	return ret
}

func (f *fielder) Log(key string, value interface{}) error {
	if f.Ctx == nil && !f.fallback {
		return nil
	}
	m, ok := value.(map[string]interface{})
	if !ok {
		m = map[string]interface{}{
			"msg": value,
		}
	}
	value = f.flatten(m)
	if f.Ctx != nil {
		return f.Ctx.Log(key, value)
	}
	jw, err := json.Marshal(value)
	if err != nil {
		return err
	}
	_, err = FallbackFormatter([]FallbackKey{{key, 0}}, jw)
	return err
}

func (f *fielder) LogDict(key string, kvs ...interface{}) error {
	if f.Ctx == nil && !f.fallback {
		return nil
	}
	return f.Log(key, slog.KVsMap(kvs...))
}

func (f *fielder) LogMsg(key, msg string, kvs ...interface{}) {
	if f.Ctx == nil && !f.fallback {
		return
	}
	m := slog.KVsMap(kvs...)
	m["msg"] = msg
	errWrap(f.Log, key, m)
}

func (f *fielder) LevelPrint(lvl Level, value ...interface{}) {
	if lvl > SetupLevel {
		return
	}
	if f.Ctx == nil && !f.fallback {
		return
	}
	f.Log(lvl.Code(), fmt.Sprint(value...))
}

func (f *fielder) LevelPrintf(lvl Level, format string, value ...interface{}) {
	if lvl > SetupLevel {
		return
	}
	if f.Ctx == nil && !f.fallback {
		return
	}
	f.Log(lvl.Code(), fmt.Sprintf(format, value...))
}

func (f *fielder) Emergency(value ...interface{}) {
	f.LevelPrint(Emergency, value...)
}

func (f *fielder) Emergencyf(format string, value ...interface{}) {
	f.LevelPrintf(Emergency, format, value...)
}

func (f *fielder) Alert(value ...interface{}) {
	f.LevelPrint(Alert, value...)
}

func (f *fielder) Alertf(format string, value ...interface{}) {
	f.LevelPrintf(Alert, format, value...)
}

func (f *fielder) Critical(value ...interface{}) {
	f.LevelPrint(Critical, value...)
}

func (f *fielder) Criticalf(format string, value ...interface{}) {
	f.LevelPrintf(Critical, format, value...)
}

func (f *fielder) Error(value ...interface{}) {
	f.LevelPrint(Error, value...)
}

func (f *fielder) Errorf(format string, value ...interface{}) {
	f.LevelPrintf(Error, format, value...)
}

func (f *fielder) Warning(value ...interface{}) {
	f.LevelPrint(Warning, value...)
}

func (f *fielder) Warningf(format string, value ...interface{}) {
	f.LevelPrintf(Warning, format, value...)
}

func (f *fielder) Notice(value ...interface{}) {
	f.LevelPrint(Notice, value...)
}

func (f *fielder) Noticef(format string, value ...interface{}) {
	f.LevelPrintf(Notice, format, value...)
}

func (f *fielder) Info(value ...interface{}) {
	f.LevelPrint(Info, value...)
}

func (f *fielder) Infof(format string, value ...interface{}) {
	f.LevelPrintf(Info, format, value...)
}

func (f *fielder) Debug(value ...interface{}) {
	f.LevelPrint(Debug, value...)
}

func (f *fielder) Debugf(format string, value ...interface{}) {
	f.LevelPrintf(Debug, format, value...)
}

type typeFielder struct {
	*fielder
	key string
}

// With creates a TypeLogger with the key-values. See the package level
// function for more information. TypeLogger is different from Logger
// in the key is fixed and not part of the function signatures.
// kvs is parsed via slog.KVsMap.
func (l Level) With(kvs ...interface{}) TypeLogger {
	ctx := Default
	fallback := true
	if l > SetupLevel {
		ctx = nil
		fallback = false
	}
	return &typeFielder{newFielder(nil, slog.KVsMap(kvs...), ctx, fallback), l.Code()}
}

func (f *typeFielder) With(kvs ...interface{}) TypeLogger {
	return &typeFielder{newFielder(f.fields, slog.KVsMap(kvs...), f.Ctx, f.fallback), f.key}
}

// Type create a TypeLogger for this context and key. Fields can then
// be added by the With function. See Level.With for more information.
func (plog *Plog) Type(key string) TypeLogger {
	return &typeFielder{newFielder(nil, nil, plog, false), key}
}

func (f *fielder) Type(key string) TypeLogger {
	return &typeFielder{f, key}
}

func (f *typeFielder) Print(value ...interface{}) {
	f.fielder.LogMsg(f.key, fmt.Sprint(value...))
}

func (f *typeFielder) LogMsg(msg string, kvs ...interface{}) {
	f.fielder.LogMsg(f.key, msg, kvs...)
}

func (f *typeFielder) Printf(format string, value ...interface{}) {
	f.fielder.LogMsg(f.key, fmt.Sprintf(format, value...))
}

func (f *typeFielder) Fatal(value ...interface{}) {
	f.fielder.LogMsg(f.key, fmt.Sprint(value...))
	os.Exit(1)
}

func (f *typeFielder) Fatalf(format string, value ...interface{}) {
	f.fielder.LogMsg(f.key, fmt.Sprintf(format, value...))
	os.Exit(1)
}

func (f *typeFielder) Panic(value ...interface{}) {
	f.fielder.LogMsg(f.key, fmt.Sprint(value...))
	panic(fmt.Sprint(value...))
}

func (f *typeFielder) Panicf(format string, value ...interface{}) {
	f.fielder.LogMsg(f.key, fmt.Sprintf(format, value...))
	panic(fmt.Sprintf(format, value...))
}

func (f *typeFielder) Msg(msg string) {
	f.fielder.LogMsg(f.key, msg)
}

func (f *typeFielder) Msgf(format string, value ...interface{}) {
	f.fielder.LogMsg(f.key, fmt.Sprintf(format, value...))
}
