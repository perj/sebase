// Copyright 2018 Schibsted

package acl

// Code to initialize and check ACLs. Clients of this code should first use
// InitFromBconf or InitDefault. Then CheckRequest should be used to check if a
// given HTTP request should be allowed or not.

import (
	"crypto"
	"net"
	"net/http"
	"strings"

	"github.com/schibsted/sebase/util/pkg/keyid"
	"github.com/schibsted/sebase/util/pkg/slog"
)

type Access struct {
	Method     string
	Path       string
	RemoteAddr *string `yaml:"remote_addr"`
	CommonName *string `yaml:"cert.cn"`
	Issuer     *string `yaml:"issuer.cn"`
	KeyId      *string
	Action     string
}

type Acl struct {
	Acl []Access
}

func (acl *Acl) InitDefault() {
	slog.Info("Adding default ACL")
	p := func(s string) *string {
		return &s
	}
	acl.Acl = []Access{
		Access{Method: "*", Path: "/", RemoteAddr: p("::1"), Action: "allow"},
		Access{Method: "*", Path: "/", RemoteAddr: p("127.0.0.1"), Action: "allow"},
		Access{Method: "*", Path: "/", CommonName: p("*"), Action: "allow"},
	}
}

func (acl *Acl) CheckRequest(req *http.Request) bool {
	v := reqValues{req: req}
	return acl.check(&v)
}

func (acl *Acl) check(v aclValues) bool {
	for _, ac := range acl.Acl {
		if ac.Method != "*" && v.Method() != ac.Method {
			continue
		}

		if !strings.HasPrefix(v.Path(), ac.Path) {
			continue
		}
		if !strings.HasSuffix(ac.Path, "/") && len(v.Path()) != len(ac.Path) {
			continue
		}

		if ac.RemoteAddr != nil {
			ra, err := v.RemoteAddr()
			if err != nil {
				slog.Error("Failed to extract remote address", "error", err)
				continue
			}
			if *ac.RemoteAddr != ra {
				continue
			}
		}

		if ac.CommonName != nil {
			if v.CommonName() == nil {
				continue
			}
			if *ac.CommonName != "*" && *ac.CommonName != *v.CommonName() {
				continue
			}
		}

		if ac.Issuer != nil {
			if v.Issuer() == nil {
				continue
			}
			if *ac.Issuer != "*" && *ac.Issuer != *v.Issuer() {
				continue
			}
		}

		if ac.KeyId != nil {
			if v.KeyId() == nil {
				continue
			}
			if *ac.KeyId != *v.KeyId() {
				continue
			}
		}

		if ac.Action == "allow" {
			return true
		}
		// Parse other actions as deny
		break
	}
	return false
}

type aclValues interface {
	Method() string
	Path() string
	RemoteAddr() (string, error)
	CommonName() *string
	Issuer() *string
	KeyId() *string
}

type reqValues struct {
	req *http.Request
}

func (v *reqValues) Method() string {
	return v.req.Method
}

func (v *reqValues) Path() string {
	return v.req.URL.Path
}

func (v *reqValues) RemoteAddr() (string, error) {
	host, _, err := net.SplitHostPort(v.req.RemoteAddr)
	return host, err
}

func (v *reqValues) CommonName() *string {
	if v.req.TLS == nil || len(v.req.TLS.PeerCertificates) == 0 {
		return nil
	}
	return &v.req.TLS.PeerCertificates[0].Subject.CommonName
}

func (v *reqValues) Issuer() *string {
	if v.req.TLS == nil || len(v.req.TLS.PeerCertificates) == 0 {
		return nil
	}
	return &v.req.TLS.PeerCertificates[0].Issuer.CommonName
}

func (v *reqValues) KeyId() *string {
	if v.req.TLS == nil || len(v.req.TLS.PeerCertificates) == 0 {
		return nil
	}
	k, err := keyid.CertKeyId(v.req.TLS.PeerCertificates[0], crypto.SHA256)
	if err != nil {
		slog.Error("Failed to hash peer public key", "error", err)
		return nil
	}
	ret := k.String()
	return &ret
}
