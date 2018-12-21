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

	// Make sure to fail to connect.
	os.Setenv("PLOG_SOCKET", "/")
	Setup("test", "info")
}

func TestLogPrint(t *testing.T) {
	log.Print("huh")
	if !strings.Contains(testStderr.String(), "INFO:") {
		t.Error("INFO wasn't logged to stderr")
	}
	if !strings.Contains(testStderr.String(), "huh") {
		t.Error("huh wasn't logged to stderr")
	}
}

func TestLevelPrint(t *testing.T) {
	Warning.Print("huh")
	if !strings.Contains(testStderr.String(), "WARNING:") {
		t.Error("WARNING wasn't logged to stderr")
	}
	if !strings.Contains(testStderr.String(), "huh") {
		t.Error("huh wasn't logged to stderr")
	}
}
