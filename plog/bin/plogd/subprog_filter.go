// Copyright 2018 Schibsted

package main

import (
	"strings"

	"github.com/schibsted/sebase/plog/pkg/plogd"
	"github.com/schibsted/sebase/util/pkg/slog"
)

type subprogFilter struct {
	plogd.OutputWriter
	subprog []string
}

func (f *subprogFilter) WriteMessage(logmsg slog.Logger, msg plogd.LogMessage) {
	progs := strings.SplitN(msg.Prog, "+", len(f.subprog)+1)
	if len(progs) > 1 {
		msg.Prog = progs[0]
		if msg.KV == nil {
			msg.KV = make(map[string]interface{})
		}
		for i := range f.subprog {
			if i+1 >= len(progs) {
				break
			}
			msg.KV[f.subprog[i]] = progs[i+1]
		}
	}
	f.OutputWriter.WriteMessage(logmsg, msg)
}
