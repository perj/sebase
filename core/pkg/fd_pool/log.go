// Copyright 2018 Schibsted

package fd_pool

import "log"

// Custom loggers support for fd_pool.
// Used by plog to inject itself. Using plog directly from here would create a
// circular dependency.
// By default log.Printf is used, except for DebugLog which doesn't log.
type Logger interface {
	Printf(format string, a ...interface{})
}

type defaultLogger bool

func (d defaultLogger) Printf(format string, a ...interface{}) {
	if d {
		log.Printf(format, a...)
	}
}

var (
	DebugLog Logger = defaultLogger(false)
	InfoLog  Logger = defaultLogger(true)
	ErrLog   Logger = defaultLogger(true)
)
