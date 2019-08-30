// Copyright 2018 Schibsted

package fd_pool

import (
	"bufio"
	"context"
	"crypto/tls"
	"net"
	"net/http"

	"github.com/schibsted/sebase/util/pkg/sbalance"
	"github.com/schibsted/sebase/util/pkg/slog"
)

type HttpConn struct {
	Req *http.Request
	Fdc NetConn
	Tls *tls.Conn

	// One of Fdc or Tls, for convenience.
	net.Conn
}

// Send an HTTP request, repeatedly calling conn.Next() as long as they
// fail. The tls config is required for HTTPS connections.
func HttpRequest(ctx context.Context, conn NetConn, tlsconf *tls.Config, req *http.Request) (*HttpConn, error) {
	for {
		hc, err := httpSend(ctx, conn, tlsconf, req)
		if err == nil {
			return hc, nil
		}
		slog.CtxError(ctx, "Failed to send HTTP request", "error", err)
		err = conn.Next(ctx, sbalance.Fail)
		if err != nil {
			return nil, err
		}
	}
}

func httpSend(ctx context.Context, c NetConn, tlsconf *tls.Config, req *http.Request) (*HttpConn, error) {
	hc := &HttpConn{Req: req, Fdc: c, Conn: c.(net.Conn)}
	if tlsconf != nil {
		hc.Tls = tls.Client(c, tlsconf)
		hc.Conn = hc.Tls
	}

	// Don't close C connections, they can't handle it during a read or write.
	if _, ok := c.(*conn); ok {
		done := make(chan struct{})
		defer close(done)
		go func() {
			select {
			case <-ctx.Done():
				hc.Close()
			case <-done:
			}
		}()
	}

	err := req.Write(hc)
	return hc, err
}

// Read the HTTP response. The http package requires a *bufio.Reader so one
// is created and returned here.
func (hc *HttpConn) ReadResponse(ctx context.Context) (*bufio.Reader, *http.Response, error) {
	// Don't close C connections, they can't handle it during a read or write.
	if _, ok := hc.Fdc.(*conn); ok {
		done := make(chan struct{})
		defer close(done)
		go func() {
			select {
			case <-ctx.Done():
				hc.Close()
			case <-done:
			}
		}()
	}

	bufr := bufio.NewReader(hc)
	resp, err := http.ReadResponse(bufr, hc.Req)
	return bufr, resp, err
}

func (hc *HttpConn) Close() error {
	if hc.Tls != nil {
		return hc.Tls.Close()
	}
	return hc.Fdc.Close()
}
