// Copyright 2018 Schibsted

package fd_pool

import (
	"context"
	"fmt"
	"log"
	"net"
	"runtime"
	"strconv"
	"testing"
	"time"

	"github.com/schibsted/sebase/util/pkg/sbalance"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

func TestConf(t *testing.T) {
	b := &bconf.Node{}
	b.Add("host.test.name")("localhost")
	b.Add("host.test.port")(strconv.Itoa(port))
	pool := NewGoPool(nil)
	service, err := pool.AddConf(context.TODO(), b)
	if err != nil {
		t.Fatal("addconf", err)
	}
	readwrite(t, pool, service)
}

func TestGoSingle(t *testing.T) {
	pool := NewGoPool(nil)
	defer pool.Close()
	err := pool.AddSingle(context.TODO(), "test", "tcp", fmt.Sprintf("localhost:%d", port), 1, 1*time.Second)
	if err != nil {
		t.Fatal("addsingle", err)
	}

	readwrite(t, pool, "test")
}

func TestGoDialer(t *testing.T) {
	testDialer(t, NewGoPool(nil))
}

type testCloser struct {
	net.Conn
}

var testLastClosed net.Conn

func (t testCloser) Close() error {
	testLastClosed = t.Conn
	return t.Conn.Close()
}

func testDial(ctx context.Context, nw, addr string) (net.Conn, error) {
	c, err := (&net.Dialer{}).DialContext(ctx, nw, addr)
	return testCloser{c}, err
}

func TestUpdateHosts(t *testing.T) {
	pool := NewGoPool(nil)
	defer pool.Close()
	pool.AddSingle(context.TODO(), "test", "tcp", fmt.Sprintf("127.0.0.1:%d", port), DefaultRetries, 1*time.Second)
	pool.SetDialFunc("test", testDial)

	srv := pool.findService("test")
	if srv == nil {
		t.Fatal("Didn't find test service")
	}
	if srv.Len() != 1 {
		t.Errorf("Weird sb len, %d != 1", srv.Len())
	}

	c, err := pool.NewConn(context.TODO(), "test", "port", "")
	if err != nil {
		t.Fatal("NewConn", err)
	}

	// Test put and reget the same connection.
	testLastClosed = nil
	nc := c.(*conn).Conn
	c.Put()
	c.Close() // Test that close after put is no-op.
	if testLastClosed != nil {
		t.Fatal("Connection closed by Put")
	}

	c, err = pool.NewConn(context.TODO(), "test", "port", "")
	if err != nil {
		t.Fatal("NewConn 2", err)
	}
	if c.(*conn).Conn != nc {
		t.Error("NewConn 2 created a new connection.")
	}

	if runtime.GOOS == "openbsd" {
		t.Skip("Test requires unsetting IPV6_V6ONLY.")
	}
	origsb := srv.Service
	newhost := &bconf.Node{}
	newhost.Add("host.1.name")("::1")
	newhost.Add("host.1.port")(fmt.Sprint(port))
	n, err := pool.UpdateHosts(context.TODO(), "test", newhost)
	if err != nil {
		t.Fatal("UpdateHosts", err)
	}
	if n != 1 {
		t.Fatalf("UpdateHosts got unexpected result %v, expected 1.", n)
	}

	newsb := srv.Service
	if newsb == origsb {
		t.Error("Sbalance pointer wasn't updated")
	}
	if c.(*conn).sb != origsb {
		t.Error("Unexpected conn.sb pointer, should be orig sb")
	}

	c.Put()
	if testLastClosed == nil {
		t.Errorf("Put on old gen conn didn't close it. %#v", c.(*conn).connset)
	}

	// Test that same node is reused if addr didn't change.
	testLastClosed = nil
	c, err = pool.NewConn(context.TODO(), "test", "port", "")
	if err != nil {
		t.Fatal("NewConn 3", err)
	}
	n, err = pool.UpdateHosts(context.TODO(), "test", newhost)
	if err != nil {
		t.Fatal("UpdateHosts", err)
	}
	if n != 1 {
		t.Fatalf("UpdateHosts got unexpected result %v, expected 1.", n)
	}
	c.Put()
	if testLastClosed != nil {
		t.Error("Put on existing node closed it.")
	}
}

func TestNext(t *testing.T) {
	pool := NewGoPool(nil)
	defer pool.Close()
	pool.AddSingle(context.TODO(), "test", "tcp", fmt.Sprintf("127.0.0.1:%d", port), DefaultRetries, 1*time.Second)
	pool.AddSingle(context.TODO(), "test", "tcp", fmt.Sprintf("[::1]:%d", port), DefaultRetries, 1*time.Second)

	c, err := pool.NewConn(context.TODO(), "test", "port", "")
	if err != nil {
		t.Fatal("NewConn", err)
	}
	if runtime.GOOS == "openbsd" {
		t.Skip("Test requires unsetting IPV6_V6ONLY.")
	}
	err = c.Next(context.TODO(), sbalance.Fail)
	if err != nil {
		t.Fatal("Next", err)
	}
	// The way sbalance works the first two nodes for random might be the same one.
	// Thus we only check after the first next.
	n1 := c.(*conn).srvnode
	err = c.Next(context.TODO(), sbalance.Fail)
	if err != nil {
		t.Fatal("Next", err)
	}
	n2 := c.(*conn).srvnode
	if n1 == n2 {
		t.Error("Got the same node after next.")
	}
	err = c.Next(context.TODO(), sbalance.Fail)
	if err == nil {
		t.Error("Expected next to fail but it didn't.")
	}
	if c.(*conn).Conn != nil {
		t.Error("Expected conn.Conn to be nil after Next failed.")
	}
}

func TestReused(t *testing.T) {
	ln, err := net.Listen("tcp", ":0")
	if err != nil {
		log.Fatal(err)
	}
	defer ln.Close()
	lconch := make(chan net.Conn)
	go func() {
		defer close(lconch)
		for {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			lconch <- conn
		}
	}()

	// Setup
	port := ln.Addr().(*net.TCPAddr).Port
	pool := NewGoPool(nil)
	defer pool.Close()
	pool.AddSingle(context.TODO(), "test", "tcp", fmt.Sprintf("127.0.0.1:%d", port), DefaultRetries, 1*time.Second)
	c, err := pool.NewConn(context.TODO(), "test", "port", "")
	if err != nil {
		t.Fatal("NewConn", err)
	}

	// Check that put + newconn gets the same connection back.
	conn1 := c.(*conn).Conn
	c.Put()
	c, err = pool.NewConn(context.TODO(), "test", "port", "")
	if err != nil {
		t.Fatal("NewConn", err)
	}
	if conn1 != c.(*conn).Conn {
		t.Error("Connection wasn't reused after Put.")
	}

	// Check again but close from server-side. NewConn should now return a new connection.
	// I haven't found a way to make this check work on OpenBSD yet.
	if runtime.GOOS == "openbsd" {
		c.Close()
		t.Skip("Not yet implemented for OpenBSD.")
	}
	c.Put()
	lconn := <-lconch
	lconn.Close()
	c, err = pool.NewConn(context.TODO(), "test", "port", "")
	if err != nil {
		t.Fatal("NewConn", err)
	}
	if conn1 == c.(*conn).Conn {
		t.Error("Connection was reused after server-side close.")
	}

	// Cleanup
	lconn = <-lconch
	lconn.Close()
	c.Close()
}

func TestEmptyRemoteAddr(t *testing.T) {
	pool := NewGoPool(nil)
	defer pool.Close()
	err := pool.AddSingle(context.TODO(), "test", "tcp", fmt.Sprintf("localhost:%d", port), 1, 1*time.Second)
	if err != nil {
		t.Fatal("addsingle", err)
	}

	c, err := pool.NewConn(context.TODO(), "test", "port", "")
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()

	if c.(*conn).sbseed != nil {
		t.Errorf("Expected nil sbseed, got %#v", c.(*conn).sbseed)
	}
}
