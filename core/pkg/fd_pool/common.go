// Copyright 2018 Schibsted

// Go fd pool implementation, providing client side load balancing.
//
// Basic usage is to set it up with NewGoPool, add a node using AddSingle and
// then use pool.NewConn() to connect.
//
// After you're done using the connection you either use conn.Put() to put it
// back into the pool of open connections, or conn.Close() to close it.
//
// If you encounter an error and wish to connect to the next node in the pool
// you call conn.Next() with either sbalance.Fail or sbalannce.TempFail and
// then keep using the connection as normal, restarting the communication.
//
// Normally pools are setup with multiple nodes from bconf or SD, which is why
// AddConf takes a bconf node.
//
// Configuration
//
// The configuration given to AddConf or stored in Service Discvory have the
// following keys. Typically either bconf or json is used to encode. The bconf
// syntax is shown here, while json would have recursive dictionaries.
// Almost everything is optional, with defaults as mentioned. A host name and
// port is required for a node to exist.
//
// host.X.name=ip
//
// host.X.port=port
//
// Specifies the location of the daemon(s) to connect to. X is often a number but does not have to be.
// In SD it's often a UUID.
//
// host.X.keepalive_port=port
//
// Alternative port used by some API users when the keepalive protocol differs from the normal one.
// Additional ports can be added by any key ending on port.
//
// host.X.cost=number
//
// How expensive it is to connect to this host relatively the other ones. Used to weigh the random connection sequence. Defaults to 1.
//
// strat=random
//
// strat=hash
//
// strat=seq
//
// Changes the order of the connection attempts from sequential to either
// random or deterministically random based on a value given by the API user
// (typically the client ip). By default a random strat is used.
//
// Old keys for these are .random_pick=1 resp. .client_hash=1
//
// failcost=number
//
// If a connection attempt fails, the cost of the host is temporarily changed to this number until a successful query is made. Use 0 for no value, defaults to 100.
//
// tempfailcost=number
//
// If the API user indicates a temporary failure (eg. info:goaway), the cost of the host is changed to this number until a successful query is made. Use 0 for no value, which is also default.
//
// connect_timeout=number
//
// Timeout in milliseconds for the connection attempt. Defaults to 5000 (5 seconds). Note that this is per connection attempt. If a connection fails, the next host is tried, and the connection timeout resets.
//
// retries=number
//
// How many times to iterate the whole host array before giving up.
package fd_pool

import "fmt"

// Error returned when no more nodes could be found when calling Next or NewConn.
type ErrNoServiceNodes struct {
	Service, PortKey string
}

func (e *ErrNoServiceNodes) Error() string {
	return fmt.Sprintf("fd_pool: Couldn't connect to any service nodes for service %s port key %s", e.Service, e.PortKey)
}

type poc struct {
	NetConn
}

// Returns a NetConn that when closed
// will call Put on the passed conn.
// Returns nil on nil input.
func PutOnClose(c NetConn) NetConn {
	if c == nil {
		return nil
	}
	return poc{c}
}

// PutOnClose version with err passthrough
func PutOnCloseErr(c NetConn, err error) (NetConn, error) {
	if c == nil {
		return nil, err
	}
	return poc{c}, err
}

func (c poc) Close() error {
	c.Put()
	return nil
}
