// Copyright 2018 Schibsted

// +build cgo,sebase_cgo

package fd_pool

import (
	"context"
	"fmt"
	"strconv"
	"testing"
	"time"

	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

func TestVtree(t *testing.T) {
	b := bconf.NewCBconf()
	b.Add("host.test.name")("localhost")
	b.Add("host.test.port")(strconv.Itoa(port))
	pool := NewCPool(nil)
	service, err := pool.AddVtree(b.Vtree())
	if err != nil {
		t.Fatal("addvtree", err)
	}
	readwrite(t, pool, service)
}

func TestConfig(t *testing.T) {
	b := &bconf.Node{}
	b.Add("host.test.name")("localhost")
	b.Add("host.test.port")(strconv.Itoa(port))
	pool := NewCPool(nil)
	service, err := pool.AddBconf(context.TODO(), b)
	if err != nil {
		t.Fatal("addvtree", err)
	}
	readwrite(t, pool, service)
}

func TestSingle(t *testing.T) {
	pool := NewCPool(nil)
	err := pool.AddSingle(context.TODO(), "test", "tcp", fmt.Sprintf("localhost:%d", port), 1, 1000*time.Second)
	if err != nil {
		t.Fatal("addsingle", err)
	}

	readwrite(t, pool, "test")
}

func TestCDialer(t *testing.T) {
	testDialer(t, NewCPool(nil))
}
