// Copyright 2018 Schibsted

package main

import (
	"io"
	"log"
	"net"
	"os"
	"syscall"
)

func main() {
	path := os.Args[1]

	var conn *net.UnixConn
	for {
		c, err := net.Dial("unix", path)
		if err == nil {
			conn = c.(*net.UnixConn)
			break
		}
		if operr, ok := err.(*net.OpError); ok {
			err = operr.Err
		}
		if scerr, ok := err.(*os.SyscallError); ok {
			err = scerr.Err
		}
		if err == syscall.ENOENT || err == syscall.ECONNREFUSED {
			continue
		}
		log.Fatal(err)
	}

	go (func() { io.Copy(conn, os.Stdin); conn.CloseWrite() })()
	io.Copy(os.Stdout, conn)
}
