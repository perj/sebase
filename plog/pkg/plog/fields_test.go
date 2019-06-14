package plog

import (
	"context"
	"strings"
	"testing"
)

func TestPlogLogger(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	plog, ch := NewTestLogContext(ctx, "test")
	logger := plog.WithFields(Fields{"a": "b"})
	logger.Error("Test error")
	msg := <-ch
	if string(msg.Value) != `{"a":"b","msg":"Test error"}` {
		t.Errorf("Wrong value, got %s", msg.Value)
	}
	logger.Log("test", map[string]interface{}{
		"q": "w",
	})
	msg = <-ch
	if string(msg.Value) != `{"a":"b","q":"w"}` {
		t.Errorf("Wrong value, got %s", msg.Value)
	}
	logger = plog
	logger.Error("Test plog directly")
	msg = <-ch
	if string(msg.Value) != `"Test plog directly"` {
		t.Errorf("Wrong value, got %s", msg.Value)
	}
}

func TestFallbackLogger(t *testing.T) {
	testStderr.Reset()
	WithFields(Fields{"a": "b"}).Info("fields info")
	if !strings.Contains(testStderr.String(), `INFO: {"a":"b","msg":"fields info"}`) {
		t.Errorf("fields weren't logged to stderr, got %s", testStderr.String())
	}
}

func TestLevelTypeLogger(t *testing.T) {
	testStderr.Reset()
	TypeLogger(Info).With("a", "b").Msg("type logger info")
	if !strings.Contains(testStderr.String(), `INFO: {"a":"b","msg":"type logger info"}`) {
		t.Errorf("fields weren't logged to stderr, got %s", testStderr.String())
	}
}
