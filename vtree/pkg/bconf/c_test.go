// Copyright 2018 Schibsted

// +build cgo

package bconf

import (
	"reflect"
	"testing"
)

var expected_map = map[string]interface{}{
	"bconf_file": "/opt/blocket/conf/bconf.txt",
	"blocket_id": "tester",
	"irrelevant": map[string]interface{}{
		"1": map[string]interface{}{
			"foo":       "bar",
			"something": "else",
		},
	},
	"in_included":   "yes",
	"after_include": "foo",
}

func TestBconfBasic(t *testing.T) {
	bconf := NewCBconf()
	err := bconf.InitFromFile("tests/bconf.conf")
	if err != nil {
		t.Error(err)
	}
	actual := bconf.ToMap()
	if !reflect.DeepEqual(actual, expected_map) {
		t.Errorf("Got %v\nExpected %v", actual, expected_map)
	}
}

func TestInitFromJson(t *testing.T) {
	js := []byte(`
{
  "blocket_id": "tester",
  "bconf_file": "/opt/blocket/conf/bconf.txt",
  "irrelevant": {
    "1": {"foo": "bar",
          "something": "else"}
  },
  "in_included": "yes",
  "after_include": "foo"
}
`)
	bconf := NewCBconf()
	err := bconf.InitFromJson(js)
	if err != nil {
		t.Error(err)
	}
	actual := bconf.ToMap()
	if !reflect.DeepEqual(actual, expected_map) {
		t.Errorf("Got %v\nExpected %v", actual, expected_map)
	}
}

func TestInitFromBconf(t *testing.T) {
	expected_bconf := NewCBconf()
	err := expected_bconf.InitFromFile("tests/bconf.conf")
	if err != nil {
		t.Error(err)
	}

	bconf := NewCBconf()
	InitFromBconf(bconf, expected_bconf)
	actual := bconf.ToMap()
	if !reflect.DeepEqual(actual, expected_map) {
		t.Errorf("Got %v\nExpected %v", actual, expected_map)
	}
}
