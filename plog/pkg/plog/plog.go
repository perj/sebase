// Copyright 2018 Schibsted

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

// A plog context is an object opened in the log server. The server will
// keep track of it and all its contents until it's closed (and beyond,
// in case of state contexts). If a program crashes/exits without closing
// the context the server will detect this and log an "@interrupted" key
// for easier debugging.
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

// Returns the default context if the level is enabled by the threshold given
// to setup, otherwise nil.
// It's safe to call functions on nil contexts.
func IfEnabled(lvl Level) *Plog {
	if lvl > SetupLevel {
		return nil
	}
	return Default
}

// Open a new root logging plog context.
// If you called Setup, then you should probably use Default instead of this.
func NewPlogLog(appname string) *Plog {
	return openRoot([]string{appname}, plogproto.CtxType_log)
}

// Open a new root state plog context. State is kept in plogd and only logged
// with a "state" key once all state contexts for the appname are closed. You
// can also query it over HTTP from plogd.
func NewPlogState(appname string) *Plog {
	return openRoot([]string{appname}, plogproto.CtxType_state)
}

// Open a new root count plog context.  When integers are logged in this
// context, it's applied as a delta to the state. When the context is closed
// the integer values added are removed. Can be used to keep statistics such as
// number of open fds and aggregate them between multiple processes with the
// same name.
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

// Opens a sub-context dictionary. Once this and any sub-contexts to it are
// closed, a dictionary object will be logged in the parent plog.
func (pctx *Plog) OpenDict(key string) *Plog {
	return pctx.openSub([]string{key}, plogproto.CtxType_dict)
}

// Opens a sub-context list. Once this and any sub-contexts to it are closed, a
// list will be logged in the parent plog.
func (pctx *Plog) OpenList(key string) *Plog {
	return pctx.openSub([]string{key}, plogproto.CtxType_list)
}

func (pctx *Plog) openSub(key []string, ctype plogproto.CtxType) *Plog {
	if pctx == nil {
		return nil
	}
	ctx := &Plog{pctx: pctx}
	ctx.conn, ctx.generation = pctx.conn.retain()
	ctx.ctype = ctype
	ctx.id = atomic.AddUint64(&ctxId, 1)
	ctx.key = key
	if !ctx.openContext() {
		ctx.conn.reconnect()
		ctx.checkGeneration()
		if ctx.conn.Writer == nil {
			ts, _ := json.Marshal(time.Now())
			ctx.fallbackWrite("start_timestamp", ts)
		}
	}
	return ctx
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

// Encode value as JSON and log it. Might return errors from json.Marshal,
// but that should only happen in very rare cases. For strings, ints and
// other basic types you can ignore the return value.
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

// Log value as if it was a string.
func (ctx *Plog) LogAsString(key string, value []byte) {
	// Could possibly optimize this later.
	err := ctx.Log(key, string(value))
	if err != nil {
		panic("Failed to json encode string: " + err.Error())
	}
}

// Log a JSON dictionary from the variadic arguments, which are parsed with
// slog.KVsMap.  Note that the first argument is not part of the dictionary,
// it's the message key.
// Might return errors from json.Marshal.
func (ctx *Plog) LogDict(key string, kvs ...interface{}) error {
	if ctx == nil {
		return nil
	}
	return ctx.Log(key, slog.KVsMap(kvs...))
}

// Log a human readable message with a JSON dictionary from the variadic
// arguments, which are parsed with slog.KVsMap.
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

// Log raw JSON encoded data in value. You must make sure the JSON is valid,
// or the context will be aborted.
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

func (c *refconn) release() {
	refs := atomic.AddUint64(&c.refs, ^uint64(0))
	if refs == 0 {
		c.Close()
	}
}

// Lock and reconnect, increasing generation.
func (c *refconn) reconnect() {
	gen := c.generation
	c.Lock()
	if gen == c.generation {
		var err error
		wasnil := c.Writer == nil
		c.Writer, err = c.dial(os.Getenv("PLOG_SOCKET"))
		if err != nil {
			// Errors are silently ignored here. Typically ENOFILE.
			c.Writer = nil
		}
		if c.Writer != nil || !wasnil {
			atomic.AddUint64(&c.generation, 1)
		}
	}
	c.Unlock()
}

// Messages sent to the channel created by NewTestLogContext
// Value will be json encoded data.
type TestMessage struct {
	CtxId uint64
	Key   string
	Value []byte
}

// Create a custom root log context connected to the channel, for testing.
// Only messages are sent on the channel, not open or close.
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
			return nil, fmt.Errorf("Can't reconnect.")
		},
	}
	pctx.conn.Writer = plogproto.NewWriter(w, false)
	pctx.conn.generation = 1
	pctx.checkGeneration()
	return pctx, ch
}
