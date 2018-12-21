#!/bin/bash
# Copyright 2018 Schibsted
#exec 2>/dev/null
openssl req -config <(printf "
[req]
distinguished_name=dname
x509_extensions=exts
prompt=no
[dname]
CN=localhost
[exts]
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid:always,issuer:always
basicConstraints = CA:true
subjectAltName = DNS:localhost
") -newkey rsa:2048 -passout pass:temp -new -x509 -days 2 -keyout >(openssl rsa -passin pass:temp)
