# Copyright 2018 Schibsted

# Controllers only work on linux for now, needs epoll and eventfd.
ifeq ($(findstring linux,${MAKE_HOST}),linux)
print-tests:
	@echo TEST: test

test:
	regress-http-fd-pool
else
print-tests:
endif
