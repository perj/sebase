// Copyright 2019 Schibsted

// Package slog is used to log from the other sebase packages.
// The purpose of this is to not depend on a particular log package since
// different users might want to use different packages.
// Plog will install itself here if used, but the default values simply creates
// a string and calls log.Printf.
package slog

import (
	"encoding/json"
	"fmt"
	"log"
)

// Logger is the function type used to log.
//
// The arguments are alternated keys as strings and values which can be any
// type, keys first. The first key given will be "msg" and the first value will
// be a string with a human readable message.  If you call this package, make
// sure to follow these rules as there's likely to be a panic otherwise.
type Logger func(kvs ...interface{})

// DefaultLogger is used as a default logger, wrapping a Printf like function.
type DefaultLogger struct {
	Logf func(format string, v ...interface{})
}

// LogDict creates a string and calls the log.Printf like function
// contained if not nil.
// Error interface values are special handled, converted to the error message.
func (d DefaultLogger) LogDict(kvs ...interface{}) {
	if d.Logf == nil {
		return
	}
	var msg string
	m := make(map[string]interface{}, len(kvs)/2)
	for i := 0; i < len(kvs); i += 2 {
		k := kvs[i].(string)
		var v interface{}
		if i < len(kvs)-1 {
			v = kvs[i+1]
		}
		if err, ok := v.(error); ok {
			v = err.Error()
		}
		if k == "msg" {
			msg = v.(string)
		} else {
			m[k] = v
		}
	}
	if len(m) == 0 {
		d.Logf("%s", msg)
		return
	}
	data, err := json.Marshal(m)
	var v interface{} = data
	if err != nil {
		v = fmt.Sprint(m)
	}
	d.Logf("%s: %s", msg, v)
}

var (
	Critical Logger = DefaultLogger{log.Printf}.LogDict
	Error    Logger = DefaultLogger{log.Printf}.LogDict
	Warning  Logger = DefaultLogger{log.Printf}.LogDict
	Info     Logger = DefaultLogger{log.Printf}.LogDict
	Debug    Logger = DefaultLogger{nil}.LogDict
)

// SetLogPrintf sets the receiver to log to log.Printf via the DefaultLogger type.
// It's a convenience function. For example you can enable debug logging with
// slog.Debug.SetLogPrintf()
func (logger *Logger) SetLogPrintf() {
	*logger = DefaultLogger{log.Printf}.LogDict
}

// Disable sets the receiver to a value discaring the logs.
func (logger *Logger) Disabe() {
	*logger = DefaultLogger{nil}.LogDict
}

// SetErrf sets the receiver to a function that can return errors. When that
// happens, log.Printf is called as a backup with a "log-error" indicating the
// error.
func (logger *Logger) SetErrf(f func(kvs ...interface{}) error) {
	*logger = func(kvs ...interface{}) {
		err := f(kvs...)
		if err == nil {
			return
		}
		nkvs := make([]interface{}, len(kvs)+2)
		copy(nkvs, kvs)
		nkvs[len(kvs)] = "log-error"
		nkvs[len(kvs)+1] = err
		DefaultLogger{log.Printf}.LogDict(nkvs)
	}
}
