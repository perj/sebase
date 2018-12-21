// Copyright 2018 Schibsted

#include "sbp/bconf.h"
#include "sbp/fd_pool.h"
#include "sbp/logging.h"
#include "sbp/sd_registry.h"
#include "sbp/vtree.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <netdb.h>

int gai_idx;
int (*real_getaddrinfo)(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);

int
getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
	gai_idx++;
	return real_getaddrinfo(node, service, hints, res);
}

int
main(int argc, char *argv[]) {
	real_getaddrinfo = dlsym(RTLD_NEXT, "getaddrinfo");
	assert(real_getaddrinfo != NULL);
	assert(real_getaddrinfo != getaddrinfo);

	log_setup_perror("sd_reload_test", "debug");

	struct bconf_node *root = NULL;
	bconf_add_data(&root, "host.1.name", "::1");
	bconf_add_data(&root, "host.1.port", "1234");
	bconf_add_data(&root, "service", "search/asearch");
	bconf_add_data(&root, "sd.reload_s", "5");
	bconf_add_data(&root, "sd.merge", "1");
	bconf_add_data(&root, "sd.initial_wait_ms", "6000");
	struct vtree_chain vtree;
	bconf_vtree(&vtree, root);

	struct sd_registry *sdr = sd_registry_create("test", "sd_reload_test", NULL);

	struct fd_pool *pool = fd_pool_new(sdr);
	int err = fd_pool_add_config(pool, &vtree, NULL, NULL);
	assert(err == 0);

	/* Need to create a conn to trigger the initial wait. */
	struct fd_pool_conn *conn = fd_pool_new_conn(pool, "search/asearch", NULL, NULL);
	printf("%d\n", gai_idx);
	assert(gai_idx == 2);
	fd_pool_free_conn(conn);

	fd_pool_free(pool);
	sd_registry_free(sdr);
	vtree_free(&vtree);
	bconf_free(&root);
}
