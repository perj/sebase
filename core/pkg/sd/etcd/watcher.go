// Copyright 2018 Schibsted

// Etcd driver for the SD registry package.
//
// Most of the Etcd driver is implemented by an Etcd watcher. The watcher is
// mostly meant to be used by the driver, but if you have a similar use case
// you can use the watcher directly.
package etcd

import (
	"context"
	"crypto/tls"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"path"
	"strings"
	"time"

	"github.com/schibsted/sebase/core/internal/pkg/etcdlight"
	"github.com/schibsted/sebase/core/pkg/sd/sdr"
	"github.com/schibsted/sebase/util/pkg/slog"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

// Default SourceConn channel buffer size, in number of messages.
// The thread sending these messages should preferably not block too long,
// since that can make it tough to catch up. Thus this should be big enough
// for normal usage. Can Also be set per watcher.
var MessageBufSz int = 10

// Timeout for non-waiting GET requests.
var RequestTimeout = 2 * time.Second

func init() {
	sdr.InitSourceType(&etcdSourceType{sdr.SourceTypeTemplate{"etcd", "etcd_url"}})
}

type etcdSourceType struct {
	sdr.SourceTypeTemplate
}

func etcdClient(burl string, TLS *tls.Config) (*etcdlight.KAPI, error) {
	baseurl, err := url.Parse(burl)
	if err != nil {
		return nil, err
	}
	t := &http.Transport{
		Proxy: http.ProxyFromEnvironment,
		Dial: (&net.Dialer{
			Timeout:   30 * time.Second,
			KeepAlive: 30 * time.Second,
		}).Dial,
		TLSHandshakeTimeout: 10 * time.Second,
		TLSClientConfig:     TLS,
	}
	kapi := &etcdlight.KAPI{
		Client:  &http.Client{Transport: t},
		BaseURL: baseurl,
	}
	return kapi, nil
}

func (e *etcdSourceType) SdrSourceSetup(conf bconf.Bconf, TLS *tls.Config) (sdr.SourceInstance, error) {
	url := conf.Get("sd.etcd_url").String("http://localhost:2379")
	prefix := conf.Get("sd.etcd.prefix").String("/service/")
	w, err := NewWatcher(url, prefix, TLS)
	if w != nil {
		w.MessageBufSz = conf.Get("sd.etcd.go.message_buf_sz").Int(MessageBufSz)
	}
	return w, err
}

type Watcher struct {
	Kapi           *etcdlight.KAPI
	Prefix         string
	RequestTimeout time.Duration
	MessageBufSz   int

	index  uint64
	conns  []*watcherConn
	commCh chan commMsg
	closed chan struct{}
}

// Returns a new Etcd Watcher for the given etcd URL and path prefix.
// Note that we do not sync the etcd nodes by default, since a proxy connection
// is assumed. You could sync them manually though.
//
// The TLS pointer is optional, used for HTTPS connections if given.
//
// Errors returned are from url.Parse
func NewWatcher(url, prefix string, TLS *tls.Config) (*Watcher, error) {
	kapi, err := etcdClient(url, TLS)
	if err != nil {
		return nil, err
	}
	w := &Watcher{
		Kapi:           kapi,
		Prefix:         prefix,
		RequestTimeout: 2 * time.Second,
		MessageBufSz:   MessageBufSz,

		commCh: make(chan commMsg),
		closed: make(chan struct{}),
	}
	// We don't sync since we're assuming a proxy connection.
	go w.run()
	return w, nil

}

func (w *Watcher) run() {
	defer close(w.closed)
	rch := make(chan *etcdlight.Response)
	for {
		cancel := func() {}
		if len(w.conns) > 0 {
			var ctx context.Context
			ctx, cancel = context.WithCancel(context.Background())
			slog.Debug("etcd-watcher: Watching prefix", "prefix", w.Prefix, "index", w.index)
			ew := w.Kapi.Watch(w.Prefix, true, w.index)
			go func() {
				rsp, err := ew.Next(ctx)
				if err == nil {
					rch <- rsp
				}
			}()
		}
		select {
		case cmd, ok := <-w.commCh:
			cancel()
			if ok {
				w.doCommand(cmd)
			} else {
				return
			}
		case rsp := <-rch:
			cancel()
			w.handleResponse(rsp, false)
		}
	}
	for _, conn := range w.conns {
		close(conn.channel)
	}
}

type commMsg interface {
	commMsg()
}

type addService struct {
	ctx     context.Context
	service string
	conn    *watcherConn
	err     error
	done    chan struct{}
}

func (cmd *addService) commMsg() {}

type removeService struct {
	conn *watcherConn
	done chan struct{}
}

func (cmd *removeService) commMsg() {}

func (w *Watcher) doCommand(cmd commMsg) {
	switch cmd := cmd.(type) {
	case *addService:
		w.doAddService(cmd)
	case *removeService:
		slog.Debug("etcd-watcher: remove service", "service", cmd.conn.srvpath)
		for idx, conn := range w.conns {
			if conn == cmd.conn {
				close(conn.channel)
				w.conns = append(w.conns[:idx], w.conns[idx+1:]...)
				break
			}
		}
		cmd.done <- struct{}{}
	}
}

func (w *Watcher) doAddService(cmd *addService) {
	srvpath := path.Join(w.Prefix, cmd.service)

	var rsp *etcdlight.Response
	// Loop on some errors, e.g failure to connect.
	retry := true
	for retry {
		retry = false
		ctx, cancel := context.WithTimeout(cmd.ctx, w.RequestTimeout)
		slog.Debug("etcd-watcher: Initial GET", "service", srvpath)
		nrsp, err := w.Kapi.Get(ctx, srvpath, true)
		// Try to whitelist some errors for retry, but abort on unknown ones.
		switch err := err.(type) {
		case nil:
			// Success
			rsp = nrsp
		case *etcdlight.ErrorResponse:
			switch err.ErrorCode {
			case 100: // Not found
				// Not really an error, treat as success.
			case 110: // Unauthorized
				slog.Error("etcd-watcher: unauthorized", "error", err)
				cmd.err = err
			default:
				retry = true
				slog.Warning("etcd-watcher: Client error", "error", err)
			}
			if w.index == 0 {
				w.index = err.Index
			}
		case *url.Error:
			// Assume this is a temporary error.
			slog.Warning("etcd-watcher: network error", "error", err)
			retry = true
			// Check for TLS error though.
			ope, ok := err.Err.(*net.OpError)
			// Seriously the best check I can find.
			if ok && (ope.Op == "remote error" || ope.Op == "local error") {
				cmd.err = err
				retry = false
			}
		default:
			if err == context.DeadlineExceeded {
				retry = true
			} else {
				slog.Warning("etcd-watcher: Initial GET unknown error", "type", fmt.Sprintf("%T", err), "error", err)
				cmd.err = err
			}
		}
		if retry {
			slog.Info("etcd-watcher: Initial fetch failed, retrying.")
			// Waiting here should give a default timeout interval.
			// Also detect parent context exit.
			select {
			case <-cmd.ctx.Done():
				cmd.err = cmd.ctx.Err()
				retry = false
			case <-ctx.Done():
			}
		}
		cancel()
	}

	if cmd.err == nil {
		// Etcd documentation recommends to not block in the thread reading
		// the updates. Thus we use a buffered channel here, configurable size.
		cmd.conn = &watcherConn{w, srvpath, make(chan sdr.Message, w.MessageBufSz), 0}
		w.conns = append(w.conns, cmd.conn)
	}
	// Best to send done before trying to send to the channel, gives them
	// a chance to start to listen.
	cmd.done <- struct{}{}

	if rsp != nil {
		w.handleResponse(rsp, false)
	}
}

func (w *Watcher) handleResponse(rsp *etcdlight.Response, flush bool) {
	if w.index == 0 {
		w.index = rsp.MaxIndex
	}
	if flush {
		slog.Debug("etcd-watcher: flush")
		for _, conn := range w.conns {
			conn.lastIndex = w.index
			conn.channel <- sdr.Message{Index: w.index, Type: sdr.Flush}
		}
	}
	switch rsp.Action {
	case "get", "update", "create", "set":
		w.handleUpdate(rsp.Values)
	case "expire", "delete":
		w.handleDelete(rsp.Values)
	default:
		slog.Error("etcd-watcher: Unexpected action", "action", rsp.Action)
	}
	for _, conn := range w.conns {
		if conn.lastIndex == w.index {
			conn.channel <- sdr.Message{Index: w.index, Type: sdr.EndOfBatch}
		}
	}
}

func (w *Watcher) handleUpdate(kvs []etcdlight.KV) {
	for _, node := range kvs {
		if strings.HasPrefix(node.Key, w.Prefix) {
			w.handleValue(sdr.Update, node.Key, node.Value)
		}
	}
}

func (w *Watcher) handleDelete(kvs []etcdlight.KV) {
	for _, node := range kvs {
		if strings.HasPrefix(node.Key, w.Prefix) {
			w.handleValue(sdr.Delete, node.Key, "")
		}
	}
}

func (w *Watcher) handleValue(mtype sdr.MessageType, key, value string) {
	for _, conn := range w.conns {
		if strings.HasPrefix(key, conn.srvpath) {
			ukey := strings.TrimPrefix(key[len(conn.srvpath):], "/")
			slog.Debug("etcd-watcher: sending", "mtype", mtype, "service", conn.srvpath, "key", ukey, "value", value)
			slash := strings.LastIndexByte(ukey, '/')
			msg := sdr.Message{Index: w.index, Type: mtype, Value: value}
			if slash >= 0 {
				msg.HostKey = ukey[:slash]
				msg.Key = ukey[slash+1:]
			} else {
				msg.HostKey = ukey
			}
			conn.lastIndex = w.index
			conn.channel <- msg
		} else {
			slog.Debug("not sending", "key", key, "service", conn.srvpath)
		}
	}
}

// Start listening for updates on the service path in etcd, combined with the
// watcher prefix.  It's assumed that the first step in this directory are the
// host keys, and the rest of the path are value keys.
// This will wait until a valid response is received from etcd,
// you can abort it by cancelling the context.
func (w *Watcher) Connect(ctx context.Context, service string, conf bconf.Bconf) (sdr.SourceConn, error) {
	req := addService{
		ctx:     ctx,
		service: service,
		done:    make(chan struct{}),
	}
	w.commCh <- &req
	<-req.done
	if req.conn == nil {
		return nil, req.err
	}
	return req.conn, req.err
}

// Tell the watcher to terminate. It's an error to call this if there's still
// any open channels. They need to be closed first, or panics might occur.
func (w *Watcher) Close() error {
	close(w.commCh)
	<-w.closed
	return nil
}

// watcherConn implements the sdr.SourceConn interface for the etcd driver.
type watcherConn struct {
	watcher   *Watcher
	srvpath   string
	channel   chan sdr.Message
	lastIndex uint64
}

func (c *watcherConn) Channel() <-chan sdr.Message {
	return c.channel
}

func (c *watcherConn) Close() error {
	if c.watcher.commCh == nil {
		return nil
	}
	req := removeService{
		conn: c,
		done: make(chan struct{}),
	}
	c.watcher.commCh <- &req
	<-req.done
	return nil
}
