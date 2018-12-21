# Copyright 2018 Schibsted

print-tests:
	@echo TEST: test-small1
	@echo CLEANUP: cleanup

cleanup:
	rm -f .cmd.out

test-%:
	cat $*.in | xargs test_latin1_to_utf8 > .cmd.out
	match .cmd.out $*.out
