# Copyright 2018 Schibsted

print-tests::
	@echo DEPEND: ${REGRESS_DEPEND}
	@echo TEST: ${REGRESS_TARGETS}
	@echo CLEANUP: ${REGRESS_CLEANUP} cleanup

BUILDPATH?=build
FLAVOR?=dev
TDIR=$(abspath ../../../${BUILDPATH}/${FLAVOR}/regress/sapp)

REGRESS_DEPEND=etcd-start
REGRESS_DEPEND+=plogd-start regress-plog-writer-start
REGRESS_DEPEND+=gencerts sapp-start

REGRESS_TARGETS+=plog-redirect-sapp-test-server-log:http-in:100:$(subst /,--,${TDIR})--log
REGRESS_TARGETS+=run-sapp-tests
REGRESS_TARGETS+=plog-redirect-sapp-test-server-log:http-in:0:none
REGRESS_TARGETS+=check-log

REGRESS_CLEANUP+=sapp-stop
REGRESS_CLEANUP+=plogd-stop regress-plog-writer-stop
REGRESS_CLEANUP+=etcd-stop

SDPORT=sd-port -etcd-url http://localhost:$$(cat .etcd-port)

cleanup:
	rm -f .etcd-port .plog-port .plog-writer-port .etcd.pid .plog.sock .plog-writer.pid .plog-writer-http-port
	rm -f .ca.key.pem .ca.cert.pem .other-ca.key.pem .other-ca.cert.pem

etcd-start:
	rm -rf "${TDIR}/etcd"
	bash -c 'echo "$$((49152 + $$RANDOM % 16384))" > .etcd-port'
	sd_start --timeout 5 --maybe --pidfile .etcd.pid -- etcd --data-dir "${TDIR}/etcd" --listen-client-urls http://localhost:$$(cat .etcd-port) --advertise-client-urls http://localhost:$$(cat .etcd-port) >> ${TDIR}/../../logs/etcd.log 2>&1

etcd-stop:
	test -s .etcd.pid && kill $$(cat .etcd.pid)
	rm -f .etcd.pid

plogd-start:
	bash -c 'echo "$$((49152 + $$RANDOM % 16384))" > .plog-port'
	bash -c 'echo "$$((49152 + $$RANDOM % 16384))" > .plog-writer-port'
	sd_start -- plogd -unix-socket .plog.sock -json localhost:$$(cat .plog-writer-port) -httpd :$$(cat .plog-port)

plogd-stop:
	curl -f http://localhost:$$(cat .plog-port)/stop

# XXX make this sd_start compatible.
regress-plog-writer-start:
	bash -c 'echo "$$((49152 + $$RANDOM % 16384))" > .plog-writer-http-port'
	regress-plog-writer :$$(cat .plog-writer-port) ../../../${BUILDPATH}/${FLAVOR}/logs :$$(cat .plog-writer-http-port) & echo $$! > .plog-writer.pid
	while ! tcpsend -null localhost $$(cat .plog-writer-port); do \
		echo -n . ;\
		sleep 0.5 ;\
	done

regress-plog-writer-stop:
	test -s .plog-writer.pid && kill $$(cat .plog-writer.pid)
	rm -f .plog-writer.pid

# Redirect some events in regress-plog-writer to a file.
# Usage:
#     plog-redirect-<app>:<type>:<number of events to redirect>:<target filename>
#
# <target filename>: Path to file where the redirected events will be
# written. Any slashes in the path should be replaced by "--". If <target
# filename> is "none", then the logs are discarded instead of being redirected.
#
# Examples:
#     plog-redirect-test:regress:1:$(subst /,--,${TDIR})--test
# Redirect at most one event with app=test and type=regress to the file "${TDIR}/test".
#
#     plog-redirect-test:regress:0:none
# Turn off any previous redirection with app=test and type=regress.
plog-redirect-%:
	@[ "$(lastword $(subst :, ,$*))" = "none" ] || rm -f $(subst --,/,$(lastword $(subst :, ,$*)))
	@test "$$(curl -s --write-out "%{http_code}" http://localhost:$$(cat .plog-writer-http-port)/'redirect?app=$(firstword $(subst :, ,$*))&type=$(word 2,$(subst :, ,$*))&n=$(word 3,$(subst :, ,,$*))&target=$(subst --,/,$(lastword $(subst :, ,$*)))' -o /dev/null)" = 200

gencerts:
	./gencacert.sh
	./gencacert.sh .other-ca.key.pem .other-ca.cert.pem

sapp-start:
	-${SDPORT} -retry 10 search/search > /dev/null
	PLOG_SOCKET=.plog.sock sd_start -- sapp-test-server -conf ${TDIR}/conf/default-acl.conf -etcd-url http://localhost:$$(cat .etcd-port) sapp-test-server-default-acl
	PLOG_SOCKET=.plog.sock sd_start -- sapp-test-server -conf ${TDIR}/conf/custom-acl.conf -etcd-url http://localhost:$$(cat .etcd-port) sapp-test-server-custom-acl
	PLOG_SOCKET=.plog.sock sd_start -- sapp-test-server -conf ${TDIR}/conf/log.conf -etcd-url http://localhost:$$(cat .etcd-port) sapp-test-server-log

sapp-stop:
	@echo -e "\\033[1;35mStopping sapp test server\\033[39;0m"
	regress-stopper -etcd-url http://localhost:$$(cat .etcd-port) sapp-test-server-default-acl
	regress-stopper -etcd-url http://localhost:$$(cat .etcd-port) sapp-test-server-custom-acl
	regress-stopper -etcd-url http://localhost:$$(cat .etcd-port) sapp-test-server-log

run-sapp-tests:
	sapp-tests -etcd-url http://localhost:$$(cat .etcd-port) -conf ${TDIR}/conf/sapp-tests.conf
	invalid-cert -etcd-url http://localhost:$$(cat .etcd-port) -conf ${TDIR}/conf/invalid-cert.conf


# Loop for few tries since the log is written async
check-%:
	for i in $$(seq 1 30); do \
		jq --tab 'select(contains({"message": {"uri": "/healthcheck"}}) | not)' ${TDIR}/$* > ${TDIR}/jq.out ; \
		match --force-new ${TDIR}/jq.out $@.out > ${TDIR}/match.out 2>&1 && break ; \
		sleep 0.5 ; \
	done; \
	if [ $$i = 30 ]; then \
		echo "Timed out in $@"; \
		cat ${TDIR}/match.out; \
		exit 1; \
	fi; \
	true
