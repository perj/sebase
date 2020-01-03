// Copyright 2020 Schibsted

package plog

import (
	"fmt"
	"os"
)

// LevelPrint logs with a standard level key.
// Only logs if level is above the threshold given to Setup (defaults to Info).
// The value is formatted via fmt.Sprint. For JSON formatting use LogMsg.
func (plog *Plog) LevelPrint(level Level, value ...interface{}) {
	if plog == nil || level > SetupLevel {
		return
	}
	plog.Log(level.Code(), fmt.Sprint(value...))
}

// LevelPrintf logs with a standard level key.
// Only logs if level is above the threshold given to Setup (defaults to Info).
// The value is formatted via fmt.Sprintf. For JSON formatting use LogMsg.
func (plog *Plog) LevelPrintf(level Level, format string, value ...interface{}) {
	if plog == nil || level > SetupLevel {
		return
	}
	plog.Log(level.Code(), fmt.Sprintf(format, value...))
}

// Emergency is shorthand for plog.LevelPrint(Emergency, value).
func (plog *Plog) Emergency(value ...interface{}) {
	plog.LevelPrint(Emergency, value...)
}

// Emergencyf is shorthand for plog.LevelPrintf(Emergency, value).
func (plog *Plog) Emergencyf(format string, value ...interface{}) {
	plog.LevelPrintf(Emergency, format, value...)
}

// Alert is shorthand for plog.LevelPrint(Alert, value).
func (plog *Plog) Alert(value ...interface{}) {
	plog.LevelPrint(Alert, value...)
}

// Alertf is shorthand for plog.LevelPrintf(Alert, value).
func (plog *Plog) Alertf(format string, value ...interface{}) {
	plog.LevelPrintf(Alert, format, value...)
}

// Critical is shorthand for plog.LevelPrint(Critical, value).
func (plog *Plog) Critical(value ...interface{}) {
	plog.LevelPrint(Critical, value...)
}

// Criticalf is shorthand for plog.LevelPrintf(Critical, value).
func (plog *Plog) Criticalf(format string, value ...interface{}) {
	plog.LevelPrintf(Critical, format, value...)
}

// Error is shorthand for plog.LevelPrint(Error, value).
func (plog *Plog) Error(value ...interface{}) {
	plog.LevelPrint(Error, value...)
}

// Errorf is shorthand for plog.LevelPrintf(Error, value).
func (plog *Plog) Errorf(format string, value ...interface{}) {
	plog.LevelPrintf(Error, format, value...)
}

// Warning is shorthand for plog.LevelPrint(Warning, value).
func (plog *Plog) Warning(value ...interface{}) {
	plog.LevelPrint(Warning, value...)
}

// Warningf is shorthand for plog.LevelPrintf(Warning, value).
func (plog *Plog) Warningf(format string, value ...interface{}) {
	plog.LevelPrintf(Warning, format, value...)
}

// Notice is shorthand for plog.LevelPrint(Notice, value).
func (plog *Plog) Notice(value ...interface{}) {
	plog.LevelPrint(Notice, value...)
}

// Noticef is shorthand for plog.LevelPrintf(Notice, value).
func (plog *Plog) Noticef(format string, value ...interface{}) {
	plog.LevelPrintf(Notice, format, value...)
}

// Info is shorthand for plog.LevelPrint(Info, value).
func (plog *Plog) Info(value ...interface{}) {
	plog.LevelPrint(Info, value...)
}

// Infof is shorthand for plog.LevelPrintf(Info, value).
func (plog *Plog) Infof(format string, value ...interface{}) {
	plog.LevelPrintf(Info, format, value...)
}

// Debug is shorthand for plog.LevelPrint(Debug, value).
func (plog *Plog) Debug(value ...interface{}) {
	plog.LevelPrint(Debug, value...)
}

// Debugf is shorthand for plog.LevelPrintf(Debug, value).
func (plog *Plog) Debugf(format string, value ...interface{}) {
	plog.LevelPrintf(Debug, format, value...)
}

// Fatal logs value with Critical level, then calls os.Exit(1).
func (plog *Plog) Fatal(value ...interface{}) {
	plog.LevelPrint(Crit, value...)
	os.Exit(1)
}

// Fatalf logs arguments with Critical level, then calls os.Exit(1).
func (plog *Plog) Fatalf(format string, value ...interface{}) {
	plog.LevelPrintf(Crit, format, value...)
	os.Exit(1)
}

// Panic logs value with Critical level, then calls panic.
func (plog *Plog) Panic(value ...interface{}) {
	plog.LevelPrint(Crit, value...)
	panic(fmt.Sprint(value...))
}

// Panicf logs arguments with Critical level, then calls panic.
func (plog *Plog) Panicf(format string, value ...interface{}) {
	plog.LevelPrintf(Crit, format, value...)
	panic(fmt.Sprintf(format, value...))
}
