keyid(1) -- Hash a certificate public key
=========================================

## SYNOPSIS

`keyid` [`-hash` function] [`-colon`] pem-file

## DESCRIPTION

Extracts the public key stored in the certificate in DER format, and then
hashes it, printing the hash with hex encoding. The will give you a unique hash
for the public key used in the certficate, which in turn can be used for key
pinning or similar.

Some certificates have a subjectKeyId stored as an extension, that key id is
often the same as the SHA1 hash of the DER encoded public key, but it doesn't
have to be. It's up to the CA signing the certificate how the subjectKeyId is
generated, and it thus does not identify the key outside the context of that
CA.

## OPTIONS

* `-hash` function:
	Hash functions to use, one of `sha1` or `sha256`. By default `sha256`
	is used.

* `-colon`:
	Output a colon between each hex encoded byte in the output.

## EXIT STATUS

Exits 0 if successful, or 1 if there's any error, which will be printed to stderr.

## COPYRIGHT

Copyright 2018 Schibsted
