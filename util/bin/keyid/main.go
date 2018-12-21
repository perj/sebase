// Copyright 2018 Schibsted

package main

import (
	"crypto"
	_ "crypto/sha1"
	_ "crypto/sha256"
	"crypto/x509"
	"encoding/pem"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"

	"github.com/schibsted/sebase/util/pkg/keyid"
)

var (
	hashes = map[string]crypto.Hash{
		"sha1":   crypto.SHA1,
		"sha256": crypto.SHA256,
	}
)

func main() {
	hash := flag.String("hash", "sha256", "Hash algorithm to use. Only sha1 and sha256 are currently supported.")
	colon := flag.Bool("colon", false, "Output a colon separated string.")
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s [options] cert\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Arguments:\n")
		fmt.Fprintf(os.Stderr, "   cert - Pem encoded certificate file.\n")
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
	}
	flag.Parse()
	h, ok := hashes[*hash]
	if !ok || flag.NArg() != 1 {
		flag.Usage()
		os.Exit(1)
	}
	ParseCert(flag.Arg(0), h, *colon)
}

func ParseCert(path string, h crypto.Hash, colon bool) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		log.Fatal(err)
	}
	for len(data) > 0 {
		var pemdata *pem.Block
		pemdata, data = pem.Decode(data)
		if pemdata == nil {
			fmt.Fprintf(os.Stderr, "No PEM data found in %s\n", path)
			os.Exit(1)
		}
		if pemdata.Type != "CERTIFICATE" {
			continue
		}

		cert, err := x509.ParseCertificate(pemdata.Bytes)
		if err != nil {
			log.Fatal(err)
		}

		k, err := keyid.CertKeyId(cert, h)
		if err != nil {
			log.Fatal(err)
		}
		if colon {
			fmt.Println(k.ColonString())
		} else {
			fmt.Println(k)
		}
		return
	}
	fmt.Fprintf(os.Stderr, "No certificate found in %s\n", path)
	os.Exit(1)
}
