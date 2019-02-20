// Copyright 2018 Schibsted

package plog

import (
	"bytes"
	"log"
	"os"
	"strings"
	"testing"
)

var testStderr = new(bytes.Buffer)

func init() {
	FallbackWriter = testStderr
	FallbackFormatter = FallbackFormatterSimple

	// Make sure to fail to connect.
	os.Setenv("PLOG_SOCKET", "/")
	Setup("test", "info")
}

func TestLogPrint(t *testing.T) {
	testStderr.Reset()
	log.Print("huh")
	if !strings.Contains(testStderr.String(), "INFO:") {
		t.Error("INFO wasn't logged to stderr")
	}
	if !strings.Contains(testStderr.String(), "huh") {
		t.Error("huh wasn't logged to stderr")
	}
}

func TestLevelPrint(t *testing.T) {
	testStderr.Reset()
	Warning.Print("huh")
	if !strings.Contains(testStderr.String(), "WARNING:") {
		t.Error("WARNING wasn't logged to stderr")
	}
	if !strings.Contains(testStderr.String(), "huh") {
		t.Error("huh wasn't logged to stderr")
	}
}

func TestFallbackJsonWrap(t *testing.T) {
	testStderr.Reset()
	FallbackFormatter = FallbackFormatterJsonWrap
	defer func() {
		FallbackFormatter = FallbackFormatterSimple
	}()
	Error.Print("fallback\n")
	scan := `"type":"ERR","key":["test"],"message":"fallback"}`
	if !strings.Contains(testStderr.String(), scan) {
		t.Errorf(`%s wasn't logged to stderr. Have: %s`, scan, testStderr.String())
	}
}
