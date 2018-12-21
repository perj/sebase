# Copyright 2018 Schibsted

print-tests:
	@echo TEST: test_1
	@echo CLEANUP: cleanup

test_1:
	bconf_test > .test.out
	match --force-new .test.out test_1.out

cleanup:
	rm -f .test.out
