// Copyright 2018 Schibsted

#include <assert.h>
#include <dlfcn.h>
#include <netdb.h>
#include <stdlib.h>

int (*real_getaddrinfo)(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);

int
getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
	if (!hints)
		return real_getaddrinfo(node, service, hints, res);
	struct addrinfo h = *hints;
	h.ai_flags &= ~AI_ADDRCONFIG;
	return real_getaddrinfo(node, service, &h, res);
}

void setup_noaddrconfig(void) __attribute__((constructor));
void
setup_noaddrconfig(void) {
	real_getaddrinfo = dlsym(RTLD_NEXT, "getaddrinfo");
	assert(real_getaddrinfo != NULL);
	assert(real_getaddrinfo != getaddrinfo);
}
