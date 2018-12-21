// Copyright 2018 Schibsted

package keyid

import (
	"crypto"
	"crypto/x509"
	"encoding/hex"
	"errors"
)

var (
	HashNotAvailable = errors.New("Package implementing hash is not imported")
)

// A calculated hash. The custom type is mostly for the custom Stringer
// function, which returns an hex encoded string similar to other hash tools.
type KeyId []byte

// Calculate the keyid from the public key stored in the certificate.
//
// This is not necessarily the same as the subjectKeyId stored in the
// certificate, which could have been generated using any method.
func CertKeyId(cert *x509.Certificate, h crypto.Hash) (KeyId, error) {
	if !h.Available() {
		return nil, HashNotAvailable
	}
	data, err := x509.MarshalPKIXPublicKey(cert.PublicKey)
	if err != nil {
		return nil, err
	}
	hf := h.New()
	hf.Write(data)
	return KeyId(hf.Sum(nil)), nil
}

// Returns k type converted to []byte.
func (k KeyId) Bytes() []byte {
	return []byte(k)
}

// Returns a hex encoded string of the keyid.
func (k KeyId) String() string {
	dst := make([]byte, hex.EncodedLen(len(k)))
	hex.Encode(dst, k)
	return string(dst)
}

// Returns a hex encoded string of the keyid, with colons inserted between each
// byte.
func (k KeyId) ColonString() string {
	dst := make([]byte, 3*len(k)-1)
	for i := range k {
		if i == 0 {
			hex.Encode(dst, k[:1])
		} else {
			dst[i*3-1] = ':'
			hex.Encode(dst[i*3:], k[i:i+1])
		}
	}
	return string(dst)
}
