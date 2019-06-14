package plog

import (
	"fmt"
	"os"
)

// Log with a standard level key.
// Only logs if level is above the threshold given to Setup (defaults to Info).
// The value is formatted via fmt.Sprint. For JSON formatting use LogMsg.
func (plog *Plog) LevelPrint(level Level, value ...interface{}) {
	if plog == nil || level > SetupLevel {
		return
	}
	plog.Log(level.Code(), fmt.Sprint(value...))
}

// Log with a standard level key.
// Only logs if level is above the threshold given to Setup (defaults to Info).
// The value is formatted via fmt.Sprintf. For JSON formatting use LogMsg.
func (plog *Plog) LevelPrintf(level Level, format string, value ...interface{}) {
	if plog == nil || level > SetupLevel {
		return
	}
	plog.Log(level.Code(), fmt.Sprintf(format, value...))
}

// Shorthand for plog.LevelPrint(Emergency, value).
func (plog *Plog) Emergency(value ...interface{}) {
	plog.LevelPrint(Emergency, value...)
}

// Shorthand for plog.LevelPrintf(Emergency, value).
func (plog *Plog) Emergencyf(format string, value ...interface{}) {
	plog.LevelPrintf(Emergency, format, value...)
}

// Shorthand for plog.LevelPrint(Alert, value).
func (plog *Plog) Alert(value ...interface{}) {
	plog.LevelPrint(Alert, value...)
}

// Shorthand for plog.LevelPrintf(Alert, value).
func (plog *Plog) Alertf(format string, value ...interface{}) {
	plog.LevelPrintf(Alert, format, value...)
}

// Shorthand for plog.LevelPrint(Critical, value).
func (plog *Plog) Critical(value ...interface{}) {
	plog.LevelPrint(Critical, value...)
}

// Shorthand for plog.LevelPrintf(Critical, value).
func (plog *Plog) Criticalf(format string, value ...interface{}) {
	plog.LevelPrintf(Critical, format, value...)
}

// Shorthand for plog.LevelPrint(Error, value).
func (plog *Plog) Error(value ...interface{}) {
	plog.LevelPrint(Error, value...)
}

// Shorthand for plog.LevelPrintf(Error, value).
func (plog *Plog) Errorf(format string, value ...interface{}) {
	plog.LevelPrintf(Error, format, value...)
}

// Shorthand for plog.LevelPrint(Warning, value).
func (plog *Plog) Warning(value ...interface{}) {
	plog.LevelPrint(Warning, value...)
}

// Shorthand for plog.LevelPrintf(Warning, value).
func (plog *Plog) Warningf(format string, value ...interface{}) {
	plog.LevelPrintf(Warning, format, value...)
}

// Shorthand for plog.LevelPrint(Notice, value).
func (plog *Plog) Notice(value ...interface{}) {
	plog.LevelPrint(Notice, value...)
}

// Shorthand for plog.LevelPrintf(Notice, value).
func (plog *Plog) Noticef(format string, value ...interface{}) {
	plog.LevelPrintf(Notice, format, value...)
}

// Shorthand for plog.LevelPrint(Info, value).
func (plog *Plog) Info(value ...interface{}) {
	plog.LevelPrint(Info, value...)
}

// Shorthand for plog.LevelPrintf(Info, value).
func (plog *Plog) Infof(format string, value ...interface{}) {
	plog.LevelPrintf(Info, format, value...)
}

// Shorthand for plog.LevelPrint(Debug, value).
func (plog *Plog) Debug(value ...interface{}) {
	plog.LevelPrint(Debug, value...)
}

// Shorthand for plog.LevelPrintf(Debug, value).
func (plog *Plog) Debugf(format string, value ...interface{}) {
	plog.LevelPrintf(Debug, format, value...)
}

// Logs value with Critical level, then calls os.Exit(1).
func (plog *Plog) Fatal(value ...interface{}) {
	plog.LevelPrint(Crit, value...)
	os.Exit(1)
}

// Logs arguments with Critical level, then calls os.Exit(1).
func (plog *Plog) Fatalf(format string, value ...interface{}) {
	plog.LevelPrintf(Crit, format, value...)
	os.Exit(1)
}

// Logs value with Critical level, then calls panic.
func (plog *Plog) Panic(value ...interface{}) {
	plog.LevelPrint(Crit, value...)
	panic(fmt.Sprint(value...))
}

// Logs arguments with Critical level, then calls panic.
func (plog *Plog) Panicf(format string, value ...interface{}) {
	plog.LevelPrintf(Crit, format, value...)
	panic(fmt.Sprintf(format, value...))
}
