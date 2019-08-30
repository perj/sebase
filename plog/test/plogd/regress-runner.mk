# Copyright 2018 Schibsted

print-tests:
	@echo DEPEND: plogd-start
# There was an annoying bug where it wouldn't detect regress-plog-reader
# shutting down, and blackhole second write.  Try again to check for this
# regression.
	@echo TEST: log-json-foo-bar log-json-foo-bar
	@echo TEST: log-srcjson-list
	@echo CLEANUP: plogd-stop
	@echo CLEANUP: cleanup

cleanup:
	rm -f .json.out plog.sock plog-packet.sock

plogd-start:
	sd_start -- plogd -unix-socket plog.sock -json 127.0.0.1:33333 -httpd :8180

plogd-stop:
	curl http://localhost:8180/stop

log-json-%:
	PLOG_SOCKET=plog.sock plogger --appname test --type INFO < $@.in
	go run json_reader.go 33333 1 > .json.out
	match .json.out $@.out

log-srcjson-%:
	PLOG_SOCKET=plog.sock plogger --appname test --type INFO -json < $@.in
	go run json_reader.go 33333 1 > .json.out
	match .json.out $@.out
