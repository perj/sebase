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
// The first argument is a human readable message. The rest are alternating
// keys and values as parsed by the KVsMap function.
// This is not a printf-like function, despite the signature. Typically
// you give key-value pairs with string keys and any type values as the
// kvs.
type Logger func(msg string, kvs ...interface{})

// DefaultLogger is used as a default logger, wrapping a Printf like function.
type DefaultLogger struct {
	Logf func(format string, v ...interface{})
}

// LogMsg creates a string and calls the log.Printf like function
// contained if not nil. This is not a printf-like function, although the
// signature matches. Instead kvs is expected to contain alternating keys
// and values as documented in the KVsMap function.
func (d DefaultLogger) LogMsg(msg string, kvs ...interface{}) {
	if d.Logf == nil {
		return
	}
	m := KVsMap(kvs...)
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
	Critical Logger = DefaultLogger{log.Printf}.LogMsg
	Error    Logger = DefaultLogger{log.Printf}.LogMsg
	Warning  Logger = DefaultLogger{log.Printf}.LogMsg
	Info     Logger = DefaultLogger{log.Printf}.LogMsg
	Debug    Logger = DefaultLogger{nil}.LogMsg
)

// SetLogPrintf sets the receiver to log to log.Printf via the DefaultLogger type.
// It's a convenience function. For example you can enable debug logging with
// slog.Debug.SetLogPrintf()
func (logger *Logger) SetLogPrintf() {
	*logger = DefaultLogger{log.Printf}.LogMsg
}

// Disable sets the receiver to a value discaring the logs.
func (logger *Logger) Disabe() {
	*logger = DefaultLogger{nil}.LogMsg
}
