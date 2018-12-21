#!/usr/bin/env bash
# Copyright 2018 Schibsted

# Create a CA cert and private key. gencert.sh can be used to create
# certificates signed by the CA cert created by this script.

set -e

ca_key="${1:-.ca.key.pem}"
ca_cert="${2:-.ca.cert.pem}"

# Generate private CA key
openssl genrsa -out "$ca_key" 2048 >& /dev/null

# Generate CA cert
openssl req -config <(printf "
[req]
prompt=no
distinguished_name=dname
[dname]
CN=Search Engineering regress CA
[ca_v3]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true
keyUsage = critical, digitalSignature, cRLSign, keyCertSign
") -key "$ca_key" -extensions ca_v3 -new -x509 -sha256 -out "$ca_cert"

# View CA cert
# openssl x509 -noout -text -in "$ca_cert"
