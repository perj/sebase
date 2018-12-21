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
	"net"
	"net/http"
	"path"
	"strings"
	"time"

	"github.com/coreos/etcd/client"
	"github.com/schibsted/sebase/core/pkg/sd/sdr"
	"github.com/schibsted/sebase/plog/pkg/plog"
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

func etcdClient(url string, TLS *tls.Config) (client.Client, error) {
	t := &http.Transport{
		Proxy: http.ProxyFromEnvironment,
		Dial: (&net.Dialer{
			Timeout:   30 * time.Second,
			KeepAlive: 30 * time.Second,
		}).Dial,
		TLSHandshakeTimeout: 10 * time.Second,
		TLSClientConfig:     TLS,
	}
	cfg := client.Config{
		Endpoints: []string{url},
		Transport: t,
	}
	return client.New(cfg)
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
	Client         client.Client
	Kapi           client.KeysAPI
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
// Errors returned are from the call to NewClient in the etcd/client package.
func NewWatcher(url, prefix string, TLS *tls.Config) (*Watcher, error) {
	cl, err := etcdClient(url, TLS)
	if err != nil {
		return nil, err
	}
	w := &Watcher{
		Client:         cl,
		Kapi:           client.NewKeysAPI(cl),
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
	rch := make(chan *client.Response)
	for {
		cancel := func() {}
		if len(w.conns) > 0 {
			var ctx context.Context
			ctx, cancel = context.WithCancel(context.Background())
			plog.Debug.Printf("etcd-watcher: Watching %s index %d", w.Prefix, w.index)
			ew := w.Kapi.Watcher(w.Prefix, &client.WatcherOptions{AfterIndex: w.index, Recursive: true})
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
		plog.Debug.Printf("etcd-watcher: remove %s", cmd.conn.srvpath)
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

	var rsp *client.Response
	// Loop on some errors, e.g failure to connect.
	retry := true
	for retry {
		retry = false
		ctx, cancel := context.WithTimeout(cmd.ctx, w.RequestTimeout)
		plog.Debug.Printf("etcd-watcher: Initial GET %s", srvpath)
		nrsp, err := w.Kapi.Get(ctx, srvpath, &client.GetOptions{Recursive: true})
		// Try to whitelist some errors for retry, but abort on unknown ones.
		switch err := err.(type) {
		case nil:
			// Success
			rsp = nrsp
		case client.Error:
			switch err.Code {
			case client.ErrorCodeKeyNotFound:
				// Not really an error, treat as success.
			case client.ErrorCodeUnauthorized:
				plog.Error.Printf("etcd-watcher: %v", err)
				cmd.err = err
			default:
				retry = true
				plog.Warning.Printf("etcd-watcher: %v", err)
			}
			if w.index == 0 {
				w.index = err.Index
			}
		case *client.ClusterError:
			// Assume this is a temporary error such as a 500 response.
			plog.Warning.Printf("etcd-watcher: %v", err)
			retry = true
			// Check for TLS error though.
			for _, e := range err.Errors {
				ope, ok := e.(*net.OpError)
				if !ok {
					continue
				}
				// Seriously the best check I can find.
				if ope.Op == "remote error" || ope.Op == "local error" {
					cmd.err = err
					retry = false
					break
				}
			}
		default:
			if err == context.DeadlineExceeded {
				retry = true
			} else {
				plog.Warning.Printf("etcd-watcher: Initial GET unknown error (%T): %v", err, err)
				cmd.err = err
			}
		}
		if retry {
			plog.Info.Printf("etcd-watcher: Initial fetch failed, retrying.")
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

func (w *Watcher) handleResponse(rsp *client.Response, flush bool) {
	if w.index == 0 {
		w.index = rsp.Index
	}
	if flush {
		plog.Debug.Printf("etcd-watcher: flush")
		for _, conn := range w.conns {
			conn.lastIndex = w.index
			conn.channel <- sdr.Message{Index: w.index, Type: sdr.Flush}
		}
	}
	switch rsp.Action {
	case "get", "update", "create", "set":
		w.handleUpdate(rsp.Node)
	case "expire", "delete":
		w.handleDelete(rsp.Node)
	default:
		plog.Error.Printf("etcd-watcher: Unexpected action %s", rsp.Action)
	}
	for _, conn := range w.conns {
		if conn.lastIndex == w.index {
			conn.channel <- sdr.Message{Index: w.index, Type: sdr.EndOfBatch}
		}
	}
}

func (w *Watcher) handleUpdate(node *client.Node) {
	if node.ModifiedIndex >= w.index {
		w.index = node.ModifiedIndex
	}

	if !node.Dir {
		if strings.HasPrefix(node.Key, w.Prefix) {
			w.handleValue(sdr.Update, node.Key, node.Value)
		}
	} else {
		if strings.HasPrefix(node.Key, w.Prefix) || strings.HasPrefix(w.Prefix, node.Key) {
			for _, n := range node.Nodes {
				w.handleUpdate(n)
			}
		}
	}
}

func (w *Watcher) handleDelete(node *client.Node) {
	if node.ModifiedIndex >= w.index {
		w.index = node.ModifiedIndex
	}

	if strings.HasPrefix(node.Key, w.Prefix) {
		w.handleValue(sdr.Delete, node.Key, "")
	}
}

func (w *Watcher) handleValue(mtype sdr.MessageType, key, value string) {
	for _, conn := range w.conns {
		if strings.HasPrefix(key, conn.srvpath) {
			ukey := strings.TrimPrefix(key[len(conn.srvpath):], "/")
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
