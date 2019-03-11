// Copyright 2018 Schibsted

package fd_pool

import (
	"context"
	"encoding/json"
	"strconv"
	"strings"
	"time"

	"github.com/schibsted/sebase/core/pkg/sd/sdr"
	"github.com/schibsted/sebase/util/pkg/slog"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

func (p *GoPool) connectSd(ctx context.Context, srvname string, srv *fdService, conf bconf.Bconf) error {
	var serr error
	srv.sdConn, serr = p.sdReg.ConnectSource(ctx, srvname, conf)
	if srv.sdConn != nil {
		srv.sdConnInitial = make(chan struct{})
		go p.readSd(srvname, srv)
	}
	return serr
}

func initialWait(ctx context.Context, srv *fdService, d time.Duration) {
	// Check first
	select {
	case <-srv.sdConnInitial:
		return
	case <-ctx.Done():
		return
	default:
	}
	// Now wait.
	select {
	case <-srv.sdConnInitial:
	case <-time.After(d):
	case <-ctx.Done():
	}
}

func (p *GoPool) readSd(srvname string, srv *fdService) {
	hosts := make(map[string]*HostConfig)
	initial := true
	for {
		msg, ok := <-srv.sdConn.Channel()
		if !ok {
			break
		}
		switch {
		case msg.Type == sdr.EndOfBatch:
			slog.Info("fd-pool: SD updating service", "service", srvname)
			l, _ := p.UpdateHostsConfig(context.Background(), srvname, hosts)
			if initial && l > 0 {
				close(srv.sdConnInitial)
				initial = false
			}
		case msg.Type == sdr.Update && msg.Key == "config":
			parseConfig(hosts, msg.HostKey, p.sdReg.Host, p.sdReg.Appl, msg.Value)
		case msg.Type == sdr.Update && msg.Key == "health":
			parseHealth(hosts, msg.HostKey, msg.Value)
		case msg.Type == sdr.Delete:
			if host := hosts[msg.HostKey]; host != nil {
				switch msg.Key {
				case "":
					delete(hosts, msg.HostKey)
				case "name":
					host.Name = ""
				case "cost":
					host.Cost = 0
				case "health":
					host.Disabled = true
				default:
					delete(host.Ports, msg.Key)
				}
			}
		case msg.Type == sdr.Flush:
			hosts = make(map[string]*HostConfig)
		}
	}
}

// Parse a json encoded host node config, with host and appl prefix.
// Keys are on the form *.*.x (but json encoded, not bconf), and the
// valid keys are those for a single node as given in common.go.
func parseConfig(hosts map[string]*HostConfig, hostkey, host, appl, src string) {
	// XXX possibly the last value could be a dictionary/array?
	var data map[string]map[string]map[string]string
	err := json.Unmarshal([]byte(src), &data)
	if err != nil {
		slog.Error("Error parsing SD config", "error", err)
		return
	}
	node := hosts[hostkey]
	if node == nil {
		// Default disabled to true, but keep any previous value.
		node = &HostConfig{Disabled: true}
		hosts[hostkey] = node
	}
	node.Ports = make(map[string]string)
	node.Cost = 0
	node.Name = ""
	for _, kk := range [][2]string{{"*", "*"}, {"*", appl}, {host, "*"}, {host, appl}} {
		if data[kk[0]] != nil && data[kk[0]][kk[1]] != nil {
			for k, v := range data[kk[0]][kk[1]] {
				switch k {
				case "name":
					node.Name = v
				case "cost":
					node.Cost, _ = strconv.Atoi(v)
				default:
					node.Ports[k] = v
				}
			}
		}
	}
}

// Parse the health value into a disabled conf key. The value should be
// either "up" or "down".
func parseHealth(hosts map[string]*HostConfig, hostkey, src string) {
	v := true
	src = strings.TrimSpace(src)
	slog.Debug("New health for host", "hostkey", hostkey, "value", src)
	if src == "up" {
		v = false
	}
	node := hosts[hostkey]
	if node == nil {
		node = &HostConfig{}
		hosts[hostkey] = node
	}
	node.Disabled = v
}
