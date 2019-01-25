#!/usr/bin/env bash
# Copyright 2018 Schibsted

set -e
img=$(docker build -q - < test/test.dockerfile)
docker run --volume "$PWD:/sebase" -t --rm "$img" bash -e -x -c "
	cd /sebase
	mkdir -p build/_go
	# Use a local gopath for pkg caching.
	export GOPATH=\$PWD/build/_go:\$GOPATH
	seb
	build/dev/bin/regress-runner --travis-fold

	# Also do some tests with cgo enabled
	seb -configvars test/cgo.ninja
	build/dev/bin/regress-runner --travis-fold -d test/simple
	grep CDialer build/dev/tests/test_simple.xml

	# Also test that building with explicit nocgo works.
	seb -condition nocgo
"
