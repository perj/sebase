// Copyright 2018 Schibsted

package main

import (
	"context"
	"log"
	"path"
	"reflect"
	"strings"
	"time"

	"github.com/schibsted/sebase/core/internal/pkg/etcdlight"
)

func watchEtcd(watchers map[string]etcdlight.Watcher) {
	type event struct {
		srv string
		r   *etcdlight.Response
	}
	evch := make(chan event)
	for srv, w := range watchers {
		go func(srv string, w etcdlight.Watcher) {
			for {
				r, err := w.Next(context.Background())
				if err != nil {
					log.Print(err)
					time.Sleep(1 * time.Second)
				} else {
					evch <- event{srv, r}
				}
			}
		}(srv, w)
	}

	for {
		ev := <-evch
		resp := ev.r
		data := make(map[string]interface{})
		newkvs := nodeValues(resp.Values, "/service/"+ev.srv)
		if len(newkvs) > 0 {
			data["new"] = newkvs
		}
		doprint := true
		switch {
		case resp.Action != "update" && resp.Action != "set":
			break
		case len(resp.PrevValues) > 0 && reflect.DeepEqual(resp.PrevValues, resp.Values):
			delete(data, "new")
			data["unchanged"] = true
			doprint = watchall
		case len(resp.PrevValues) > 0:
			data["old"] = nodeValues(resp.PrevValues, "/service/"+ev.srv)
		}
		if doprint {
			envelope := map[string]interface{}{
				resp.Action: map[string]interface{}{
					ev.srv: data,
				},
			}
			output(envelope)
		}
	}
}

func nodeValues(kvs []etcdlight.KV, key string) (vs []interface{}) {
	for _, kv := range kvs {
		if !strings.HasPrefix(kv.Key, key) {
			continue
		}
		switch path.Base(kv.Key) {
		case "config":
			v := make(map[string]interface{})
			addConf(v, kv.Value, false)
			vs = append(vs, v)
		case "health":
			v := make(map[string]interface{})
			addCheckFiltered(v, "health", kv.Value, false)
			vs = append(vs, v)
		default:
			vs = append(vs, kv.Value)
		}
	}
	return
}
