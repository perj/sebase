// Copyright 2018 Schibsted

package main

import (
	"bufio"
	"fmt"
	"log"
	"net"
	"os"
	"strconv"
)

func main() {
	port := os.Args[1]
	n, _ := strconv.Atoi(os.Args[2])

	l, err := net.Listen("tcp", ":"+port)
	if err != nil {
		log.Fatal(err)
	}
	conn, err := l.Accept()
	if err != nil {
		log.Fatal(err)
	}
	r := bufio.NewReader(conn)

	for n > 0 {
		var line []byte
		line, err = r.ReadBytes('\n')
		if len(line) > 0 {
			fmt.Print(string(line))
			n--
		}
	}
}
