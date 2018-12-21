# Vendored libraries

Copyright 2018 Schibsted

This directory contains third party sources that we deemed important enough to
vendor. Some of these are available as libraries on some systems, you should
prefer to use the system distributed once in that case. The vendored versions
are used by default but conditions exist to use the system ones instead,
using the form `system_library`. E.g. `seb -condition system_sha` will disable
the use of the vendored sha library.

In general, all these are available as system libraries on Ubuntu, but for
other systems in varies.

Note that you might have to clean your build directory to remove the vendored
versions if you switch to the system ones.

These libraries all have permissive licenses but you may want to inspect them
individually. We've added Builddesc files in each directory.

## http-parser

Joyent C http parser. This is available as libhttp-parser on some systems, add
`-condition system_http_parser` to use that library instead if so.

See [http_parser/http_parser.c](http_parser/http_parser.c) for license details.

## sha

Secure Hash Algorithm implementations.

These are the same as provided by OpenBSD and by libmd on Linux. However not
all Linux distributions have that library. Add `-condition system_sha` to the
seb command line to use the system version instead.

See [sha/sha1.c](sha/sha1.c) and [sha/sha2.c](sha/sha2.c) for license details.

## xxhash

A fast hashing algorithm, see [xxhash/xxhash.h](xxhash/xxhash.h) for details.

Ubuntu has a system package for this, but it's generally not available outside
of Linux. Add `-condition system_xxhash` to the seb command line to use the
system version instead.
