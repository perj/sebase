// Copyright (c) 2015 Cory Jacobsen
// Copyright 2018 Schibsted
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
)

type LogFunc = func(map[string]interface{})

// Add an logging middleware to an http.Handler. For each HTTP request the
// middleware will call infoLogger if the status code indicates a successful
// HTTP request (1xx, 2xx, or 3xx). errorLogger will be called otherwise. The
// argument passed to infoLogger and errorLogger are intended to be logged by
// plog.LogAsJson or by logging the result of json.Marshal.
func LogMiddleware(h http.Handler, infoLogger LogFunc, errorLogger LogFunc) http.Handler {
	if h == nil {
		h = http.DefaultServeMux
	}
	return http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		start := time.Now()

		// FIXME: Would be nice to log remote IP here. See
		// https://aws.amazon.com/blogs/aws/elastic-load-balancing-adds-support-for-proxy-protocol/
		// for one approach which might work if we want to terminate the
		// TLS connection in the application instead of in the ELB.
		//
		// FIXME: Would be nice to log size of request body.
		logObj := map[string]interface{}{
			"proto":  req.Proto,
			"method": req.Method,
			"host":   req.Host,
			"uri":    req.RequestURI,
		}
		if cn := commonName(req); cn != nil {
			logObj["cert_cn"] = cn
		}

		crw := newCustomResponseWriter(rw)
		defer func() {
			if r := recover(); r != nil {
				logObj["duration_ms"] = float64(time.Since(start)) / 1000000.0
				logObj["status"] = "panic"
				logObj["panic"] = fmt.Sprintf("%v", r)
				errorLogger(logObj)
				panic(r)
			}
		}()
		h.ServeHTTP(crw, req)
		logObj["duration_ms"] = float64(time.Since(start)) / 1000000.0
		logObj["status"] = crw.status
		logObj["response_size"] = crw.size

		if statusIsOk(crw.status) {
			infoLogger(logObj)
		} else {
			if crw.body != nil {
				logObj["response_body"] = string(crw.body)
			}
			errorLogger(logObj)
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
