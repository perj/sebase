// Copyright 2018 Schibsted

package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"net/http"
	"net/url"
	"os"
	"sort"
)

func main() {
	flag.Usage = func() {
		fmt.Fprintf(flag.CommandLine.Output(), "Usage: %s [options] <portfile>\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()
	portfile := flag.Arg(0)
	if portfile == "" {
		flag.Usage()
		os.Exit(1)
	}

	srv := &http.Server{}

	stopped := make(chan struct{})
	done := make(chan struct{})
	http.HandleFunc("/stop", func(w http.ResponseWriter, r *http.Request) {
		go func() {
			srv.Shutdown(context.Background())
			close(done)
		}()
		<-stopped
	})

	http.HandleFunc("/ok", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, "OK handler")
	})

	http.HandleFunc("/post", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			http.Error(w, "", http.StatusMethodNotAllowed)
			return
		}
		fmt.Fprintln(w, "Foobar")
		switch r.Header.Get("Content-Type") {
		case "application/x-www-form-urlencoded":
			if err := r.ParseForm(); err != nil {
				panic(err)
			}
			var ks []string
			for k := range r.PostForm {
				ks = append(ks, k)
			}
			sort.Strings(ks)
			for _, k := range ks {
				for _, v := range r.PostForm[k] {
					fmt.Fprintf(w, "%s: %s\n", k, v)
				}
			}
		default:
			// Test still expects 200.
		}
	})

	var upload bytes.Buffer
	moved := false
	http.HandleFunc("/upload", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case "GET":
			io.Copy(w, &upload)
		case "PUT":
			upload.Reset()
			io.Copy(&upload, r.Body)
			r.Body.Close()
			moved = false
			w.WriteHeader(201)
		case "DELETE":
			upload.Reset()
			w.WriteHeader(204)
		case "MOVE", "COPY":
			dst, err := url.Parse(r.Header.Get("Destination"))
			if err != nil {
				panic(err)
			}
			if dst.Path != "/upload_moved" {
				w.WriteHeader(403)
				return
			}
			moved = true
			w.WriteHeader(201)
		default:
			http.Error(w, "", http.StatusMethodNotAllowed)
		}
	})

	http.HandleFunc("/upload_moved", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "DELETE" {
			http.Error(w, "", http.StatusMethodNotAllowed)
			return
		}
		if moved {
			w.WriteHeader(204)
			moved = false
		} else {
			w.WriteHeader(404)
		}
	})

	http.HandleFunc("/cmp-upload", func(w http.ResponseWriter, r *http.Request) {
		data, err := ioutil.ReadFile("binary_blob")
		if err != nil {
			panic(err)
		}
		if bytes.Compare(upload.Bytes(), data) == 0 {
			w.WriteHeader(204)
			return
		}
		http.Error(w, "Mismatch", 400)
	})

	l, err := net.Listen("tcp", ":")
	if err != nil {
		panic(err)
	}
	_, p, err := net.SplitHostPort(l.Addr().String())
	if err != nil {
		panic(err)
	}
	ioutil.WriteFile(portfile, []byte(fmt.Sprintln(p)), 0666)

	err = srv.Serve(l)
	if err != http.ErrServerClosed {
		panic(err)
	}
	l.Close()
	close(stopped)
	<-done
}
