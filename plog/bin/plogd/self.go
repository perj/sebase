// Copyright 2018 Schibsted

package main

import "github.com/schibsted/sebase/plog/internal/pkg/plogproto"

type selfSession struct {
	*Session
}

func (s selfSession) Write(p []byte) (n int, err error) {
	if err := s.Session.Writer.Write("log", string(p)); err != nil {
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
