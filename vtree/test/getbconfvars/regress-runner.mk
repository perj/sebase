# Copyright 2018 Schibsted

print-tests:
	@echo TEST: test_1 test_2
	@echo CLEANUP: cleanup

cleanup:
	rm -f .test.out

TEST_OUTPUT=.test.out

test_1:
	getbconfvars --file cfg1 --root a -k . > ${TEST_OUTPUT}
	match .test.out $@.out

test_2:
	getbconfvars --file cfg1 --prefix var_ -k [xz] > ${TEST_OUTPUT}
	match .test.out $@.out

