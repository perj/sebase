package plog

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/schibsted/sebase/util/pkg/slog"
)

type Fields = slog.KV

// An interface with functions for level based logging. Plog context
// conform to this interface, as well as the WithFields return value.
type Logger interface {
	Log(key string, value interface{}) error
	LogDict(key string, kvs ...interface{}) error
	LogMsg(key string, msg string, kvs ...interface{})

	LevelPrint(lvl Level, value ...interface{})
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

	Print(value ...interface{})
	Printf(format string, value ...interface{})
	Fatal(value ...interface{})
	Fatalf(format string, value ...interface{})
	Panic(value ...interface{})
	Panicf(format string, value ...interface{})
}

// WithFields for compatibility with logrus. If the Log function is used with
// a map[string]interface{} value then the fields are merged with that map,
// otherwise the value is put in the "msg" key in a dictionary with the
// fields. LogDict and LogMsg are wrappers for Log with a map[string]interface{}.
// The latter adds the msg argument with the "msg" key.
//
// This functions logs to Default if non-nil otherwise to FallbackWriter.
//
// WithKeys(...).Info(x) is equal to Info.LogMsg(x, ...)
func WithFields(f Fields) Logger {
	return &fielder{f, Default, true}
}

// WithFields for compatibility with logrus. See top level WithFields for more
// info.
func (plog *Plog) WithFields(f Fields) Logger {
	return &fielder{f, plog, false}
}

type fielder struct {
	fields   map[string]interface{}
	Ctx      *Plog
	fallback bool
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
	vv := make(map[string]interface{}, len(m)+len(f.fields))
	for k, v := range f.fields {
		vv[k] = v
	}
	for k, v := range m {
		vv[k] = v
	}
	value = vv
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

func (f *fielder) Print(value ...interface{}) {
	f.LevelPrint(Info, value...)
}

func (f *fielder) Printf(format string, value ...interface{}) {
	f.LevelPrintf(Info, format, value...)
}

func (f *fielder) Fatal(value ...interface{}) {
	f.LevelPrint(Crit, value...)
	os.Exit(1)
}

func (f *fielder) Fatalf(format string, value ...interface{}) {
	f.LevelPrintf(Crit, format, value...)
	os.Exit(1)
}

func (f *fielder) Panic(value ...interface{}) {
	f.LevelPrint(Crit, value...)
	panic(fmt.Sprint(value...))
}

func (f *fielder) Panicf(format string, value ...interface{}) {
	f.LevelPrintf(Crit, format, value...)
	panic(fmt.Sprintf(format, value...))
}
