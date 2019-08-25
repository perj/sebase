package plogd

import (
	"net/url"

	"github.com/schibsted/sebase/util/pkg/slog"
)

// NewOutputFunc is the signature that the function NewOutput should have in
// output plugins.
type NewOutputFunc func(url.Values, string) (OutputWriter, error)

// OutputWriter is the interface used to write log messages.
type OutputWriter interface {
	WriteMessage(logmsg slog.Logger, message LogMessage)
	Close() error
}
