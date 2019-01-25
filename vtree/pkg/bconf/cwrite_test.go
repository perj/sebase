// Copyright 2018 Schibsted

// +build cgo,sebase_cgo

package bconf

import "testing"

func TestAdd(t *testing.T) {
	b := NewCBconf()
	defer b.Free()
	err := b.Add("a.b", "c")("3")
	if err != nil {
		t.Error(err)
	}
	i := b.Get("a.b.c").Int(0)
	if i != 3 {
		t.Errorf("%v != 3", i)
	}
}

func TestAddv(t *testing.T) {
	b := NewCBconf()
	defer b.Free()
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
	// ToMap is currently the only way to see the key. Slice will also get the value.
	m := b.ToMap()
	s := m["a.b"].(map[string]interface{})["c"].(string)
	if s != "5" {
		t.Errorf("%v != 5", s)
	}
}

func TestConflict(t *testing.T) {
	b := NewCBconf()
	defer b.Free()
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
	if aerr.Key.(string) != "a.b.c" {
		t.Errorf("%v != \"a.b.c\"", aerr.Key)
	}
	if aerr.Value != "2" {
		t.Errorf("%v != \"2\"", aerr.Value)
	}
}
