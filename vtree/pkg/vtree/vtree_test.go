// Copyright 2018 Schibsted

// +build cgo

package vtree

import (
	"reflect"
	"testing"
)

func TestVtreeLength(t *testing.T) {
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "1",
		"bar": "2",
	}).Vtree()
	defer vt.Close()
	v := vt.Length()
	if v != 2 {
		t.Errorf("VtreeLength returned %v, expected 2\n", v)
	}
	v = vt.Length("foo")
	if v != 0 {
		t.Errorf("VtreeLength returned %v, expected 0\n", v)
	}
}

func TestVtreeGet(t *testing.T) {
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "bar",
	}).Vtree()
	defer vt.Close()
	v := vt.Get("foo")
	if v != "bar" {
		t.Errorf("VtreeGet returned %v, expected \"bar\"\n", v)
	}
	v = vt.Get("baz")
	if v != "" {
		t.Errorf("VtreeGet returned %v, expected empty string\n", v)
	}
}

func TestVtreeGetint(t *testing.T) {
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "42",
	}).Vtree()
	defer vt.Close()
	i := vt.GetInt("foo")
	if i != 42 {
		t.Errorf("VtreeGetint returned %v, expected 42\n", i)
	}
	i = vt.GetInt("baz")
	if i != 0 {
		t.Errorf("VtreeGetint returned %v, expected 0\n", i)
	}
}

func TestVtreeHasKey(t *testing.T) {
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "42",
	}).Vtree()
	defer vt.Close()
	v := vt.HasKey("foo")
	if !v {
		t.Errorf("VtreeHasKey returned %v, expected true\n", v)
	}
	v = vt.HasKey("baz")
	if v {
		t.Errorf("VtreeHasKey returned %v, expected false\n", v)
	}
}

func TestVtreeGetNode(t *testing.T) {
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "42",
	}).Vtree()
	defer vt.Close()
	n := vt.GetNode("foo")
	if n == nil {
		t.Errorf("VtreeHasKey returned %v, expected non-nil\n", n)
	}
	v := n.GetInt()
	if v != 42 {
		t.Errorf("VtreeGetint returned %v, expected 42\n", v)
	}
	n = vt.GetNode("baz")
	if n != nil {
		t.Errorf("VtreeHasKey returned %v, expected nil\n", n)
	}
}

func TestVtreeKeys(t *testing.T) {
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "1",
		"bar": "2",
	}).Vtree()
	defer vt.Close()
	keys := vt.Keys()
	exp := []string{"bar", "foo"}
	if !reflect.DeepEqual(keys, exp) {
		t.Errorf("VtreeKeys returned %v, expected %v\n", keys, exp)
	}
	keys = vt.Keys("foo")
	if keys != nil {
		t.Errorf("VtreeKeys returned %v, expected nil\n", keys)
	}
}

func TestVtreeValues(t *testing.T) {
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "1",
		"bar": "2",
	}).Vtree()
	defer vt.Close()
	vs := vt.Values(nil, nil)
	exp := []string{"2", "1"}
	if !reflect.DeepEqual(vs, exp) {
		t.Errorf("VtreeValues returned %v, expected %v\n", vs, exp)
	}
	vs = vt.Values(nil, []string{"baz"})
	exp = []string{"", ""}
	if !reflect.DeepEqual(vs, exp) {
		t.Errorf("VtreeValues returned %v, expected %v\n", vs, exp)
	}
	vs = vt.Values([]string{"baz"}, nil)
	exp = nil
	if !reflect.DeepEqual(vs, exp) {
		t.Errorf("VtreeValues returned %v, expected %v\n", vs, exp)
	}
}

func TestVtreeNodes(t *testing.T) {
	foovt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"1": "2",
		"2": "3",
	}).Vtree()
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "1",
		"bar": foovt,
	}).Vtree()
	defer vt.Close()
	ns := vt.Nodes("foo")
	if len(ns) != 0 {
		t.Errorf("VtreeValues len %v, expected %v\n", len(ns), 0)
	}
	ns = vt.Nodes("bar")
	if len(ns) != 2 {
		t.Errorf("VtreeValues len %v, expected %v\n", len(ns), 2)
	}
	i := ns[1].GetInt()
	if i != 3 {
		t.Errorf("VtreeGetInt returned %v, expected %v\n", i, 3)
	}
}

func TestVtreeKeysByValue(t *testing.T) {
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "1",
		"bar": "2",
		"baz": "1",
	}).Vtree()
	defer vt.Close()
	vs := vt.KeysByValue(nil, nil, "1")
	exp := []string{"baz", "foo"}
	if !reflect.DeepEqual(vs, exp) {
		t.Errorf("VtreeKeysByValue returned %v, expected %v\n", vs, exp)
	}
}

func TestVtreeKeysAndValues(t *testing.T) {
	bazvt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"1": "3",
		"2": "4",
	}).Vtree()
	vt := VtreeBuildKeyvalsMap(map[string]VtreeValue{
		"foo": "1",
		"bar": "2",
		"baz": bazvt,
	}).Vtree()
	defer vt.Close()
	kvs := vt.KeysAndValues(nil, nil)
	defer kvs.Close()
	if kvs.Type() != VktDict {
		t.Errorf("Type() returned %v, expected VktDict\n", kvs.Type())
	}
	ks := kvs.Keys()
	exp := []string{"bar", "baz", "foo"}
	if !reflect.DeepEqual(ks, exp) {
		t.Errorf("Keys() returned %v, expected %v\n", ks, exp)
	}

	vs := kvs.Values()
	if len(vs) != 3 {
		t.Errorf("Values() returned len %v, expected %v\n", len(vs), 3)
	}
	if vs[0] != "2" || vs[2] != "1" {
		t.Errorf("Values() returned [0] = %v, [2] = %v, expected 2, 1", vs[0], vs[2])
	}
	i := vs[1].(*Vtree).GetInt("1")
	if i != 3 {
		t.Errorf("VtreeGetInt returned %v, expected 3\n", i)
	}

	m := kvs.Map()
	if len(m) != 3 {
		t.Errorf("Map() returned len %v, expected %v\n", len(m), 3)
	}
	if m["foo"] != "1" || m["bar"] != "2" {
		t.Errorf("Values() returned [\"foo\"] = %v, [\"bar\"] = %v, expected 1, 2", m["foo"], m["bar"])
	}
	i = m["baz"].(*Vtree).GetInt("2")
	if i != 4 {
		t.Errorf("VtreeGetInt returned %v, expected 4\n", i)
	}
}

func TestVtreeBuilding(t *testing.T) {
	m := map[string]VtreeValue{
		"1": "foo",
		"2": "bar",
		"3": "baz",
	}
	kvs := VtreeBuildKeyvalsMap(m)
	ks := kvs.Keys()
	exp := []string{"1", "2", "3"}
	if !reflect.DeepEqual(ks, exp) {
		t.Errorf("Keys() returned %v, expected %v\n", ks, exp)
	}

	vs := kvs.Values()
	if len(vs) != 3 {
		t.Errorf("Values() returned len %v, expected %v\n", len(vs), 3)
	}
	if vs[0] != "foo" || vs[1] != "bar" || vs[2] != "baz" {
		t.Errorf("Values() returned [0] = %v, [1] = %v, [2] = %v, expected foo, bar, baz", vs[0], vs[1], vs[2])
	}

	vt := kvs.Vtree()

	v := vt.Get("1")
	if v != "foo" {
		t.Errorf("VtreeGet returned %v, expected foo", v)
	}

	m = map[string]VtreeValue{
		"foo": "1",
		"bar": nil,
		"baz": vt,
	}
	kvs = VtreeBuildKeyvalsMap(m)
	ks = kvs.Keys()
	exp = []string{"bar", "baz", "foo"}
	if !reflect.DeepEqual(ks, exp) {
		t.Errorf("Keys() returned %v, expected %v\n", ks, exp)
	}

	vt = kvs.Vtree()
	v = vt.Get("baz", "2")
	if v != "bar" {
		t.Errorf("VtreeGet returned %v, expected bar", v)
	}

	vt.Close()
}

func TestVkvElement(t *testing.T) {
	ks := []string{"x", "z", "w"}
	vs := []VtreeValue{"1", "2", "3"}
	kvs := VtreeBuildKeyvals(VktDict, ks, vs)

	eks := ks
	evs := vs
	for elem := kvs.Index(0); elem != nil; elem = elem.Step(1) {
		if elem.Key() != eks[0] {
			t.Errorf("Wrong element key, expected %v, got %v", eks[0], elem.Key())
		}
		if elem.Value() != evs[0] {
			t.Errorf("Wrong element value, expected %v, got %v", evs[0], elem.Value())
		}
		eks = eks[1:]
		evs = evs[1:]
	}
	if len(eks) > 0 {
		t.Errorf("Didn't deplete eks: %v", eks)
	}
}
