// Copyright 2018 Schibsted

package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"sync"
	"time"
)

type ioWriter struct {
	w io.Writer
}

func (out ioWriter) Write(msg LogMessage) error {
	fmt.Fprintln(out.w, "---")
	fmt.Fprintln(out.w, msg.Prog)
	fmt.Fprintln(out.w, msg.Type)
	js, err := json.Marshal(msg.Data)
	if err != nil {
		panic(err)
	}
	js = append(js, byte('\n'))
	_, err = out.w.Write(js)
	if err != nil {
		panic(err)
	}
	for k, v := range msg.KV {
		vs, err := json.Marshal(v)
		if err != nil {
			panic(err)
		}
		fmt.Fprintf(out.w, "%s: %s\n", k, vs)
	}
	return nil
}

func (out ioWriter) Close() error {
	return nil
}

type jsonIoWriter struct {
	w io.Writer
}

func (wr jsonIoWriter) Write(msg LogMessage) error {
	body, err := msg.ToJSON()
	if err != nil {
		return err
	}
	body = append(body, '\n')
	_, err = wr.w.Write(body)
	return err
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

func NewJsonFileWriter(file string) (*jsonFileWriter, error) {
	w := &jsonFileWriter{path: file}
	err := w.rotate()
	if err != nil {
		return nil, err
	}
	go w.flusherThread()
	return w, nil
}

func (jf *jsonFileWriter) Write(msg LogMessage) error {
	jf.Lock()
	defer jf.Unlock()
	return jf.jsonIoWriter.Write(msg)
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
