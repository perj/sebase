package etcdlight

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"strconv"
	"strings"

	"github.com/schibsted/sebase/util/pkg/slog"
)

// Type Response is a parsed successful response from etcd.
type Response struct {
	// The action that was taken, such as "update".
	Action string

	// Max index encountered in either header or parsed ModifiedIndex fields.
	MaxIndex uint64

	// Values matching the given key.
	Values []KV

	// PrevValues are currenly only set by Watch. It contains previous
	// values on updates.
	PrevValues []KV
}

type jsonResponse struct {
	Action string

	Node     responseNode
	PrevNode *responseNode
}

// Type ErrorResponse is used to report errors from Etcd.
type ErrorResponse struct {
	// StatusCode is set from the HTTP request.
	StatusCode int

	// ErrorCode is returned from Etcd. It will be 0 if unset or JSON decoding failed.
	ErrorCode int
	// Message is returned from Etcd, but if JSON decoding failed it contains the
	// HTTP body instead.
	Message string

	// Index is the Etcd index returned as part of the error from Etcd.
	Index uint64
}

func (e *ErrorResponse) Error() string {
	return fmt.Sprintf("HTTP code %d, etcd status %d: %s", e.StatusCode, e.ErrorCode, e.Message)
}

// IsErrorCode returns true if err is of type *ErrorResponse and the ErrorCode
// field matches one of the codes given.
func IsErrorCode(err error, code ...int) bool {
	er, ok := err.(*ErrorResponse)
	if !ok {
		return false
	}
	for _, c := range code {
		if er.ErrorCode == c {
			return true
		}
	}
	return false
}

type responseNode struct {
	Key string

	Dir bool

	Value string
	Nodes []*responseNode

	ModifiedIndex uint64
}

// ReadResponse parses a response from etcd.
// If the HTTP response code is not 200 or 201, an *ErrorResponse error is returned.
// If key is empty string body is decoded but not inspected. If key is not empty
// string, any non-directory nodes with that prefix is returned in values.
// This function consumes and closes resp.Body.
// In addition to *ErrorResponse, it might return errors from reading the body
// or json.Unmarshal.
func ReadResponse(resp *http.Response, key string) (*Response, error) {
	var r Response
	r.MaxIndex, _ = strconv.ParseUint(resp.Header.Get("X-Etcd-Index"), 10, 64)
	data, err := ioutil.ReadAll(resp.Body)
	resp.Body.Close()
	if err != nil {
		return nil, err
	}
	if resp.StatusCode != 200 && resp.StatusCode != 201 {
		var errResp ErrorResponse
		err := json.Unmarshal(data, &errResp)
		if err != nil {
			// Probably not a json response
			errResp.Message = string(data)
		}
		errResp.StatusCode = resp.StatusCode
		return nil, &errResp
	}
	var body jsonResponse
	err = json.Unmarshal(data, &body)
	if err != nil || key == "" {
		return &r, err
	}
	r.Action = body.Action
	var maxI uint64
	maxI, r.Values = parseResponseNode(&body.Node, key)
	if maxI > r.MaxIndex {
		r.MaxIndex = maxI
	}
	if body.PrevNode != nil {
		_, r.PrevValues = parseResponseNode(body.PrevNode, key)
	}
	return &r, nil
}

func parseResponseNode(node *responseNode, key string) (maxIndex uint64, values []KV) {
	maxIndex = node.ModifiedIndex

	if !node.Dir {
		if strings.HasPrefix(node.Key, key) {
			slog.Debug("XXX adding", "key", node.Key, "value", node.Value)
			values = []KV{{node.Key, node.Value, node.ModifiedIndex}}
		}
		return
	}

	// Check that one of the keys is a prefix of the other.
	for i := 0; i < len(node.Key) && i < len(key); i++ {
		if node.Key[i] != key[i] {
			return
		}
	}

	for _, n := range node.Nodes {
		maxI, kvs := parseResponseNode(n, key)
		if maxI > maxIndex {
			maxIndex = maxI
		}
		values = append(values, kvs...)
	}
	return
}
