// Copyright 2018 Schibsted

package bconf

import (
	"reflect"
	"testing"
)

func TestBasic(t *testing.T) {
	bconf := &Node{}
	err := ReadFile(bconf, "tests/bconf.conf", true)
	if err != nil {
		t.Error(err)
	}
	if !reflect.DeepEqual(bconf, &Node{KeyName: "", subnodes: []Bconf{
		&Leaf{"after_include", "foo"},
		&Leaf{"bconf_file", "/opt/blocket/conf/bconf.txt"},
		&Leaf{"blocket_id", "tester"},
		&Leaf{"in_included", "yes"},
		&Node{KeyName: "irrelevant", subnodes: []Bconf{
			&Node{KeyName: "1", subnodes: []Bconf{
				&Leaf{"foo", "bar"},
				&Leaf{"something", "else"},
			}},
		}},
	}}) {
		t.Errorf("bconf.conf didn't match expected contents, got %#v", bconf)
	}
}

func TestNodeAdd(t *testing.T) {
	b := &Node{}
	err := b.Add("a.b", "c")("3")
	if err != nil {
		t.Error(err)
	}
	i := b.Get("a.b.c").Int(0)
	if i != 3 {
		t.Errorf("%v != 3", i)
	}
}

func TestNodeAddv(t *testing.T) {
	b := &Node{}
	err := b.Addv([]string{"a", "b", "c"}, "4")
	if err != nil {
		t.Error(err)
	}
	err = b.Addv([]string{"a.b", "c"}, "5")
	if err != nil {
		t.Error(err)
	}
	i := b.Get("a", "b.c").Int(0)
	if i != 4 {
		t.Errorf("%v != 4", i)
	}
	// Get can't see the key, use ToMap or Slice.
	m := b.ToMap()
	s := m["a.b"].(map[string]interface{})["c"].(string)
	if s != "5" {
		t.Errorf("%v != 5", s)
	}
}

func TestNodeConflict(t *testing.T) {
	b := &Node{}
	err := b.Add("a.b")("1")
	if err != nil {
		t.Error(err)
	}
	err = b.Add("a.b.c")("2")
	if err == nil {
		t.Error("Expected err != nil")
		return
	}
	aerr := err.(*AddError)
	if !reflect.DeepEqual(aerr.Key, []string{"a", "b", "c"}) {
		t.Errorf("%v != \"a.b.c\"", aerr.Key)
	}
	if aerr.Value != "2" {
		t.Errorf("%v != \"2\"", aerr.Value)
	}
}

func TestDelete(t *testing.T) {
	b := &Node{}
	b.Add("a")("1")
	b.Add("b.a")("2")
	b.Add("b.b")("3")
	b.Add("c.a")("4")
	b.Add("c.b")("5")

	b.Delete("b", "b")
	m := b.ToMap()
	e := map[string]interface{}{
		"a": "1",
		"b": map[string]interface{}{
			"a": "2",
		},
		"c": map[string]interface{}{
			"a": "4",
			"b": "5",
		},
	}
	if !reflect.DeepEqual(m, e) {
		t.Fatalf("Failed test 1, got %v", m)
	}
	b.Delete("c")
	m = b.ToMap()
	e = map[string]interface{}{
		"a": "1",
		"b": map[string]interface{}{
			"a": "2",
		},
	}
	b.Delete("nonexisting")
	if !reflect.DeepEqual(m, e) {
		t.Fatalf("Failed test 2, got %v", m)
	}
	b.Delete("a")
	m = b.ToMap()
	e = map[string]interface{}{
		"b": map[string]interface{}{
			"a": "2",
		},
	}
	if !reflect.DeepEqual(m, e) {
		t.Fatalf("Failed test 3, got %v", m)
	}
}
