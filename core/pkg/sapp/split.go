// Copyright 2018 Schibsted

package sapp

// This code is based on https://play.golang.org/p/_nxbkYZlXoQ

import (
	"crypto/tls"
	"net"
)

// A listener which can serve both HTTP and HTTPS traffic on the same port.
type splitListener struct {
	net.Listener
	config *tls.Config
}

func (l *splitListener) Accept() (net.Conn, error) {
	c, err := l.Listener.Accept()
	if err != nil {
		return nil, err
	}

	bconn := &conn{
		Conn: c,
	}

	first, err := bconn.peekFirst()
	if err != nil {
		bconn.Close()
		return nil, err
	}

	/* All HTTPS connections start with this byte. */
	if first == 0x16 {
		return tls.Server(bconn, l.config), nil
	}

	return bconn, nil
}

type conn struct {
	net.Conn
	first *byte
}

func (c *conn) Read(b []byte) (int, error) {
	if len(b) == 0 {
		return 0, nil
	}

	if c.first != nil {
		b[0] = *c.first
		c.first = nil
		return 1, nil
	} else {
		return c.Conn.Read(b)
	}
}

// Peek at the first byte in conn without consuming it.
func (c *conn) peekFirst() (byte, error) {
	if c.first != nil {
		return *c.first, nil
	}

	buf := []byte{0}
	for {
		n, err := c.Conn.Read(buf)
		if n != 0 || err != nil {
			c.first = &buf[0]
			return *c.first, err
		}
	}
}
