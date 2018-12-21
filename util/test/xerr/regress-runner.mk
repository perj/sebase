# Copyright 2018 Schibsted

print-tests:
	@echo TEST: test_warn test_err test_errno test_sl_errno

test_warn:
	test "`sh -c "test_xerr 2>&1"`" = "test_xerr: hej: hopp"

test_err:
	test "`sh -c "test_xerr -e 2>&1 || exit 0"`" = "test_xerr: hej: hopp"

test_errno:
	test "`sh -c "test_xerr -n 2>&1"`" = "test_xerr: hej: Invalid argument"

test_sl_errno:
	echo INVALIDATE PREVIOUS TESTRUN >> ${SYSLOGROOT}/xerr.log
	${SYSLOGHOOK} test_xerr -ns
	tail -1 ${SYSLOGROOT}/xerr.log | grep -q "xerr: hej: Invalid argument"
