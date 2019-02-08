// Copyright 2018 Schibsted

// +build cgo,sebase_cgo

package bconf

// #include "sbp/json_vtree.h"
// #include "sbp/bconf.h"
// #include "sbp/bconfig.h"
// #include <stdlib.h>
import "C"
import (
	"fmt"
	"strconv"
	"unsafe"
)

// The C bconf node, implementing the interface and several
// extra functions via C calls.
type CBconf struct {
	n *C.struct_bconf_node
}

// Returns an empty bconf we can work with.
// If this is a root node you should call Free when done.
// Often you write `defer b.Free()` directly after `b := bconf.NewCBconf()`.
func NewCBconf() *CBconf {
	return &CBconf{}
}

// Get pointer to the underlying struct bconf_node. Useful when integrating with
// C code.
func (b *CBconf) GetCPointer() unsafe.Pointer {
	return unsafe.Pointer(b.n)
}

// Initialize from JSON.
func (b *CBconf) InitFromJson(js []byte) error {
	var bc *C.struct_bconf_node
	r := C.json_bconf(&bc, nil, (*C.char)(unsafe.Pointer(&js[0])), C.ssize_t(len(js)), 0)
	if r != 0 {
		estr := []C.char{'e', 'r', 'r', 'o', 'r', '\000'}
		return fmt.Errorf("Failed to parse JSON: %s", C.GoString(C.bconf_get_string(bc, &estr[0])))
	}

	b.n = bc
	return nil
}

// Initialize the base config from a config file.
//
// Consider using ReadFile instead.
func (b *CBconf) InitFromFile(filename string) error {
	fn := C.CString(filename)
	defer C.free(unsafe.Pointer(fn))
	var err error
	b.n, err = C.config_init(fn)
	if b.n == nil {
		return err
	}
	return nil
}

// Initialize from a struct bconf_node C pointer.
func (b *CBconf) InitFromCPointer(ptr unsafe.Pointer) {
	b.n = (*C.struct_bconf_node)(ptr)
}

// Loads a bconf from a proper bconf file.
// (this usually requires a pre-loaded config so that we can get the blocket_id)
func (b *CBconf) LoadBconfFile(appl, filename string) error {
	cappl := C.CString(appl)
	defer C.free(unsafe.Pointer(cappl))
	fn := C.CString(filename)
	defer C.free(unsafe.Pointer(fn))
	if e, err := C.load_bconf_file(cappl, &b.n, fn); e != 0 {
		return err
	}
	return nil
}

// Free a bconf node. It's only valid to free the root node.
func (b *CBconf) Free() {
	C.bconf_free(&b.n)
}

// Get a subnode
func (b CBconf) Get(k ...string) Bconf {
	n := b.n
	// We can't pass vargargs into C, we need to iterate ourselves
	for _, v := range k {
		s := C.CString(v)
		n = C.bconf_get(n, s)
		C.free(unsafe.Pointer(s))
	}
	return CBconf{n}
}

// Is this a valid node
func (b CBconf) Valid() bool {
	return b.n != nil
}

// Is this a leaf node that has a value
func (b CBconf) Leaf() bool {
	return C.bconf_value(b.n) != nil
}

// Get the string value for the node, or the default value.
// Only leaf nodes have values.
func (b CBconf) String(def string) string {
	v := C.bconf_value(b.n)
	if v == nil {
		return def
	}
	return C.GoString(v)
}

// Get the int value for the node, or the default value.
// Only leaf nodes have values.
func (b CBconf) Int(def int) int {
	v := C.bconf_value(b.n)
	if v == nil {
		return def
	}
	i, _ := strconv.Atoi(C.GoString(v))
	return i
}

// Get the int value for the node, or the default value.
// Only leaf nodes have values.
func (b CBconf) Bool(def bool) bool {
	v := C.bconf_value(b.n)
	if v == nil {
		return def
	}
	i, _ := strconv.Atoi(C.GoString(v))
	return i != 0
}

// Get a slice with all sub nodes. Meant to be use with range operator.
func (b CBconf) Slice() []Bconf {
	n := C.bconf_count(b.n)
	ret := make([]Bconf, n)
	for i := C.int(0); i < n; i++ {
		ret[i] = CBconf{C.bconf_byindex(b.n, i)}
	}
	return ret
}

// Gets a bconf string from the bconf.
func (b CBconf) GetString(k ...string) (string, bool) {
	b = b.Get(k...).(CBconf)
	v := C.bconf_value(b.n)
	if v == nil {
		return "", false
	}
	return C.GoString(v), true
}

// Convert to a go map. This will traverse the node recursively
// and insert all leaves into a map. Values are either strings or
// map[string]interface{} sub nodes.
//
// Returns nil on leaf nodes.
func (b CBconf) ToMap() map[string]interface{} {
	if b.Leaf() {
		return nil
	}
	n := C.bconf_count(b.n)
	ret := make(map[string]interface{}, n)
	for i := C.int(0); i < n; i++ {
		node := C.bconf_byindex(b.n, i)
		k := C.GoString(C.bconf_key(node))
		v := C.bconf_value(node)
		if v != nil {
			ret[k] = C.GoString(v)
		} else {
			ret[k] = CBconf{node}.ToMap()
		}
	}
	return ret
}

// Get the number of sub nodes.
func (b CBconf) Length() int {
	return int(C.bconf_count(b.n))
}

// Get key
func (b CBconf) Key() string {
	return C.GoString(C.bconf_key(b.n))
}
