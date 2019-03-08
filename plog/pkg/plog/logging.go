// Copyright 2018 Schibsted

// Package for logging and sending logs to a log server, where they'll be
// aggregated into log objects and kept safe even if the client program exists
// or crashes.
package plog

import (
	"encoding/json"
	"log"

	"github.com/schibsted/sebase/util/pkg/slog"
	"golang.org/x/crypto/ssh/terminal"
)

// Threshold for logging, only this and higher prio levels will log.
var SetupLevel = Info

// Initializes the default plog context and changes SetupLevel based on a
// string. Changes the functions used by the slog package to plog and also
// calls log.SetOutput(plog.Info) to redirect log.Printf output to this package
// as well as log.SetFlags(0). Finally checks if FallbackWriter is a TTY. If
// so, changes FallbackFormatter to FallbackFormatterSimple.
func Setup(appname, lvl string) {
	SetupLevel = LogLevel(lvl, SetupLevel)
	Default = NewPlogLog(appname)
	log.SetOutput(Info)
	log.SetFlags(0)

	// Check for Fd function implemented by *os.File.
	type Fd interface {
		Fd() uintptr
	}
	fd, ok := FallbackWriter.(Fd)
	if ok && terminal.IsTerminal(int(fd.Fd())) {
		FallbackFormatter = FallbackFormatterSimple
	}
	slog.Critical.SetErrf(Critical.LogDict)
	slog.Error.SetErrf(Error.LogDict)
	slog.Warning.SetErrf(Warning.LogDict)
	slog.Info.SetErrf(Info.LogDict)
	slog.Debug.SetErrf(Debug.LogDict)
}

// Closes Default, disconnecting from the logging server.
func Shutdown() {
	Default.Close()
	Default = nil
}

// Logs to Default if not nil, or to FallbackWriter if Default is nil.
func Log(key string, value interface{}) error {
	if Default != nil {
		return Default.Log(key, value)
	}
	jw, err := json.Marshal(value)
	if err != nil {
		return err
	}
	_, err = FallbackFormatter([]FallbackKey{{key, 0}}, jw)
	return err
}

// Log a JSON dictionary from the variadic arguments, alternating keys and
// values, key first. Logs to Default or to FallbackWriter if nil
// See Plog.LogDict for more information.
func LogDict(key string, kvs ...interface{}) error {
	return Log(key, kvsToDict(kvs))
}

// Shorthand for Info.Print
func Print(value ...interface{}) {
	Info.Print(value...)
}

// Shorthand for Info.Printf
func Printf(fmt string, value ...interface{}) {
	Info.Printf(fmt, value...)
}
