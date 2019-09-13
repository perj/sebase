// Package etcdlight is a lightweight implementation of the v2 etcd API.
// This package is here because the module the official Etcd client package is
// in has a lot of dependencies and we wish to keep sebase depencency light.
package etcdlight

import (
	"context"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"github.com/schibsted/sebase/util/pkg/slog"
)

type KAPI struct {
	Client  *http.Client
	BaseURL *url.URL
}

// NewKAPI returns a *KAPI with http.DefaultClient and the given URL.
// It returns errors from url.Parse unwrapped.
func NewKAPI(burl string) (*KAPI, error) {
	baseurl, err := url.Parse(burl)
	if err != nil {
		return nil, err
	}
	return &KAPI{
		Client:  http.DefaultClient,
		BaseURL: baseurl,
	}, nil
}

// URLForKey returns a copy of cl.BaseURL with key appended to the path.
func (cl *KAPI) URLForKey(key string) *url.URL {
	nurl := *cl.BaseURL
	nurl.Path += "/v2/keys" + key
	return &nurl
}

func (cl *KAPI) do(ctx context.Context, method, key string, qs, body url.Values) (*http.Response, error) {
	rqurl := cl.URLForKey(key)
	if len(qs) > 0 {
		rqurl.RawQuery = qs.Encode()
	}
	var ebody io.Reader
	if len(body) > 0 {
		ebody = strings.NewReader(body.Encode())
	}
	req, err := http.NewRequest(method, rqurl.String(), ebody)
	if err != nil {
		return nil, err
	}
	if len(body) > 0 {
		req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	}
	slog.CtxDebug(ctx, "etcdlight http request", "url", rqurl.String())
	return cl.Client.Do(req.WithContext(ctx))
}

// Get retrieves values. Only the current (latest) values can be retrieved.
// If recursive is true multiple key-values might be retrieved.
// Key should start with /.
// Errors from http.Client.Get and json.DecoderDecode are returned unwrapped.
// Returns a *ErrorResponse error if the HTTP status code was not 200 or 201.
func (cl *KAPI) Get(ctx context.Context, key string, recursive bool) (*Response, error) {
	qs := make(url.Values)
	if recursive {
		qs.Set("recursive", "true")
	}
	resp, err := cl.do(ctx, "GET", key, qs, nil)
	if err != nil {
		return nil, err
	}
	return ReadResponse(resp, key)
}

// Watch creates a watcher that can be used repeatedly to watch for changes.
// It will watch for changes after afterIndex. If 0 is used for that
// argument it will watch for the next future change from the time
// Next is called.
// This call doesn't do any network request, it only creates the Watcher object.
// Call Next to do the actual request.
// Key should start with /.
func (cl *KAPI) Watch(key string, recursive bool, afterIndex uint64) Watcher {
	return &watcher{cl, key, recursive, afterIndex}
}

// Set sets the value at key, possibly with a ttl. Pass 0 as ttl to not
// expire. If exclusive is true then Set will fail if the key already exists.
// Key should start with /.
// Returns errors from http.PostForm unwrapped.
// Returns a *ErrorResponse error if the HTTP status code was not 200 or 201.
func (cl *KAPI) Set(ctx context.Context, key string, value string, exclusive bool, ttl time.Duration) error {
	body := make(url.Values)
	body.Set("value", value)
	if exclusive {
		body.Set("prevExist", "false")
	}
	if ttl > 0 {
		body.Set("ttl", strconv.FormatInt(int64(ttl/time.Second), 10))
	}
	resp, err := cl.do(ctx, "PUT", key, nil, body)
	if err == nil {
		_, err = ReadResponse(resp, "")
	}
	return err
}

// MkDir creates a directory at key, possibly with a ttl. Pass 0 as ttl
// to not expire.
// Returns errors from http.PostForm unwrapped.
// Returns a *ErrorResponse error if the HTTP status code was not 200 or 201.
// Key should start with /.
func (cl *KAPI) MkDir(ctx context.Context, key string, ttl time.Duration) error {
	body := make(url.Values)
	body.Set("dir", "true")
	if ttl > 0 {
		body.Set("ttl", strconv.FormatInt(int64(ttl/time.Second), 10))
	}
	resp, err := cl.do(ctx, "PUT", key, nil, body)
	if err == nil {
		_, err = ReadResponse(resp, "")
	}
	return err
}

// RefreshDir updates the ttl for a directory. It must already exist.
// Returns errors from http.PostForm unwrapped.
// Returns a *ErrorResponse error if the HTTP status code was not 200 or 201.
// Key should start with /.
func (cl *KAPI) RefreshDir(ctx context.Context, key string, ttl time.Duration) error {
	body := make(url.Values)
	body.Set("dir", "true")
	body.Set("prevExist", "true")
	body.Set("refresh", "true")
	body.Set("ttl", strconv.FormatInt(int64(ttl/time.Second), 10))
	resp, err := cl.do(ctx, "PUT", key, nil, body)
	if err == nil {
		_, err = ReadResponse(resp, "")
	}
	return err
}

// RmDir removes the directory given, possibly recursively.
// Returns errors from http.Client.Do unwrapped.
// Returns a *ErrorResponse error if the HTTP status code was not 200 or 201.
// Key should start with /.
func (cl *KAPI) RmDir(ctx context.Context, key string, recursive bool) error {
	qs := make(url.Values)
	qs.Set("dir", "true")
	if recursive {
		qs.Set("recursive", "true")
	}
	resp, err := cl.do(ctx, "DELETE", key, qs, nil)
	if err == nil {
		_, err = ReadResponse(resp, "")
	}
	return err
}

// Type KV represents a value in etcd. Only leaf values can be represented.
type KV struct {
	// Key is the full path of the key, starting with /.
	Key   string
	Value string

	ModifiedIndex uint64
}

// Type Watcher is the watcher interface. Each call to Next does a new
// request waiting for the next value.
type Watcher interface {
	Next(ctx context.Context) (*Response, error)
}

type watcher struct {
	cl         *KAPI
	key        string
	recursive  bool
	afterIndex uint64
}

// Next returns the next set of values. The watcher is updated such that the
// next time Next is called it waits for new values.
// Errors from http.Client.Get and json.Decoder.Decode are returned unwrapped.
func (w *watcher) Next(ctx context.Context) (r *Response, err error) {
	qs := make(url.Values)
	qs.Set("wait", "true")
	qs.Set("waitIndex", strconv.FormatUint(w.afterIndex+1, 10))
	if w.recursive {
		qs.Set("recursive", "true")
	}
	resp, err := w.cl.do(ctx, "GET", w.key, qs, nil)
	if err != nil {
		return nil, err
	}
	r, err = ReadResponse(resp, w.key)
	if r != nil && r.MaxIndex > w.afterIndex {
		w.afterIndex = r.MaxIndex
	}
	return
}
