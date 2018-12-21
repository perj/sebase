// Copyright 2018 Schibsted

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"sync"
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
	path string
	io.Closer
	sync.RWMutex
}

func (jf *jsonFileWriter) Write(msg LogMessage) error {
	jf.RLock()
	defer jf.RUnlock()
	return jf.jsonIoWriter.Write(msg)
}

func (jf *jsonFileWriter) rotate() error {
	f, err := os.OpenFile(jf.path, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0666)
	if err != nil {
		return err
	}
	jf.Lock()
	jf.Close()
	jf.w = f
	jf.Closer = f
	jf.Unlock()
	return nil
}

func (jf *jsonFileWriter) Close() error {
	if jf.Closer == nil {
		return nil
	}
	err := jf.Closer.Close()
	if err == nil {
		jf.Closer = nil
		jf.w = nil
	}
	return err
}
