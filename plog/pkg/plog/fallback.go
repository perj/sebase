// Copyright 2020 Schibsted

package plog

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"
	"time"
)

// FallbackWriter is where logs go if we can't connect to plogd. Defaults to stderr.
var FallbackWriter io.Writer = os.Stderr

// FallbackFormatter used for fallback writes. Can be overwritten to customize
// output. All values are json encoded. The key is constructed recursively with
// parent contexts prefixed, if any.
// Setup will change the default value to FallbackFormatterSimple if it
// detects that FallbackWriter is a tty when it's called. The default format
// might change without a major package version bump, set this manually if you
// depend on it.
var FallbackFormatter func(key []FallbackKey, value []byte) (n int, err error) = FallbackFormatterJsonWrap

// FallbackKey used for key argument to the formatter. The slice passed to the
// functions has one element per nested context level, followed by the key
// passed to the log funtion.
type FallbackKey struct {
	Key   string
	CtxId uint64
}

// FallbackFormatterSimple creates logs thath will be written prefixed with
// key and suffixed with a newline.
// After FallbackFormatKey the key is joined with dots (.). The key
// is then printed with a colon and the json encoded value after.
func FallbackFormatterSimple(key []FallbackKey, value []byte) (n int, err error) {
	prefix := strings.Join(FallbackFormatKey(key), ".") + ": "
	ww := append([]byte(prefix), value...)
	ww = append(ww, '\n')
	n, err = FallbackWriter.Write(ww)
	// Calculate how much of value we wrote.
	if n <= len(prefix) {
		n = 0
	} else {
		n -= len(prefix)
		if n > len(value) {
			n = len(value)
		}
	}
	return
}

// FallbackFormatterJsonWrap creates logs that will be written as JSON objects
// with key, message and @timestamp fields.
// The last entry in key is logged as "type" instead.
func FallbackFormatterJsonWrap(key []FallbackKey, value []byte) (n int, err error) {
	var v = struct {
		Time  time.Time       `json:"@timestamp"`
		Prog  string          `json:"prog,omitempty"`
		Type  string          `json:"type,omitempty"`
		Key   []string        `json:"key,omitempty"`
		Value json.RawMessage `json:"message"`
	}{time.Now(), "", "", nil, value}
	if len(key) > 1 {
		v.Prog = key[0].Key
		key = key[1:]
	}
	v.Type = key[len(key)-1].Key
	v.Key = FallbackFormatKey(key[:len(key)-1])
	ww, err := json.Marshal(v)
	if err != nil {
		return 0, err
	}
	ww = append(ww, '\n')
	_, err = FallbackWriter.Write(ww)
	if err == nil {
		n = len(value)
	}
	return
}

// FallbackFormatKey formats key in a standard way as a string slice.
// If the ctx id is <= 1 then that ctx id is skipped due to
// being reduntant and that key is used as-is. Otherwise each entry
// has the form key[ctxId].
func FallbackFormatKey(key []FallbackKey) []string {
	v := make([]string, len(key))
	for i := range key {
		if key[i].CtxId <= 1 {
			v[i] = key[i].Key
		} else {
			v[i] = fmt.Sprintf("%s[%d]", key[i].Key, key[i].CtxId)
		}
	}
	return v
}
