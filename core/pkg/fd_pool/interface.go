// Copyright 2018 Schibsted

package fd_pool

import (
	"context"
	"net"
	"time"

	"github.com/schibsted/sebase/util/pkg/sbalance"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

type FdPool interface {
	Close() error

	SetDialDomain(dom string)
	DialDomain() string

	AddConf(ctx context.Context, conf bconf.Bconf) (string, error)
	AddSingle(ctx context.Context, service, netw, addr string, retries int, connectTimeout time.Duration) error

	NewConn(ctx context.Context, service, portKey, remoteAddr string) (NetConn, error)
}

// Extends net.Conn with functions to move to next node, put back the
// connection into the pool, and some state extraction.
//
// After you're done using a connection you call either Close or Put. If you
// chose the latter, the connection will be put back into the pool for later
// reuse.
//
// If the connection fails for some reason, typically either a timeout or the
// server giving a "too busy" answer, you can call Next to move the connection
// to another node in the pool, and then use it again, provided no error was
// returned. The status should be either sbalance.Fail for a seemingly broken
// server, or sbalance.TempFail for a more temporary problem.
// It will return ErrNoServiceNodes if the nodes are depleted and no connection
// attempt was made. If a connection was attempted but failed the error from
// the dial function will be returned.
// After Next any deadlines or other connection specific value set will be
// removed so you will have to set them again.
//
// Reset can be used to start over from the start. In case of random strat a
// new node will be chosen. It's rarely used.
// It will call Close first, while PutReset is the same but calls Put.
// You must not have called Close or Put if you use these. They have the same
// return values as Next.
//
// Peer and PortKey gives information about the currently connected to
// node.
type NetConn interface {
	net.Conn

	Put()

	Next(ctx context.Context, status sbalance.ConnStatus) error
	Reset(ctx context.Context) error
	PutReset(ctx context.Context) error

	Peer() string
	PortKey() string
}

type PortInfo struct {
	NodeKey        string `json:"node"`
	Netw           string `json:"network"`
	Addr           string `json:"peer"`
	PortKey        string `json:"port_key"`
	Cost           int    `json:"cost"`
	EffectiveCost  int    `json:"effective_cost"`
	NumStoredConns int    `json:"num_stored_fds"`
}
