#!/bin/sh
# Copyright 2018 Schibsted

if command -v ronn >/dev/null; then
	echo "man"
else
	echo "No ronn binary. Man pages will not be built." >&2
fi
