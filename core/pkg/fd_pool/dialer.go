// Copyright 2018 Schibsted

package fd_pool

import (
	"context"
	"net"
	"strings"

	"github.com/schibsted/sebase/core/pkg/sd/sdr"
)

func dialRA(ctx context.Context, p FdPool, addr, remoteAddr string) (NetConn, error) {
	host, port, err := net.SplitHostPort(addr)
	if err != nil {
		return nil, err
	}
	host = sdr.HostToService(host, p.DialDomain())
	return p.NewConn(ctx, host, port, remoteAddr)
}

// Simple dial. the nw parameter is currently ignored, the type of connection
// is determined by the fd pool service.
//
// You can plug this into for example http.Transport like so:
//
//	transport.DialContext = fd_pool.Dialer(p)
//
// Note that when using the dialer, the port given in e.g. a URL might have to
// be chosen carefully. If possible, you should use a textual port key, e.g.
// "port", but URLs do not strictly allow anything else than numbers in the
// port specification. Thus we've added a default mapping from numbers to
// port keys. "80" and "443" will map to "http_port", while "8080" maps
// to "port". For more mappings see fd_pool.c symbol default_upmap.
//
// If SetDialDomain has been called, then a hostname to service name
// conversion is done as it specifies.
func Dialer(p FdPool) func(ctx context.Context, nw, addr string) (net.Conn, error) {
	return DialerRA(p, "")
}

// Helper to construct a URL from a service with the dial domain set
// in the pool
func GetUrl(p FdPool, scheme, service, portKey string) string {
	switch portKey {
	case "port":
		portKey = "8080"
	case "http_port":
		portKey = ""
	case "controller_port":
		portKey = "8081"
	case "plog_port":
		portKey = "8180"
	}
	sparts := strings.Split(service, "/")
	// Reverse
	for i, j := 0, len(sparts)-1; i < j; i, j = i+1, j-1 {
		sparts[i], sparts[j] = sparts[j], sparts[i]
	}
	dd := p.DialDomain()
	if dd != "" {
		sparts = append(sparts, dd)
	}
	host := strings.Join(sparts, ".")
	return scheme + "://" + net.JoinHostPort(host, portKey)
}

// Simple dialer, but when the conn.Close is called, the connection
// will be put back into the pool instead of closed.
func DialerPutOnClose(p FdPool) func(ctx context.Context, nw, addr string) (net.Conn, error) {
	return DialerRAPutOnClose(p, "")
}

// Dialer that lets you set the remoteAddr parameter given to NewConn.
func DialerRA(p FdPool, remoteAddr string) func(ctx context.Context, nw, addr string) (net.Conn, error) {
	return func(ctx context.Context, nw, addr string) (net.Conn, error) {
		return dialRA(ctx, p, addr, remoteAddr)
	}
}

// Returns a function that can is compatible with net.Dial, put on close version.
func DialerRAPutOnClose(p FdPool, remoteAddr string) func(ctx context.Context, nw, addr string) (net.Conn, error) {
	return func(ctx context.Context, nw, addr string) (net.Conn, error) {
		return PutOnCloseErr(dialRA(ctx, p, addr, remoteAddr))
	}
}

// Returns a dialer that ignores the argument given to it and always connects
// to the service specified to this function.
func DialerFor(p FdPool, service, portKey, remoteAddr string) func(ctx context.Context, nw, addr string) (net.Conn, error) {
	return func(ctx context.Context, nw, addr string) (net.Conn, error) {
		return p.NewConn(ctx, service, portKey, remoteAddr)
	}
}

// Returns a dialer that ignores the port part of addr, instead using the given
// portKey(s).  The host part is converted to a service in the normal manner.
func PortDialer(p FdPool, portKey, remoteAddr string) func(ctx context.Context, nw, addr string) (net.Conn, error) {
	return func(ctx context.Context, nw, addr string) (net.Conn, error) {
		host, _, err := net.SplitHostPort(addr)
		if err != nil {
			return nil, err
		}
		host = sdr.HostToService(host, p.DialDomain())
		return p.NewConn(ctx, host, portKey, remoteAddr)
	}
}
