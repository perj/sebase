// Copyright 2018 Schibsted

package fd_pool

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"hash/fnv"
	"net"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/schibsted/sebase/core/pkg/sd/sdr"
	"github.com/schibsted/sebase/util/pkg/sbalance"
	"github.com/schibsted/sebase/util/pkg/slog"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

var (
	EmptyConfig   = errors.New("empty config node")
	NoSuchService = errors.New("no such service")
	ConnClosed    = errors.New("Use of closed or put connection")
)

// Port map to support numeric ports mapping to port keys.
// Mostly to support the net/url package which requires numeric ports.
// You can modify this if needed, but be careful as it's used without locks.
var PortMap = map[string]string{
	"80":   "http_port",
	"443":  "http_port",
	"8080": "port",
	"8081": "controller_port",
	"8082": "keepalive_port,port",
	"8180": "plog_port",
}

const (
	DefaultCost           = 1
	DefaultFailCost       = 100
	DefaultSoftFailCost   = 100
	DefaultRetries        = 1
	DefaultConnectTimeout = 1 * time.Second
	DefaultStrat          = sbalance.StratRandom
	InitialWaitDuration   = 1 * time.Second
)

// The main fd pool struct. Typically long-lived, a single pool is created
// for an application and used throughout. Services are added as needed.
type GoPool struct {
	dialDomain string

	services map[string]*fdService
	srvLock  sync.RWMutex
	sdReg    *sdr.Registry

	connLock sync.Mutex
	connSet  map[fdPort]*fdConnSet
}

type fdService struct {
	*sbalance.Service
	sblock sync.RWMutex

	DialContext func(ctx context.Context, nw, addr string) (net.Conn, error)
	NetNetwork  string
	UnixNetwork string

	sdConn        sdr.SourceConn
	sdConnInitial chan struct{}
}

// Unique identifier for connections.
type fdPort struct {
	PortKey    string
	Netw, Addr string
}

// Refcounted stored connections, closing them if refs reach 0.
type fdConnSet struct {
	sync.Mutex
	port  fdPort
	conns []net.Conn
	refs  int32
}

func (cs *fdConnSet) Retain() *fdConnSet {
	if cs == nil {
		return nil
	}
	cs.Lock()
	cs.refs++
	defer cs.Unlock()
	return cs
}

// A fdConnSet reaching refs == 0 has connections closed but is still valid.
// It will be kept in the pool connset map and might be retained again.
func (cs *fdConnSet) Release() {
	if cs == nil {
		return
	}
	cs.Lock()
	defer cs.Unlock()
	cs.refs--
	if cs.refs > 0 {
		return
	}
	if cs.refs < 0 {
		panic("Double close of fdConnSet")
	}
	for _, c := range cs.conns {
		c.Close()
	}
	cs.conns = nil
}

// Refcounted node for a service, containing a map from portkey to
// connections sets for a single ip.
type fdServiceNode struct {
	key     string
	connSet map[string]*fdConnSet
	refs    int32
}

func (fds *fdServiceNode) Retain() *fdServiceNode {
	if fds == nil {
		return nil
	}
	if atomic.AddInt32(&fds.refs, 1) == 1 {
		return nil
	}
	return fds
}

func (fds *fdServiceNode) Release() {
	if fds == nil {
		return
	}
	if atomic.AddInt32(&fds.refs, -1) > 0 {
		return
	}
	if fds.refs < 0 {
		panic("Double close of fdServiceNode")
	}
	for _, cs := range fds.connSet {
		cs.Release()
	}
}

// Create a new Go fd pool. Should be preferred over NewCPool if you
// don't need to interact with C code.
func NewGoPool(sdreg *sdr.Registry) *GoPool {
	return &GoPool{
		services: make(map[string]*fdService),
		connSet:  make(map[fdPort]*fdConnSet),
		sdReg:    sdreg,
	}
}

func (pool *GoPool) Close() error {
	return nil
}

func (pool *GoPool) SetDialDomain(dom string) {
	pool.dialDomain = dom
}

func (pool *GoPool) DialDomain() string {
	return pool.dialDomain
}

func (p *GoPool) getService(srvname string, retries int, connectTimeout time.Duration) *fdService {
	p.srvLock.Lock()
	defer p.srvLock.Unlock()
	srv := p.services[srvname]
	if srv != nil {
		return srv
	}
	dialer := &net.Dialer{Timeout: connectTimeout}
	srv = &fdService{
		Service: &sbalance.Service{
			Retries:      retries,
			FailCost:     DefaultFailCost,
			SoftFailCost: DefaultSoftFailCost,
			Strat:        DefaultStrat,
		},
		NetNetwork:  "tcp",
		UnixNetwork: "unix",
		DialContext: dialer.DialContext,
	}
	p.services[srvname] = srv
	return srv
}

// Add bconf config based nodes to the pool. Uses default stream socket types.
// Deprecated due to confusing name. AddBconf is the same function.
func (p *GoPool) AddConf(ctx context.Context, conf bconf.Bconf) (string, error) {
	return p.AddBconfNetwork(ctx, conf, "tcp", "unix")
}

// Add bconf config based nodes to the pool. Uses default stream socket types.
func (p *GoPool) AddBconf(ctx context.Context, conf bconf.Bconf) (string, error) {
	return p.AddBconfNetwork(ctx, conf, "tcp", "unix")
}

// Like AddConf but allows to specify the network parameter later given to Dial.
// ipnetw is used for ports and unixnetw is used for paths.
// Deprecated due to confusing name. AddBconfNetwork is the same function.
func (p *GoPool) AddConfNetwork(ctx context.Context, conf bconf.Bconf, ipnetw, unixnetw string) (string, error) {
	return p.AddBconfNetwork(ctx, conf, ipnetw, unixnetw)
}

// Like AddBconf but allows to specify the network parameter later given to Dial.
// ipnetw is used for ports and unixnetw is used for paths.
// Creates a Config and calls AddConfig.
func (p *GoPool) AddBconfNetwork(ctx context.Context, conf bconf.Bconf, ipnetw, unixnetw string) (string, error) {
	config := BconfConfig(conf)
	config.NetNetwork = ipnetw
	config.UnixNetwork = unixnetw
	return p.AddConfig(ctx, config)
}

// Add a service based on a configuration struct. The conf values are copied,
// the struct can be discarded or reused after the call returns.
func (p *GoPool) AddConfig(ctx context.Context, config *ServiceConfig) (string, error) {
	if config == nil {
		return "", EmptyConfig
	}
	srvname := config.Service
	canSd := true
	if srvname == "" {
		// This won't match the C version, but I think that's fine.
		data, err := json.Marshal(config.Hosts)
		if err != nil {
			return "", err
		}
		h := fnv.New32a()
		h.Write(data)
		srvname = fmt.Sprintf("0x%x", h.Sum32())
		canSd = false
	}
	srv := p.getService(srvname, config.GetRetries(), config.GetConnectTimeout())
	if srv.Len() > 0 {
		// Assume already configured.
		return srvname, nil
	}
	srv.sblock.Lock()
	defer srv.sblock.Unlock()

	srv.Strat = config.Strat
	srv.FailCost = config.GetFailCost()
	srv.SoftFailCost = config.GetSoftFailCost()
	if config.DialContext != nil {
		srv.DialContext = config.DialContext
	}
	srv.NetNetwork = config.GetNetNetwork()
	srv.UnixNetwork = config.GetUnixNetwork()

	err := p.populateFromConf(ctx, srv.Service, config.Hosts, srv.NetNetwork, srv.UnixNetwork)

	if canSd && srv.sdConn == nil && p.sdReg != nil {
		serr := p.connectSd(ctx, srvname, srv, config.ServiceDiscovery)
		if serr == nil {
			initialWait(ctx, srv, InitialWaitDuration)
		}
		if err == nil {
			err = serr
		}
	}

	// Don't report errors if we added something or if sd is setup.
	if srv.Len() > 0 || srv.sdConn != nil {
		err = nil
	}

	return srvname, err
}

// Add a single node to the pool. Uses DefaultCost as cost.
func (p *GoPool) AddSingle(ctx context.Context, service, netw, addr string, retries int, connectTimeout time.Duration) error {
	srv := p.getService(service, retries, connectTimeout)
	srv.sblock.Lock()
	defer srv.sblock.Unlock()

	ports := make(map[string][]fdPort)
	ports[""] = []fdPort{{"port", netw, addr}}

	p.addPorts(srv.Service, "", DefaultCost, ports)

	if srv.sdConn == nil && p.sdReg != nil {
		err := p.connectSd(ctx, service, srv, nil)
		if err == nil {
			initialWait(ctx, srv, InitialWaitDuration)
		}
	}
	return nil
}

// Populate a specific sbalance from the host configs. Ports are added to the
// pools as needed and connected to the added nodes. IPs are resolved and each
// IP is considered a separte node, even if they come from the same DNS lookup.
// Thus there might be more than one node with the same key.
// Might return an error from DNS lookup.
func (p *GoPool) populateFromConf(ctx context.Context, sb *sbalance.Service, hosts map[string]*HostConfig, ipnetw, unixnetw string) error {
	var err error
	for key, h := range hosts {
		if h.Disabled {
			continue
		}

		// Map to create nodes based on IP. This is not stable on the DNS
		// resolver order, but I don't think that matters much.
		ports := make(map[string][]fdPort)
		var addrs []string
		var rerr error
		for pname, port := range h.Ports {
			if strings.HasSuffix(pname, "port") {
				if h.Name == "" || port == "" {
					continue
				}

				if addrs == nil {
					// It seems that the only function that gives us
					// multiple IPS for a single hostname is
					// net.LookupHost. The others just return a single one.
					// Seems weird for a modern library, let's hope it's
					// improved in the future.
					addrs, rerr = net.DefaultResolver.LookupHost(ctx, h.Name)
					if rerr != nil {
						// Since host is global for all ports we break
						// here to handle the error.
						break
					}
				}
				for _, a := range addrs {
					astr := net.JoinHostPort(a, port)
					ports[a] = append(ports[a], fdPort{pname, ipnetw, astr})
				}
			} else if pname == "path" {
				// Use default "port" name for these.
				a := port
				ports[a] = append(ports[a], fdPort{"port", unixnetw, a})
			}
		}
		if rerr != nil {
			err = rerr
		}

		p.addPorts(sb, key, h.Cost, ports)
	}
	return err
}

// Add nodes to sb for each slice in ports. The nodes will all have the same
// key name and cost, and they'll point to connections shared between all nodes.
func (pool *GoPool) addPorts(sb *sbalance.Service, key string, cost int, ports map[string][]fdPort) {
	pool.connLock.Lock()
	defer pool.connLock.Unlock()
	for _, pkeys := range ports {
		if len(pkeys) == 0 {
			continue
		}

		// Look up or create shared connSets from the spec given.
		connSets := make(map[string]*fdConnSet)
		for i := range pkeys {
			cset := pool.connSet[pkeys[i]].Retain()
			if cset == nil {
				cset = &fdConnSet{port: pkeys[i], refs: 1}
				pool.connSet[pkeys[i]] = cset
			}
			// Assume each entry in pkeys is unique. It doesn't make sense to have
			// duplicates.
			connSets[pkeys[i].PortKey] = cset
		}

		sb.AddNode(&fdServiceNode{key, connSets, 1}, cost)
	}
}

func (pool *GoPool) findService(srvname string) *fdService {
	pool.srvLock.RLock()
	defer pool.srvLock.RUnlock()
	return pool.services[srvname]
}

// Create a new conneciton to the given service and port keys. remoteAddr can be
// given to use as a hash for that sbalance strat. The returned NetConn is ready
// for use. An error might be returned instead if a connection can't be established.
// It will be the error from the last Dial attempt made, or ErrNoServiceNodes if no
// connection attempt were made. NoSuchService might also be returned, as well as
// errors from connecting to Service Discovery if such is configured.
func (pool *GoPool) NewConn(ctx context.Context, service, portKey, remoteAddr string) (NetConn, error) {
	srv := pool.findService(service)
	if srv == nil {
		if pool.sdReg == nil {
			return nil, NoSuchService
		}
		slog.Info("NewConn adding service", "service", service)
		srv = pool.getService(service, DefaultRetries, DefaultConnectTimeout)
		srv.sblock.Lock()
		if srv.sdConn == nil {
			err := pool.connectSd(ctx, service, srv, nil)
			if err != nil {
				srv.sblock.Unlock()
				return nil, err
			}
		}
		srv.sblock.Unlock()
		initialWait(ctx, srv, InitialWaitDuration)
	}

	mpk := PortMap[portKey]
	if mpk != "" {
		portKey = mpk
	}

	var sbseed []byte
	if remoteAddr != "" {
		sbseed = []byte(remoteAddr)
	}
	c := &conn{pool: pool, service: service, srv: srv, sbseed: sbseed, portKey: strings.Split(portKey, ",")}
	return c.get(ctx, sbalance.Start)
}

// Update the nodes for a service, using a bconf node.
// Deprecated, same as UpdateHostsBconf.
func (pool *GoPool) UpdateHosts(ctx context.Context, service string, conf bconf.Bconf) (int, error) {
	return pool.UpdateHostsBconf(ctx, service, conf)
}

// Update the nodes for a service, using a bconf node.
// Creates a ServiceConfig and calls UpdateHostsConfig.
func (pool *GoPool) UpdateHostsBconf(ctx context.Context, service string, conf bconf.Bconf) (int, error) {
	return pool.UpdateHostsConfig(ctx, service, BconfConfig(conf).Hosts)
}

// Update the nodes for a service, e.g. when a new node is detected via Service
// Discovery. The hosts given replace the old ones for the service.  At least
// one enabled node must be in hosts or the update won't be made, this is to
// protect from misconfigurations.
// Returns the number of sb nodes added, i.e. the number of non-disabled
// IP addresses found. If 0 is returned nothing was changed.
// Returns NoSuchService if the service given doesn't exist. Might also
// return errors from DNS lookup and similar.
func (pool *GoPool) UpdateHostsConfig(ctx context.Context, service string, hosts map[string]*HostConfig) (int, error) {
	srv := pool.findService(service)
	if srv == nil {
		return 0, NoSuchService
	}
	sb := &sbalance.Service{
		Retries:      srv.Service.Retries,
		FailCost:     srv.Service.FailCost,
		SoftFailCost: srv.Service.SoftFailCost,
		Strat:        srv.Service.Strat,
	}
	err := pool.populateFromConf(ctx, sb, hosts, srv.NetNetwork, srv.UnixNetwork)
	if err != nil {
		return 0, err
	}
	l := sb.Len()
	if l > 0 {
		srv.sblock.Lock()
		sb, srv.Service = srv.Service, sb
		srv.sblock.Unlock()
		for _, node := range sb.Nodes() {
			node.(*fdServiceNode).Release()
		}
	}
	return l, nil
}

type conn struct {
	pool       *GoPool
	portKey    []string
	portKeyIdx int
	sbseed     []byte

	service    string
	srv        *fdService
	sb         *sbalance.Service
	sbconn     sbalance.Connection
	srvnode    *fdServiceNode
	connset    *fdConnSet
	newConnect bool

	closed uint32

	net.Conn
}

// Move to the next port key in the same node.
// Sets c.connset to the right connset, or nil if none were found.
func (c *conn) movePort() {
	c.connset.Release()
	c.connset = nil
	c.portKeyIdx++
	for c.portKeyIdx < len(c.portKey) {
		pk := c.portKey[c.portKeyIdx]
		c.connset = c.srvnode.connSet[pk].Retain()
		if c.connset != nil {
			return
		}
		c.portKeyIdx++
	}
}

// Move to the next node, resetting to the first portKey.
// Sets c.srvnode and c.connset to the right values, or
// nil if no more nodes were found.
func (c *conn) moveNode(status sbalance.ConnStatus) {
	c.connset.Release()
	c.connset = nil
	for {
		c.srvnode.Release()
		next := c.sbconn.Next(status)
		if next == nil {
			c.srvnode = nil
			return
		}
		c.srvnode = next.(*fdServiceNode).Retain()
		c.portKeyIdx = -1
		c.movePort()
		if c.connset != nil {
			return
		}
	}
}

func (c *conn) findConnset(status sbalance.ConnStatus) sbalance.ConnStatus {
	// Lock until we have a connset, to avoid fdServiceNodes being released.
	c.srv.sblock.RLock()
	if c.sb != c.srv.Service {
		// The sb is updated, reset.
		c.sb = c.srv.Service
		c.sbconn = c.srv.NewConn(c.sbseed)
		status = sbalance.Start
		c.newConnect = false
		c.connset.Release()
		c.connset = nil
	}
	if c.connset == nil {
		c.moveNode(status)
	} else if c.newConnect {
		c.movePort()
		if c.connset == nil {
			c.moveNode(status)
		}
	}
	c.srv.sblock.RUnlock()
	return status
}

// Fetch a stored connection, or make a new connection attempt.
// Calls moveNode and movePort as needed. Will use up stored
// connections until none is left for a node and port key combo, then
// make a new connection attempt using the pool Dial function.
// Returns ErrNoServiceNodes when on more connections can be made.
func (c *conn) get(ctx context.Context, status sbalance.ConnStatus) (NetConn, error) {
	if c.closed == 1 {
		return nil, ConnClosed
	}
	if c.Conn != nil {
		c.Conn.Close()
	}
	var err error
	for {
		status = c.findConnset(status)
		if c.connset == nil {
			if err == nil {
				err = &ErrNoServiceNodes{c.service, strings.Join(c.portKey, ",")}
			}
			c.Conn = nil
			c.closed = 1
			return nil, err
		}

		c.connset.Lock()
		for len(c.connset.conns) > 0 {
			netc := c.connset.conns[0]
			c.connset.conns = c.connset.conns[1:]

			if checkDeadConnection(netc) {
				netc.Close()
				continue
			}

			c.Conn = netc
			c.SetDeadline(time.Time{})
			c.connset.Unlock()
			c.newConnect = false
			slog.Debug("Reusing connection", "peer", c.connset.port.Addr)
			return c, nil
		}
		c.connset.Unlock()
		c.newConnect = true

		c.Conn, err = c.srv.DialContext(ctx, c.connset.port.Netw, c.connset.port.Addr)
		if c.Conn != nil {
			return c, nil
		}
		// We ran into a hard failure.
		status = sbalance.Fail
	}
}

func (c *conn) Next(ctx context.Context, status sbalance.ConnStatus) error {
	_, err := c.get(ctx, status)
	return err
}

func (c *conn) Close() error {
	if !atomic.CompareAndSwapUint32(&c.closed, 0, 1) {
		return nil
	}
	c.connset.Release()
	c.connset = nil
	c.srvnode.Release()
	c.srvnode = nil
	if c.Conn == nil {
		return nil
	}
	nc := c.Conn
	c.Conn = nil
	return nc.Close()
}

func (c *conn) Put() {
	if !atomic.CompareAndSwapUint32(&c.closed, 0, 1) {
		return
	}
	c.connset.Lock()
	c.connset.conns = append(c.connset.conns, c.Conn)
	c.connset.Unlock()
	c.connset.Release()
	c.Conn = nil
	c.srvnode.Release()
	c.srvnode = nil
}

func (c *conn) Reset(ctx context.Context) error {
	c.closed = 0
	_, err := c.get(ctx, sbalance.Start)
	return err
}

func (c *conn) PutReset(ctx context.Context) error {
	c.Put()
	return c.Reset(ctx)
}

func (c *conn) Peer() string {
	return c.connset.port.Addr
}

func (c *conn) PortKey() string {
	return c.connset.port.PortKey
}

// Fetch the internal node state of the pool. Used for debugging
// purposes. Returns a list of node keys and ports with their
// costs and number of stored connections.
func (p *GoPool) GetNodePorts(service string) []PortInfo {
	srv := p.findService(service)
	if srv == nil {
		return nil
	}
	p.connLock.Lock()
	defer p.connLock.Unlock()
	res := make([]PortInfo, 0, len(p.connSet))
	for i, node := range srv.Nodes() {
		fdn := node.(*fdServiceNode)
		cost, effcost := srv.GetCosts(i)
		for _, port := range fdn.connSet {
			res = append(res, PortInfo{
				fdn.key,
				port.port.Netw,
				port.port.Addr,
				port.port.PortKey,
				cost,
				effcost,
				len(port.conns),
			})
		}
	}
	return res
}

// Change the dial function for a specific service. By default a
// net.Dialer with Timeout set to the configured connect timeout is used.
// Returns NoSuchService if the service has not been configured.
func (p *GoPool) SetDialFunc(srvname string, df func(ctx context.Context, nw, addr string) (net.Conn, error)) error {
	srv := p.findService(srvname)
	if srv == nil {
		return NoSuchService
	}
	srv.DialContext = df
	return nil
}
