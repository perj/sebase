// Copyright (c) 2015 Cory Jacobsen
// Copyright 2020 Schibsted
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

package loghttp

// Package to log http requests.
//
// This code is based on https://github.com/unrolled/logger/blob/master/logger.go

import (
	"bufio"
	"fmt"
	"net"
	"net/http"
	"time"

	"github.com/schibsted/sebase/plog/pkg/plog"
)

type LogFunc = func(map[string]interface{})

// LogMiddleware adds a logging middleware to an http.Handler. For each HTTP
// request the middleware will log via plog.CtxPlog(req.Context()).OpenDict("http-in").
// When the handler is called it's with a "log" sub-context to that plog context.
//
// The infoLogger and errorLogger arguments are deprecated but if non-nil those
// functions will be called with information about the request.  infoLogger is
// called on 1xx, 2xx, 3xx requests, errorLogger is called otherwise. This is
// done in addition to the direct logging.
//
// If h is nil then http.DefaultServeMux is used.
func LogMiddleware(h http.Handler, infoLogger LogFunc, errorLogger LogFunc) http.Handler {
	if h == nil {
		h = http.DefaultServeMux
	}
	useLogger := infoLogger != nil || errorLogger != nil
	return http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		ctx := req.Context()
		pctx := plog.CtxPlog(ctx).OpenDict("http-in")
		defer pctx.Close()

		// FIXME: Would be nice to log remote IP here. See
		// https://aws.amazon.com/blogs/aws/elastic-load-balancing-adds-support-for-proxy-protocol/
		// for one approach which might work if we want to terminate the
		// TLS connection in the application instead of in the ELB.
		//
		// FIXME: Would be nice to log size of request body.
		pctx.Log("proto", req.Proto)
		pctx.Log("method", req.Method)
		pctx.Log("host", req.Host)
		pctx.Log("uri", req.URL.RequestURI())
		var logObj map[string]interface{}
		var start time.Time
		if useLogger {
			logObj = map[string]interface{}{
				"proto":  req.Proto,
				"method": req.Method,
				"host":   req.Host,
				"uri":    req.RequestURI,
			}
			start = time.Now()
		}
		if cn := commonName(req); cn != nil {
			pctx.Log("cert_cn", cn)
			if useLogger {
				logObj["cert_cn"] = cn
			}
		}
		logpctx := pctx.OpenListOfDicts("log")
		defer logpctx.Close()
		subctx := plog.ContextWithLogger(ctx, logpctx)

		crw := newCustomResponseWriter(rw)
		defer func() {
			if r := recover(); r != nil {
				pctx.Log("status", "panic")
				pctx.Log("panic", fmt.Sprint(r))
				if errorLogger != nil {
					logObj["duration_ms"] = float64(time.Since(start)) / 1000000.0
					logObj["status"] = "panic"
					logObj["panic"] = fmt.Sprintf("%v", r)
					errorLogger(logObj)
				}
				panic(r)
			}
		}()
		h.ServeHTTP(crw, req.WithContext(subctx))
		pctx.Log("status", crw.status)
		pctx.Log("response_size", crw.size)
		if useLogger {
			logObj["duration_ms"] = float64(time.Since(start)) / 1000000.0
			logObj["status"] = crw.status
			logObj["response_size"] = crw.size
		}

		if statusIsOk(crw.status) {
			if infoLogger != nil {
				infoLogger(logObj)
			}
		} else {
			if crw.body != nil {
				pctx.LogAsString("response_body", crw.body)
				if errorLogger != nil {
					logObj["response_body"] = string(crw.body)
				}
			}
			if errorLogger != nil {
				errorLogger(logObj)
			}
		}
	})
}

func statusIsOk(status int) bool {
	return status/100 <= 3
}

func commonName(req *http.Request) *string {
	if req.TLS == nil || len(req.TLS.PeerCertificates) == 0 {
		return nil
	}
	return &req.TLS.PeerCertificates[0].Subject.CommonName
}

// Maximum number of bytes of response body which will be logged.
const RESPONSE_BODY_LOG_LIMIT = 1000

type customResponseWriter struct {
	http.ResponseWriter
	status int
	size   int

	// Up to RESPONSE_BODY_LOG_LIMIT bytes from the response body. Only
	// bodies for unsuccessful requests are kept.
	body []byte
}

func (c *customResponseWriter) WriteHeader(status int) {
	c.status = status
	c.ResponseWriter.WriteHeader(status)
}

func (c *customResponseWriter) Write(b []byte) (int, error) {
	size, err := c.ResponseWriter.Write(b)
	c.size += size
	if !statusIsOk(c.status) && len(c.body) < RESPONSE_BODY_LOG_LIMIT {
		sz := RESPONSE_BODY_LOG_LIMIT - len(c.body)
		if sz > size {
			sz = size
		}
		c.body = append(c.body, b[:sz]...)
	}
	return size, err
}

func (c *customResponseWriter) Flush() {
	if f, ok := c.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	}
}

func (c *customResponseWriter) Hijack() (net.Conn, *bufio.ReadWriter, error) {
	if hj, ok := c.ResponseWriter.(http.Hijacker); ok {
		return hj.Hijack()
	}
	return nil, nil, fmt.Errorf("ResponseWriter does not implement the Hijacker interface")
}

func newCustomResponseWriter(w http.ResponseWriter) *customResponseWriter {
	// When WriteHeader is not called, it's safe to assume the status will be 200.
	return &customResponseWriter{
		ResponseWriter: w,
		status:         200,
	}
}
