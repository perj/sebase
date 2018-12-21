// Copyright 2018 Schibsted

// +build cgo,!sebase_nocgo

package sapp

import (
	"unsafe"

	"github.com/schibsted/sebase/core/pkg/fd_pool"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

func init() {
	setupCgoPoolFunc = (*Sapp).setupCPool
	bconfNewFunc = func() bconf.MutBconf {
		return bconf.NewCBconf()
	}
}

func (s *Sapp) setupCPool(appl string) {
	csdr := fd_pool.NewCSdr(s.Bconf.Get("blocket_id").String(""), appl, s.httpsStateCgo.https)
	s.Pool = fd_pool.NewCPool(csdr)
	csdr.AddSources(s.Bconf.(*bconf.CBconf).Vtree())
	s.csdr = unsafe.Pointer(csdr)
}
