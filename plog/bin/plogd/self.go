// Copyright 2018 Schibsted

package main

import (
	"context"
	"fmt"
	"os"

	"github.com/schibsted/sebase/plog/internal/pkg/plogproto"
	"github.com/schibsted/sebase/plog/pkg/plog"
	"github.com/schibsted/sebase/plog/pkg/plogd"
	"github.com/schibsted/sebase/util/pkg/slog"
)

type selfSession struct {
	*Session
}

func (s selfSession) Write(p []byte) (n int, err error) {
	if err := s.Writer.Write("log", string(p)); err != nil {
		return 0, err
	}
	return len(p), nil
}

func newSelfSession(dataStore *DataStorage, sessionStore *SessionStorage) (*selfSession, error) {
	output, err := dataStore.findOutput("plogd", plogproto.CtxType_log)
	if err != nil {
		return nil, err
	}

	sess, err := sessionStore.newSession(output, plogproto.CtxType_log)
	if err != nil {
		return nil, err
	}

	return &selfSession{sess}, nil
}

func (s selfSession) InjectSlog() {
	slog.Critical = selfSessionLevel{s, plog.Critical.Code()}.LogMsg
	slog.Error = selfSessionLevel{s, plog.Error.Code()}.LogMsg
	slog.Warning = selfSessionLevel{s, plog.Warning.Code()}.LogMsg
	slog.Info = selfSessionLevel{s, plog.Info.Code()}.LogMsg
	slog.Debug = selfSessionLevel{s, plog.Debug.Code()}.LogMsg

	slog.CtxCritical = selfSessionLevel{s, plog.Critical.Code()}.LogCtxMsg
	slog.CtxError = selfSessionLevel{s, plog.Error.Code()}.LogCtxMsg
	slog.CtxWarning = selfSessionLevel{s, plog.Warning.Code()}.LogCtxMsg
	slog.CtxInfo = selfSessionLevel{s, plog.Info.Code()}.LogCtxMsg
	slog.CtxDebug = selfSessionLevel{s, plog.Debug.Code()}.LogCtxMsg
}

type selfSessionLevel struct {
	selfSession
	level string
}

func (s selfSessionLevel) LogMsg(msg string, kvs ...interface{}) {
	m := slog.KVsMap(kvs...)
	if len(m) == 0 {
		s.Writer.Write(s.level, msg)
	} else {
		m["msg"] = msg
		s.Writer.Write(s.level, m)
	}
}

func (s selfSessionLevel) LogCtxMsg(ctx context.Context, msg string, kvs ...interface{}) {
	// Avoid infinite recursing messages about plogd problems.
	if plogd.ContextProg(ctx) == "plogd" {
		stderrLogger.LogMsg(msg, kvs...)
	} else {
		s.LogMsg(msg, kvs...)
	}
}

var stderrLogger = slog.DefaultLogger{stderrPrintf}

func stderrPrintf(format string, v ...interface{}) {
	fmt.Fprintf(os.Stderr, format, v...)
}
