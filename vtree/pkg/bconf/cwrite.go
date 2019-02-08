// Copyright 2018 Schibsted

// +build cgo,sebase_cgo

package bconf

import (
	"strings"
	"unsafe"
)

//#include "sbp/bconf.h"
//#include <stdlib.h>
import "C"

func (b *CBconf) Add(k ...string) func(v string) error {
	return func(v string) error {
		jk := strings.Join(k, ".")
		ck := C.CString(jk)
		defer C.free(unsafe.Pointer(ck))
		cv := C.CString(v)
		defer C.free(unsafe.Pointer(cv))
		r := C.bconf_add_data_canfail(&b.n, ck, cv)
		if r != -1 {
			return nil
		}
		return &AddError{jk, v}
	}
}

func (b *CBconf) Addv(kv []string, v string) error {
	ckv := make([]*C.char, len(kv))
	for i, k := range kv {
		ck := C.CString(k)
		defer C.free(unsafe.Pointer(ck))
		ckv[i] = ck
	}
	cv := C.CString(v)
	r := C.bconf_add_datav_canfail(&b.n, C.int(len(ckv)), &ckv[0], cv, C.BCONF_OWN)
	if r != -1 {
		return nil
	}
	C.free(unsafe.Pointer(cv))
	return &AddError{kv, v}
}
