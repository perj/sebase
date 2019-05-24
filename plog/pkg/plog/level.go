// Copyright 2018 Schibsted

package plog

import (
	"encoding/json"
	"fmt"
	"os"
)

// Type Level is a syslog like log level. The primary way to use it is via
// the predefined package Constants and the LogMsg functions.
// Level is also used as an argument for the LevelPrint and LevelPrintf
// functions on the Plog and Logger types.
type Level int

const (
	Emergency Level = iota
	Alert
	Critical
	Error
	Warning
	Notice
	Info
	Debug
)

const Crit = Critical

var LevelNames = map[Level]struct {
	Name, Code, Lower string
}{
	Emergency: {"Emergency", "EMERG", "emerg"},
	Alert:     {"Alert", "ALERT", "alert"},
	Critical:  {"Critical", "CRIT", "crit"},
	Error:     {"Error", "ERR", "err"},
	Warning:   {"Warning", "WARNING", "warning"},
	Notice:    {"Notice", "NOTICE", "notice"},
	Info:      {"Info", "INFO", "info"},
	Debug:     {"Debug", "DEBUG", "debug"},
}

var LevelByName = map[string]Level{
	"Emergency": Emergency,
	"EMERG":     Emergency,
	"emerg":     Emergency,
	"emergency": Emergency,
	"Alert":     Alert,
	"ALERT":     Alert,
	"alert":     Alert,
	"Critical":  Critical,
	"CRIT":      Critical,
	"crit":      Critical,
	"critical":  Critical,
	"Error":     Error,
	"ERR":       Error,
	"err":       Error,
	"error":     Error,
	"Warning":   Warning,
	"WARNING":   Warning,
	"warning":   Warning,
	"Notice":    Notice,
	"NOTICE":    Notice,
	"notice":    Notice,
	"Info":      Info,
	"INFO":      Info,
	"info":      Info,
	"Debug":     Debug,
	"DEBUG":     Debug,
	"debug":     Debug,
}

// Returns the Level mathcing lvl, if it's valid, otherwise returns def.
// Commonly used with the lower case codes, either shortened or full.
func LogLevel(lvl string, def Level) Level {
	l, ok := LevelByName[lvl]
	if !ok {
		l = def
	}
	return l
}

// Human-readable name of level.
func (l Level) String() string {
	return LevelNames[l].Name
}

// Upper-case code for level, matching logs.
// Levels Emergency to Error are shortened, while Warning to Debug are full name upper-case.
func (l Level) Code() string {
	return LevelNames[l].Code
}

// Lower-case code for level.
// Levels Emergency to Error are shortened, while Warning to Debug are full name lower-case.
func (l Level) Lower() string {
	return LevelNames[l].Lower
}

// Writes to Default if l <= SetupLevel, or to FallbackWriter if Default is nil.
// Can be used with log.SetOutput, log.New or fmt.Fprint*.
// Example: log.SetOutput(plog.Info)
func (l Level) Write(w []byte) (n int, err error) {
	if l > SetupLevel {
		return 0, nil
	}
	// Strip trailing newline.
	if len(w) > 0 && w[len(w)-1] == '\n' {
		w = w[:len(w)-1]
	}
	if Default != nil {
		Default.LogAsString(l.Code(), w)
		return len(w), nil
	}
	jw, err := json.Marshal(string(w))
	if err != nil {
		return 0, err
	}
	return FallbackFormatter([]FallbackKey{{l.Code(), 0}}, jw)
}

// Calls l.Write via fmt.Fprint.
// Checks l against the SetupLevel first and thus is slightly more
// efficient than calling fmt.Fprint directly.
// While not deprecated it's recommended to use LogMsg instead for
// more structured logging.
func (l Level) Print(v ...interface{}) {
	if l <= SetupLevel {
		fmt.Fprint(l, v...)
	}
}

// Calls l.Write via fmt.Fprintf.
// Checks l against the SetupLevel first and thus is slightly more
// efficient than calling fmt.Fprintf directly.
// While not deprecated it's recommended to use LogMsg instead for
// more structured logging.
func (l Level) Printf(format string, v ...interface{}) {
	if l <= SetupLevel {
		fmt.Fprintf(l, format, v...)
	}
}

// Calls l.Write followed by os.Exit(1)
func (l Level) Fatal(v ...interface{}) {
	fmt.Fprint(l, v...)
	os.Exit(1)
}

// Calls l.Write followed by os.Exit(1)
func (l Level) Fatalf(format string, v ...interface{}) {
	fmt.Fprintf(l, format, v...)
	os.Exit(1)
}

// Calls l.LogMsg followed by os.Exit(1)
func (l Level) FatalMsg(msg string, v ...interface{}) {
	l.LogMsg(msg, v...)
	os.Exit(1)
}

// Calls l.Write followed by panic.
func (l Level) Panic(v ...interface{}) {
	fmt.Fprint(l, v...)
	panic(fmt.Sprint(v...))
}

// Calls l.Write followed by panic.
func (l Level) Panicf(format string, v ...interface{}) {
	fmt.Fprintf(l, format, v...)
	panic(fmt.Sprintf(format, v...))
}

// Calls l.LogMsg followed by panic.
func (l Level) PanicMsg(msg string, v ...interface{}) {
	l.LogMsg(msg, v...)
	panic(msg + ": " + fmt.Sprint(v...))
}

// Convience function that calls LogDict with l.Code() as key. This is
// deprecated in favor of LogMsg which enforces a human readable message.
// This function will be removed in version 2.0.
func (l Level) LogDict(kvs ...interface{}) error {
	return LogDict(l.Code(), kvs...)
}

// Convience function that calls LogMsg with l.Code() as key. The msg
// parameter contains a human readable message, while the rest are passed
// to slog.KVsMap to convert to a dictionary.
// This is not a printf-like function, despite the signature.
func (l Level) LogMsg(msg string, kvs ...interface{}) {
	if l <= SetupLevel {
		LogMsg(l.Code(), msg, kvs...)
	}
}

// Msg is a convenience function that calls LogMsg with l.Code() as key and
// only a human readable message.
func (l Level) Msg(msg string) {
	if l <= SetupLevel {
		LogMsg(l.Code(), msg)
	}
}

// Msgf is a convenience message calling LogMsg with l.Code() as key and
// the printf formatted message as the msg parameter.
// a human readable message.
func (l Level) Msgf(format string, values ...interface{}) {
	if l <= SetupLevel {
		return
	}
	LogMsg(l.Code(), fmt.Sprintf(format, values...))
}
