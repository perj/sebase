#!/usr/bin/env bash
# Copyright 2018 Schibsted

set -e
img=$(docker build -q - < test/test.dockerfile)
docker run --volume "$PWD:/sebase" -t --rm "$img" bash -e -c "
	cd /sebase
	mkdir -p build/_go
	# Use a local gopath for pkg caching.
	export GOPATH=\$PWD/build/_go:\$GOPATH
	seb
	build/dev/bin/regress-runner --travis-fold
"
