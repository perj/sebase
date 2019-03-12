// Copyright 2018 Schibsted

package fd_pool

import (
	"context"
	"encoding/json"
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
	conf := &bconf.Node{}
	initial := true
	for {
		msg, ok := <-srv.sdConn.Channel()
		if !ok {
			break
		}
		switch {
		case msg.Type == sdr.EndOfBatch:
			slog.Info("fd-pool: SD updating service", "service", srvname)
			l, _ := p.UpdateHosts(context.Background(), srvname, conf)
			if initial && l > 0 {
				close(srv.sdConnInitial)
				initial = false
			}
		case msg.Type == sdr.Update && msg.Key == "config":
			parseConfig(conf, msg.HostKey, p.sdReg.Host, p.sdReg.Appl, msg.Value)
		case msg.Type == sdr.Update && msg.Key == "health":
			parseHealth(conf, msg.HostKey, msg.Value)
		case msg.Type == sdr.Delete:
			if msg.Key != "" {
				conf.Delete("host", msg.HostKey, msg.Key)
			} else {
				conf.Delete("host", msg.HostKey)
			}
		case msg.Type == sdr.Flush:
			conf = &bconf.Node{}
		}
	}
}

// Parse a json encoded host node config, with host and appl prefix.
// Keys are on the form *.*.x (but json encoded, not bconf), and the
// valid keys are those for a single node as given in common.go.
func parseConfig(dst *bconf.Node, hostkey, host, appl, src string) {
	// XXX possibly the last value could be a dictionary/array?
	var data map[string]map[string]map[string]string
	err := json.Unmarshal([]byte(src), &data)
	if err != nil {
		slog.Error("Error parsing SD config", "error", err)
		return
	}
	// Always add the disabled key, but don't touch a pre-set value.
	disabled := dst.Get("host", hostkey, "disabled").String("1")
	dst.Delete("host", hostkey)
	for _, kk := range [][2]string{{"*", "*"}, {"*", appl}, {host, "*"}, {host, appl}} {
		if data[kk[0]] != nil && data[kk[0]][kk[1]] != nil {
			for k, v := range data[kk[0]][kk[1]] {
				dst.Add("host", hostkey, k)(v)
			}
		}
	}
	if disabled != "" {
		dst.Add("host", hostkey, "disabled")(disabled)
	}
}

// Parse the health value into a disabled conf key. The value should be
// either "up" or "down".
func parseHealth(dst *bconf.Node, hostkey, src string) {
	v := "1"
	src = strings.TrimSpace(src)
	slog.Debug("New health for host", "hostkey", hostkey, "value", src)
	if src == "up" {
		v = "0"
	}
	dst.Add("host", hostkey, "disabled")(v)
}
