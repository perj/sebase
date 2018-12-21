// Copyright 2018 Schibsted

package sdr

import "strings"

// Converts slash syntax service to a dot separated host name and appends
// domain.  E.g. search/asearch with domain example.com returns
// asearch.search.example.com.
func ServiceToHost(service, domain string) string {
	ss := strings.Split(service, "/")
	for i, j := 0, len(ss)-1; i < j; i, j = i+1, j-1 {
		ss[i], ss[j] = ss[j], ss[i]
	}
	if domain != "" && domain[0] != '.' {
		domain = "." + domain
	}
	return strings.Join(ss, ".") + domain
}

// If the host ends with the domain, strip that domain and convert the host to
// a service name. E.g. asearch.search.example.com with domain example.com
// returns search/asearch.
//
// In case the host does NOT end with the domain, it's returned unmodified.
func HostToService(host, domain string) string {
	if !strings.HasSuffix(host, domain) {
		return host
	}
	host = host[:len(host)-len(domain)]
	host = strings.TrimSuffix(host, ".")
	ss := strings.Split(host, ".")
	for i, j := 0, len(ss)-1; i < j; i, j = i+1, j-1 {
		ss[i], ss[j] = ss[j], ss[i]
	}
	return strings.Join(ss, "/")
}
