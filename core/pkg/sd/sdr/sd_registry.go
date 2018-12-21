// Copyright 2018 Schibsted

package sdr

import "crypto/tls"

type Registry struct {
	Host string
	Appl string

	TLS *tls.Config

	sources map[[2]string]SourceInstance
}

// Default global registry.
var Default = &Registry{}

// Closes all the sources, if any errors were reported this function will
// return a SourcesError.
//
// Note that some drivers require that all connections are closed first, which
// you should do before calling this.
func (sdr *Registry) Close() error {
	var errs SourcesError
	for _, src := range sdr.sources {
		err := src.Close()
		if err != nil {
			errs = append(errs, err)
		}
	}
	return errs
}
