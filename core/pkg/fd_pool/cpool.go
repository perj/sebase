// Copyright 2018 Schibsted

// +build cgo,sebase_cgo

package fd_pool

import (
	"context"
	"io"
	"net"
	"runtime"
	"syscall"
	"time"
	"unsafe"

	"github.com/schibsted/sebase/util/pkg/sbalance"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
	"github.com/schibsted/sebase/vtree/pkg/vtree"
)

//#include "sbp/fd_pool.h"
//#include "sbp/sd_registry.h"
//#include <netdb.h>
//#include <stdlib.h>
//#include <sys/socket.h>
//#include <unistd.h>
import "C"

// Fd pool wrapping a C pool. Normally you should prefer to use the Go version
// instead. CPool usually ignores any context.Context arguments given to it.
//
// CPool connections do not currently support deadlines.
type CPool struct {
	pool          *C.struct_fd_pool
	dialDomain    *C.char
	dialDomainLen C.ssize_t
}

func node(vt *vtree.Vtree) *C.struct_vtree_chain {
	return (*C.struct_vtree_chain)(vt.CPtr())
}

// Fd pool error codes from the C header.
type CPoolError struct {
	Code C.int
	Msg  string
}

func (e *CPoolError) Error() string {
	return e.Msg
}

func fdPoolError(ec C.int) error {
	if ec == 0 {
		return nil
	}
	buf := [128]C.char{}
	e := C.fd_pool_strerror(ec, &buf[0], 128)
	return &CPoolError{ec, C.GoString(e)}
}

func (p *CPool) add_unix(service, path string, socktype C.int, retries int, to C.int) error {
	cs := C.CString(service)
	defer C.free(unsafe.Pointer(cs))
	cp := C.CString(path)
	defer C.free(unsafe.Pointer(cp))
	ret := C.fd_pool_add_unix(p.pool, cs, cp, C.int(retries), to, socktype)
	return fdPoolError(ret)
}

// A new C fd pool, collecting file descriptors for easy use and re-use.
// Only use this version if you need to pass the pool to C functions,
// otherwise use the Go pool
// Typically fd pools are long-lived, application global.
// sdr is an optional *C.struct_sd_registry pointer for enabling
// Service Discovery.
func NewCPool(sdr *CSdRegistry) *CPool {
	var s *C.struct_sd_registry
	if sdr != nil {
		s = sdr.sdr
	}
	p := C.fd_pool_new(s)
	res := &CPool{pool: p}
	runtime.SetFinalizer(res, (*CPool).Close)
	return res
}

// Calls AddVtree with vtree from conf. Will panic if it doesn't
// support Vtree creation.
func (p *CPool) AddConf(ctx context.Context, conf bconf.Bconf) (string, error) {
	// Ignoring ctx
	type vt interface {
		Vtree() *vtree.Vtree
	}
	return p.AddVtree(conf.(vt).Vtree())
}

// Add a service to the pool from a vtree. The service name as specified
// by the config is returned, or one is generated. This name is then
// used in NewConn to connect to the services.
// Returns generic string errors based on fd_pool_strerror.
func (p *CPool) AddVtree(vt *vtree.Vtree) (string, error) {
	var rs *C.char
	ret := C.fd_pool_add_config(p.pool, node(vt), nil, &rs)
	if ret != 0 {
		return "", fdPoolError(ret)
	}
	return C.GoString(rs), nil
}

// Same as AddVtree, but with additional hints for getaddrinfo.
func (p *CPool) AddVtreeHints(vt *vtree.Vtree, ai_flags C.int, ai_family C.int, ai_socktype C.int) (string, error) {
	var rs *C.char
	hints := C.struct_addrinfo{ai_flags: ai_flags, ai_family: ai_family, ai_socktype: ai_socktype}
	ret := C.fd_pool_add_config(p.pool, node(vt), &hints, &rs)
	if ret != 0 {
		return "", fdPoolError(ret)
	}
	return C.GoString(rs), nil
}

// Add a named service connecting to a single address. tcp, udp and unix networks are supported (with variants).
// Will reutrn net.UnknownNetworkError for an unrecognized netw parameter.
// Calls net.SplitHostPort on addr and might return errors from there.
// Errors generated with fd_pool_strerror might also be returned.
func (p *CPool) AddSingle(ctx context.Context, service, netw, addr string, retries int, timeout time.Duration) error {
	// Ignoring ctx
	to := C.int(timeout.Seconds() * 1000)
	hints := C.struct_addrinfo{ai_flags: C.AI_ADDRCONFIG}
	switch netw {
	case "tcp", "tcp4", "tcp6":
		hints.ai_socktype = C.SOCK_STREAM
	case "udp", "udp4", "udp6":
		hints.ai_socktype = C.SOCK_DGRAM
	case "unix":
		return p.add_unix(service, addr, C.SOCK_STREAM, retries, to)
	case "unixgram":
		return p.add_unix(service, addr, C.SOCK_DGRAM, retries, to)
	default:
		return net.UnknownNetworkError(netw)
	}
	switch netw {
	case "tcp4", "udp4":
		hints.ai_family = C.AF_INET
	case "tcp6", "udp6":
		hints.ai_family = C.AF_INET6
	}
	h, port, err := net.SplitHostPort(addr)
	if err != nil {
		return err
	}
	cs := C.CString(service)
	defer C.free(unsafe.Pointer(cs))
	ch := C.CString(h)
	defer C.free(unsafe.Pointer(ch))
	cp := C.CString(port)
	defer C.free(unsafe.Pointer(cp))
	ret := C.fd_pool_add_single(p.pool, cs, ch, cp, C.int(retries), to, &hints)
	return fdPoolError(ret)
}

// Close the fd pool.
func (f *CPool) Close() error {
	if f.pool == nil {
		return nil
	}
	C.fd_pool_free(f.pool)
	C.free(unsafe.Pointer(f.dialDomain))
	f.pool = nil
	return nil
}

// Access the C pointer directly, for APIs accepting those.
// As this is a C pointer you can use store it in other C APIs.
func (f *CPool) CPool() unsafe.Pointer {
	if f == nil {
		return nil
	}
	return unsafe.Pointer(f.pool)
}

// An established conncection to a node in the C pool.
// Implements io.ReadWriteCloser, but if using a keepalive
// protocol you will want to call Put instead of Close to
// save the fd for later use.
type cConn struct {
	serviceArg string
	portKeyArg string

	conn     *C.struct_fd_pool_conn
	fd       int
	peer     string
	port_key string
}

// Establish a new connection to the C pool.
// remoteAddr optionally specifies the client address to use to hash
// the order of nodes.
// Uses package syscall errors, the error returned will be for the last
// connection attempt, or an ErrNoServiceNodes if the pool is empty.
// The returned connection is ready to use for Read, Write, etc.
func (f *CPool) NewConn(ctx context.Context, service, port_key, remoteAddr string) (NetConn, error) {
	cra := C.CString(remoteAddr)
	defer C.free(unsafe.Pointer(cra))
	cse := C.CString(service)
	defer C.free(unsafe.Pointer(cse))
	cpk := C.CString(port_key)
	defer C.free(unsafe.Pointer(cpk))
	conn := &cConn{fd: -1, serviceArg: service, portKeyArg: port_key}
	conn.conn = C.fd_pool_new_conn(f.pool, cse, cpk, cra)
	err := conn.get(C.SBCS_START)
	if err != nil {
		C.fd_pool_free_conn(conn.conn)
		return nil, err
	} else {
		runtime.SetFinalizer(conn, (*cConn).Close)
	}
	return conn, nil
}

// Set domain for converting from host names to fd pool service names.
// E.g. with domain example.com dialing to mysearch.search.example.com
// will connect to the service search/mysearch.
func (p *CPool) SetDialDomain(dom string) {
	p.dialDomain = C.CString(dom)
	p.dialDomainLen = C.ssize_t(len(dom))
}

// Return the previously set dial domain.
func (p *CPool) DialDomain() string {
	if p.dialDomain == nil {
		return ""
	}
	return C.GoString(p.dialDomain)
}

func (c *cConn) get(status C.enum_sbalance_conn_status) error {
	if c.fd != -1 {
		syscall.Close(c.fd)
	}
	var peer *C.char
	var port_key *C.char
	cfd, err := C.fd_pool_get(c.conn, status, &peer, &port_key)
	c.fd = int(cfd)
	if cfd == -1 {
		if err == syscall.EAGAIN {
			err = &ErrNoServiceNodes{Service: c.serviceArg, PortKey: c.portKeyArg}
		}
		return err
	}
	c.peer = C.GoString(peer)
	c.port_key = C.GoString(port_key)
	return nil
}

// Moves the connection to the next node in the pool.
// Will return nil if successful.
// Otherwise the error for the last connection attempt made
// is returned, or ErrNoServiceNodes if we ran out of nodes without
// trying to connect.
// Will close any currently open fd.
func (c *cConn) Next(ctx context.Context, status sbalance.ConnStatus) error {
	// Ingoring ctx
	if c.conn == nil {
		return syscall.EINVAL
	}
	return c.get(C.enum_sbalance_conn_status(status))
}

// Resets the connection to start at the first node again, or
// a new one in case of random strat.
// Return value is as per Next.
// Will close any currently open fd.
// Note: You may NOT call this if you already called Put or Close.
func (c *cConn) Reset(ctx context.Context) error {
	// Ignoring ctx
	if c.conn == nil {
		return syscall.EINVAL
	}
	return c.get(C.SBCS_START)
}

// Puts the current fd and resets the conneciton as per Reset.
// You're likely to get the same fd back so this is very situational
// (e.g. if the pool is set to always rotate fds).
func (c *cConn) PutReset(ctx context.Context) error {
	if c.fd != -1 {
		C.fd_pool_put(c.conn, C.int(c.fd))
		c.fd = -1
	}
	return c.Reset(ctx)
}

// Node name of the last successfully connected node.
func (c *cConn) Peer() string {
	return c.peer
}

// The port key of the last successful connection.
func (c *cConn) PortKey() string {
	return c.port_key
}

// Put the fd back in the pool. You chose whether you put or close,
// and only call one of them.
func (c *cConn) Put() {
	if c.fd < 0 {
		return
	}
	C.fd_pool_put(c.conn, C.int(c.fd))
	c.fd = -1
	C.fd_pool_free_conn(c.conn)
	c.conn = nil
}

// Close the associated fd. You chose whether you put or close,
// and only call one of them.
func (c *cConn) Close() error {
	var err error
	if c.fd >= 0 {
		err = syscall.Close(c.fd)
		c.fd = -1
	}
	C.fd_pool_free_conn(c.conn)
	c.conn = nil
	return err
}

// Implements io.Reader
func (c *cConn) Read(p []byte) (n int, err error) {
	for {
		n, err = syscall.Read(c.fd, p)
		if n >= 0 || err != syscall.EINTR {
			break
		}
		err = nil
	}
	if n < 0 {
		n = 0
	} else if n == 0 {
		err = io.EOF
	}
	return
}

// Implements io.Writer
func (c *cConn) Write(p []byte) (n int, err error) {
	for err == nil && len(p) > 0 {
		var l int
		l, err = syscall.Write(c.fd, p)
		if l > 0 {
			n += l
			p = p[n:]
		}
		if err == syscall.EINTR {
			err = nil
		}
	}
	return n, err
}

func parseSa(sa syscall.Sockaddr) net.Addr {
	switch sa := sa.(type) {
	case *syscall.SockaddrInet4:
		return &net.TCPAddr{IP: sa.Addr[0:], Port: sa.Port}
	case *syscall.SockaddrInet6:
		zone := ""
		if sa.ZoneId > 0 {
			ifa, _ := net.InterfaceByIndex(int(sa.ZoneId))
			if ifa != nil {
				zone = ifa.Name
			}
		}
		return &net.TCPAddr{IP: sa.Addr[0:], Port: sa.Port, Zone: zone}
	}
	return nil
}

// Part of net.Conn interface.
func (c *cConn) LocalAddr() net.Addr {
	sa, _ := syscall.Getsockname(c.fd)
	return parseSa(sa)
}

// Part of net.Conn interface.
// See also: Conn.Peer
func (c *cConn) RemoteAddr() net.Addr {
	sa, _ := syscall.Getpeername(c.fd)
	return parseSa(sa)
}

// Part of net.Conn interface. Not implemented (returns EINVAL).
func (c *cConn) SetDeadline(t time.Time) error {
	return syscall.EINVAL
}

// Part of net.Conn interface. Not implemented (returns EINVAL).
func (c *cConn) SetReadDeadline(t time.Time) error {
	return syscall.EINVAL
}

// Part of net.Conn interface. Not implemented (returns EINVAL).
func (c *cConn) SetWriteDeadline(t time.Time) error {
	return syscall.EINVAL
}

// Wrapper for a C sd_registry struct. Normally you shouldn't have to use this.
type CSdRegistry struct {
	sdr *C.struct_sd_registry
}

// Create an SD registry for the given host and appl.
// Only required for C fd pool, otherwise use the separate sdr package.
// The https pointer can be a *C.struct_https_state from sbp/http.h
// if you have one.
func NewCSdr(host, appl string, https unsafe.Pointer) *CSdRegistry {
	h := C.CString(host)
	defer C.free(unsafe.Pointer(h))
	a := C.CString(appl)
	defer C.free(unsafe.Pointer(a))
	sdr := C.sd_registry_create(h, a, (*C.struct_https_state)(https))
	if sdr == nil {
		return nil
	}
	return &CSdRegistry{sdr}
}

// Call sd_registry_add_sources with the given vtree.
func (sdr *CSdRegistry) AddSources(vt *vtree.Vtree) {
	C.sd_registry_add_sources(sdr.sdr, node(vt))
}

func (sdr *CSdRegistry) Close() error {
	C.sd_registry_free(sdr.sdr)
	return nil
}
