# Copyright 2018 Schibsted

TOPDIR=../..

BUILDPATH?=build
FLAVOR?=dev

STINC=../../${BUILDPATH}/${FLAVOR}/regress/simple/simple_targets.mk
GTINC=../../${BUILDPATH}/${FLAVOR}/regress/simple/gotests.mk

include ${STINC}
include ${GTINC}

print-tests:
	@echo TEST: ${REGRESS_TARGETS}
