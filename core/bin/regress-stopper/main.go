// Copyright 2018 Schibsted

package main

import (
	"crypto/tls"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"syscall"

	"github.com/schibsted/sebase/core/pkg/fd_pool"
	"github.com/schibsted/sebase/core/pkg/sapp"
	"github.com/schibsted/sebase/plog/pkg/plog"
)

var app sapp.Sapp

func main() {
	app.Flags("", true)

	if err := app.Init("regress-stopper"); err != nil {
		log.Fatal("init: ", err)
	}

	if flag.NArg() != 1 {
		fmt.Fprintf(os.Stderr, "Usage: regress-stopper <flags> <service>\n")
		flag.PrintDefaults()
		os.Exit(1)
	}

	ret := stopService(fd_pool.GetUrl(app.Pool, "http", flag.Arg(0), "port") + "/stop")
	if ret != 0 {
		ret = stopService(fd_pool.GetUrl(app.Pool, "https", flag.Arg(0), "port") + "/stop")
	}
	os.Exit(ret)
}

func stopService(getUrl string) int {
	var ret int
	for {
		tlsConf := &tls.Config{InsecureSkipVerify: true}
		c := http.Client{Transport: &http.Transport{DialContext: fd_pool.Dialer(app.Pool), TLSClientConfig: tlsConf}}
		rsp, err := c.Get(getUrl)
		if err != nil {
			if urlErr, ok := err.(*url.Error); ok {
				// We can get these two kinds of errors if no
				// instance of the service is running. Ignore
				// them and exit successfully.

				netErr, ok := urlErr.Err.(*net.OpError)
				if ok {
					syscallErr, ok := netErr.Err.(*os.SyscallError)
					if ok {
						if syscallErr.Err == syscall.ECONNREFUSED {
							plog.Debug.Printf("Got expected error: %s", err)
							break
						}
					}
				}

				syscallErr, ok := urlErr.Err.(syscall.Errno)
				if ok && syscallErr == syscall.ECONNREFUSED {
					plog.Debug.Printf("Got expected error: %s", err)
					break
				}

				_, ok = urlErr.Err.(*fd_pool.ErrNoServiceNodes)
				if ok {
					plog.Debug.Printf("Got expected error: %s", err)
					break
				}
			}
			ret = 1
			plog.Error.Printf("Unexpected error: %s", err)
			break
		} else if rsp.StatusCode != 200 {
			plog.Error.Printf("Got unexpected status code %d", rsp.StatusCode)
			ret = 1
			break
		} else {
			plog.Debug.Printf("Got 200 OK")
		}
	}

	return ret
}
