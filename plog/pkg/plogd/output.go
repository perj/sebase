package plogd

import (
	"context"
	"net/url"
)

// NewOutputFunc is the signature that the function NewOutput should have in
// output plugins.
type NewOutputFunc func(url.Values, string) (OutputWriter, error)

// OutputWriter is the interface used to write log messages.
type OutputWriter interface {
	WriteMessage(ctx context.Context, message LogMessage)
	Close() error
}
