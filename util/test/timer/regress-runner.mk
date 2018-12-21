# Copyright 2018 Schibsted

print-tests:
	@echo TEST: test_timer
	@echo CLEANUP: cleanup

cleanup:
	rm -f .test.out

test_timer:
	test_timer > .test.out
	match .test.out $@.out
