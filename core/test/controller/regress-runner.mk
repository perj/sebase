# Copyright 2018 Schibsted

print-tests:
	@echo DEPEND: ${REGRESS_DEPEND}
	@echo TEST: ${REGRESS_TARGETS}
	@echo CLEANUP: ${REGRESS_CLEANUP} cleanup

BUILDPATH?=build
FLAVOR?=dev
TESTDIR=../../../${BUILDPATH}/${FLAVOR}/regress/controller

REGRESS_DEPEND=platform-regress-controller-start-regress-controller.conf
REGRESS_CLEANUP=platform-regress-controller-stop

REGRESS_TARGETS+=ctrl-hej
REGRESS_TARGETS+=ctrl-dump_vars
REGRESS_TARGETS+=pctrl-sha256
REGRESS_TARGETS+=hctrl-custom_headers
REGRESS_TARGETS+=ctrl-missing
REGRESS_TARGETS+=ctrl-hej-missing
REGRESS_TARGETS+=ctrl-hej-hopp
REGRESS_TARGETS+=ctrl-hej-tjo-param
REGRESS_TARGETS+=ctrl-partial-param
REGRESS_TARGETS+=ctrl-two-params
REGRESS_TARGETS+=ctrl-middle-param
REGRESS_TARGETS+=ctrl-keepalive-calls
REGRESS_TARGETS+=enforce-min-nthreads
REGRESS_TARGETS+=platform-regress-controller-stop

REGRESS_TARGETS+=platform-regress-controller-start-acl.conf
REGRESS_TARGETS+=ctrl-no_substring_match
REGRESS_TARGETS+=ctrl-no_substring_match--
REGRESS_TARGETS+=ctrl-no_substring_matchfoo
REGRESS_TARGETS+=ctrl-no_substring_match--foo

REGRESS_TARGETS+=ctrl-substring_match
REGRESS_TARGETS+=ctrl-substring_match--
REGRESS_TARGETS+=ctrl-substring_matchfoo
REGRESS_TARGETS+=ctrl-substring_match--foo

REGRESS_TARGETS+=ctrl-any_method
REGRESS_TARGETS+=post-ctrl-any_method
REGRESS_TARGETS+=ctrl-only_get
REGRESS_TARGETS+=post-ctrl-only_get


TESTOUT=${TESTDIR}/test.out
PYTHON=$$(command -v python || command -v python3)

cleanup:
	rm -f .template.out .testport

platform-regress-controller-start-%:
	rm -f .testport
	${CMD_PREFIX} regress-controller ${TESTDIR}/$* .testport
	while ! test -s .testport ; do sleep 0.1 ; done ; true

platform-regress-controller-stop:
	curl http://127.0.0.1:$$(cat .testport)/stop

ctrl-%:
	curl -s http://127.0.0.1:$$(cat .testport)/`cat $@.in` > ${TESTOUT}
	match ${TESTOUT} $@.out

post-ctrl-%:
	curl -XPOST -s http://127.0.0.1:$$(cat .testport)/`cat $@.in` > ${TESTOUT}
	match ${TESTOUT} $@.out

hctrl-%:
	curl -s -D ${TESTOUT} http://127.0.0.1:$$(cat .testport)/`cat $@.in`
	match --force-new ${TESTOUT} $@.out

pctrl-%:
	curl -s --data-binary @$@.post http://127.0.0.1:$$(cat .testport)/`cat $@.in` > ${TESTOUT}
	match ${TESTOUT} $@.out

enforce-min-nthreads:
	curl -s http://127.0.0.1:$$(cat .testport)/stats | ${PYTHON} $@.py
