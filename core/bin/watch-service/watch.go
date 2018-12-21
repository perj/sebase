// Copyright 2018 Schibsted

package main

import (
	"context"
	"log"
	"strings"
	"time"

	"github.com/coreos/etcd/client"
)

func watchEtcd(watchers map[string]client.Watcher) {
	type event struct {
		srv  string
		resp *client.Response
	}
	evch := make(chan event)
	for srv, w := range watchers {
		go func(srv string, w client.Watcher) {
			for {
				resp, err := w.Next(context.Background())
				if err != nil {
					log.Print(err)
					time.Sleep(1 * time.Second)
				} else {
					evch <- event{srv, resp}
				}
			}
		}(srv, w)
	}

	for {
		ev := <-evch
		resp := ev.resp
		data := make(map[string]interface{})
		path := strings.Split(strings.TrimPrefix(resp.Node.Key, "/service/"+ev.srv+"/"), "/")
		key := path[len(path)-1]
		if resp.Node.Value != "" && resp.Action != "delete" {
			data["new"] = nodeValue(resp.Node.Value, key)
		}
		doprint := true
		switch {
		case resp.Action != "update" && resp.Action != "set":
			break
		case resp.PrevNode != nil && resp.Node.Value == resp.PrevNode.Value:
			delete(data, "new")
			data["unchanged"] = true
			doprint = watchall
		case resp.PrevNode != nil:
			data["old"] = nodeValue(resp.PrevNode.Value, key)
		}
		if doprint {
			envelope := map[string]interface{}{
				resp.Action: map[string]interface{}{
					ev.srv: map[string]interface{}{
						path[0]: data,
					},
				},
			}
			output(envelope)
		}
	}
}

func nodeValue(str, key string) interface{} {
	switch key {
	case "config":
		v := make(map[string]interface{})
		addConf(v, str, false)
		return v
	case "health":
		v := make(map[string]interface{})
		addCheckFiltered(v, "health", str, false)
		return v
	default:
		return str
	}
}
