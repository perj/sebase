package plogd

import (
	"context"
	"testing"
)

type testQW struct {
	ch chan LogMessage
}

func (tw *testQW) WriteMessage(ctx context.Context, message LogMessage) {
	tw.ch <- message
}

func (tw *testQW) Close() error {
	close(tw.ch)
	return nil
}

func testQueuedWriter(N int, tb testing.TB) {
	tw := &testQW{make(chan LogMessage)}
	qw := NewQueuedWriter(tw)

	go func() {
		ctx := context.Background()
		for i := 0; i < N; i++ {
			qw.WriteMessage(ctx, LogMessage{})
		}
	}()
	n := 0
	for range tw.ch {
		n++
		if n == N {
			if err := qw.Close(); err != nil {
				tb.Error(err)
			}
		}
	}
	if n != N {
		tb.Errorf("Received %v messages, expected %v", n, N)
	}
}

func TestQueuedWriter(t *testing.T) {
	testQueuedWriter(10000, t)
}

func BenchmarkQueuedWriter(b *testing.B) {
	// Not sure this is too useful as a benchmark but it's an easy way to
	// test different N.
	testQueuedWriter(b.N, b)
}
