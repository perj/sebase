// Copyright 2018 Schibsted

package main

import (
	"sync"
	"time"

	"github.com/schibsted/sebase/plog/internal/pkg/plogproto"
)

type SessionOutput interface {
	OpenDict(key string) (SessionOutput, error)
	OpenList(key string) (SessionOutput, error)
	Write(key string, value interface{}) error
	Close(proper, lastRef bool)
	ConfKey() string
}

type Session struct {
	store          *SessionStorage
	SessionType    plogproto.CtxType
	Writer         SessionOutput
	StartTimestamp time.Time
}

func (sess *Session) Close(proper bool) {
	sess.store.lock.Lock()
	lastRef := false
	if sess.SessionType == plogproto.CtxType_count {
		ck := sess.Writer.ConfKey()
		lastRef = sess.store.CountRefs[ck] == 1
		if lastRef {
			delete(sess.store.CountRefs, ck)
		} else {
			sess.store.CountRefs[ck]--
		}
	}
	sess.store.lock.Unlock()
	sess.Writer.Close(proper, lastRef)
}

type SessionStorage struct {
	lock      sync.Mutex
	CountRefs map[string]int
}

func (store *SessionStorage) newSession(output SessionOutput, stype plogproto.CtxType) (*Session, error) {
	store.lock.Lock()
	defer store.lock.Unlock()

	sess := &Session{store, stype, output, time.Now()}

	if stype == plogproto.CtxType_count {
		if store.CountRefs == nil {
			store.CountRefs = make(map[string]int)
		}
		store.CountRefs[output.ConfKey()]++
	}
	return sess, nil
}
