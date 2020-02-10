// Copyright 2018 Schibsted

// A privilege separation HTTP proxy
//
// The privilege it's guarding is the permission to talk to the upstream server.
// Incoming requests are checked against an ACL before forwarded using the
// configured client certificate, which should be kept private.
// In addition, the incoming client must authenticate with a separate client
// certificate that the proxy accepts. This can be done with a simple valid
// cert check or by specifying an exact common name or key signature.
//
// Thus you can have a certificate with more permissions and this proxy reduces
// it to a lower set. Obviously the incoming client can't have access to the
// upstream client certificate file or this is pointless.
//
// The program will exit with status code 10 if it detects that any of the
// certificates expires. You can use that exit code to detect this condition.
package main

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/pem"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"math/big"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"strings"
	"syscall"
	"time"
	"unicode"

	"github.com/schibsted/sebase/core/pkg/acl"
	"github.com/schibsted/sebase/core/pkg/loghttp"
	"github.com/schibsted/sebase/plog/pkg/plog"
	yaml "gopkg.in/yaml.v2"
)

type Proxy struct {
	Handler http.Handler
	Acl     acl.Acl
	Server  *http.Server
}

func (p *Proxy) ServeHTTP(rw http.ResponseWriter, req *http.Request) {
	if p.Acl.CheckRequest(req) {
		plog.Info.CtxMsg(req.Context(), "Forwarding request", "url", req.URL.String())
		if req.URL.Path == "/stop" {
			p.Server.Shutdown(context.Background())
		}
		p.Handler.ServeHTTP(rw, req)
	} else {
		plog.Info.CtxMsg(req.Context(), "Forbidden by ACL", "url", req.URL.String())
		http.Error(rw, "Forbidden by ACL", http.StatusForbidden)
	}
}

func isNoIPv6(err error) bool {
	nerr, ok := err.(*net.OpError)
	if ok {
		err = nerr.Err
	}
	serr, ok := err.(*os.SyscallError)
	if ok {
		err = serr.Err
	}
	return err == syscall.EADDRNOTAVAIL || err == syscall.EAFNOSUPPORT
}

func main() {
	laddr := flag.String("listen", "", "Address to listen on. By default the port is extracted from the upstream URL and the proxy will listen on localhost on the same port.")
	insecure := flag.Bool("insecure", false, "Listen for HTTP, not HTTPs. Server certificates are not required if using this. Not recommended.")

	serverCert := flag.String("server-cert", "", "File containing PEM encoded certificate and private key concatenated, to send to incoming clients.")
	serverCA := flag.String("server-ca", "", "File containing PEM encoded CA certificate to authenticate incoming clients with.")

	clientCert := flag.String("upstream-cert", "", "File containing PEM encoded certificate and private key concatenated, to send to server we are proxying.")
	clientCA := flag.String("upstream-ca", "", "File containing PEM encoded CA certificate to authenticate server we are proxying. If unset the system CA pool will be used.")

	aclFile := flag.String("acl", "", "Load ACL from this file.")

	minExitEarly := flag.Duration("min-exit-early", 0, "Exit with exit code 10 at least this duration before any of the certificates expires. A random duration between min and max is used, or the exact time if only min is given.")
	maxExitEarly := flag.Duration("max-exit-early", 0, "Exit with exit code 10 at most this duration before any of the certificates expires. A random duration between min and max is used.")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "\t%s -upstream-cert <file> -server-cert <file> -server-ca <file> [<options>] <https-url>\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "\t%s -upstream-cert <file> -insecure [<options>] <https-url>\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "\nOptions:\n")
		flag.PrintDefaults()
	}
	flag.Parse()

	urlstr := flag.Arg(0)
	if urlstr == "" || *clientCert == "" {
		flag.Usage()
		os.Exit(1)
	}
	if !*insecure && (*serverCert == "" || *serverCA == "") {
		flag.Usage()
		os.Exit(1)
	}

	plog.Setup("acl-proxy", "info")

	if *minExitEarly < 0 {
		log.Fatal("-min-exit-early is negative")
	}
	if *maxExitEarly == 0 {
		*maxExitEarly = *minExitEarly
	}
	if *maxExitEarly < *minExitEarly {
		log.Fatal("-max-exit-early is lower than -min-exit-early")
	}

	url, err := url.Parse(urlstr)
	if err != nil {
		log.Fatal(err)
	}

	var accessList acl.Acl
	if *aclFile != "" {
		aclData, err := ioutil.ReadFile(*aclFile)
		if err != nil {
			log.Fatal(err)
		}
		err = yaml.UnmarshalStrict(aclData, &accessList.Acl)
		if err != nil {
			log.Fatal(err)
		}
	}

	if len(accessList.Acl) == 0 {
		log.Print("Empty ACL, all requests will be denied")
	}

	if *laddr == "" {
		if strings.ContainsRune(url.Host, ':') {
			_, port, err := net.SplitHostPort(url.Host)
			if err != nil {
				log.Fatal(err)
			}
			*laddr = "[::1]:" + port
		} else {
			*laddr = "[::1]:https"
		}
	}

	clCert, runUntil := LoadCert(*clientCert, time.Time{})
	var clCA *x509.CertPool // A nil CertPool will default to system certs.
	if *clientCA != "" {
		clCA, runUntil = LoadCA(*clientCA, runUntil)
	}

	h := httputil.NewSingleHostReverseProxy(url)
	h.Transport = &http.Transport{
		TLSClientConfig: &tls.Config{
			Certificates: clCert,
			RootCAs:      clCA,
		},
	}
	proxy := Proxy{
		Handler: h,
		Acl:     accessList,
	}

	l, err := net.Listen("tcp", *laddr)
	if err != nil {
		// Apparently there's still kernels out there that don't have
		// IPv6 enabled.
		log.Printf("Try again with IPv4 socket.")
		if strings.HasPrefix(*laddr, "[::1]:") && isNoIPv6(err) {
			*laddr = "127.0.0.1:" + strings.TrimPrefix(*laddr, "[::1]:")
			l, err = net.Listen("tcp", *laddr)
		}
		if err != nil {
			log.Fatal(err)
		}
	}
	defer l.Close()

	proxy.Server = &http.Server{
		Handler: loghttp.LogMiddleware(&proxy, nil, nil),
	}
	stopCh := make(chan struct{})
	if *insecure {
		go func() {
			err := proxy.Server.Serve(l)
			if err != http.ErrServerClosed {
				log.Fatal(err)
			}
			close(stopCh)
		}()
	} else {
		var svCert []tls.Certificate
		var svCA *x509.CertPool
		svCert, runUntil = LoadCert(*serverCert, runUntil)
		svCA, runUntil = LoadCA(*serverCA, runUntil)
		proxy.Server.TLSConfig = &tls.Config{
			Certificates: svCert,
			ClientCAs:    svCA,
			ClientAuth:   tls.RequireAndVerifyClientCert,
		}
		go func() {
			err := proxy.Server.ServeTLS(l, "", "")
			if err != http.ErrServerClosed {
				log.Fatal(err)
			}
			close(stopCh)
		}()
	}

	runUntil = SubExitEarly(runUntil, *minExitEarly, *maxExitEarly)

	if runUntil.IsZero() {
		log.Printf("No certificats, will run until stopped.")
		<-stopCh
		return
	}
	log.Printf("Will run until %v", runUntil)
	select {
	case <-time.After(runUntil.Sub(time.Now())):
		os.Exit(10)
	case <-stopCh:
		log.Printf("Stopped via controller")
	}
}

func SubExitEarly(t time.Time, min, max time.Duration) time.Time {
	if t.IsZero() || min < 0 || max < 0 || max < min {
		return t
	}
	max -= min
	if max > 0 {
		r, err := rand.Int(rand.Reader, big.NewInt(int64(max)))
		if err != nil {
			panic(err)
		}
		max = time.Duration(r.Int64())
	}
	return t.Add(-min - max)
}

func LoadCert(certFile string, na time.Time) (certs []tls.Certificate, notAfter time.Time) {
	if certFile == "/dev/null" {
		return nil, na
	}
	cert, err := tls.LoadX509KeyPair(certFile, certFile)
	if err != nil {
		log.Fatal(err)
	}
	cert.Leaf, err = x509.ParseCertificate(cert.Certificate[0])
	if err != nil {
		log.Fatal(err)
	}
	if na.IsZero() || cert.Leaf.NotAfter.Before(na) {
		na = cert.Leaf.NotAfter
	}
	return []tls.Certificate{cert}, na
}

func LoadCA(caFile string, na time.Time) (cpool *x509.CertPool, notAfter time.Time) {
	caData, err := ioutil.ReadFile(caFile)
	if err != nil {
		log.Fatal(err)
	}
	caData = bytes.TrimLeftFunc(caData, unicode.IsSpace)
	pool := x509.NewCertPool()
	for len(caData) > 0 {
		var pemdata *pem.Block
		lenCheck := len(caData)
		pemdata, caData = pem.Decode(caData)
		if pemdata == nil && len(caData) == lenCheck {
			log.Printf("Warning: CA file contains non-PEM data.")
			break
		}
		caData = bytes.TrimLeftFunc(caData, unicode.IsSpace)
		if pemdata == nil || pemdata.Type != "CERTIFICATE" {
			continue
		}

		cert, err := x509.ParseCertificate(pemdata.Bytes)
		if err != nil {
			log.Fatal(err)
		}

		if na.IsZero() || cert.NotAfter.Before(na) {
			na = cert.NotAfter
		}
		pool.AddCert(cert)
	}

	return pool, na
}
