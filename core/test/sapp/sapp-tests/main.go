// Copyright 2018 Schibsted

package main

import (
	"bytes"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/http/httptrace"
	"os"
	"os/exec"

	"github.com/schibsted/sebase/core/pkg/fd_pool"
	"github.com/schibsted/sebase/core/pkg/sapp"
)

var app sapp.Sapp

const dialDomain = "foobar"
const bodyOk = "Hello, world!\n"
const bodyDenied = "Forbidden by ACL\n"
const getFailLog = "client.Do failed: Get https://sapp-test-server-log.foobar:8080/panic: EOF"

func main() {
	app.Flags("", true)
	if err := app.Init("sapp-tests"); err != nil {
		log.Fatal(err)
	}

	app.Pool.SetDialDomain(dialDomain)

	var tests = []struct {
		service  string
		method   string
		endpoint string
		status   int
		body     string
	}{
		{"sapp-test-server-default-acl", "GET", "/allow_get", 200, bodyOk},
		{"sapp-test-server-custom-acl", "GET", "/allow_get", 200, bodyOk},
		{"sapp-test-server-custom-acl", "POST", "/allow_get", 403, bodyDenied},
		{"sapp-test-server-custom-acl", "GET", "/deny_remote_addr", 403, bodyDenied},
		{"sapp-test-server-custom-acl", "GET", "/no_substring_match", 200, bodyOk},
		{"sapp-test-server-custom-acl", "GET", "/no_substring_match/", 403, bodyDenied},
		{"sapp-test-server-custom-acl", "GET", "/no_substring_matchfoo", 403, bodyDenied},
		{"sapp-test-server-custom-acl", "GET", "/no_substring_match/foo", 403, bodyDenied},
		{"sapp-test-server-custom-acl", "GET", "/substring_match", 403, bodyDenied},
		{"sapp-test-server-custom-acl", "GET", "/substring_match/", 200, bodyOk},
		{"sapp-test-server-custom-acl", "GET", "/substring_matchfoo", 403, bodyDenied},
		{"sapp-test-server-custom-acl", "GET", "/substring_match/foo", 200, bodyOk},
		{"sapp-test-server-log", "GET", "/panic", -1, getFailLog},
		{"sapp-test-server-log", "GET", "/allow_get", 200, bodyOk},
		{"sapp-test-server-log", "GET", "/forbidden", 403, bodyDenied},
	}
	// Preallocate room for the service.
	watch := make([]string, 0, 5)
	watch = append(watch, "-appl", "sapp-tests", "-etcd", app.Bconf.Get("sd", "etcd_url").String(""))
	for _, t := range tests {
		status, body := req(t.service, t.method, t.endpoint)
		if status != t.status || body != t.body {
			log.Fatalf("Expected status %d body '%s', got status %d body '%s'\n", t.status, t.body, status, body)
		}
		cmd := exec.Command("watch-service", append(watch, t.service)...)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		err := cmd.Run()
		if err != nil {
			log.Fatal(err)
		}
	}
}

func req(service string, method string, endpoint string) (int, string) {
	getUrl := fd_pool.GetUrl(app.Pool, "https", service, "port") + endpoint
	client := http.Client{Transport: &http.Transport{DialContext: fd_pool.Dialer(app.Pool), TLSClientConfig: app.TlsConf}}

	req, err := http.NewRequest(method, getUrl, nil)
	if err != nil {
		log.Fatalf("http.NewRequest failed: %s\n", err)
	}

	var ra net.Addr
	trace := &httptrace.ClientTrace{
		GotConn: func(connInfo httptrace.GotConnInfo) {
			ra = connInfo.Conn.RemoteAddr()
		},
	}

	req = req.WithContext(httptrace.WithClientTrace(req.Context(), trace))

	rsp, err := client.Do(req)
	if err != nil {
		return -1, fmt.Sprintf("client.Do failed: %s", err)
	}

	buf := new(bytes.Buffer)
	buf.ReadFrom(rsp.Body)
	body := buf.String()
	fmt.Printf("%s http://%s%s: status: %d body: '%s'\n", method, ra, endpoint, rsp.StatusCode, body)
	rsp.Body.Close()
	return rsp.StatusCode, body
}
