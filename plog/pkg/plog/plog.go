// Copyright 2020 Schibsted

package plog

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/schibsted/sebase/plog/internal/pkg/plogproto"
	"github.com/schibsted/sebase/util/pkg/slog"
)

// Plog is a context object opened in the log server. The server will
// keep track of it and all its contents until it's closed (and beyond,
// in case of state contexts). If a program crashes/exits without closing
// the context the server will detect this and log an "@interrupted" key
// for easier debugging.
// Can also be used without the log server in which case it logs to the
// fallback writer.
type Plog struct {
	pctx *Plog

	conn       *refconn
	generation uint64

	id    uint64
	ctype plogproto.CtxType
	key   []string
}

// Default plog context, opened by Setup.
var Default *Plog

// Each new context opened get a new ctxId. This is a global because it's also
// used in case we use a fallback when plogd is not running.
var ctxId uint64

// IfEnabled returns the default context if the level is enabled by the
// threshold given to setup, otherwise nil.
// It's safe to call functions on nil contexts.
func IfEnabled(lvl Level) *Plog {
	if lvl > SetupLevel {
		return nil
	}
	return Default
}

// NewPlogLog opens a new root logging plog context.
// If you called Setup, then you should probably use Default instead of this.
func NewPlogLog(appname string) *Plog {
	return openRoot([]string{appname}, plogproto.CtxType_log)
}

// NewPlogState opens a new root state plog context. State is kept in plogd and
// only logged with a "state" key once all state contexts for the appname are
// closed. You can also query it over HTTP from plogd.
func NewPlogState(appname string) *Plog {
	return openRoot([]string{appname}, plogproto.CtxType_state)
}

// NewPlogCount opens a new root count plog context.  When integers are logged
// in this context, it's applied as a delta to the state. When the context is
// closed the integer values added are removed. Can be used to keep statistics
// such as number of open fds and aggregate them between multiple processes
// with the same name.
func NewPlogCount(appname string, path ...string) *Plog {
	return openRoot(append([]string{appname}, path...), plogproto.CtxType_count)
}

func openRoot(key []string, ctype plogproto.CtxType) *Plog {
	ctx := &Plog{}
	ctx.id = atomic.AddUint64(&ctxId, 1)
	ctx.ctype = ctype
	ctx.key = key

	ctx.conn = &refconn{refs: 1, dial: plogproto.NewClientConn}
	ctx.conn.reconnect()
	ctx.checkGeneration()
	return ctx
}

// OpenDict opens a sub-context dictionary. Once this and any sub-contexts to
// it are closed, a dictionary object will be logged in the parent plog.
func (ctx *Plog) OpenDict(key string) *Plog {
	return ctx.openSub([]string{key}, plogproto.CtxType_dict)
}

// OpenList opens a sub-context list. Once this and any sub-contexts to it are
// closed, a list will be logged in the parent plog.
func (ctx *Plog) OpenList(key string) *Plog {
	return ctx.openSub([]string{key}, plogproto.CtxType_list)
}

// OpenListOfDicts opens a sub-context list. Once this and any sub-contexts to
// it are closed, a list will be logged in the parent plog, where each element
// is a dictionary with a single element, the key value pair given to Log*.
// This context type is useful for putting logs in a sub-key but does drop the
// timestamps of the individual messages.
func (ctx *Plog) OpenListOfDicts(key string) *Plog {
	return ctx.openSub([]string{key}, plogproto.CtxType_list_of_dicts)
}

func (ctx *Plog) openSub(key []string, ctype plogproto.CtxType) *Plog {
	if ctx == nil {
		return nil
	}
	sctx := &Plog{pctx: ctx}
	sctx.conn, sctx.generation = ctx.conn.retain()
	sctx.ctype = ctype
	sctx.id = atomic.AddUint64(&ctxId, 1)
	sctx.key = key
	if !sctx.openContext() {
		sctx.conn.reconnect()
		sctx.checkGeneration()
		if sctx.conn.Writer == nil {
			ts, _ := json.Marshal(time.Now())
			sctx.fallbackWrite("start_timestamp", ts)
		}
	}
	return sctx
}

// Close the context, marking it as properly closed in the server.
// For subcontext, this is what triggers it being sent to the parent
// context.
func (ctx *Plog) Close() error {
	if ctx == nil {
		return nil
	}
	var err error
	if ctx.conn.Writer != nil {
		err = ctx.conn.SendClose(ctx.id)
	} else {
		ts, _ := json.Marshal(time.Now())
		ctx.fallbackWrite("@timestamp", ts)
	}
	ctx.conn.release()
	return err
}

// Log encodes value as JSON and logs it. Might return errors from
// json.Marshal, but that should only happen in very rare cases. For strings,
// ints and other basic types you can ignore the return value.
// This is a low-level function, consider using LogMsg instead.
func (ctx *Plog) Log(key string, value interface{}) error {
	if ctx == nil {
		return nil
	}
	v, err := json.Marshal(value)
	if err != nil {
		return err
	}
	ctx.send(key, v)
	return nil
}

// LogAsString logs value as if it was a string.
func (ctx *Plog) LogAsString(key string, value []byte) {
	// Could possibly optimize this later.
	err := ctx.Log(key, string(value))
	if err != nil {
		panic("Failed to json encode string: " + err.Error())
	}
}

// LogDict logs a JSON dictionary from the variadic arguments, which are parsed
// with slog.KVsMap.  Note that the first argument is not part of the
// dictionary, it's the message key.
// Deprecated in favor of LogMsg.
// Might return errors from json.Marshal.
func (ctx *Plog) LogDict(key string, kvs ...interface{}) error {
	if ctx == nil {
		return nil
	}
	return ctx.Log(key, slog.KVsMap(kvs...))
}

// LogMsg logs a human readable message with a JSON dictionary from the
// variadic arguments, which are parsed with slog.KVsMap.
// This function does not return errors. If json encoding fails it
// converts to a string and tries again, adding a "log-error" key.
func (ctx *Plog) LogMsg(key, msg string, kvs ...interface{}) {
	if ctx == nil {
		return
	}
	m := slog.KVsMap(kvs...)
	m["msg"] = msg
	errWrap(ctx.Log, key, m)
}

// LogJSON logs raw JSON encoded data in value. You must make sure the JSON is
// valid, or the context will be aborted.
func (ctx *Plog) LogJSON(key string, value []byte) {
	if ctx == nil {
		return
	}
	ctx.send(key, value)
}

// Send a single open context message. Used when initially connecting
// and if any connection problems are detected.
func (ctx *Plog) openContext() bool {
	var open plogproto.OpenContext
	open.Ctxtype = &ctx.ctype
	open.Key = ctx.key
	if ctx.pctx != nil {
		if ctx.pctx.id == 0 {
			// This is not arbitraty, the server checks this and aborts as well.
			panic("Can't open sub contexts on cached contexts.")
		}
		open.ParentCtxId = &ctx.pctx.id
	}
	if ctx.conn.Writer == nil {
		return false
	}
	return ctx.conn.SendOpen(ctx.id, &open) == nil
}

// Log a key-value pair. If connection is ok, sent to plogd, otherwise try to
// reconnect and finally do a fallback write.
func (ctx *Plog) send(key string, value []byte) {
	if ctx.conn.Writer == nil {
		ctx.conn.reconnect()
	}
	ctx.checkGeneration()
	ok := false
	if ctx.conn.Writer != nil {
		ok = ctx.conn.SendKeyValue(ctx.id, key, value) == nil
	}
	if !ok {
		ctx.conn.reconnect()
		ctx.checkGeneration()
		if ctx.conn.Writer != nil {
			ok = ctx.conn.SendKeyValue(ctx.id, key, value) == nil
		}
	}
	if !ok {
		ctx.fallbackWrite(key, value)
	}
}

func (ctx *Plog) fallbackWrite(key string, value []byte) {
	// Count stack height.
	n := 0
	for c := ctx; c != nil; c = c.pctx {
		n++
	}
	fkey := make([]FallbackKey, n+1)
	fkey[n].Key = key
	for c := ctx; c != nil; c = c.pctx {
		n--
		fkey[n].Key = strings.Join(c.key, ".")
		fkey[n].CtxId = c.id
	}
	FallbackFormatter(fkey, value)
}

// Re-open if generation mismatch. Do it on parents first otherwise the server
// might not recognize the parent context id.
func (ctx *Plog) checkGeneration() {
	if ctx.generation == ctx.conn.generation {
		return
	}
	if ctx.pctx != nil {
		ctx.pctx.checkGeneration()
	}
	ctx.generation = ctx.conn.generation
	ctx.openContext()
}

type refconn struct {
	sync.Mutex
	*plogproto.Writer
	refs       uint64
	generation uint64

	dial func(sock string) (*plogproto.Writer, error)
}

func (r *refconn) retain() (*refconn, uint64) {
	refs := atomic.AddUint64(&r.refs, 1)
	if refs <= 1 {
		panic("plog.refconn.retain got bad refs")
	}
	return r, r.generation
}

func (r *refconn) release() {
	refs := atomic.AddUint64(&r.refs, ^uint64(0))
	if refs == 0 {
		r.Close()
	}
}

// Lock and reconnect, increasing generation.
func (r *refconn) reconnect() {
	gen := r.generation
	r.Lock()
	if gen == r.generation {
		var err error
		wasnil := r.Writer == nil
		r.Writer, err = r.dial(os.Getenv("PLOG_SOCKET"))
		if err != nil {
			// Errors are silently ignored here. Typically ENOFILE.
			r.Writer = nil
		}
		if r.Writer != nil || !wasnil {
			atomic.AddUint64(&r.generation, 1)
		}
	}
	r.Unlock()
}

// TestMessage is used for messages sent to the channel created by
// NewTestLogContext. Value will be json encoded data.
type TestMessage struct {
	CtxId uint64
	Key   string
	Value []byte
}

// NewTestLogContext creates a custom root log context connected to the
// channel, for testing.  Only messages are sent on the channel, not open or
// close.
// Close the context and then wait for the channel to close for proper cleanup.
func NewTestLogContext(ctx context.Context, key ...string) (*Plog, <-chan TestMessage) {
	pctx := &Plog{}
	pctx.id = atomic.AddUint64(&ctxId, 1)
	pctx.ctype = plogproto.CtxType_log
	pctx.key = key

	r, w := io.Pipe()
	ch := make(chan TestMessage)
	go func() {
		defer close(ch)
		pr := plogproto.NewReader(r, false)
		defer pr.Close()
		for {
			var plog plogproto.Plog
			if err := pr.Receive(&plog); err != nil {
				if err != io.EOF {
					// Shouldn't happen.
					panic(err)
				}
				return
			}
			for _, m := range plog.Msg {
				select {
				case <-ctx.Done():
					return
				case ch <- TestMessage{
					CtxId: *plog.CtxId,
					Key:   *m.Key,
					Value: m.Value,
				}:
				}
			}
		}
	}()
	pctx.conn = &refconn{
		refs: 1,
		dial: func(sock string) (*plogproto.Writer, error) {
			return nil, fmt.Errorf("can't reconnect")
		},
	}
	pctx.conn.Writer = plogproto.NewWriter(w, false)
	pctx.conn.generation = 1
	pctx.checkGeneration()
	return pctx, ch
}
