// Copyright 2018 Schibsted

package main

import "testing"

type expectMsg struct {
	key   string
	value interface{}
}

type countTester struct {
	tb                        testing.TB
	closeProper, closeLastRef bool
	expect                    []expectMsg
}

func (ct *countTester) OpenDict(key string) (SessionOutput, error) {
	panic("N/A")
}

func (ct *countTester) OpenList(key string, dicts bool) (SessionOutput, error) {
	panic("N/A")
}

func (ct *countTester) Write(key string, value interface{}) error {
	if len(ct.expect) == 0 {
		ct.tb.Errorf("Expected no more write, got {%v, %v}", key, value)
		return nil
	}
	exp := ct.expect[0]
	if key != exp.key || value != exp.value {
		ct.tb.Errorf("Expected {%v, %v} got {%v, %v}", exp.key, exp.value, key, value)
	}
	ct.expect = ct.expect[1:]
	return nil
}

func (ct *countTester) Close(proper, lastRef bool) {
	if proper != ct.closeProper {
		ct.tb.Errorf("Close proper expected %v, got %v", ct.closeProper, proper)
	}
	if lastRef != ct.closeLastRef {
		ct.tb.Errorf("Close lastRef expected %v, got %v", ct.closeLastRef, lastRef)
	}
	if len(ct.expect) != 0 {
		ct.tb.Errorf("Expected more writes: %+v", ct.expect)
	}
}

func (ct *countTester) ConfKey() string {
	return ""
}

func TestCounterWrite(t *testing.T) {
	ct := countTester{t, true, true, []expectMsg{
		{"a", "b"},
		{"q", int(1)},
		{"q", int(-1)},
	}}
	co := NewCountOutput(&ct)
	co.Write("a", "b")
	co.Write("q", float64(1))
	co.Write("q", float64(-1))
	co.Close(true, true)
}

func TestCounterDelete(t *testing.T) {
	ct := countTester{t, true, true, []expectMsg{
		{"a", "b"},
		{"q", int(1)},
		{"q", int(-1)},
	}}
	co := NewCountOutput(&ct)
	co.Write("a", "b")
	co.Write("q", float64(1))
	co.Write("q", nil)
	co.Close(true, true)
}
