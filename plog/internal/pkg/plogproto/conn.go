// Copyright 2018 Schibsted

package plogproto

import (
	"encoding/binary"
	fmt "fmt"
	"io"
	"net"
	"net/url"
	"sync"

	proto "github.com/golang/protobuf/proto"
)

//go:generate protoc --go_out=. plog.proto

const (
	// Default path to listen on / connect to.
	DefaultSock = "/run/plog/plog.sock"
)

// A convenience listener type. Set seqpacket to true in case that's
// the socket type used.
type Listener struct {
	net.Listener
	Seqpacket bool
}

// Call Accept on the contained listener and wrap the returned connection.
func (l Listener) Accept() (*Reader, error) {
	c, err := l.Listener.Accept()
	if err != nil {
		return nil, err
	}
	return NewReader(c, l.Seqpacket), nil
}

// Parses sock either as a URL or a path and opens a connection there.
// If sock is empty string the DefaultSock value is used.
// The underlying WriteCloser can be cast to net.Conn if necessary.
func NewClientConn(sock string) (*Writer, error) {
	surl, err := url.Parse(sock)
	if err != nil {
		return nil, err
	}
	if surl.Scheme == "" {
		if surl.Host != "" {
			surl.Scheme = "tcp"
		} else {
			surl.Scheme = "unix"
		}
	}
	if surl.Path == "" {
		surl.Path = DefaultSock
	}
	var conn net.Conn
	switch surl.Scheme {
	case "tcp", "tcp4", "tcp6":
		conn, err = net.Dial(surl.Scheme, surl.Host)
	case "unix", "unixpacket":
		conn, err = net.Dial(surl.Scheme, surl.Path)
	default:
		return nil, fmt.Errorf("Bad scheme in PLOG_SOCKET url")
	}
	return NewWriter(conn, surl.Scheme == "unixpacket"), err
}

// Type for receiving plog messages. Has Closer for easier use.
type Reader struct {
	io.ReadCloser
	Seqpacket bool
}

// Creates a new reader for plog messages.
func NewReader(rc io.ReadCloser, seqpacket bool) *Reader {
	return &Reader{rc, seqpacket}
}

// Overwrite *msg with a message received from the connection.
func (r *Reader) Receive(msg *Plog) error {
	var l uint32
	if err := binary.Read(r, binary.BigEndian, &l); err != nil {
		return err
	}
	data := make([]byte, l)
	_, err := io.ReadFull(r, data)
	if err != nil {
		return err
	}
	return proto.Unmarshal(data, msg)
}

// Type for sending plog messages.
// Has closer for easier use.
type Writer struct {
	io.WriteCloser
	Seqpacket bool
	sync.Mutex
}

// Creates a new writer for plog messages.
func NewWriter(wc io.WriteCloser, seqpacket bool) *Writer {
	return &Writer{WriteCloser: wc, Seqpacket: seqpacket}
}

// Send *msg on the connection.
func (w *Writer) Send(msg *Plog) error {
	data, err := proto.Marshal(msg)
	if err != nil {
		return err
	}

	// Use a simple len prefix.
	w.Lock()
	defer w.Unlock()
	l := uint32(len(data))
	if err := binary.Write(w, binary.BigEndian, l); err != nil {
		return err
	}
	var tot uint32
	for tot < l {
		n, err := w.Write(data)
		if err != nil {
			return err
		}
		tot += uint32(n)
	}
	return nil
}

// Convenience wrapper for sending only the open message.
func (w *Writer) SendOpen(ctxId uint64, msg *OpenContext) error {
	return w.Send(&Plog{CtxId: &ctxId, Open: msg})
}

// Convenienc wrapper for sending only the close message.
func (w *Writer) SendClose(ctxId uint64) error {
	cl := true
	return w.Send(&Plog{CtxId: &ctxId, Close: &cl})
}

// Convenience wrapper for sending only message messages.
func (w *Writer) SendMessage(ctxId uint64, msg ...*PlogMessage) error {
	return w.Send(&Plog{CtxId: &ctxId, Msg: msg})
}

// Convenience wrapper for sending a single message.
func (w *Writer) SendKeyValue(ctxId uint64, key string, value []byte) error {
	msg := PlogMessage{Key: &key, Value: value}
	return w.SendMessage(ctxId, &msg)
}
