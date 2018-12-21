// Copyright 2018 Schibsted

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net"
	"strings"
	"time"

	"github.com/schibsted/sebase/core/pkg/sapp"
)

// This program is for getting a port number for a service in regress tests.
// Also, it allows us to wait for a service to come up, but that should be seen
// as a solution of last resort.

func main() {
	app := sapp.Sapp{}
	port := flag.String("port", "port", "service port")
	retry := flag.Int("retry", 0, "How many retries before giving up (50ms timeout)")
	app.Flags("", true)
	if err := app.Init("sd-port"); err != nil {
		log.Fatal(err)
	}
	svc := flag.Arg(0)

	// XXX - this is very brutal. Instead of just querying sd, we just create a connection
	//       and then parse its remote addr. It might also make sense to return the full address.

	var conn net.Conn
	var err error

	for ; *retry >= 0; *retry-- {
		conn, err = app.Pool.NewConn(context.Background(), svc, *port, "")
		if err == nil {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	if err != nil {
		log.Fatal(err)
	}
	defer conn.Close()

	ra := strings.Split(conn.RemoteAddr().String(), ":")
	l := len(ra)
	fmt.Println(ra[l-1])
}
