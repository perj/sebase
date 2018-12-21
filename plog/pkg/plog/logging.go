// Copyright 2018 Schibsted

// Package for logging and sending logs to a log server, where they'll be
// aggregated into log objects and kept safe even if the client program exists
// or crashes.
package plog

import (
	"io"
	"log"
	"os"
)

// Threshold for logging, only this and higher prio levels will log.
var SetupLevel = Info

// Where logs go if we can't connect to plogd. Defaults to stderr.
// Logs will be written prefixed with key and suffixed with a newline.
// All output is json encoded.
var FallbackWriter io.Writer = os.Stderr

// Initializes the default plog context and changes SetupLevel based on a
// string.  Also calls log.SetOutput(plog.Info) to redirect log.Printf output
// to this package.
func Setup(appname, lvl string) {
	SetupLevel = LogLevel(lvl, SetupLevel)
	Default = NewPlogLog(appname)
	log.SetOutput(Info)
}

// Closes Default, disconnecting from the logging server.
func Shutdown() {
	Default.Close()
	Default = nil
}

func fallbackWrite(key string, value []byte) (n int, err error) {
	prefix := key + ": "
	ww := append([]byte(prefix), value...)
	if ww[len(ww)-1] != '\n' {
		ww = append(ww, '\n')
	}
	n, err = FallbackWriter.Write(ww)
	// Calculate how much of w we wrote.
	if n <= len(prefix) {
		n = 0
	} else {
		n -= len(prefix)
		if n > len(value) {
			n = len(value)
		}
	}
	return
}
