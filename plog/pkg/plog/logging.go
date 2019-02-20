// Copyright 2018 Schibsted

// Package for logging and sending logs to a log server, where they'll be
// aggregated into log objects and kept safe even if the client program exists
// or crashes.
package plog

import (
	"log"

	"golang.org/x/crypto/ssh/terminal"
)

// Threshold for logging, only this and higher prio levels will log.
var SetupLevel = Info

// Initializes the default plog context and changes SetupLevel based on a
// string.  Also calls log.SetOutput(plog.Info) to redirect log.Printf output
// to this package and log.SetFlags(0). Finally checks if FallbackWriter is a
// TTY. If so, changes FallbackFormatter to FallbackFormatterSimple.
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
}

// Closes Default, disconnecting from the logging server.
func Shutdown() {
	Default.Close()
	Default = nil
}
