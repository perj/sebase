// Copyright 2018 Schibsted

package bconf

import (
	"strconv"
	"strings"
)

// A leaf bconf node, with key and string value
type Leaf struct {
	KeyName string
	Value   string
}

func (leaf *Leaf) Get(k ...string) Bconf {
	if len(k) == 0 {
		return leaf
	}
	return (*Leaf)(nil)
}

func (leaf *Leaf) Valid() bool {
	return leaf != nil
}

func (leaf *Leaf) Leaf() bool {
	return true
}

func (leaf *Leaf) String(def string) string {
	if leaf == nil {
		return def
	}
	return leaf.Value
}

func (leaf *Leaf) Int(def int) int {
	if leaf == nil {
		return def
	}
	n, _ := strconv.Atoi(leaf.Value)
	return n
}

func (leaf *Leaf) Bool(def bool) bool {
	n := 0
	if def {
		n = 1
	}
	return leaf.Int(n) != 0
}

func (leaf *Leaf) Slice() []Bconf {
	return nil
}

func (leaf *Leaf) ToMap() map[string]interface{} {
	return nil
}

func (leaf *Leaf) Length() int {
	return 0
}

func (leaf *Leaf) Key() string {
	if leaf == nil {
		return ""
	}
	return leaf.KeyName
}

// A non-leaf bconf node with key name and slice of subnodes.
// The subnodes need to be kept in a consistent state and are thus
// kept private.
// Use Add or Addv to add data, and Get, Slice or ToMap to read it.
//
// Root nodes use an empty string key, subnodes have it set.
type Node struct {
	KeyName string

	subnodes []Bconf
	starIdx  int
}

func (node *Node) Search(step string) (found bool, index int) {
	start := 0
	end := len(node.subnodes)
	for end > start {
		i := start + (end-start)/2
		cmp := KeyCompare(step, node.subnodes[i].Key())
		switch {
		case cmp == 0:
			return true, i
		case cmp < 0:
			end = i
		default:
			start = i + 1
		}
	}
	return false, start
}

func (node *Node) Get(k ...string) Bconf {
	if node == nil || len(k) == 0 {
		return node
	}
	ks := k
	if strings.ContainsRune(k[0], '.') {
		ks = append(strings.Split(k[0], "."), k[1:]...)
	}
	if found, idx := node.Search(ks[0]); found {
		return node.subnodes[idx].Get(ks[1:]...)
	} else if node.starIdx > 0 {
		// starIdx is 1-based to keep the 0-value as N/A.
		return node.subnodes[node.starIdx-1].Get(ks[1:]...)
	}
	return (*Node)(nil)
}

func (node *Node) Valid() bool {
	return node != nil
}

func (node *Node) Leaf() bool {
	return false
}

func (node *Node) String(def string) string {
	return def
}

func (node *Node) Int(def int) int {
	return def
}

func (node *Node) Bool(def bool) bool {
	return def
}

func (node *Node) Slice() []Bconf {
	if node == nil {
		return nil
	}
	return node.subnodes
}

// Convert to a go map. This will traverse the node recursively
// and insert all leaves into a map. Values are either strings or
// map[string]interface{} sub nodes.
func (node *Node) ToMap() map[string]interface{} {
	if node == nil {
		return nil
	}
	m := make(map[string]interface{}, len(node.subnodes))
	for _, n := range node.subnodes {
		if n.Leaf() {
			m[n.Key()] = n.String("")
		} else {
			m[n.Key()] = n.ToMap()
		}
	}
	return m
}

func (node *Node) Length() int {
	if node == nil {
		return 0
	}
	return len(node.subnodes)
}

func (node *Node) Key() string {
	if node == nil {
		return ""
	}
	return node.KeyName
}

// Add data to a bconf node
//
// The only possible error is an AddError, the Key field will
// be the key split on dots.
//
// Only leaf nodes can be created, not calling the returned function
// might create an inconsistent state.
//
// Example: err := b.Add("foo.bar", id, "name")("baz")
func (node *Node) Add(k ...string) func(v string) error {
	var ks []string
	for _, kv := range k {
		ks = append(ks, strings.Split(kv, ".")...)
	}
	return node.addvf(ks)
}

// Add data to a bconf node, using a slice.
//
// Note: . (dots) are NOT special in this function call, if passed the node
// key created will contain the dot, and can't be retrieved using Get, only
// Slice and ToMap.
func (node *Node) Addv(kv []string, v string) error {
	return node.addvf(kv)(v)
}

func (node *Node) addvf(kv []string) func(v string) error {
	errf := func(v string) error { return &AddError{kv, v} }
	if len(kv) == 0 {
		return errf
	}
	// To avoid modifying kv used in errf.
	ks := kv
	for ; len(ks) > 1; ks = ks[1:] {
		found, idx := node.Search(ks[0])
		if found {
			var ok bool
			node, ok = node.subnodes[idx].(*Node)
			if !ok {
				return errf
			}
		} else {
			newnode := &Node{KeyName: ks[0]}
			node.insert(idx, ks[0], newnode)
			node = newnode
		}
	}
	found, idx := node.Search(ks[0])
	var leaf *Leaf
	if found {
		var ok bool
		leaf, ok = node.subnodes[idx].(*Leaf)
		if !ok {
			return errf
		}
	} else {
		leaf = &Leaf{KeyName: ks[0]}
		node.insert(idx, ks[0], leaf)
	}
	return func(v string) error {
		leaf.Value = v
		return nil
	}
}

func (node *Node) insert(idx int, key string, v Bconf) {
	node.subnodes = append(node.subnodes, nil)
	copy(node.subnodes[idx+1:], node.subnodes[idx:])
	node.subnodes[idx] = v
	if node.starIdx > idx {
		node.starIdx++
	} else if key == "*" {
		// starIdx is 1-based to keep the 0-value as N/A.
		node.starIdx = idx + 1
	}
}

func (node *Node) Delete(path ...string) {
	if len(path) == 0 {
		return
	}
	node, ok := node.Get(path[:len(path)-1]...).(*Node)
	if !ok || node == nil {
		return
	}
	found, idx := node.Search(path[len(path)-1])
	if !found {
		return
	}
	copy(node.subnodes[idx:], node.subnodes[idx+1:])
	node.subnodes = node.subnodes[:len(node.subnodes)-1]
}
