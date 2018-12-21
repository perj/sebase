// Copyright 2018 Schibsted

#include "sbp/bconf.h"
#include "sbp/create_socket.h"

#include <assert.h>
#include <dlfcn.h>

#include "../lib/fd_pool.c"

int (*real_close)(int);

int expect_close = -1;

int
close(int fd) {
	if (expect_close != -1 && fd == expect_close)
		expect_close = -1;
	return real_close(fd);
}

void setup_fd_pool_test(void) __attribute__((constructor));
void
setup_fd_pool_test(void) {
	real_close = dlsym(RTLD_NEXT, "close");
	assert(real_close != NULL);
	assert(real_close != close);
}

static void
test_fd_pool_update_hosts(void) {
	char *port;
	int s = create_socket_any_port(NULL, &port);
	assert(s >= 0);

	/* 10 retries. */
	struct fd_pool *pool = fd_pool_create_single("test", "127.0.0.1", port, 10, 1000, NULL);

	struct fd_pool_service *srv = fd_pool_find_service(pool, "test");
	assert(srv != NULL);
	assert(srv->sb->sb_nserv == 1);
	assert(srv->sb_gen == 1);

	struct fd_pool_conn *conn = fd_pool_new_conn(pool, "test", NULL, NULL);

	int cfd = fd_pool_get(conn, SBCS_START, NULL, NULL);
	assert(cfd >= 0);
	assert(conn->sb_gen == 1);

	/* Check that put works without closing the fd */
	fd_pool_put(conn, cfd);
	assert(fcntl(cfd, F_GETFD) != -1);
	int nfd = fd_pool_get(conn, SBCS_START, NULL, NULL);
	assert(nfd == cfd);

	struct fd_pool_conn *conn2 = fd_pool_new_conn(pool, "test", NULL, NULL);

	struct sbalance *sb1 = srv->sb;

	struct bconf_node *root = NULL;
	bconf_add_data(&root, "host.1.name", "::1");
	bconf_add_data(&root, "host.1.port", port);
	struct vtree_chain node = {0};
	fd_pool_update_hosts(pool, "test", bconf_vtree(&node, root));

	struct sbalance *sb2 = srv->sb;

	/* Conn is still using old sb. */
	assert(conn->sb_gen == 1);
	assert(srv->sb_gen == 2);
	assert(sb1->sb_refs == 1);
	assert(sb2->sb_refs == 1);

	fd_pool_put(conn, cfd);

#ifdef __OpenBSD__
	(void)conn2;
	// These tests depend on IPV6_V6ONLY being unset, OpenBSD forces it on.
#else
	int cfd2 = fd_pool_get(conn2, SBCS_START, NULL, NULL);
	assert(cfd2 >= 0);
	assert(conn2->sb_gen == 2);
	assert(sb2->sb_refs == 2);

	/* Make sure we didn't reuse cfd, since it's not the same address. */
	assert(cfd2 != cfd);

	/* Running get again should update the sb, which should close the old one and
	 * the out of date fd.
	 * Can't check with fcntl as same fd number might be reused.
	 */
	expect_close = cfd;

	cfd = fd_pool_get(conn, SBCS_TEMPFAIL, NULL, NULL);
	/* Conn now using new sb. */
	assert(cfd >= 0);
	assert(conn->sb_gen == 2);
	assert(sb2->sb_refs == 3);
	assert(expect_close == -1);

	fd_pool_update_hosts(pool, "test", bconf_vtree(&node, root));
	assert(srv->sb_gen == 3);
	assert(sb2->sb_refs == 2);

	/* Test last generation put works. */
	struct fd_pool_port *cp = conn->port;
	fd_pool_put(conn, cfd);
	assert(!SLIST_EMPTY(&cp->entries));
	cfd = fd_pool_get(conn, SBCS_START, NULL, NULL);
	assert(sb2->sb_refs == 1);

	fd_pool_update_hosts(pool, "test", bconf_vtree(&node, root));
	assert(srv->sb_gen == 4);

	/* Test two generations ago put works.
	 * It used to be that nodes were deleted early, but with reference counted nodes this
	 * should work.
	 */
	struct fd_pool_port *cp2 = conn2->port;
	fd_pool_put(conn2, cfd2);
	assert(!SLIST_EMPTY(&cp2->entries));
	assert(cp2 == cp);

	fd_pool_free_conn(conn);
	fd_pool_free_conn(conn2);
	vtree_free(&node);
	bconf_free(&root);
	fd_pool_free(pool);
	close(cfd);
	close(s);
	free(port);
#endif
}

static void
test_new_conn_new_service(void) {
	struct fd_pool *pool = fd_pool_new(NULL);

	struct fd_pool_conn *conn = fd_pool_new_conn(pool, "test", "port", NULL);
	assert(conn->srv == NULL);

	int fd = fd_pool_get(conn, SBCS_START, NULL, NULL);
	assert(fd == -1);
	assert(errno == ENOENT);

	fd_pool_free_conn(conn);
	fd_pool_free(pool);

	struct sd_registry *sdr = sd_registry_create("local", "fd_pool_test", NULL);
	pool = fd_pool_new(sdr);

	conn = fd_pool_new_conn(pool, "test", "port", NULL);
	assert(conn->srv != NULL);

	fd = fd_pool_get(conn, SBCS_START, NULL, NULL);
	assert(fd == -1);
	assert(errno == EAGAIN);

	fd_pool_free_conn(conn);
	fd_pool_free(pool);
}

static void
test_host_to_service(void) {
	const char *domain = "example.com";
	ssize_t dlen = strlen(domain);

	char *host = fd_pool_host_to_service("mysearch.search.example.com", -1, domain, dlen);
	assert(strcmp(host, "search/mysearch") == 0);
	free(host);

	host = fd_pool_host_to_service("mysearch.search.example.com", -1, domain, -1);
	assert(strcmp(host, "search/mysearch") == 0);
	free(host);

	host = fd_pool_host_to_service("search.example.com", -1, domain, dlen);
	assert(strcmp(host, "search") == 0);
	free(host);

	host = fd_pool_host_to_service("search.fakeexample.com", -1, domain, dlen);
	assert(host == NULL);

	// Not sure if this should be allowed, but it is for now.
	host = fd_pool_host_to_service("mysearch..search..example.com", -1, domain, dlen);
	assert(strcmp(host, "search/mysearch") == 0);
	free(host);
}

int
main(int argc, char *argv[]) {
	test_fd_pool_update_hosts();
	test_new_conn_new_service();
	test_host_to_service();
}
