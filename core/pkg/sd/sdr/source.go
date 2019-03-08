// Copyright 2018 Schibsted

package sdr

import (
	"context"
	"crypto/tls"
	"fmt"

	"github.com/schibsted/sebase/util/pkg/slog"
	"github.com/schibsted/sebase/vtree/pkg/bconf"
)

// Interface implemented by SD source drivers. When AddSources is called, the
// SdrConfigKey is checked to exist in the conf, and if so SdrSourceSetup is
// called. It should setup reading from the SD source and return an instance
// service users can connect to.
type SourceType interface {
	SdrName() string
	SdrConfigKey() string

	SdrSourceSetup(conf bconf.Bconf, tls *tls.Config) (SourceInstance, error)
}

// Interface for active driver sources. Any service might be connected, it's
// allowed to return nil, nil from Connect if a driver doesn't want to handle
// a service. Errors will be reported to the user, but only if no other
// driver accepted the service.
//
// Close is called when the registry is closed.
type SourceInstance interface {
	Connect(ctx context.Context, service string, config bconf.Bconf) (SourceConn, error)
	Close() error
}

// An active connection for a service from an SD source.  Updates are received
// on the channel, until you call the Close function which will close the
// channel from the sending side. Since channels are usually buffered there
// might still be some messages left after close, it's up to the user whether
// those need to be read or not.
type SourceConn interface {
	Channel() <-chan Message
	Close() error
}

// Simple error wrapper for when calling multiple sources that might
// each return an error.
type SourcesError []error

func (errs SourcesError) Error() string {
	return fmt.Sprint([]error(errs))
}

var sourceTypes []SourceType

// Called by source driver init function to register a source type.
func InitSourceType(src SourceType) {
	sourceTypes = append(sourceTypes, src)
}

// Instanciates any sources found in conf and adds them to the registry.
// Returns the number of sources found, plus any errors encounters, which will
// be a SourcesError. Sources might've been added even if err is non-nil.  A
// found source will be counted even if it is already present in the registry.
func (sdr *Registry) AddSources(conf bconf.Bconf) (n int, err error) {
	var errs SourcesError
	if sdr.sources == nil {
		sdr.sources = make(map[[2]string]SourceInstance)
	}
	for _, src := range sourceTypes {
		key := src.SdrConfigKey()
		value := conf.Get("sd", key).String("")
		if value == "" {
			continue
		}
		skey := [2]string{key, value}

		if sdr.sources[skey] != nil {
			// Source instance already added
			n++
			continue
		}

		sval, err := src.SdrSourceSetup(conf, sdr.TLS)
		if err != nil {
			errs = append(errs, err)
		} else if sval != nil {
			sdr.sources[skey] = sval
			slog.Info("Sd registry source added", "type", src.SdrName(), "value", value)
			n++
		}
	}
	if len(errs) == 0 {
		return n, nil
	}
	return n, errs
}

// Connect to updates for a service. conf is optional but might give hints to
// where to connect, and further parameters parsed by the source instance.
// Might return nil, nil if there was no error but no matching source instance
// could be found. Else returns either a connection or an error from the instance.
func (sdr *Registry) ConnectSource(ctx context.Context, service string, conf bconf.Bconf) (SourceConn, error) {
	var matcherr error
	tried := make(map[[2]string]bool)
	// First look for sources mathcing on key and value.
	if conf != nil {
		for skey, src := range sdr.sources {
			key := skey[0]
			value := conf.Get(key).String("")
			if value == "" {
				continue
			}
			if skey[1] != value {
				continue
			}
			conn, err := src.Connect(ctx, service, conf)
			if conn != nil {
				return conn, err
			}
			tried[skey] = true
			if err != nil {
				matcherr = err
			}
		}
	}
	// No match, let anyone grab it, but only store error if not already set.
	for skey, src := range sdr.sources {
		if tried[skey] {
			continue
		}
		conn, err := src.Connect(ctx, service, conf)
		if conn != nil {
			return conn, nil
		}
		if matcherr == nil {
			matcherr = err
		}
	}
	return nil, matcherr
}

// Many source types is just a struct with name and config key.
// You can use this type as a convenience.
type SourceTypeTemplate struct {
	Name, ConfigKey string
}

func (st *SourceTypeTemplate) SdrName() string {
	return st.Name
}

func (st *SourceTypeTemplate) SdrConfigKey() string {
	return st.ConfigKey
}
