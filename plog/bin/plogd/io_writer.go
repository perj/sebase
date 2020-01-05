// Copyright 2018 Schibsted

package main

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"sync"
	"time"

	"github.com/schibsted/sebase/plog/pkg/plogd"
	"github.com/schibsted/sebase/util/pkg/slog"
)

type ioWriter struct {
	w io.Writer
}

func (out ioWriter) WriteMessage(ctx context.Context, msg plogd.LogMessage) {
	fmt.Fprintln(out.w, "---")
	fmt.Fprintln(out.w, msg.Prog)
	fmt.Fprintln(out.w, msg.Type)
	js, err := json.Marshal(msg.Message)
	if err != nil {
		slog.CtxError(ctx, "Failed to marshal message", "error", err, "message", fmt.Sprint(msg.Message))
		return
	}
	js = append(js, byte('\n'))
	_, err = out.w.Write(js)
	if err != nil {
		slog.CtxError(ctx, "Failed to write message", "error", err, "json", js)
		return
	}
	for k, v := range msg.KV {
		vs, err := json.Marshal(v)
		if err != nil {
			slog.CtxError(ctx, "Failed to marshal key-value", "error", err, "key", k, "value", fmt.Sprint(v))
			continue
		}
		fmt.Fprintf(out.w, "%s: %s\n", k, vs)
	}
}

func (out ioWriter) Close() error {
	return nil
}

type jsonIoWriter struct {
	w io.Writer
}

func (wr jsonIoWriter) WriteMessage(ctx context.Context, msg plogd.LogMessage) {
	body, err := msg.MarshalJSON()
	if err != nil {
		slog.CtxError(ctx, "Failed to marshal message", "error", err, "msg", fmt.Sprint(msg))
		return
	}
	body = append(body, '\n')
	_, err = wr.w.Write(body)
	if err != nil {
		slog.CtxError(ctx, "Failed to write message", "error", err, "body", body)
	}
}

func (wr jsonIoWriter) Close() error {
	return nil
}

type jsonFileWriter struct {
	jsonIoWriter
	path   string
	closer io.Closer
	sync.Mutex
}

func newJSONFileWriter(file string) (*jsonFileWriter, error) {
	w := &jsonFileWriter{path: file}
	err := w.rotate()
	if err != nil {
		return nil, err
	}
	go w.flusherThread()
	return w, nil
}

func (jf *jsonFileWriter) WriteMessage(ctx context.Context, msg plogd.LogMessage) {
	jf.Lock()
	defer jf.Unlock()
	jf.jsonIoWriter.WriteMessage(ctx, msg)
}

func (jf *jsonFileWriter) rotate() error {
	f, err := os.OpenFile(jf.path, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0666)
	if err != nil {
		return err
	}
	jf.Lock()
	jf.closeLocked()
	jf.w = bufio.NewWriter(f)
	jf.closer = f
	jf.Unlock()
	return nil
}

func (jf *jsonFileWriter) Close() error {
	jf.Lock()
	defer jf.Unlock()
	return jf.closeLocked()
}

func (jf *jsonFileWriter) closeLocked() error {
	if jf.closer == nil {
		return nil
	}
	err := jf.w.(*bufio.Writer).Flush()
	if err == nil {
		err = jf.closer.Close()
	}
	if err == nil {
		jf.closer = nil
		jf.w = nil
	}
	return err
}

func (jf *jsonFileWriter) flusherThread() {
	const defaultPeriod = time.Second
	const maxPeriod = time.Minute

	period := defaultPeriod
	for {
		time.Sleep(period)
		jf.Lock()
		if jf.w == nil {
			jf.Unlock()
			return
		}
		err := jf.w.(*bufio.Writer).Flush()
		jf.Unlock()
		if err == nil {
			period = defaultPeriod
			continue
		}

		log.Print("Periodic flush failed:", err)
		period *= 2
		if period > maxPeriod {
			period = maxPeriod
		}
	}
}
