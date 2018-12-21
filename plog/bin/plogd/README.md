What is it?
===========

This tool is for aggregating log lines into log objects.
An example of an object would be a transaction call including all input, output
and log lines.  The whole object is then sent off to logstash, which passes it
on, or simply to a file or similar.

Since it keeps track of sessions it will also know if one was interrupted
abnormally, and can then mark the object as incomplete.

It will also keep a "log state" for each client application, where stuff such
as number of connections, database size etc. can be kept. Those are sent off at
client shutdown but can also be queried over HTTP.

How to build
============

Currently this is a subdirectory of the parent repo, and you build it with
go >= 1.11 with module support. It will use the root go.mod etc.

Commands:

	go build .

How to test
===========

Unit tests can be run with `go build .`. Integration tests are done using the
client Go library or other code that compiles the protobuf used to send
messages.

Copyright
=========

Copyright 2018 Schibsted
