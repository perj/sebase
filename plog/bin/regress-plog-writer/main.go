// Copyright 2018 Schibsted

package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"strconv"
	"time"
)

func listenTcp(addr string) <-chan []byte {
	l, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatal(err)
	}
	ret := make(chan []byte, 1*1024*1024)
	go func() {
		for {
			conn, err := l.Accept()
			if err != nil {
				log.Print(err)
				continue
			}
			go func() {
				r := bufio.NewReader(conn)
				var err error
				for err == nil {
					var line []byte
					line, err = r.ReadBytes('\n')
					if len(line) > 0 {
						ret <- line
					}
				}
			}()
		}
	}()
	return ret
}

type redirect struct {
	fname string
	w     io.WriteCloser
	n     int
	cc    chan struct{}
}

var redirects = make(chan redirect)

func redirectHandler(w http.ResponseWriter, r *http.Request) {
	query := r.URL.Query()

	apps := query["app"]
	ltypes := query["type"]

	if len(apps) == 0 || len(ltypes) == 0 {
		http.NotFound(w, r)
		return
	}

	fname := apps[0] + "." + ltypes[0]

	target := query["target"]
	if len(target) == 0 || target[0] == "none" {
		// Wait for close confirmation from main thread.
		cc := make(chan struct{})
		redirects <- redirect{fname, nil, 0, cc}
		select {
		case <-cc:
			fmt.Fprintln(w, "closed")
		case <-time.After(2 * time.Second):
			fmt.Fprintln(w, "timed out")
		}
		return
	}

	f, err := os.OpenFile(target[0], os.O_WRONLY|os.O_APPEND|os.O_CREATE, 0666)
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	n := 0
	ns := query["n"]
	if len(ns) > 0 {
		n, _ = strconv.Atoi(ns[0])
	}
	redirects <- redirect{fname, f, n, nil}
}

func main() {
	url := os.Args[1]
	dir := os.Args[2]

	if len(os.Args) > 3 {
		ctrlport := os.Args[3]
		http.HandleFunc("/redirect", redirectHandler)
		go http.ListenAndServe(ctrlport, nil)
	}

	ch := listenTcp(url)
	var files = map[string]struct {
		w  io.WriteCloser
		n  int
		cc chan struct{}
	}{}
	running := true
	for running {
		var msg []byte
		select {
		case redir := <-redirects:
			r := files[redir.fname]
			if redir.w != nil {
				if r.w != nil {
					r.w.Close()
				}
				r.w = redir.w
				r.n = redir.n
			}
			if redir.cc != nil {
				if r.n == 0 {
					close(redir.cc)
				} else {
					r.cc = redir.cc
				}
			}
			files[redir.fname] = r
		case msg, running = <-ch:
			//
		}
		if msg == nil {
			continue
		}
		var logm map[string]interface{}
		if err := json.Unmarshal(msg, &logm); err != nil {
			log.Fatal(err)
		}
		prog, _ := logm["prog"].(string)
		if prog == "" {
			prog = "_unknown"
		}
		ttype, _ := logm["type"].(string)
		fname := prog + "." + ttype
		r := files[fname]
		if r.w == nil {
			fname = prog + ".log"
			r = files[fname]
			if r.w == nil {
				f, err := os.OpenFile(dir+"/"+fname, os.O_WRONLY|os.O_APPEND|os.O_CREATE, 0666)
				if err != nil {
					log.Fatal(err)
				}
				r.w = f
				files[fname] = r
			}
		}
		var d []byte
		var err error
		if r.n > 0 {
			// Always expand redirected logs.
			d, err = json.MarshalIndent(&logm, "", "\t")
		} else {
			switch ttype {
			case "log", "EMERG", "ALERT", "CRIT", "ERR", "WARNING", "NOTICE", "INFO", "DEBUG":
				d, err = json.Marshal(&logm)
			default:
				d, err = json.MarshalIndent(&logm, "", "\t")
			}
		}
		if err != nil {
			d = []byte(err.Error())
		}
		d = append(d, byte('\n'))
		r.w.Write(d)
		if r.n == 1 {
			if r.cc != nil {
				close(r.cc)
			}
			r.w.Close()
			delete(files, fname)
		} else if r.n > 0 {
			r.n--
			files[fname] = r
		}
	}
}
