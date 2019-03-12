// Copyright 2018 Schibsted

package sapp

import (
	"context"
	"crypto/rand"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"time"

	"github.com/coreos/etcd/client"
	"github.com/schibsted/sebase/util/pkg/slog"
)

func registerService(kapi client.KeysAPI, ttl time.Duration, service, hostkey, health, prevHealth, config, prevConfig string) {
	dir := service + "/" + hostkey

	newdir := false
	_, err := kapi.Set(context.Background(), dir, "", &client.SetOptions{Dir: true, Refresh: true, TTL: ttl, PrevExist: "true"})
	if err != nil && client.IsKeyNotFound(err) {
		_, err = kapi.Set(context.Background(), dir, "", &client.SetOptions{Dir: true, TTL: ttl})
		newdir = true
	}
	if err != nil {
		slog.Error("Error updating directory", "error", err)
		newdir = true
	}

	setopts := &client.SetOptions{}
	if !newdir && config == prevConfig {
		setopts.PrevExist = "false"
	}
	_, err = kapi.Set(context.Background(), dir+"/config", string(config), setopts)
	if err != nil {
		if cerr, ok := err.(client.Error); ok && cerr.Code == client.ErrorCodeNodeExist {
			// Means the prevExist check failed, which is ok.
		} else {
			slog.Error("Error setting config in etcd", "error", err)
		}
	}

	if health == "" {
		// Don't register empty string, which is only possible for static clients.
		return
	}

	newval := newdir || health != prevHealth
	if !newval {
		resp, _ := kapi.Get(context.Background(), dir+"/health", nil)
		newval = resp == nil || resp.Node == nil || resp.Node.Value != health
	}

	if newval {
		_, err = kapi.Set(context.Background(), dir+"/health", health, nil)
		if err != nil {
			slog.Error("Error setting health in etcd", "error", err)
		}
	}
}

func unregisterService(kapi client.KeysAPI, service, hostkey string) {

	dir := service + "/" + hostkey

	_, err := kapi.Delete(context.Background(), dir, &client.DeleteOptions{Recursive: true, Dir: true})
	if err != nil {
		slog.Error("Error deleting service in etcd", "error", err)
	}
}

func hostkeyRandom() string {
	var id [16]byte
	n, err := rand.Read(id[:])
	if err != nil {
		slog.Critical("Failed to generate random hostkey", "error", err)
		panic("Failed to generate random hostkey")
	}
	if n != len(id) {
		panic("Short read without error")
	}
	id[6] = (id[6] & 0x0F) | 0x40
	id[8] = (id[8] & 0x3F) | 0x80
	return fmt.Sprintf("%x-%x-%x-%x-%x", id[0:4], id[4:6], id[6:8], id[8:10], id[10:])
}

func (s *Sapp) hostkey() string {
	return hostkeyRandom() // for now.
}

func (s *Sapp) healthCheck() (health string) {
	// XXX - should be overridable from config
	url := s.healthCheckEndpoint
	if url == "" {
		slog.Info("No healthcheck url configured")
		return "down"
	}

	resp, err := s.healthCheckClient.Get(url)
	if err != nil {
		slog.Warning("Error checking health, assuming service is down. ", "error", err)
		return "down"
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 200 && resp.StatusCode <= 299 {
		return "up"
	}

	return "down"
}

type sdCtl struct {
	portName  string
	portValue string
	useHttps  bool
}

var SDNotConfigured = errors.New("No SD registry has been configured.")

// SDRegister starts the process of registering to service discovery.
// Returns SDNotConfigured if no SD is available.
//
// No actual data is added to service discovery until an actual port is
// added through SDListener and that listener socket starts answering
// healthcheck queries.
//
// There is currently no way to indicate which port answers the healthcheck
// queries, the first one registered will be used.
func (s *Sapp) SDRegister() error {
	if !s.Bconf.Get("sd.registry.url").Valid() {
		return SDNotConfigured
	}
	url := s.Bconf.Get("sd.registry.url").String("http://localhost:2379")
	tport := *client.DefaultTransport.(*http.Transport)
	tport.TLSClientConfig = s.TlsConf
	cfg := client.Config{
		Endpoints: []string{url},
		Transport: &tport,
	}
	cl, err := client.New(cfg)
	if err != nil {
		return err
	}

	kapi := client.NewKeysAPI(cl)
	service := "/service/" + s.app
	hostkey := s.hostkey()
	hostname := "127.0.0.1" // XXX

	// XXX - this is to allow us to configure a couple of ports
	// before actually starting serving requests so that things
	// don't deadlock on healthchecks before serving.
	s.sdCtl = make(chan sdCtl, 5)
	s.sdClosed = make(chan struct{})

	values := make(map[string]interface{})
	star := make(map[string]interface{})
	starstar := make(map[string]interface{})
	star["*"] = starstar
	values["*"] = star
	starstar["cost"] = "1000"
	starstar["name"] = hostname

	go func() {
		prevConfig := ""
		prevHealth := ""
		stop := false
		for !stop {
			config, err := json.Marshal(&values)
			if err != nil {
				panic(err)
			}

			health := s.healthCheck()
			registerService(kapi, 30*time.Second, service, hostkey, health, prevHealth, string(config), prevConfig)
			prevConfig = string(config)
			prevHealth = health
			select {
			case ctl := <-s.sdCtl:
				if ctl.portName == "" {
					stop = true
				} else if ctl.portValue == "" {
					delete(starstar, ctl.portName)
				} else {
					starstar[ctl.portName] = ctl.portValue

					// This is dodgy, we should be able to designate a healthcheck port.
					// Or maybe, we need to healthcheck all endpoints?
					if s.healthCheckEndpoint == "" {
						proto := "http"
						if ctl.useHttps {
							proto = "https"
						}
						s.healthCheckEndpoint = fmt.Sprintf("%s://%s:%s/healthcheck", proto, hostname, ctl.portValue)
					}

				}
			case <-time.After(10 * time.Second):
			}
		}
		unregisterService(kapi, service, hostkey)
		close(s.sdClosed)
	}()
	return nil
}

// SDPort registers a port number in service discovery. This shouldn't
// be used unless there are no other options.
func (s *Sapp) SDPort(n, v string, useHttps bool) {
	s.sdCtl <- sdCtl{n, v, useHttps}
}

// SDListener registers this net.Listener in service discovery.
func (s *Sapp) SDListener(n string, l net.Listener, useHttps bool) {
	if s.sdCtl == nil {
		return
	}
	_, p, err := net.SplitHostPort(l.Addr().String())
	if err != nil {
		panic(err)
	}
	s.sdCtl <- sdCtl{n, p, useHttps}
}
