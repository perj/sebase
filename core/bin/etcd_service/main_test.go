// Copyright 2018 Schibsted

package main

import (
	"io/ioutil"
	"os"
	"testing"
	"time"
)

func TestHostkeyFromFile(t *testing.T) {
	path := "gotest.hostkeyFromFile"
	os.Remove(path)
	ch := make(chan string)
	go func() {
		ch <- hostkeyFromFile(path)
	}()
	select {
	case <-ch:
		t.Fatal("Unexpected early response from hostkeyFromFile")
	case <-time.After(100 * time.Millisecond):
	}

	f, _ := os.Create(path)
	select {
	case <-ch:
		t.Fatal("Unexpected early response from hostkeyFromFile")
	case <-time.After(600 * time.Millisecond):
	}

	f.Write([]byte("test\n"))

	var s string
	select {
	case s = <-ch:
	case <-time.After(600 * time.Millisecond):
		t.Fatal("No response from hostkeyFromFile")
	}

	if s != "test" {
		t.Fatalf("Unexpected reply from hostkeyFromFile: %s != test", s)
	}
	os.Remove(path)
}

func TestHostkeyGenerate(t *testing.T) {
	tdir, err := ioutil.TempDir("", "etcd_service-tests")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tdir)

	hk := hostkeyGenerateFile(tdir + "/hostkey")
	if len(hk) != 8+1+4+1+4+1+4+1+12 {
		t.Error("bad uuid ", hk)
	}

	hk2 := hostkeyGenerateFile(tdir + "/hostkey")
	if hk2 != hk {
		t.Error("mismatch ", hk2, " != ", hk)
	}
}
