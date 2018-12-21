acl-proxy(1) -- Privilege separated user certificate
====================================================

## SYNOPSIS

`acl-proxy` `-upstream-cert` file `-server-cert` file `-server-ca` file
[`-listen` address] [`-acl` file] [`-min-exit-early` duration]
[`-max-exit-early` duration] https-url

`acl-proxy` `-upstream-cert` file `-insecure`
[`-listen` address] [`-acl` file] [`-min-exit-early` duration]
[`-max-exit-early` duration] https-url

## DESCRIPTION

A privilege separation HTTPS proxy

The privilege it's guarding is the permission to talk to the upstream server.
Incoming requests are checked against an ACL before forwarded using the
configured client certificate, which should be kept private.
In addition, the incoming client must authenticate with a separate client
certificate that the proxy accepts. This can be done with a simple valid
cert check or by specifying an exact common name or key signature.

Thus you can have a certificate with more permissions and this proxy reduces
it to a lower set. Obviously the incoming client can't have access to the
upstream client certificate file or this is pointless.

The program will exit with status code 10 if it detects that any of the
certificates expires. You can use that exit code to detect this condition.

## OPTIONS

* `-listen` address:
	Address to listen on, as ip:port. By default the port is extracted from
	the upstream URL and the proxy will listen on ::1 on the same port.

* `-acl` file:
	Load ACL from this file. This option is mandatory in practice since by
	default all requests are denied. See below for format.

* `-insecure`:
	Listen for HTTP, not HTTPs. Server certificates are not required if
	using this. Not recommended.

* `-server-cert` file:
	File containing PEM encoded certificate and private key concatenated,
	to send to incoming clients.

* `-server-ca` file:
	File containing PEM encoded CA certificate to authenticate incoming
	clients with.

* `-upstream-cert` file:
	File containing PEM encoded certificate and private key concatenated,
	to send to server we are proxying.

* `-upsteam-ca` file:
	File containing PEM encoded CA certificate to authenticate server we
	are proxying. If unset the system CA pool will be used.

* `-min-exit-early` duration:
	Exit with exit code 10 at least this duration before any of the
	certificates expires. A random duration between min and max is used, or
	the exact time if only min is given.

* `-maxExitEarly` duration:
	Exit with exit code 10 at most this duration before any of the
	certificates expires. A random duration between min and max is used.

## EXIT CODE

Exit code 1 is used if any errors in options or files are detected.
Exit code 10 will indicate a certificate expired.

## ACL FILE

The ACL file is a YAML file containing a list of access checks. See auth(7) for
the semantics.

### Example

	- method: GET
	  path: /
	  keyid: 00112233445566778899aabbccddeeff101112131415161718191a1b1c1d1e1f
	  action: allow
	- method: POST
	  path: /foo
	  keyid: 00112233445566778899aabbccddeeff101112131415161718191a1b1c1d1e1f
	  action: allow

Will allow the client certificate with the given key id to get everything, but
only post to `/foo`.

## SEE ALSO

auth(7)

## COPYRIGHT

Copyright 2018 Schibsted
