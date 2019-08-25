// Copyright 2018 Schibsted

package main

import (
	"errors"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/schibsted/sebase/plog/internal/pkg/plogproto"
	"github.com/schibsted/sebase/plog/pkg/plogd"
	"github.com/schibsted/sebase/util/pkg/slog"
)

const (
	interruptedKey = "@interrupted"
)

type smCode int

const (
	smClose smCode = iota
	smMsg
	smLastRef
)

type storageMessage struct {
	code           smCode
	key            string
	value          interface{}
	startTimestamp time.Time
}

type Storage struct {
	Prog            string
	State           map[string]interface{}
	storageSessions map[plogproto.CtxType]*storageSession
	channelQueue    chan progChannel
	sendState       chan string
	cbState         chan func(map[string]interface{})
}

func logger(prog string) slog.Logger {
	// Discard recursive errors that use our own prog.
	if prog == "plogd" {
		return slog.DefaultLogger{nil}.LogMsg
	}
	return slog.Error
}

type DataStorage struct {
	lock      sync.Mutex
	ProgStore map[string]*Storage
	Output    plogd.OutputWriter
	dumpEmpty bool
	// For tests
	testStatePing chan struct{}
}

/*
 * For storing data, we build up a hierarchy of open sessions.
 * There's a goroutine at each level which accepts input for that
 * level and keeps a reference to the parent object.
 * Once both the session and all sub objects are closed, the object
 * is sent to the parent and the parent reference released.
 */

type storageSession struct {
	channel chan storageMessage
	refs    uint32
	confKey string
	isState bool
}

var sm_pool = make(chan chan storageMessage, 256)

func newStorageSession(ck string, isState bool) *storageSession {
	select {
	case ch := <-sm_pool:
		return &storageSession{ch, 1, ck, isState}
	default:
		return &storageSession{make(chan storageMessage, 256), 1, ck, isState}
	}
}

func retainStorageSession(sess *storageSession) (*storageSession, uint32) {
	refs := atomic.AddUint32(&sess.refs, 1)
	return sess, refs
}

func releaseStorageSession(sess *storageSession) {
	if atomic.AddUint32(&sess.refs, ^uint32(0)) == 0 {
		sess.channel <- storageMessage{code: smClose}
	}
}

func reclaimSessionChannel(ch chan storageMessage) {
	select {
	case sm_pool <- ch:
	default:
	}
}

func (sess *storageSession) Write(key string, value interface{}) error {
	sess.channel <- storageMessage{smMsg, key, value, time.Time{}}
	return nil
}

func (sess *storageSession) Close(proper, lastRef bool) {
	if !proper {
		sess.channel <- storageMessage{smMsg, interruptedKey, true, time.Time{}}
	}
	if lastRef {
		sess.channel <- storageMessage{code: smLastRef}
	}
	releaseStorageSession(sess)
}

func (sess *storageSession) ConfKey() string {
	return sess.confKey
}

/* dict session */

type dictSession struct {
	storageSession
	parentSession  *storageSession
	startTimestamp time.Time
	key            string
	dict           map[string]interface{}
}

func dictStoreHandler(sess *dictSession) {
	for {
		inMsg := <-sess.storageSession.channel
		if inMsg.code == smClose {
			break
		}
		if inMsg.code == smLastRef {
			sess.parentSession.channel <- storageMessage{smMsg, sess.key, nil, sess.startTimestamp}
		} else if sess.isState {
			parentMessage := map[string]interface{}{inMsg.key: inMsg.value}
			sess.parentSession.channel <- storageMessage{smMsg, sess.key, parentMessage, sess.startTimestamp}
		} else {
			sess.dict[inMsg.key] = inMsg.value
		}
	}
	reclaimSessionChannel(sess.storageSession.channel)
	if !sess.isState {
		sess.parentSession.channel <- storageMessage{smMsg, sess.key, sess.dict, sess.startTimestamp}
	}
	releaseStorageSession(sess.parentSession)
}

func (parent *storageSession) OpenDict(key string) (SessionOutput, error) {
	ret := &dictSession{}
	ret.storageSession = *newStorageSession(parent.confKey+"."+key, parent.isState)
	ret.parentSession, _ = retainStorageSession(parent)
	ret.startTimestamp = time.Now()
	ret.key = key
	ret.dict = make(map[string]interface{})
	go dictStoreHandler(ret)
	return ret, nil
}

/* list session */

type listSession struct {
	storageSession
	parentSession  *storageSession
	startTimestamp time.Time
	key            string
	list           []interface{}
}

func listStoreHandler(sess *listSession) {
	for {
		inMsg := <-sess.storageSession.channel
		if inMsg.code == smClose {
			break
		}
		if inMsg.code == smMsg && inMsg.key != interruptedKey {
			sess.list = append(sess.list, inMsg.value)
		}
	}
	reclaimSessionChannel(sess.storageSession.channel)
	sess.parentSession.channel <- storageMessage{smMsg, sess.key, sess.list, sess.startTimestamp}
	releaseStorageSession(sess.parentSession)
}

func (parent *storageSession) OpenList(key string) (SessionOutput, error) {
	ret := &listSession{}
	ret.storageSession = *newStorageSession(parent.confKey+"."+key, parent.isState)
	ret.parentSession, _ = retainStorageSession(parent)
	ret.startTimestamp = time.Now()
	ret.key = key
	ret.list = make([]interface{}, 0, 10)
	go listStoreHandler(ret)
	return ret, nil
}

/* root session */

func sendState(dataStore *DataStorage, prog, keyPath string, value interface{}) {
	if value == nil {
		return
	}
	ks := strings.Split(keyPath, ".")
	for i := len(ks) - 1; i >= 0; i-- {
		value = map[string]interface{}{ks[i]: value}
	}
	outMsg := plogd.LogMessage{time.Now(), prog, "state", value, nil, "", nil}
	dataStore.Output.WriteMessage(logger(prog), outMsg)
}

func updateState(dataStore *DataStorage, progStore *Storage, node map[string]interface{}, stype plogproto.CtxType, key string, value interface{}, confKey string) {
	if value == nil {
		delete(node, key)
		return
	}
	switch value := value.(type) {
	case map[string]interface{}:
		m, ismap := node[key].(map[string]interface{})
		if !ismap {
			m = make(map[string]interface{})
			node[key] = m
		}
		for k, v := range value {
			updateState(dataStore, progStore, m, stype, k, v, confKey+"."+k)
		}
	case int:
		if stype == plogproto.CtxType_count {
			if f, ok := node[key].(int); ok {
				value += f
			}
			if value == 0 {
				/* Recurse with a delete. */
				updateState(dataStore, progStore, node, stype, key, nil, confKey)
				return
			}
		}
		node[key] = value
	default:
		node[key] = value
	}
}

func dumpState(dataStore *DataStorage, progStore *Storage) {
	if len(progStore.State) > 0 || dataStore.dumpEmpty {
		outMsg := plogd.LogMessage{time.Now(), progStore.Prog, "state", progStore.State, nil, "", nil}
		dataStore.Output.WriteMessage(logger(progStore.Prog), outMsg)
	}
}

func sendForKeyPath(dataStore *DataStorage, progStore *Storage, keyPath string) {
	node := progStore.State
	ismap := true
	var v interface{}
	for _, key := range strings.Split(keyPath, ".") {
		if !ismap {
			return
		}
		v = node[key]
		node, ismap = v.(map[string]interface{})
	}
	sendState(dataStore, progStore.Prog, keyPath, v)
}

type progChannel struct {
	key     plogproto.CtxType
	channel chan storageMessage
}

type muxMessage struct {
	stype   plogproto.CtxType
	channel chan storageMessage
	storageMessage
}

func progStoreMuxer(out chan<- muxMessage, ch progChannel) {
	for {
		msg := <-ch.channel
		out <- muxMessage{ch.key, ch.channel, msg}
		if msg.code == smClose {
			break
		}
	}
}

func progStoreHandler(dataStore *DataStorage, progStore *Storage) {
	muxCh := make(chan muxMessage)
	chs := make(map[plogproto.CtxType]chan storageMessage)
	for {
		dataStore.lock.Lock()
		select {
		case ch := <-progStore.channelQueue:
			chs[ch.key] = ch.channel
			go progStoreMuxer(muxCh, ch)
		default:
			if len(chs) == 0 {
				delete(dataStore.ProgStore, progStore.Prog)
			}
		}
		dataStore.lock.Unlock()
		if len(chs) == 0 {
			// Depleted even under lock.
			break
		}

		for len(chs) > 0 {
			select {
			case ch := <-progStore.channelQueue:
				chs[ch.key] = ch.channel
				go progStoreMuxer(muxCh, ch)
			case confKey := <-progStore.sendState:
				if confKey == "" {
					dumpState(dataStore, progStore)
				} else {
					sendForKeyPath(dataStore, progStore, confKey)
				}
			case cb := <-progStore.cbState:
				cb(progStore.State)
			case inMsg := <-muxCh:
				if inMsg.code == smClose {
					reclaimSessionChannel(inMsg.channel)
					if chs[inMsg.stype] == inMsg.channel {
						// Might've been replaced, only delete if latest.
						delete(chs, inMsg.stype)
					}
					continue
				}
				if inMsg.code == smLastRef {
					continue
				}

				if inMsg.stype != plogproto.CtxType_log {
					updateState(dataStore, progStore, progStore.State, inMsg.stype, inMsg.key, inMsg.value, inMsg.key)
					if dataStore.testStatePing != nil {
						close(dataStore.testStatePing)
					}
				} else {
					ts := &inMsg.startTimestamp
					if ts.IsZero() {
						ts = nil
					}
					outMsg := plogd.LogMessage{time.Now(), progStore.Prog, inMsg.key, inMsg.value, ts, "", nil}
					dataStore.Output.WriteMessage(logger(progStore.Prog), outMsg)
				}
			}
		}
	}

	// Make sure there aren't any queued state callbacks before we disintegrate.
cbState:
	for {
		select {
		case cb := <-progStore.cbState:
			cb(progStore.State)
		default:
			break cbState
		}
	}

	dumpState(dataStore, progStore)
}

func (store *DataStorage) findOutput(prog string, stype plogproto.CtxType) (SessionOutput, error) {
	store.lock.Lock()
	defer store.lock.Unlock()

	if store.ProgStore == nil {
		store.ProgStore = make(map[string]*Storage)
	}

	progStore := store.ProgStore[prog]
	var ssess *storageSession
	if progStore == nil {
		progStore = new(Storage)
		progStore.Prog = prog
		progStore.State = make(map[string]interface{})
		progStore.sendState = make(chan string, 256)
		progStore.storageSessions = make(map[plogproto.CtxType]*storageSession)
		progStore.channelQueue = make(chan progChannel, 5)
		progStore.cbState = make(chan func(map[string]interface{}), 1)

		go progStoreHandler(store, progStore)
		store.ProgStore[prog] = progStore
	} else {
		ssess = progStore.storageSessions[stype]
		if ssess != nil {
			_, refs := retainStorageSession(ssess)
			// release might've dropped the refs to 0 temporarily and thus closed the
			// channel. Reopen if so.
			if refs == 1 {
				ssess = nil
			}
		}
	}

	if ssess == nil {
		ssess = newStorageSession(prog, stype != plogproto.CtxType_log)
		select {
		case progStore.channelQueue <- progChannel{stype, ssess.channel}:
			break
		default:
			// This should be really really unlikely, but theoretically possible.
			// When buffer size was 1 it did sometimes happen in unit tests.
			// Easy solution is to abort.
			releaseStorageSession(ssess)
			return nil, ErrorTooManyConcurrentRequests
		}
		progStore.storageSessions[stype] = ssess
	}
	return ssess, nil
}

var (
	ErrorProgNotFound              = errors.New("Program was not found")
	ErrorTooManyConcurrentRequests = errors.New("Too many concurrent requests")
)

// Returns true if prog exists and the callback was queued. The callback will be called.
// Returns false if the callback will not be called, err will be one of:
// ErrorProgNotFound, ErrorTooManyConcurrentRequests
func (store *DataStorage) CallbackState(prog string, cb func(map[string]interface{})) (bool, error) {
	store.lock.Lock()
	defer store.lock.Unlock()

	progStore := store.ProgStore[prog]
	if progStore == nil {
		return false, ErrorProgNotFound
	}

	// Make sure to not hang here while we're holding the lock.
	select {
	case progStore.cbState <- cb:
		return true, nil
	default:
		return false, ErrorTooManyConcurrentRequests
	}
}
