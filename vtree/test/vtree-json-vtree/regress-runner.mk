# Copyright 2018 Schibsted

print-tests:
	@echo TEST: testit
	@echo CLEANUP: cleanup

cleanup:
	rm -f .test.out

testit:
	vtree_json_vtree > .test.out
	match .test.out testit.out

