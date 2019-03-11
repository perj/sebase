package fd_pool

import (
	"context"
	"net"
	"strings"
	"time"

	"github.com/schibsted/sebase/util/pkg/sbalance"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

type ServiceConfig struct {
	// Service name, this is used to determine what service to create.
	// Note that if you call AddConfig again with the same Service name
	// it will not update but keep the old values.
	// If empty string a service name will be generated.
	Service string
	// Hosts to add to the service
	Hosts map[string]*HostConfig

	// Connection timeout. If 0 then DefaultConnectTimeout is used.
	ConnectTimeout time.Duration

	// Retries tells sbalance how many times to go through the entire node
	// list.
	Retries int
	// Strat tells sbalance how to select the next node. Defaults to
	// StratSeq. StratRand would probably be a better default but StratSeq
	// is the zero value.
	Strat sbalance.BalanceStrat
	// FailCost is the cost to temporarily set when encountering a hard
	// error (sbalance.Fail)
	FailCost int
	// SoftFailCost is the cost to temporarily set when encountering a soft
	// error (sbalance.SoftFail)
	SoftFailCost int

	// NetNetwork is the network parameter to pass to Dialer for "port"
	// suffix ports. Defaults to "tcp".
	NetNetwork string
	// UnixNetwork is the network parameter to pass to Dialer for "path"
	// suffix ports. Defaults to "unix".
	UnixNetwork string
	// DialContext is used to create new connections. By default a
	// net.Dialer with Timeout set to the configured connect timeout is
	// used.
	// ConnectTimeout will be ignored if this is set.
	DialContext func(ctx context.Context, nw, addr string) (net.Conn, error)

	// Service Discovery configuration, optional.
	// This member is currently experimental and might change even in a minor release.
	// The ambition is to replace it with a similar struct/interface as this one.
	ServiceDiscovery bconf.Bconf
}

// GetConnectTimeout returns the set ConnectTimeout or DefaultConnectTimeout if
// unset.
func (c *ServiceConfig) GetConnectTimeout() time.Duration {
	if c != nil && c.ConnectTimeout != 0 {
		return c.ConnectTimeout
	}
	return DefaultConnectTimeout
}

// GetRetries returns the set Retries or DefaultRetries if unset.
func (c *ServiceConfig) GetRetries() int {
	if c != nil && c.Retries != 0 {
		return c.Retries
	}
	return DefaultRetries
}

// GetFailCost returns the set FailCost or DefaultFailCost if unset.
func (c *ServiceConfig) GetFailCost() int {
	if c != nil && c.FailCost != 0 {
		return c.FailCost
	}
	return DefaultFailCost
}

// GetSoftFailCost returns the set SoftFailCost or DefaultSoftFailCost if unset.
func (c *ServiceConfig) GetSoftFailCost() int {
	if c != nil && c.SoftFailCost != 0 {
		return c.SoftFailCost
	}
	return DefaultSoftFailCost
}

// GetNetNetwork returns the set NetNetwork or "tcp" if unset.
func (c *ServiceConfig) GetNetNetwork() string {
	if c != nil && c.NetNetwork != "" {
		return c.NetNetwork
	}
	return "tcp"
}

// GetUnixNetwork returns the set UnixNetwork or "unix" if unset.
func (c *ServiceConfig) GetUnixNetwork() string {
	if c != nil && c.UnixNetwork != "" {
		return c.UnixNetwork
	}
	return "unix"
}

// HostConfig contains a node specific configuration.
type HostConfig struct {
	// Name is the dns hostname for this node. If ports only contains a
	// "path" key this can be empty, otherwise it must have a value.
	Name string

	// Ports is a map from portkeys to services (port numbers). The key is checked if it has the suffix "port" or "path". Other entries might be ignored.
	Ports map[string]string

	// Cost is the default cost when successfully using this node. Minimum 1.
	Cost int

	// Disable marks the node as ignored, same as removing it from the slice.
	Disabled bool
}

// Converts bconf fd pool configuration to a ServiceConfig struct.
// The passed conf parameter is stored in the return value in the
// ServiceDiscovery member.
func BconfConfig(conf bconf.Bconf) *ServiceConfig {
	srv := &ServiceConfig{}
	srv.Service = conf.Get("service").String("")

	if conf.Get("connect_timeout").Valid() {
		srv.ConnectTimeout = time.Duration(conf.Get("connect_timeout").Int(0)) * time.Millisecond
	}

	srv.Retries = conf.Get("retries").Int(DefaultRetries)
	switch conf.Get("strat").String("") {
	case "hash":
		srv.Strat = sbalance.StratHash
	case "random":
		srv.Strat = sbalance.StratRandom
	case "seq":
		srv.Strat = sbalance.StratSeq
	}
	srv.FailCost = conf.Get("failcost").Int(DefaultFailCost)
	srv.SoftFailCost = conf.Get("tempfailcost").Int(DefaultSoftFailCost)

	srv.ServiceDiscovery = conf

	srv.Hosts = make(map[string]*HostConfig)
	for _, h := range conf.Get("host").Slice() {
		host := &HostConfig{}
		host.Name = h.Get("name").String("")
		host.Cost = h.Get("cost").Int(DefaultCost)
		host.Disabled = h.Get("disabled").Bool(false)
		host.Ports = make(map[string]string)

		for _, pval := range h.Slice() {
			if !pval.Leaf() {
				continue
			}
			pname := pval.Key()
			if strings.HasSuffix(pname, "port") {
				host.Ports[pname] = pval.String("")
			} else if pname == "path" {
				host.Ports["path"] = pval.String("")
			}
		}
		srv.Hosts[h.Key()] = host
	}

	return srv
}
