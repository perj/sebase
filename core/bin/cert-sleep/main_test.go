// Copyright 2018 Schibsted

package main

import (
	"testing"
	"time"
)

func TestSubExitEarly(t *testing.T) {
	min := time.Duration(time.Minute)
	max := time.Duration(time.Hour)
	now := time.Now()
	var mintt, maxtt time.Time
	for i := 0; i < 10000; i++ {
		tt := SubExitEarly(now, min, max)
		if tt.Before(now.Add(-max)) || tt.After(now.Add(-min)) {
			t.Error("tt not in range", tt)
		}
		if mintt.IsZero() || tt.Before(mintt) {
			mintt = tt
		}
		if maxtt.IsZero() || tt.After(maxtt) {
			maxtt = tt
		}
	}
	t.Log("Allowed range", now.Add(-max), now.Add(-min))
	t.Log("tt range", mintt, maxtt)
}
