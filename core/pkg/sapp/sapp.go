// Copyright 2018 Schibsted

package sapp

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"unsafe"

	"github.com/schibsted/sebase/core/pkg/acl"
	"github.com/schibsted/sebase/core/pkg/fd_pool"
	"github.com/schibsted/sebase/core/pkg/loghttp"
	_ "github.com/schibsted/sebase/core/pkg/sd/etcd"
	"github.com/schibsted/sebase/core/pkg/sd/sdr"
	"github.com/schibsted/sebase/plog/pkg/plog"
	"github.com/schibsted/sebase/util/pkg/slog"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

type Sapp struct {
	app      string
	confFile *string
	etcdURL  *string
	logLevel *string
	sdr      *sdr.Registry
	csdr     unsafe.Pointer

	// bconf accessible after initialization
	Bconf bconf.MutBconf
	// fd_pool accessible after initialization
	UseCPool bool
	Pool     fd_pool.FdPool

	// TLS config read from config file during init, if any. For some reason
	// it doesn't work to use the same tls.Config for both the client and
	// the server.
	TlsConf       *tls.Config
	TlsClientConf *tls.Config

	httpsStateCgo struct {
		https    unsafe.Pointer
		cafile   string
		certfile string
	}

	healthCheckEndpoint string
	healthCheckClient   *http.Client

	// If true all HTTP requests will be logged. If false only requests that
	// failed will be logged.
	logAllHttpRequests bool

	sdCtl            chan sdCtl
	sdClosed         chan struct{}
	shutdownComplete chan struct{}
	Acl              acl.Acl
}

// Adds the default flags for all applications.
func (s *Sapp) Flags(defaultConfPath string, parse bool) {
	s.confFile = flag.String("conf", defaultConfPath, "Config file")
	s.etcdURL = flag.String("etcd-url", "", "URL to etcd for service discovery")
	s.logLevel = flag.String("loglevel", "", "Loglevel, will default to info if not set here or in config.")
	if parse {
		flag.Parse()
	}
}

// Returns a *http.Server suitable for SD queries.
func (s *Sapp) DefaultServer() *http.Server {
	srv := &http.Server{TLSConfig: s.TlsConf}
	handler := s.AclMiddleware(srv.Handler)

	errorLogger := func(m map[string]interface{}) {
		plog.Log("http_request", m)
	}

	infoLogger := func(_ map[string]interface{}) {}
	if s.logAllHttpRequests {
		infoLogger = errorLogger
	}

	handler = loghttp.LogMiddleware(handler, infoLogger, errorLogger)
	srv.Handler = handler
	return srv
}

// Serve HTTP and HTTPS on the same port.
func ServeHTTPAndHTTPS(server *http.Server, l net.Listener) error {
	if server.TLSConfig == nil {
		// If TLS is not configured we run HTTP only.
		return server.Serve(l)
	}
	sl := &splitListener{
		Listener: l,
		config:   server.TLSConfig,
	}

	return server.Serve(sl)
}

// Add an ACL middleware to an http.Handler. The middleware will reject all
// requests which doesn't pass the ACL s.acl.
// This middleware is usually already installed by DefaultServer.
func (s *Sapp) AclMiddleware(h http.Handler) http.Handler {
	if h == nil {
		h = http.DefaultServeMux
	}
	return http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		if !s.Acl.CheckRequest(req) {
			slog.Error("Request URL forbidden by ACL", "url", req.URL)
			http.Error(rw, "Forbidden by ACL", http.StatusForbidden)
			return
		}
		h.ServeHTTP(rw, req)
	})
}

// Set up default http handlers. health is optional.
// health checks if the application is healthy, returns false if it isn't.
func (s *Sapp) DefaultHandlers(srv *http.Server, health func() bool) {
	http.HandleFunc("/healthcheck", func(w http.ResponseWriter, r *http.Request) {
		if health != nil && !health() {
			http.Error(w, "healthcheck failed", 503)
		}
	})
	listenSocketClosed := make(chan struct{})
	once := sync.Once{}
	srv.RegisterOnShutdown(func() {
		once.Do(func() { close(listenSocketClosed) })
	})
	shutdownOnce := sync.Once{}
	http.HandleFunc("/stop", func(w http.ResponseWriter, r *http.Request) {
		// Initiate a shutdown. http.Serve will return with
		// http.ErrServerClosed immediately after srv.Shutdown has been
		// called. WaitForShutdown should be called by clients
		// afterwards to make sure a graceful shutdown is done.
		//
		// This handler will return before the shutdown is complete.
		go shutdownOnce.Do(func() {
			srv.Shutdown(context.Background())
			close(s.shutdownComplete)
		})
		// Wait until listening socket has been closed before returning.
		<-listenSocketClosed
	})
}

// Wait until shutdown is complete. To ensure a graceful shutdown this function
// should be called after Server.Serve has returned http.ErrServerClosed.
func (s *Sapp) WaitForShutdown() {
	<-s.shutdownComplete
}

func (s *Sapp) findConfig() string {
	if *s.confFile != "" {
		return *s.confFile
	}
	bdir, hasbd := os.LookupEnv("BDIR")
	if !hasbd {
		// XXX switch to /opt/schibsted ?
		bdir = "/opt/blocket"
	}
	confpath := filepath.Join(bdir, "conf/tls.conf")
	if _, err := os.Stat(confpath); err == nil {
		return confpath
	}
	return ""
}

func getStrPtr(bconf bconf.Bconf) *string {
	if bconf == nil || !bconf.Leaf() {
		return nil
	} else {
		s := bconf.String("")
		return &s
	}
}

func initAclFromBconf(ac *acl.Acl, bconf bconf.Bconf) {
	slog.Info("Adding ACL from bconf")
	ac.Acl = nil
	for _, b := range bconf.Slice() {
		var entry acl.Access

		if m := getStrPtr(b.Get("method")); m != nil {
			entry.Method = *m
		} else {
			slog.Error("Method not found in ACL, skipping")
			continue
		}

		if p := getStrPtr(b.Get("path")); p != nil {
			entry.Path = *p
		} else {
			slog.Error("Path not found in ACL, skipping")
			continue
		}

		entry.RemoteAddr = getStrPtr(b.Get("remote_addr"))
		entry.CommonName = getStrPtr(b.Get("cert.cn"))
		entry.Issuer = getStrPtr(b.Get("issuer.cn"))
		entry.Action = b.Get("action").String("deny")
		ac.Acl = append(ac.Acl, entry)
	}
}

// Initialize the application. Sets up plog, bconf and an fd_pool connected to service discovery.
func (s *Sapp) Init(app ...string) error {
	s.Bconf = bconfNewFunc()
	if confpath := s.findConfig(); confpath != "" {
		if err := bconf.ReadFile(s.Bconf, confpath, true); err != nil {
			return err
		}
	}

	if *s.logLevel == "" {
		*s.logLevel = s.Bconf.Get("loglevel").String("info")
	}
	s.logAllHttpRequests = s.Bconf.Get("log_all_http_requests").Bool(true)

	appl := strings.Join(app, "+")
	s.app = strings.Join(app, "/")
	plog.Setup(appl, *s.logLevel)

	if err := s.initTls(); err != nil {
		return err
	}

	s.healthCheckClient = &http.Client{Transport: &http.Transport{TLSClientConfig: s.TlsClientConf}}

	if *s.etcdURL != "" {
		s.Bconf.Addv([]string{"sd", "registry", "url"}, *s.etcdURL)
		s.Bconf.Addv([]string{"sd", "etcd_url"}, *s.etcdURL)
	}

	if s.UseCPool {
		if setupCgoPoolFunc == nil {
			return fmt.Errorf("Can't use C pool because Cgo is not enabled.")
		}
		setupCgoPoolFunc(s, appl)
	} else {
		s.sdr = &sdr.Registry{
			Host: s.Bconf.Get("blocket_id").String(""),
			Appl: appl,
			TLS:  s.TlsConf,
		}
		s.Pool = fd_pool.NewGoPool(s.sdr)
		s.sdr.AddSources(s.Bconf)
	}

	if s.Bconf.Get("dialdomain").Valid() {
		s.Pool.SetDialDomain(s.Bconf.Get("dialdomain").String(""))
	}
	if aclBconf := s.Bconf.Get("acl"); aclBconf.Valid() {
		initAclFromBconf(&s.Acl, aclBconf)
	} else {
		s.Acl.InitDefault()
	}
	s.shutdownComplete = make(chan struct{})
	return nil
}

var bconfNewFunc = func() bconf.MutBconf {
	return &bconf.Node{}
}
var initTlsCgoFunc func(s *Sapp) error
var setupCgoPoolFunc func(s *Sapp, appl string)

func (s *Sapp) initTls() error {
	if initTlsCgoFunc != nil {
		if err := initTlsCgoFunc(s); err != nil {
			return err
		}
	}
	var cadata []byte
	var err error
	if s.httpsStateCgo.cafile != "" {
		cadata, err = ioutil.ReadFile(s.httpsStateCgo.cafile)
	} else if cmdstr := s.Bconf.Get("cacert.command").String(""); cmdstr != "" {
		cadata, err = exec.Command("sh", "-c", cmdstr).Output()
	} else if path := s.Bconf.Get("cacert.path").String(""); path != "" {
		cadata, err = ioutil.ReadFile(path)
	}
	if err != nil {
		return err
	}
	if cadata != nil {
		capool := x509.NewCertPool()
		if !capool.AppendCertsFromPEM(cadata) {
			return fmt.Errorf("No CA certs found in ca command/file")
		}
		if s.TlsConf == nil {
			s.TlsConf = &tls.Config{}
		}
		s.TlsConf.ClientAuth = tls.VerifyClientCertIfGiven
		s.TlsConf.RootCAs = capool
		s.TlsConf.ClientCAs = capool
		if s.TlsClientConf == nil {
			s.TlsClientConf = &tls.Config{}
		}
		s.TlsClientConf.RootCAs = capool
	}
	var certdata []byte
	if s.httpsStateCgo.certfile != "" {
		certdata, err = ioutil.ReadFile(s.httpsStateCgo.certfile)
	} else if cmdstr := s.Bconf.Get("cert.command").String(""); cmdstr != "" {
		certdata, err = exec.Command("sh", "-c", cmdstr).Output()
	} else if path := s.Bconf.Get("cert.path").String(""); path != "" {
		certdata, err = ioutil.ReadFile(path)
	}
	if err != nil {
		return err
	}
	if certdata != nil {
		cert, err := tls.X509KeyPair(certdata, certdata)
		if err != nil {
			return err
		}
		if s.TlsConf == nil {
			s.TlsConf = &tls.Config{}
		}
		s.TlsConf.Certificates = append(s.TlsConf.Certificates, cert)
		if s.TlsClientConf == nil {
			s.TlsClientConf = &tls.Config{}
		}
		s.TlsClientConf.Certificates = append(s.TlsClientConf.Certificates, cert)
	}
	return nil
}

func (s *Sapp) Ready() error {
	sock, ok := os.LookupEnv("NOTIFY_SOCKET")
	if ok {
		conn, err := net.Dial("unixgram", sock)
		if err != nil {
			return err
		}
		_, err = io.WriteString(conn, "READY=1")
		if err != nil {
			return err
		}
		conn.Close()
	}
	return nil
}

func (s *Sapp) Close() error {
	if s.sdCtl != nil {
		close(s.sdCtl)
		<-s.sdClosed
		s.sdCtl = nil
	}
	s.Pool.Close()
	s.sdr.Close()
	plog.Shutdown()
	return nil
}
