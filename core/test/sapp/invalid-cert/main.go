// Copyright 2018 Schibsted

package main

import (
	"bytes"
	"crypto/tls"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/http/httptrace"
	"strings"

	"github.com/schibsted/sebase/core/pkg/fd_pool"
	"github.com/schibsted/sebase/core/pkg/sapp"
)

var app sapp.Sapp

func main() {
	app.Flags("", true)
	if err := app.Init("invalid-cert"); err != nil {
		log.Fatal(err)
	}

	var tests = []struct {
		service       string
		proto         string
		endpoint      string
		useClientCert bool
		status        int
		err           string
	}{
		// These requests should fail. Our cert isn't signed by the correct CA.
		{"sapp-test-server-default-acl", "https", "/allow_get", true, 0, "remote error: tls: bad certificate"},
		{"sapp-test-server-custom-acl", "https", "/allow_get", true, 0, "remote error: tls: bad certificate"},

		// Should succeed. No client cert given and the ACL accepts local connections.
		{"sapp-test-server-default-acl", "https", "/allow_get", false, 200, ""},
		{"sapp-test-server-default-acl", "http", "/allow_get", false, 200, ""},

		// These requests should fail. ACL requires a cert but we don't have
		// one which is signed by the correct CA.
		{"sapp-test-server-custom-acl", "https", "/client_cert_required", true, 0, "remote error: tls: bad certificate"},
		{"sapp-test-server-custom-acl", "https", "/client_cert_required", false, 403, ""},
		{"sapp-test-server-custom-acl", "http", "/client_cert_required", false, 403, ""},
	}
	for _, t := range tests {
		status, err := req(t.service, t.proto, t.endpoint, t.useClientCert)
		if (t.status != 0 && status != t.status) || (t.err != "" && (err == nil || !strings.HasSuffix(err.Error(), t.err))) {
			log.Fatalf("Expected status %d error '%s', got status %d error '%s'\n", t.status, t.err, status, err)
		}
	}
}

func req(service, proto, endpoint string, useClientCert bool) (int, error) {
	getUrl := fd_pool.GetUrl(app.Pool, proto, service, "port") + endpoint
	certs := app.TlsConf.Certificates
	if !useClientCert {
		certs = nil
	}
	tlsConf := &tls.Config{
		InsecureSkipVerify: true,
		Certificates:       certs,
	}
	client := http.Client{Transport: &http.Transport{DialContext: fd_pool.Dialer(app.Pool), TLSClientConfig: tlsConf}}

	req, err := http.NewRequest("GET", getUrl, nil)
	if err != nil {
		return 0, err
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
		return 0, err
	}

	buf := new(bytes.Buffer)
	buf.ReadFrom(rsp.Body)
	body := buf.String()
	fmt.Printf("GET %s://%s%s: status: %d body: '%s'\n", proto, ra, endpoint, rsp.StatusCode, body)
	rsp.Body.Close()
	return rsp.StatusCode, nil
}
