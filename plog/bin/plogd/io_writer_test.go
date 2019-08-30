package main

import (
	"bufio"
	"bytes"
	"io"
	"testing"
	"time"
)

func TestPeriodicFlush(t *testing.T) {
	jf := &jsonFileWriter{}
	var buf bytes.Buffer
	jf.w = bufio.NewWriter(&buf)
	io.WriteString(jf.w, "test")
	done := make(chan struct{})
	go func() {
		jf.flusherThread()
		close(done)
	}()
	timeout := time.After(5 * time.Second)
	for {
		select {
		case <-timeout:
			t.Fatal("Test timed out")
		case <-time.After(100 * time.Millisecond):
		}
		jf.Lock()
		str := buf.String()
		jf.Unlock()
		if str == "test" {
			jf.w = nil
			break
		}
	}
	select {
	case <-timeout:
		t.Fatal("Test timed out")
	case <-done:
	}
}
