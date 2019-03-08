// Copyright 2018 Schibsted

package fd_pool

import "log"

// Logger interface were used to log from this package.
// It's no longer in use, the slog package is used instead.
// It will be removed in the 2.0 release.
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
