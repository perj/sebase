// Copyright 2018 Schibsted

package fd_pool

import (
	"context"
	"io"
	"log"
	"net"
	"net/http"
	"testing"
)

var l net.Listener
var port int

func init() {
	DebugLog = defaultLogger(true)

	l, err := net.Listen("tcp", ":0")
	if err != nil {
		log.Fatal(err)
	}
	port = l.Addr().(*net.TCPAddr).Port
	go http.Serve(l, nil)
}

func readwrite(tb testing.TB, pool FdPool, service string) {
	conn, err := pool.NewConn(context.TODO(), service, "port", "")
	if err != nil {
		tb.Fatal(err)
	}
	defer conn.Close()

	_, err = io.WriteString(conn, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
	if err != nil {
		tb.Fatal(err)
	}

	data := make([]byte, 1024)
	n, err := conn.Read(data)
	if err != nil {
		tb.Fatal(err)
	}
	exp := "HTTP/1.1 404"
	if n < len(exp) || string(data[:len(exp)]) != exp {
		tb.Errorf("Unexpected respone %s != %s", string(data[:len(exp)]), exp)
	}
}
