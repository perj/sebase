// Copyright 2018 Schibsted

#include "../lib/fd_pool.c"

#include "sbp/bconf.h"

#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

struct addrinfo test_addrinfo4 = {
	.ai_family = AF_INET,
	.ai_socktype = SOCK_STREAM,
	.ai_protocol = IPPROTO_TCP,
	.ai_addrlen = sizeof(struct sockaddr_in),
};

struct sockaddr_in test_addr4 = {
	.sin_family = AF_INET,
};

struct addrinfo test_addrinfo6 = {
	.ai_family = AF_INET6,
	.ai_socktype = SOCK_STREAM,
	.ai_protocol = IPPROTO_TCP,
	.ai_addrlen = sizeof(struct sockaddr_in6),
};

struct sockaddr_in6 test_addr6 = {
	.sin6_family = AF_INET6
};

int
getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
	assert(strcmp(node, "localhost") == 0);
	assert(strcmp(service, "1234") == 0);
	test_addr4.sin_port = htons(1234);
	test_addr4.sin_addr.s_addr = htonl(0x7f000001);
	test_addrinfo4.ai_addr = (struct sockaddr*)&test_addr4;
	test_addr6.sin6_port = htons(1234);
	test_addr6.sin6_addr.s6_addr[15] = 1;
	test_addrinfo6.ai_addr = (struct sockaddr*)&test_addr6;
	test_addrinfo4.ai_next = &test_addrinfo6;
	*res = &test_addrinfo4;
	return 0;
}

void
freeaddrinfo(struct addrinfo *res) {
}

int
main(int argc, char *argv[]) {
	struct bconf_node *root = NULL;
	bconf_add_data(&root, "host.1.name", "localhost");
	bconf_add_data(&root, "host.1.port", "1234");
	bconf_add_data(&root, "host.1.test_port", "1234");
	struct vtree_chain node = {0};
	struct fd_pool *pool = fd_pool_create("test", bconf_vtree(&node, root), NULL);

	struct fd_pool_service *srv = fd_pool_find_service(pool, "test");
	assert(srv != NULL);
	assert(srv->sb->sb_nserv == 2);

	struct fd_pool_service_node *sn = srv->sb->sb_service[0].data;
	assert(sn->node->nports == 2);
	sn = srv->sb->sb_service[1].data;
	assert(sn->node->nports == 2);
}
