// Copyright 2018 Schibsted

#include "../lib/fd_pool_sd.c"
#include "../lib/fd_pool.c"

#include <assert.h>

int
main(int argc, char *argv[]) {
	log_setup_perror("fd_pool_sd_test", "debug");

	struct fd_pool *pool = fd_pool_create_single("test-sd", "127.0.0.1", "8080", 1, 1000, NULL);
	struct fd_pool_service *srv = fd_pool_find_service(pool, "test-sd");

	struct sd_queue queue;
	int r = sd_queue_init(&queue);
	assert(r == 0);

	struct fd_pool_sd *fps = fd_pool_sd_create(pool, "local", "fd_pool_sd_test", "test-sd", &queue);

	struct sbalance *orig_sb = srv->sb;

	fd_pool_sd_start(fps);

	int state;
	sd_queue_begin(&queue, &state);
	struct sd_value *v = sd_create_value(1, 2, (const char *[]){"config", "foo"}, NULL, "{\"*\":{\"*\":{\"cost\":1000,\"name\":\"::1\",\"port\":\"8081\"}},\"local\":{\"*\":{\"cost\":2}}}", -1);
	sd_queue_insert(&queue, v);
	sd_queue_commit(&queue, &state);

	/* This should timeout due to no nodes fully configured yet. */
	r = fd_pool_sd_wait_index(fps, 1, 500);
	assert(r == 1);

	sd_queue_begin(&queue, &state);
	v = sd_create_value(2, 2, (const char *[]){"health", "foo"}, NULL, "up", -1);
	sd_queue_insert(&queue, v);
	sd_queue_commit(&queue, &state);

	time_t t = time(NULL);
	pthread_rwlock_rdlock(&srv->sblock);
	while (srv->sb == orig_sb) {
		pthread_rwlock_unlock(&srv->sblock);
		if (time(NULL) > t + 3)
			assert(false && "timed out waiting for fd_pool to update");
		nanosleep(&(const struct timespec){ .tv_nsec = 500 * 1000 * 1000 }, NULL);
		pthread_rwlock_rdlock(&srv->sblock);
	}
	pthread_rwlock_unlock(&srv->sblock);

	assert(srv->sb->sb_nserv == 1);

	struct fd_pool_service_node *sn = srv->sb->sb_service[0].data;
	assert(sn->node->nports == 1);
	struct fd_pool_port *port = &sn->node->ports[0];
	char host[NI_MAXHOST], serv[NI_MAXSERV];
	r = getnameinfo((struct sockaddr*)&port->sockaddr, port->addrlen, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST|NI_NUMERICSERV);
	assert(r == 0);
	assert(strcmp(host, "::1") == 0);
	assert(strcmp(serv, "8081") == 0);

	assert(srv->sb->sb_service[0].cost == 2);

	/* Toggle health first down then up. */
	sd_queue_begin(&queue, &state);
	v = sd_create_value(3, 2, (const char *[]){"health", "foo"}, NULL, "down", -1);
	sd_queue_insert(&queue, v);
	sd_queue_commit(&queue, &state);

	r = fd_pool_sd_wait_index(fps, 3, 3000);
	assert(r == 0);
	assert(bconf_get_int(*fps->confroot, "host.foo.disabled") == 1);

	sd_queue_begin(&queue, &state);
	v = sd_create_value(4, 2, (const char *[]){"health", "foo"}, NULL, "up", -1);
	sd_queue_insert(&queue, v);
	sd_queue_commit(&queue, &state);

	r = fd_pool_sd_wait_index(fps, 4, 3000);
	assert(r == 0);
	assert(bconf_get_int(*fps->confroot, "host.foo.disabled") == 0);


	/* Delete */
	sd_queue_begin(&queue, &state);
	v = sd_create_value(5, 2, (const char *[]){"delete", "foo"}, NULL, "", 0);
	sd_queue_insert(&queue, v);
	sd_queue_commit(&queue, &state);

	r = fd_pool_sd_wait_index(fps, 5, 3000);
	assert(r == 0);

	assert(bconf_count(bconf_get(*fps->confroot, "host")) == 0);

	fd_pool_sd_stop(fps);

	fd_pool_sd_free(fps);
	sd_queue_destroy(&queue);
	fd_pool_free(pool);
}
