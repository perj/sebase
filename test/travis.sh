#!/usr/bin/env bash
# Copyright 2018 Schibsted

set -e
iidfile="$(mktemp)"
docker build --iidfile "$iidfile" - < test/test.dockerfile
docker run --volume "$PWD:/sebase" -t --rm "$(< "$iidfile")" bash -e -x -c "
	cd /sebase
	seb
	build/dev/bin/regress-runner --travis-fold

	# Also do some tests with cgo enabled
	seb -configvars test/cgo.ninja
	build/dev/bin/regress-runner --travis-fold -d test/simple
	grep CDialer build/dev/tests/test_simple.xml

	# Also test that building with explicit nocgo works.
	seb -condition nocgo
"
rm -f "$iidfile"
