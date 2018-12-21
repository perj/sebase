// Copyright 2018 Schibsted

// +build cgo

// Package vtree is an interface for C compatible vtrees used in sebase.
//
// The vtrees in this package can be passed to C functions that accept those.
// By necessity they deal a lot with C pointers, thus you must make sure to
// call Close on them when you're done using them to release the C resources.
//
// Often you will use bconf.BConfNode.Vtree to create these rather than the
// functions in this package.
package vtree

//#include "sbp/vtree.h"
//#include "sbp/vtree_literal.h"
//static void loop_cleanup(struct vtree_loop_var *loop) { if (loop->cleanup) loop->cleanup(loop); }
//static void keyvals_cleanup(struct vtree_keyvals *kv) { if (kv->cleanup) kv->cleanup(kv); }
//static void wrap_vtree_free(struct vtree_chain *vt) { vtree_free(vt); }
import "C"
import (
	"sort"
	"strings"
	"unsafe"
)

// A vtree node you can use to call C functions that accept them.
// When calling to C, you will have to use CPtr() and a cast:
//	(*C.struct_vtree_chain)(vt.CPtr())
// where vt is a *Vtree.
type Vtree C.struct_vtree_chain

func vtreeBuildArglist(before []string, after []string, loop bool) (C.int, **C.char, []*C.char) {
	ret := make([]*C.char, len(before)+1+len(after))
	i := 0
	for _, s := range before {
		ret[i] = C.CString(s)
		i++
	}
	if !loop {
		if i == 0 {
			return 0, nil, nil
		}
		return C.int(i), &ret[0], ret
	}
	ret[i] = (*C.char)(unsafe.Pointer(^uintptr(0))) //C.VTREE_LOOP
	i++
	for _, s := range after {
		ret[i] = C.CString(s)
		i++
	}
	return C.int(i), &ret[0], ret
}

func freeArglist(args []*C.char) {
	for _, str := range args {
		ptr := unsafe.Pointer(str)
		if uintptr(ptr) != ^uintptr(0) {
			C.free(ptr)
		}
	}
}

// The number of nodes at the path, which can be 0 length.
// A path for a value has length 0, otherwise it's the number
// of subnodes. Non-existing paths also return 0.
func (vt *Vtree) Length(key ...string) int {
	argc, argv, args := vtreeBuildArglist(key, nil, false)
	defer freeArglist(args)
	return int(C.vtree_getlen_cachev((*C.struct_vtree_chain)(vt), nil, nil, argc, argv))
}

// Get the string value at the path.
// Empty string will be returned if a value can't be found at this specific node.
func (vt *Vtree) Get(key ...string) string {
	argc, argv, args := vtreeBuildArglist(key, nil, false)
	defer freeArglist(args)
	return C.GoString(C.vtree_get_cachev((*C.struct_vtree_chain)(vt), nil, nil, argc, argv))
}

// Get the integer value at the path, which can be 0.
// Defaults to returning 0 if the path is not a leaf node.
func (vt *Vtree) GetInt(key ...string) int {
	argc, argv, args := vtreeBuildArglist(key, nil, false)
	defer freeArglist(args)
	return int(C.vtree_getint_cachev((*C.struct_vtree_chain)(vt), nil, nil, argc, argv))
}

// Returns true if the path is a valid node, otherwise false.
func (vt *Vtree) HasKey(key ...string) bool {
	argc, argv, args := vtreeBuildArglist(key, nil, false)
	defer freeArglist(args)
	return C.vtree_haskey_cachev((*C.struct_vtree_chain)(vt), nil, nil, argc, argv) != 0
}

// Return a subnode. You can use this for leaf node paths, even though
// they have 0 length.
func (vt *Vtree) GetNode(key ...string) *Vtree {
	argc, argv, args := vtreeBuildArglist(key, nil, false)
	defer freeArglist(args)
	node := new(C.struct_vtree_chain)
	return (*Vtree)(C.vtree_getnode_cachev((*C.struct_vtree_chain)(vt), nil, node, nil, argc, argv))
}

func stringList(loop *C.struct_vtree_loop_var) []string {
	if loop.len == 0 {
		return nil
	}
	ret := make([]string, loop.len)
	// https://github.com/golang/go/wiki/cgo#turning-c-arrays-into-go-slices
	list := (*(**[1 << 40]*C.char)(unsafe.Pointer(&loop.l)))[:loop.len:loop.len]
	for i, ptr := range list {
		ret[i] = C.GoString(ptr)
	}
	C.loop_cleanup(loop)
	return ret
}

// Fetch the subnode keys for the node at path, if any.
// Will return nil rather than an empty slice.
func (vt *Vtree) Keys(key ...string) []string {
	argc, argv, args := vtreeBuildArglist(key, nil, false)
	defer freeArglist(args)
	var loop C.struct_vtree_loop_var
	C.vtree_fetch_keys_cachev((*C.struct_vtree_chain)(vt), &loop, nil, argc, argv)
	return stringList(&loop)
}

// Fetch the values at the given path which is split in a before and
// after loop point. Will always return a list with the number of nodes
// at the before path, but each value will be that of the combination of
// before and after.
// Uses empty string if the after path does not exist at a given node.
// Will return nil rather than an empty slice.
func (vt *Vtree) Values(before []string, after []string) []string {
	argc, argv, args := vtreeBuildArglist(before, after, true)
	defer freeArglist(args)
	var loop C.struct_vtree_loop_var
	C.vtree_fetch_values_cachev((*C.struct_vtree_chain)(vt), &loop, nil, argc, argv)
	return stringList(&loop)
}

// Fetch the subnodes at the given path, if any.
// Will return nil rather than an empty slice.
func (vt *Vtree) Nodes(key ...string) []*Vtree {
	argc, argv, args := vtreeBuildArglist(key, nil, false)
	defer freeArglist(args)
	var loop C.struct_vtree_loop_var
	C.vtree_fetch_nodes_cachev((*C.struct_vtree_chain)(vt), &loop, nil, argc, argv)
	if loop.len == 0 {
		return nil
	}
	ret := make([]*Vtree, loop.len)
	// https://github.com/golang/go/wiki/cgo#turning-c-arrays-into-go-slices
	list := (*(**[1 << 40]C.struct_vtree_chain)(unsafe.Pointer(&loop.l)))[:loop.len:loop.len]
	for i := range list {
		node := list[i]
		ret[i] = (*Vtree)(&node)
	}
	C.loop_cleanup(&loop)
	return ret
}

// Fetch the keys at the before path, but filter on the value at after, and only
// return those nodes matching the given value.
// Will return nil rather than an empty slice.
func (vt *Vtree) KeysByValue(before []string, after []string, value string) []string {
	argc, argv, args := vtreeBuildArglist(before, after, true)
	defer freeArglist(args)
	var loop C.struct_vtree_loop_var
	v := C.CString(value)
	defer C.free(unsafe.Pointer(v))
	C.vtree_fetch_keys_by_value_cachev((*C.struct_vtree_chain)(vt), &loop, nil, v, argc, argv)
	return stringList(&loop)
}

// Wrapper for C.vtree_free
func (vt *Vtree) Close() {
	C.wrap_vtree_free((*C.struct_vtree_chain)(vt))
}

// Casts vt. It's still a Go pointer and you have to obey the rules for those.
func (vt *Vtree) CPtr() unsafe.Pointer {
	return unsafe.Pointer(vt)
}

type VktType C.enum_vtree_keyvals_type

const (
	// Unknown type of node, you will have to inspect the keys
	// to determine if it's a list of a dictionary.
	VktUnknown VktType = C.vktUnknown
	VktDict            = C.vktDict
	VktList            = C.vktList
)

// A type that can be iterated on. This is an ordered list of vtree keys and
// values, although keys are unset in case the list type is VktList.
//
// While normally extracted from a Vtree you can also build these manually.
// Then you can convert them into a vtree.  Often bconf is a nicer interface
// however.
type VtreeKeyValue C.struct_vtree_keyvals

// Will be either a string, a *Vtree or nil.
// It's possible that a key exists but has a nil value, e.g. if the after path
// points to a non-existing node.
type VtreeValue interface{}

// Fetch both keys and values at the given paths (keys at before, values at before + after).
// Commonly used to serialize a vtree, when both before and after are nil.
// The returned structure must be closed when no longer used.
func (vt *Vtree) KeysAndValues(before []string, after []string) *VtreeKeyValue {
	argc, argv, args := vtreeBuildArglist(before, after, true)
	defer freeArglist(args)
	var loop VtreeKeyValue
	C.vtree_fetch_keys_and_values_cachev((*C.struct_vtree_chain)(vt), (*C.struct_vtree_keyvals)(&loop), nil, argc, argv)
	return &loop
}

// Type of list. Not used by bconf which always return VktUnknown.
func (loop *VtreeKeyValue) Type() VktType {
	return VktType(loop._type)
}

// The list of keys for subnodes. Returns nil on VktList node types.
func (loop *VtreeKeyValue) Keys() []string {
	if loop._type == VktList {
		return nil
	}
	ret := make([]string, loop.len)
	// https://github.com/golang/go/wiki/cgo#turning-c-arrays-into-go-slices
	list := (*[1 << 40]C.struct_vtree_keyvals_elem)(unsafe.Pointer(loop.list))[:loop.len:loop.len]
	for i := range list {
		ret[i] = C.GoString(list[i].key)
	}
	return ret
}

// The list of values for subnodes.
func (loop *VtreeKeyValue) Values() []VtreeValue {
	ret := make([]VtreeValue, loop.len)
	// https://github.com/golang/go/wiki/cgo#turning-c-arrays-into-go-slices
	list := (*[1 << 40]C.struct_vtree_keyvals_elem)(unsafe.Pointer(loop.list))[:loop.len:loop.len]
	for i := range list {
		ret[i] = newVtreeValue(&list[i])
	}
	return ret
}

// Number of elements in list.
func (loop *VtreeKeyValue) Len() int {
	return int(loop.len)
}

// An iterator to access elements in a loop or similar.
type VkvElement struct {
	slice []C.struct_vtree_keyvals_elem
	idx   int
}

// Access element key, returns empty string for VktList node types.
func (elem *VkvElement) Key() string {
	return C.GoString(elem.slice[elem.idx].key)
}

// Access element value, which is a string, a *Vtree or nil.
func (elem *VkvElement) Value() VtreeValue {
	return newVtreeValue(&elem.slice[elem.idx])
}

// Access an element by index. Returns nil if out of bounds.
func (loop *VtreeKeyValue) Index(idx int) *VkvElement {
	if idx < 0 || idx >= int(loop.len) {
		return nil
	}
	// https://github.com/golang/go/wiki/cgo#turning-c-arrays-into-go-slices
	list := (*[1 << 40]C.struct_vtree_keyvals_elem)(unsafe.Pointer(loop.list))[:loop.len:loop.len]
	return &VkvElement{list, idx}
}

// Update elem to point to another element in the list.
// Returns elem if in range, otherwise returns nil.
func (elem *VkvElement) Step(steps int) *VkvElement {
	elem.idx += steps
	if elem.idx < 0 || elem.idx >= len(elem.slice) {
		return nil
	}
	return elem
}

// A convenience map conversion. Vtree nodes are normally ordered, the
// returned map will not keep the ordering. Will return nil for
// VktList node types.
func (loop *VtreeKeyValue) Map() map[string]VtreeValue {
	if loop._type == VktList {
		return nil
	}
	ret := make(map[string]VtreeValue, loop.len)
	// https://github.com/golang/go/wiki/cgo#turning-c-arrays-into-go-slices
	list := (*[1 << 40]C.struct_vtree_keyvals_elem)(unsafe.Pointer(loop.list))[:loop.len:loop.len]
	for i := range list {
		ret[C.GoString(list[i].key)] = newVtreeValue(&list[i])
	}
	return ret
}

// Access the underlying array. An example use could be to reorder it.
// You have to cast the pointer to a C.struct_vtree_keyvals_elem slice of the given length.
func (loop *VtreeKeyValue) CList() (unsafe.Pointer, int) {
	return unsafe.Pointer(loop.list), int(loop.len)
}

// Free up resources.
func (loop *VtreeKeyValue) Close() error {
	C.keyvals_cleanup((*C.struct_vtree_keyvals)(loop))
	return nil
}

func newVtreeValue(elem *C.struct_vtree_keyvals_elem) VtreeValue {
	switch elem._type {
	case C.vkvNone:
		return nil
	case C.vkvValue:
		return C.GoString(*(**C.char)(unsafe.Pointer(&elem.v)))
	case C.vkvNode:
		return (*Vtree)(unsafe.Pointer(&elem.v))
	}
	return nil
}

// Convert to Vtree. You must Close the resulting Vtree to free its memory.
// Closing the vtree will also Close the receiver.
func (loop *VtreeKeyValue) Vtree() *Vtree {
	// Convert to C pointer.
	cloop := (*C.struct_vtree_keyvals)(C.malloc(C.size_t(unsafe.Sizeof(*loop))))
	*cloop = *(*C.struct_vtree_keyvals)(loop)

	// Convert to Go pointer.
	ptr := C.vtree_literal_create(cloop)
	vt := new(Vtree)
	*vt = *(*Vtree)(ptr)
	C.free(unsafe.Pointer(ptr))
	return vt
}

// Build a VtreeKeyValue from key value pairs. keys will be ignored if you're creating
// a list. Otherwise keys and values have to be the same length, or the function panics.
// Will also panic if a value is not nil, a string or a *Vtree.
// The returned VtreeKeyValue must be closed to release resources,
// and will call Close any Vtrees in the values when that is done.
func VtreeBuildKeyvals(vktype VktType, keys []string, values []VtreeValue) *VtreeKeyValue {
	var kvs *C.struct_vtree_keyvals
	switch vktype {
	case VktList:
		kvs = C.vtree_keyvals_create_list(C.int(len(values)))
	default:
		if len(values) != len(keys) {
			panic("len(values) != len(keys)")
		}
		kvs = C.vtree_keyvals_create_dict(C.int(len(values)))
		if vktype == VktUnknown {
			kvs._type = C.vktUnknown
		}
	}

	// https://github.com/golang/go/wiki/cgo#turning-c-arrays-into-go-slices
	slice := (*[1 << 40]C.struct_vtree_keyvals_elem)(unsafe.Pointer(kvs.list))[:len(values):len(values)]
	for i, v := range values {
		if vktype != VktList {
			slice[i].key = C.CString(keys[i])
		}
		switch v := v.(type) {
		case nil:
			slice[i]._type = C.vkvNone
		case string:
			slice[i]._type = C.vkvValue
			*(**C.char)(unsafe.Pointer(&slice[i].v)) = C.CString(v)
		case *Vtree:
			slice[i]._type = C.vkvNode
			*(*C.struct_vtree_chain)(unsafe.Pointer(&slice[i].v)) = *(*C.struct_vtree_chain)(v)
		default:
			panic("Value is not nil, string or Vtree")
		}
	}
	// We want a Go pointer, to match what the Close function does
	ret := new(VtreeKeyValue)
	*ret = *(*VtreeKeyValue)(kvs)
	C.free(unsafe.Pointer(kvs))
	return ret
}

// Local bconf key compare since we don't want to import bconf due to
// import cycle.
func bconfKeyCompare(a, b string) int {
	la := len(a)
	lb := len(b)
	if la == 0 || lb == 0 || (a[0] >= '0' && a[0] <= '9' && b[0] >= '0' && b[0] <= '9') {
		if la < lb {
			return -1
		}
		if la > lb {
			return 1
		}
	}
	return strings.Compare(a, b)
}

type mapsorter struct {
	keys   []string
	values []VtreeValue
}

func (s *mapsorter) Len() int { return len(s.keys) }
func (s *mapsorter) Swap(i, j int) {
	s.keys[i], s.keys[j] = s.keys[j], s.keys[i]
	s.values[i], s.values[j] = s.values[j], s.values[i]
}
func (s *mapsorter) Less(i, j int) bool { return bconfKeyCompare(s.keys[i], s.keys[j]) < 0 }

// Sorts the map using the bconf sort order, then calls VtreeBuildKeyvals.
func VtreeBuildKeyvalsMap(m map[string]VtreeValue) *VtreeKeyValue {
	keys := make([]string, len(m))
	values := make([]VtreeValue, len(m))
	i := 0
	for k, v := range m {
		keys[i] = k
		values[i] = v
		i++
	}
	sort.Sort(&mapsorter{keys, values})
	return VtreeBuildKeyvals(VktDict, keys, values)
}
