// Copyright 2018 Schibsted

// +build cgo,sebase_cgo

package sapp

//#include "sbp/http.h"
//#include <stdlib.h>
import "C"

import (
	"unsafe"
)

func init() {
	initTlsCgoFunc = (*Sapp).initTlsCgo
}

func (s *Sapp) initTlsCgo() error {
	// Need C pointer.
	s.httpsStateCgo.https = C.calloc(1, C.sizeof_struct_https_state)
	httpsState := (*C.struct_https_state)(s.httpsStateCgo.https)

	// Ok, annoying. But http.h is in baseutil, not basecore, that's why it
	// doesn't take a bconf node.
	keys := [4]string{"cacert.command", "cacert.path", "cert.command", "cert.path"}
	args := [4]*C.char{}
	for i, k := range keys {
		v := s.Bconf.Get(k).String("")
		if v != "" {
			args[i] = C.CString(v)
			defer C.free(unsafe.Pointer(args[i]))
		}
	}
	r, err := C.http_setup_https(httpsState, args[0], args[1], args[2], args[3])
	if r <= 0 {
		// r == 0 => no https, can return here.
		return err
	}
	s.httpsStateCgo.cafile = C.GoString(&httpsState.cafile[0])
	s.httpsStateCgo.certfile = C.GoString(&httpsState.certfile[0])
	return nil
}
