# Copyright 2018 Schibsted

print-tests:
	@echo TEST: bstest_1
	@echo CLEANUP: cleanup

bstest_1:
	${CMD_PREFIX} bufstring_test > .test.out || true
	match .test.out $@.out

cleanup:
	rm -f .test.out
