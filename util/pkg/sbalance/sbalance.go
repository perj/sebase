// Copyright 2018 Schibsted

// This package allows you to iterate over a set of service nodes, either in
// sequence or randomly. The RNG can be seeded and will give the same node
// order for the same seed.
package sbalance

import (
	"bytes"
	crand "crypto/rand"
	"hash/fnv"
	"io"
	"math/rand"
)

// What kind of strategy should be used to iterate the nodes.
// Sequencial simply walks from the first downwards, ignoring cost.
// Random first picks a random node based on cost, with random fallback.
// Hash works like random, but seeds the RNG with the seed given, thus generating
// the same sequence for the same seed.
type BalanceStrat int

const (
	StratSeq BalanceStrat = iota
	StratRandom
	StratHash
)

type nodeData struct {
	cost    int
	effcost int
	node    interface{}
}

// A service with a set of nodes. Initialize with the Strat you will use and
// call Service.AddNode to add nodes.
type Service struct {
	Retries      int // How many times to retry the whole set of nodes.
	FailCost     int // Overrides normal node cost on hard failures (connection refused etc.)
	SoftFailCost int // Overrides normal node cost on soft failures (503 Service unavailable)
	Strat        BalanceStrat
	nodes        []nodeData
}

// Add a node to the service. This function is not thread safe and can't be used
// while you're connecting to the nodes.
func (sb *Service) AddNode(node interface{}, cost int) {
	sb.nodes = append(sb.nodes, nodeData{cost, cost, node})
}

// Number of nodes.
func (sb *Service) Len() int {
	return len(sb.nodes)
}

// Get all the nodes.
func (sb *Service) Nodes() []interface{} {
	ret := make([]interface{}, len(sb.nodes))
	for idx := range sb.nodes {
		ret[idx] = sb.nodes[idx].node
	}
	return ret
}

// An iterator for the service nodes. Each call to Next returns a new
// node you should connect to. Once a node has been used successfully, call
// Close to finish.
//
// Next will return nil when the nodes are exhausted, based
// on the number of nodes and the Service.Retries count.
type Connection interface {
	Next(status ConnStatus) interface{}
	Close() error
}

// The reason you're calling Connection.Next.
// Start for the first call or when you want to move to the next node even
// when the current was used successfully.
// Fail if there was a hard failure, e.g. connection refused.
// SoftFail if there was a soft failure, e.g. 503 Service Unavailable.
type ConnStatus int

const (
	Start ConnStatus = iota
	Fail
	SoftFail
)

// Setup a service connection, possibly seeding the random order
// generator with the given seed.
func (sb *Service) NewConn(seed []byte) Connection {
	return stratImpl[sb.Strat](sb, seed)
}

// Return the default and effective (current) costs for the given node at idx.
func (sb *Service) GetCosts(idx int) (cost, effcost int) {
	return sb.nodes[idx].cost, sb.nodes[idx].effcost
}

var stratImpl = [...]func(*Service, []byte) Connection{
	seqInit,
	randInit,
	hashInit,
}

type seqConn struct {
	nodes   []nodeData
	idx     int
	retries int
}

func seqInit(sb *Service, seed []byte) Connection {
	return &seqConn{sb.nodes, -1, sb.Retries}
}

func (conn *seqConn) Next(status ConnStatus) interface{} {
	conn.idx++
	for conn.idx >= len(conn.nodes) {
		if conn.retries <= 0 {
			return nil
		}
		conn.retries--
		conn.idx = 0
	}
	return conn.nodes[conn.idx].node
}

func (conn *seqConn) Close() error {
	return nil
}

type rcycleConn struct {
	sb      *Service
	rng     *rand.Rand
	rc      []int
	retries int
	last    int
	first   int
}

func randInit(sb *Service, seed []byte) Connection {
	return rcycleInit(sb, &io.LimitedReader{crand.Reader, 8})
}

func hashInit(sb *Service, seed []byte) Connection {
	if seed == nil {
		return randInit(sb, nil)
	}
	return rcycleInit(sb, bytes.NewReader(seed))
}

func rcycleInit(sb *Service, r io.Reader) Connection {
	// Ignore errors from Copy
	h := fnv.New64a()
	io.Copy(h, r)
	rng := rand.New(rand.NewSource(int64(h.Sum64())))

	conn := &rcycleConn{sb, rng, nil, sb.Retries, -1, 0}

	// Pick a first node based on cost
	// XXX is this algorithm proven?
	var totw float64
	for i := range conn.sb.nodes {
		w := float64(1.0) / float64(conn.sb.nodes[i].effcost)
		totw += w

		if w/totw > conn.rng.Float64() {
			conn.first = i
		}
	}

	return conn
}

func (conn *rcycleConn) shuffle() {
	// Cost is ignored here, just shuffle.
	// XXX why is cost ignored?
	conn.rc = conn.rng.Perm(len(conn.sb.nodes))
}

func (conn *rcycleConn) Next(status ConnStatus) interface{} {
	if conn.last != -1 {
		switch status {
		case Fail:
			conn.sb.nodes[conn.last].effcost = conn.sb.FailCost
		case SoftFail:
			conn.sb.nodes[conn.last].effcost = conn.sb.SoftFailCost
		}
	}

	// First node special case
	if conn.first != -1 {
		conn.last = conn.first
		conn.first = -1
		if len(conn.sb.nodes) > conn.last {
			return conn.sb.nodes[conn.last].node
		}
	}

	for len(conn.rc) == 0 {
		if conn.retries <= 0 {
			conn.last = -1
			return nil
		}
		conn.retries--
		conn.shuffle()
	}
	conn.last = conn.rc[0]
	conn.rc = conn.rc[1:]
	return conn.sb.nodes[conn.last].node
}

func (conn *rcycleConn) Close() error {
	if conn.last != -1 {
		conn.sb.nodes[conn.last].effcost = conn.sb.nodes[conn.last].cost
	}
	return nil
}
