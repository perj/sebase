// Copyright 2019 Schibsted

// Package slog is used to log from the other sebase packages.
// The purpose of this is to not depend on a particular log package since
// different users might want to use different packages.
// Plog will install itself here if used, but the default values simply creates
// a string and calls log.Printf.
package slog

import (
	"context"
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

// Print calls l with the msg parameter created with fmt.Sprint.
// It exists because some packages expect this interface.
func (l Logger) Print(v ...interface{}) {
	l(fmt.Sprint(v...))
}

// Printf calls l with the msg parameter created with fmt.Sprintf.
// It exists because some packages expect this interface.
func (l Logger) Printf(format string, v ...interface{}) {
	l(fmt.Sprintf(format, v...))
}

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
	// Critical is for errors that likely need operator interference.
	Critical Logger = DefaultLogger{log.Printf}.LogMsg
	// Error is an error that can't be retried but isn't fatal.
	Error Logger = DefaultLogger{log.Printf}.LogMsg
	// Warning is for retriable problems or similar non-error conditions.
	Warning Logger = DefaultLogger{log.Printf}.LogMsg
	// Info is for normal progress type of messages.
	Info Logger = DefaultLogger{log.Printf}.LogMsg
	// Debug is for extra detail that would be spamful during normal
	// operation.
	Debug Logger = DefaultLogger{nil}.LogMsg
)

// SetLogPrintf sets the receiver to log to log.Printf via the DefaultLogger type.
// It's a convenience function. For example you can enable debug logging with
// slog.Debug.SetLogPrintf()
func (l *Logger) SetLogPrintf() {
	*l = DefaultLogger{log.Printf}.LogMsg
}

// Disable sets the receiver to a value discaring the logs.
func (l *Logger) Disable() {
	*l = DefaultLogger{nil}.LogMsg
}

// CtxLogger is a Logger with a context parameter. When hooked into
// the context can be inspected.
type CtxLogger func(ctx context.Context, msg string, kvs ...interface{})

// DefaultCtxLogger is used as a default CtxLogger, wrapping a Logger.
type DefaultCtxLogger struct {
	*Logger
}

// LogMsg calls the wrapped Logger, if not nil. It discards the ctx
// parameter.
func (d DefaultCtxLogger) LogMsg(ctx context.Context, msg string, kvs ...interface{}) {
	if d.Logger != nil {
		(*d.Logger)(msg, kvs...)
	}
}

// SetLogger makes the CtxLogger log to the Logger, discarding the context.
func (d *CtxLogger) SetLogger(l *Logger) {
	*d = DefaultCtxLogger{l}.LogMsg
}

// Disable sets the receiver to discard messages.
func (d *CtxLogger) Disable() {
	*d = DefaultCtxLogger{nil}.LogMsg
}

var (
	// CtxCritical is for errors that likely need operator interference.
	CtxCritical CtxLogger = DefaultCtxLogger{&Critical}.LogMsg
	// CtxError is an error that can't be retried but isn't fatal.
	CtxError CtxLogger = DefaultCtxLogger{&Error}.LogMsg
	// CtxWarning is for retriable problems or similar non-error
	// conditions.
	CtxWarning CtxLogger = DefaultCtxLogger{&Warning}.LogMsg
	// CtxInfo is for normal progress type of messages.
	CtxInfo CtxLogger = DefaultCtxLogger{&Info}.LogMsg
	// CtxDebug is for extra detail that would be spamful during normal
	// operation.
	CtxDebug CtxLogger = DefaultCtxLogger{&Debug}.LogMsg
)
