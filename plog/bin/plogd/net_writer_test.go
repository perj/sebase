// Copyright 2018 Schibsted

package main

import "testing"

func TestConnRefused(t *testing.T) {
	wr := &NetWriter{network: "tcp", address: "localhost:1", signal: make(chan netmsg, 1), data: make(chan []byte, 1024)}
	err := wr.Connect()
	if err != nil {
		t.Fatalf("%T %#v", err, err)
	}
	wr.Close()
}
