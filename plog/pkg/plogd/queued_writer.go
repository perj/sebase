package plogd

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/schibsted/sebase/util/pkg/slog"
)

// NewQueuedWriter wraps w such that w.WriteMessage is called asynchronously
// from a single goroutine, and is thus never called in parallell. If
// additional messages arrives to the QueuedWriter while that happens, they'll
// be queued up and delivered one at a time. w.Close will also be called from
// the same goroutine during shutdown after all messages have ben flushed.
// The Close call has a 5 second timeout after it will return with an error.
func NewQueuedWriter(w OutputWriter) OutputWriter {
	qw := &queuedWriter{
		OutputWriter: w,
		nextBufSz:    8,
		// 20 is more than enough to reach the max size of 1024*1024.
		queues: make(chan chan queuedMessage, 20),
		done:   make(chan error),
	}
	go qw.run()
	return qw
}

type queuedWriter struct {
	OutputWriter

	lock       sync.Mutex
	nextBufSz  int
	curQueue   chan queuedMessage
	queues     chan chan queuedMessage
	done       chan error
	logBlocked time.Time
}

type queuedMessage struct {
	prog    string
	message LogMessage
}

func (qw *queuedWriter) WriteMessage(ctx context.Context, message LogMessage) {
	msg := queuedMessage{
		ContextProg(ctx),
		message,
	}
	qw.lock.Lock()
	for {
		if qw.curQueue != nil {
			// Try to send but don't block.
			select {
			case qw.curQueue <- msg:
				qw.lock.Unlock()
				return
			default:
				// Do block if we're at the max queue size.
				if qw.nextBufSz > 1024*1024 {
					if time.Since(qw.logBlocked) >= 1*time.Second {
						slog.Critical("QueuedWriter blocked on write")
						qw.logBlocked = time.Now()
					}
					qw.curQueue <- msg
					qw.lock.Unlock()
					return
				}
			}
			close(qw.curQueue)
		}
		// Failed to send (or first send). Create a new queue and try again.
		switch {
		case qw.nextBufSz >= 1024*1024:
			slog.Error("QueuedWriter queue size increased", "sz", qw.nextBufSz)
		case qw.nextBufSz >= 128*1024:
			slog.Warning("QueuedWriter queue size increased", "sz", qw.nextBufSz)
		case qw.nextBufSz >= 1024:
			slog.Info("QueuedWriter queue size increased", "sz", qw.nextBufSz)
		}
		qw.curQueue = make(chan queuedMessage, qw.nextBufSz)
		qw.nextBufSz *= 2
		qw.queues <- qw.curQueue
	}
}

func (qw *queuedWriter) run() {
	ctx := context.Background()
	for queue := range qw.queues {
		for msg := range queue {
			pctx := ContextWithProg(ctx, msg.prog)
			qw.OutputWriter.WriteMessage(pctx, msg.message)
		}
	}
	qw.done <- qw.OutputWriter.Close()
}

func (qw *queuedWriter) Close() error {
	close(qw.curQueue)
	close(qw.queues)
	select {
	case err := <-qw.done:
		return err
	case <-time.After(5 * time.Second):
		return fmt.Errorf("timed out waiting for writer to close, possible data loss")
	}
}
