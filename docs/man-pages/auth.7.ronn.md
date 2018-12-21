auth(7) -- Authentication and Authorization
===========================================

## SYNOPSIS

This document described the authentication and authorization mechanisms that are
available for daemons in the Search Engineering Platform.

## INTRODUCTION

TLS certificates are used for authentication. The idea is to get certificates
from HashiCorp [Vault](https://www.vaultproject.io/) during service startup and
then use these to authenticate the services to each other.

Access control lists are used for authorization and they allow you to limit
access to specific HTTP endpoints. Currently the options are to check the remote
address and/or the client certificate presented by the client.

## C AND GO

There are some differences in authorization and authentication capabilities
between daemons written in C compared to those that are written in Go.

The C daemons accepts both HTTP and HTTPS requests on the same port. Go daemons
either accept HTTP or HTTPS, but not both.

The `keyid` ACL matching is only available for daemons written in Go.

For C daemons the ACL configuration is added on the controller path. E.g., in
search.conf you would use `search.acl.X.path` etc. For Go daemons the first part
is dropped and it becomes `acl.X.path` etc.

## CONFIGURATION

### Certificates

To verify the peers you're talking to (both as client and as server), add
the config key `cacert.path` pointing to a file containing the root CA
certificate in PEM format. Alternatively, you can use `cacert.command` to
run a command via `popen` that should output the certificate.

To setup the local certificate to use, similarly add `cert.path` or
`cert.command`. The file/command should have a private key together with
one or more certificate. The first certificate is the one to use, while
any following are intermediate CAs up to the root.

Example:

    cacert.command=curl -s $VAULT_ADDR/pki/ca/pem
    cert.command=vault write -field=certificate pki/issue/myservice common_name=myservice.mydomain alt_names=localhost ip_sans=myip,127.0.0.1,::1 format=pem_bundle

The `alt_names` and `ip_sans` are for when the certificate is used as a
server certificate, for the client to check the name that matches the URL.

### ACLs

The ACL is configured in the controller configuration. A list of matches
and actions is evaluated under the bconf key `acl`, with a default action
of deny if the end of the list is reached.

If no ACL is specified in the configuration file a default ACL is added that
allow all queries from localhost, but remote queries are only allowed if a valid
certificate was used.

Full set of bconf keys:

* `acl.X.method`: The HTTP method to match. "*" means that all methods
  match. Mandatory.

* `acl.X.path`: URL path to match on, starting with `/`. Mandatory.  If it ends
  on `/` it's a prefix match, otherwise exact. The query string is not part of
  the string matched on. Putting an action on / matches all URL paths.

* `acl.X.remote_addr`: The client IP to match on, e.g. `192.0.2.1` or `::1`.

* `acl.X.cert.cn`: The certificate subject common name to match on. `*` will
  match any certificate, as long as a valid one was sent by the client. (This
  is `myservice.mydomain` in the Vault command example above.)

* `acl.X.issuer.cn`: The certificate issuer common name to match on. `*` will
  match if any certificate, as long as a valid one was sent by the client.

* `acl.X.keyid`: SHA256 hash of the public key in the client certificate
  presented by the client, as a hex string. Easiest way to obtain this is
  with the keyid(1) tool provided. This pins the client to the specific key
  pair. This configuration is only available for daemons written in Go.

* `acl.X.action`: The action to take. `allow` will allow the request, anything
  else will deny it, but preferably use `deny`.

### Examples

This is the default ACL. Not providing ACL configuration is equivalent to this.

    acl.1.method=*
    acl.1.path=/
    acl.1.remote_addr=::1
    acl.1.action=allow

    acl.2.method=*
    acl.2.path=/
    acl.2.remote_addr=127.0.0.1
    acl.2.action=allow

    acl.3.method=*
    acl.3.path=/
    acl.3.common_name=*
    acl.3.action=allow

This configuration will allow the client certificate with the given key id to
get everything, but only post to `/foo`.

    acl.1.method=GET
    acl.1.path=/
    acl.1.keyid=00112233445566778899aabbccddeeff101112131415161718191a1b1c1d1e1f
    acl.1.action=allow

    acl.2.method=POST
    acl.2.path=/foo
    acl.2.keyid=00112233445566778899aabbccddeeff101112131415161718191a1b1c1d1e1f
    acl.2.action=allow

## SEE ALSO

acl-proxy(1), keyid(1)

## COPYRIGHT

Copyright 2018 Schibsted
