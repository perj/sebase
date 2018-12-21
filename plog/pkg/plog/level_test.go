// Copyright 2018 Schibsted

package plog

import "testing"

func TestLogLevel(t *testing.T) {
	tests := []struct {
		lvl      string
		def, ret Level
	}{
		{"CRIT", Info, Critical},
		{"Critical", Info, Critical},
		{"crit", Info, Critical},
		{"asdf", Info, Info},
		{"asdf", Warning, Warning},
	}
	for _, tst := range tests {
		r := LogLevel(tst.lvl, tst.def)
		if r != tst.ret {
			t.Errorf("LogLevel(%s, %v) returned %v, expected %v", tst.lvl, tst.def, r, tst.ret)
		}
	}
}

func TestLevels(t *testing.T) {
	for _, lvl := range []Level{Emergency, Alert, Critical, Error, Warning, Notice, Info, Debug} {
		if LevelByName[lvl.String()] != lvl {
			t.Errorf("String() failed to lookup, got %v", lvl.String())
		}
		if LevelByName[lvl.Code()] != lvl {
			t.Errorf("Code() failed to lookup, got %v", lvl.Code())
		}
		if LevelByName[lvl.Lower()] != lvl {
			t.Errorf("Lower() failed to lookup, got %v", lvl.Lower())
		}
	}
}
