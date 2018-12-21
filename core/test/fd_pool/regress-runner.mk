# Copyright 2018 Schibsted

BUILDPATH?=build
FLAVOR?=dev

print-tests:
	@echo TEST: test_1
	@echo CLEANUP: cleanup

test_1:
	regress_fd_pool_test ../../../${BUILDPATH}/${FLAVOR}/regress/common/fd_pool/test.conf

cleanup:
	rm -f .test.out
