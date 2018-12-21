// Copyright 2018 Schibsted

package main

import (
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"

	"github.com/schibsted/sebase/core/pkg/sapp"
	"github.com/schibsted/sebase/plog/pkg/plog"
)

var app sapp.Sapp

func main() {
	app.Flags("", true)
	service := flag.Arg(0)
	if service == "" {
		log.Fatal("No service name provided")
	}
	if err := app.Init(service); err != nil {
		log.Fatal(err)
	}

	srv := app.DefaultServer()
	app.DefaultHandlers(srv, nil)
	hello := func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, "Hello, world!\n")
	}
	http.HandleFunc("/allow_get", hello)
	http.HandleFunc("/deny_remote_addr", hello)
	http.HandleFunc("/client_cert_required", hello)
	http.HandleFunc("/no_substring_match", hello)
	http.HandleFunc("/substring_match/", hello)
	http.HandleFunc("/panic", func(_ http.ResponseWriter, _ *http.Request) { panic("panic!") })

	if err := app.SDRegister(); err != nil {
		log.Fatal(err)
	} else {
		plog.Info.Printf("SD registration done")
	}

	l, err := net.Listen("tcp", ":")
	if err != nil {
		log.Fatal(err)
	}
	plog.Info.Printf("Listen address: %s", l.Addr())
	app.SDListener("port", l, true)
	app.SDListener("http_port", l, true)

	if err := app.Ready(); err != nil {
		log.Fatal(err)
	}

	err = sapp.ServeHTTPAndHTTPS(srv, l)
	if err == http.ErrServerClosed {
		app.WaitForShutdown()
	} else {
		log.Fatal(err)
	}
	app.Close()
}
