# Copyright 2018 Schibsted

print-tests:
	@echo TEST: test_1
	@echo CLEANUP: cleanup

test_1:
	test=$@ config_test cfg1 > .test.out
	match .test.out test_1.out

cleanup:
	rm -f .test.out
