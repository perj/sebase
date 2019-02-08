// Copyright 2018 Schibsted

// +build cgo,sebase_cgo

package bconf

//#include "sbp/bconf.h"
//#include "sbp/vtree.h"
//#include "sbp/vtree_literal.h"
//static void loop_cleanup(struct vtree_loop_var *loop) { if (loop->cleanup) loop->cleanup(loop); }
//static void keyvals_cleanup(struct vtree_keyvals *kv) { if (kv->cleanup) kv->cleanup(kv); }
//static void wrap_vtree_free(struct vtree_chain *vt) { vtree_free(vt); }
import "C"
import (
	"unsafe"

	"github.com/schibsted/sebase/vtree/pkg/vtree"
)

// Return a vtree referencing this bconf node.
// This is often what you will use to create vtrees, as Go puts
// quite heavy restrictions on what can be passed to C.
//
// Vtrees returned by this function have a no-op Close function as they're
// just a reference to the bconf node.
func (b CBconf) Vtree() *vtree.Vtree {
	var vt vtree.Vtree
	C.bconf_vtree((*C.struct_vtree_chain)(unsafe.Pointer(&vt)), b.n)
	return &vt
}
