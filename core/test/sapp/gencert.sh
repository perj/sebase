#!/usr/bin/env bash
# Copyright 2018 Schibsted

# Create signed certificate. The output contains the concatenation of the
# generated private key, the generated certificate and the CA certificate.
#
# Optionally a custom CA key and cert can be given on the command line. By
# default the regress CA is used.

if [ $# -eq 1 ]; then
    common_name=$1
    ca_key=.ca.key.pem
    ca_cert=.ca.cert.pem
elif [ $# -eq 3 ]; then
    common_name=$1
    ca_key="$2"
    ca_cert="$3"
else
    echo "Usage: $0 common_name [<path to CA private key> <path to CA cert>]" 1>&2
    exit 1
fi

cleanup() {
    rm -f $cert_key $cert $csr
}

handle_error() {
    cleanup
    exit 1
}
trap handle_error SIGHUP SIGINT SIGTERM ERR

cert_key=$(mktemp)
cert=$(mktemp)
csr=$(mktemp)

set -e

# Create server private key and CSR for server cert
openssl req -nodes -config <(printf "
[req]
prompt=no
distinguished_name=dname
[dname]
CN=%s
O=Search Engineering regress
" $common_name) -newkey rsa:2048 -new -sha256 -out $csr > $cert_key 2> /dev/null

# Sign cert
openssl x509 -extfile <(printf "
[san]
subjectAltName=IP:127.0.0.1,IP:::1,DNS:localhost,DNS:%s
" $common_name) -extensions san -req -CA $ca_cert -CAkey $ca_key -set_serial $(printf "0x%s" $(openssl rand -hex 20)) -in $csr -out $cert 2> /dev/null

# View cert
# openssl x509 -noout -text -in $cert 1>&2

cat $cert_key $cert $ca_cert
cleanup
