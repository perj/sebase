// Copyright 2018 Schibsted

package bconf

import (
	"fmt"
	"strings"
)

// The basic bconf interface.
type Bconf interface {
	Get(k ...string) Bconf
	Valid() bool
	Leaf() bool
	String(def string) string
	Int(def int) int
	Bool(def bool) bool
	Slice() []Bconf
	ToMap() map[string]interface{}
	Length() int
	Key() string
}

// Functions used to add data to bconf. Implemented by Node and BconfNode.
type BconfAdder interface {
	// Add data to a bconf node
	//
	// The only possible error is an AddError, Key will either be a
	// string or an []string.
	//
	// You can use this function on sub nodes, not only the root,
	// but be sure the node exists by creating at least one leaf value
	// first.
	//
	// Example: err := b.Add("foo.bar", id, "name")("baz")
	Add(k ...string) func(v string) error

	// Add data to a bconf node, using a slice.
	// Note: . (dots) are NOT special in this function call, if passed the node
	// key created will contain the dot, and can't be retrieved using Get, only
	// Slice and ToMap.
	Addv(kv []string, v string) error
}

// Mutable bconf interface you can use to have write access without knowing the
// underlying type.
type MutBconf interface {
	Bconf
	BconfAdder
}

// Compares the two bconf keys with the bconf sort order.
func KeyCompare(a, b string) int {
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

// Error returned by Add and Addv, indicating there was a tree structure
// conflict.
// Key is either a string or a slice of strings, value is always a string.
type AddError struct {
	Key   interface{}
	Value string
}

func (e *AddError) Error() string {
	return fmt.Sprintf("Failed to add key %v, node/list conflict", e.Key)
}

func addRecursively(b BconfAdder, m map[string]interface{}, path *[]string) {
	for k, v := range m {
		*path = append(*path, k)
		switch value := v.(type) {
		case string:
			b.Addv(*path, value)
		case map[string]interface{}:
			addRecursively(b, value, path)
		default:
			panic(fmt.Sprintf("bconf.addRecursively: Unexpected type %#v", v))
		}
		*path = (*path)[:len(*path)-1]
	}
}

// Initialize the BconfAdder from a Bconf interface.
func InitFromBconf(dst BconfAdder, src Bconf) {
	addRecursively(dst, src.ToMap(), &[]string{})
}
