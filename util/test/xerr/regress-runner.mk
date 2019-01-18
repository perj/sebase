# Copyright 2018 Schibsted

print-tests:
	@echo TEST: test_warn test_err test_errno test_sl_errno

test_warn:
	test "`sh -c "test_xerr 2>&1"`" = "test_xerr: hej: hopp"

test_err:
	test "`sh -c "test_xerr -e 2>&1 || exit 0"`" = "test_xerr: hej: hopp"

test_errno:
	test "`sh -c "test_xerr -n 2>&1"`" = "test_xerr: hej: Invalid argument"

# On OS X, all system binaries (e.g. make and sh) unset all DYLD environment
# variables for security. Thus we need to set them here just before running our
# custom binary when testing sysloghook.
test_sl_errno:
	echo INVALIDATE PREVIOUS TESTRUN >> ${SYSLOGROOT}/xerr.log
	if [ "$$(uname)" = Darwin ]; then \
		export DYLD_INSERT_LIBRARIES=$$LD_PRELOAD; \
		export DYLD_FORCE_FLAT_NAMESPACE=1; \
	fi; \
	test_xerr -ns
	tail -1 ${SYSLOGROOT}/xerr.log | grep -q "xerr: hej: Invalid argument"
