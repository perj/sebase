# Copyright 2018 Schibsted

# Controllers only work on linux for now, needs epoll and eventfd.
ifeq ($(findstring linux,${MAKE_HOST}),linux)
print-tests:
	@echo DEPEND: start-server
	@echo TEST: get_discard get_keep post post_silly_headers
	@echo TEST: post_custom_headers put_string put_binary_blob delete move
	@echo TEST: copy get_keep_header_discard_body https_get_discard
	@echo CLEANUP: stop-server
	@echo CLEANUP: cleanup

cleanup:
	rm -f .test.out .testport

start-server:
	rm -f .testport
	go run server.go .testport &
	bash -c 'for i in {1..20}; do test -s .testport && break; sleep 0.5; done; test $$i != 20'

stop-server:
	curl http://localhost:$$(cat .testport)/stop

get_discard:
	testhttp get_discard http://localhost:$$(cat .testport)/ok > .test.out
	match .test.out $@.out

get_keep_header_discard_body:
	testhttp get_headers_discard_body http://localhost:$$(cat .testport)/ok > .test.out
	match .test.out $@.out

get_keep:
	testhttp get_keep http://localhost:$$(cat .testport)/ok > .test.out
	match .test.out $@.out

post:
	testhttp post http://localhost:$$(cat .testport)/post > .test.out
	match .test.out $@.out

post_silly_headers:
	testhttp post_silly_headers http://localhost:$$(cat .testport)/post > .test.out
	match .test.out $@.out

post_custom_headers:
	testhttp post_custom_headers http://localhost:$$(cat .testport)/post > .test.out
	match .test.out $@.out

put_string:
	testhttp put_string http://localhost:$$(cat .testport)/upload > .test.out
	match .test.out $@.out

put_binary_blob:
	testhttp put_binary_blob http://localhost:$$(cat .testport)/upload > .test.out
	match .test.out $@.out
	curl -f http://localhost:$$(cat .testport)/cmp-upload

delete:
	testhttp delete http://localhost:$$(cat .testport)/upload > .test.out
	match .test.out $@.out

move:
	testhttp move http://localhost:$$(cat .testport)/upload > .test.out
	match .test.out $@.out

copy:
	testhttp copy http://localhost:$$(cat .testport)/upload > .test.out
	match .test.out $@.out

https_get_discard:
	bash -c 'export REGRESS_HTTPS_PORT=$$((49152 + $$RANDOM % 16384)) ; ${CMD_PREFIX} testhttp get_discard https://localhost:$$REGRESS_HTTPS_PORT/stats > .test.out'
	match .test.out $@.out
else
print-tests:
endif
