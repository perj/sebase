# Schibsted Search Engineering Base Libraries

This repository contains a set of base utilities used throughout the years
by the Search Engineering team and others at Schibsted.

Documentation is currently somewhat lacking, we hope to improve it reasonably
soon, depending on some priorities.

Interfaces are provided for C and Go. Some code might be lacking Go interfaces.

## Navigation

The repository is split in several submodules each residing in a different
directory.

### Vendor

This directory contains some third party sources we deemed important enough
to vendor. See the separate [README](vendor/README.md) for more information.
This directory has no outside dependencies. It can sometimes be replaced
by using system libraries.

### Util

Contains basic utilities such as AVL trees, socket utilties and encryption
wrapper functions.
This submodule depends on the vendored libraries and will also link with
Libcurl, OpenSSL and pcre.

### Vtree

A variable/config tree used in the core submodule. Can be initialized from
"bconf" files, json or manually in the code.
This submodule depends on util as well as libyajl version 2 or higher.

### Plog

This submodule contains a log aggregator for structured logging and program
state. It has client libaries and a daemon co-process expected to run on each
instance. Log lines are stored in the daemon where it's structured into a log
object that is logged once fully completed or connection interrupted.

It depends on the util submodule and uses protobuf to communicate.

### Core

Contains code for Service Discovery registration and lookup, client side
load balancing, program startup/shutdown and a few more utilities depending
on vtree.

It depends on all the other submodules. It can also optionally link with
libicu.

## Building and testing

### Dependencies

In total, the external library dependencies are

* curl
* icu (optional)
* openssl
* pcre
* protobuf-c
* yajl

Additionally a few tools are needed to build:

* Ninja (often called ninja-build or similar)
* Sebuild https://github.com/schibsted/sebuild
* Gperf
* Gcc or clang, including C++ support.
* Go

Running tests additionally requires

* GNU make
* python

### Building

Currently this repository can only built with the
[sebuild](https://github.com/schibsted/sebuild) tool. In the future we hope to
add more traditional packaging as well.

Tests can after compiling be run simply by running the `regress-runner` binary
which is compiled as part of the tree. It scans the directory it's invoked from
for regress-runner.mk files which contains a print-tests make target indicating
how to run tests.

To use this code in other projects with sebuild, you need to include this
repository as a subdirectory of your code.  This can be done with e.g. `git
subtree` or `git submodule`. Then you add the directory containing this code as
a `COMPONENT` in your top level Builddesc.

### Building Go packages

By default some of the Go packages will call the matching C libraries. For that
to work they have to be able to find them, so building with sebuild is a
requirement.

However, if you disable cgo with `CGO_ENABLED=0`, it should be possible to use
the Go packages as normal with go build, go get etc. Additionally you can also
use the go build tag `sebase_nocgo` for the same effect.

E.g. `go build -tags sebase_nocgo ./core/bin/sd-port`

If you wish to build with cgo disabled and still use sebuild, also use the
sebuild condition `nocgo` to disable compiling some test that require cgo.

## Maintainance Notice

Please note that most of the code in util, vtree and core are not currently
activly maintained, they're provided with the hope that someone will find it
useful. We do look at Pull Requests and Issues if reported.

If you wish to change the code or contribute, see
[CONTRIBUTING.md](CONTRIBUTING.md).

## LICENSE

Copyright 2018 Schibsted

Licensed under the MIT License, you may not use this code except in compliance
with the License. The full license is included in the file LICENCE.

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
