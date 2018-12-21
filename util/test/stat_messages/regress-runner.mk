# Copyright 2018 Schibsted

print-tests:
	@echo TEST: test_1
	@echo CLEANUP: cleanup

cleanup:
	rm -f .test.out

test_1:
	stat_message_test > .test.out
	contains .test.out test_1.out
