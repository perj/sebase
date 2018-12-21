// Copyright 2018 Schibsted

#include "sbp/etcdclient.h"
#include "sbp/logging.h"
#include "sbp/sd_queue.h"
#include "sbp/create_socket.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define REPLY "HTTP/1.0 200 OK\r\n\r\n" \
"{\"action\":\"get\",\"node\":{\"dir\":true,\"nodes\":[{\"key\":\"/service\",\"dir\":true,\"nodes\":[{\"key\":\"/service/search\",\"dir\":true,\"nodes\":[{\"key\":\"/service/search/asearch\",\"dir\":true,\"nodes\":[{\"key\":\"/service/search/asearch/foo\",\"dir\":true,\"expiration\":\"2015-09-03T15:42:25.907485522Z\",\"ttl\":25,\"nodes\":[{\"key\":\"/service/search/asearch/foo/config\",\"value\":\"*.*.cost=1000\\n*.*.name=::1\\n*.*.port=8081\\nlocal.*.cost=2\\n\",\"modifiedIndex\":4431,\"createdIndex\":4431},{\"key\":\"/service/search/asearch/foo/health\",\"value\":\"up\",\"modifiedIndex\":4371,\"createdIndex\":4371}],\"modifiedIndex\":4369,\"createdIndex\":4369}],\"modifiedIndex\":9,\"createdIndex\":9}],\"modifiedIndex\":5,\"createdIndex\":5}],\"modifiedIndex\":5,\"createdIndex\":5}]}}"

static int
accept_and_maybe_reply(int s, const char *request, bool doreply) {
	int fd = accept(s, NULL, NULL);
	assert(fd >= 0);

	char buf[1024];
	int r = read(fd, buf, sizeof(buf));
	assert(r > 0);

	if(memcmp(buf, request, strlen(request)) != 0) {
		buf[r] = '\0';
		fprintf(stderr, "%s != %s\n", buf, request);
		abort();
	}

	if (!doreply)
		return fd;

	UNUSED_RESULT(write(fd, REPLY, strlen(REPLY)));
	shutdown(fd, SHUT_WR);
	return fd;
}

static void
test_update_delete(void) {
	char *port;
	int s = create_socket_any_port(NULL, &port);

	assert(s >= 0);

	char url[1024];
	snprintf(url, sizeof(url), "http://localhost:%s", port);

	struct etcdwatcher *ec = etcdwatcher_create("/service/", url, NULL);

	struct sd_queue *sdq = etcdwatcher_add_listen(ec, "search/asearch", (const int[]){1, 0}, 2);

	int r = etcdwatcher_start(ec);
	assert(r == 0);

	int fd = accept_and_maybe_reply(s, "GET /v2/keys/service/?recursive=true", true);

	struct sd_value *v = sd_queue_wait(sdq, 3000);
	assert(v);
	assert(v->keyc == 2);
	assert(strcmp(v->keyv[0], "config") == 0);
	assert(strcmp(v->keyv[1], "foo") == 0);

	close(fd);

	struct sd_value *fv = v;
	v = SLIST_NEXT(v, list);
	sd_free_value(fv);

	assert(v->keyc == 2);
	assert(strcmp(v->keyv[0], "health") == 0);
	assert(strcmp(v->keyv[1], "foo") == 0);

	fv = v;
	v = SLIST_NEXT(v, list);
	sd_free_value(fv);

	assert(!v);

	fd = accept(s, NULL, NULL);

	assert(fd >= 0);
#define DELREPLY "HTTP/1.0 200 OK\r\n\r\n" \
"{\"action\":\"delete\",\"node\":{\"key\":\"/service/search/asearch/foo\",\"expiration\":\"2015-09-03T15:42:25.907485522Z\",\"modifiedIndex\":5,\"createdIndex\":5}}"
	UNUSED_RESULT(write(fd, DELREPLY, strlen(DELREPLY)));
	/* Closing here gives curl an ECONNRESET */
	shutdown(fd, SHUT_WR);

	v = sd_queue_wait(sdq, 3000);
	assert(v);
	assert(v->keyc == 2);
	assert(strcmp(v->keyv[0], "delete") == 0);
	assert(strcmp(v->keyv[1], "foo") == 0);

	fv = v;
	v = SLIST_NEXT(v, list);
	sd_free_value(fv);

	assert(!v);

	close(fd);

	etcdwatcher_stop(ec);
	close(s);
	free(port);
}

static void
test_flush(void) {
	char *port;
	int s = create_socket_any_port(NULL, &port);

	assert(s >= 0);

	char url[1024];
	/*
	 * Have to use a ip literal here or a test connect is made.
	 * Not sure why the other test works, possibly due to the
	 * number of connects or somesuch.
	 */
	snprintf(url, sizeof(url), "http://localhost:%s", port);

	struct etcdwatcher *ec = etcdwatcher_create("/service/", url, NULL);

	etcdwatcher_set_flush_period(ec, 1);

	struct sd_queue *sdq = etcdwatcher_add_listen(ec, "search/asearch", NULL, 0);

	int r = etcdwatcher_start(ec);
	assert(r == 0);

	int fd = accept_and_maybe_reply(s, "GET /v2/keys/service/?recursive=true", true);

	/* Discard initial reply. */
	struct sd_value *v = sd_queue_wait(sdq, 3000);
	while (v) {
		struct sd_value *nv = SLIST_NEXT(v, list);
		sd_free_value(v);
		v = nv;
	}

	close(fd);

	/* Accept but don't reply, to wait for a flush. */
	fd = accept_and_maybe_reply(s, "GET /v2/keys/service/?recursive=true&wait=true&waitIndex=4432", false);

	/* Do accept and reply to the flush. */
	int fd2 = accept_and_maybe_reply(s, "GET /v2/keys/service/?recursive=true", true);

	v = sd_queue_wait(sdq, 3000);
	assert(v);
	assert(v->keyc == 1);
	assert(strcmp(v->keyv[0], "flush") == 0);

	while (v) {
		struct sd_value *nv = SLIST_NEXT(v, list);
		sd_free_value(v);
		v = nv;
	}

	close(fd);
	close(fd2);
	etcdwatcher_stop(ec);
}

int
main(int argc, char *argv[]) {
	if (argc > 1)
		log_setup_perror("etcdclient_test", "debug");
	test_update_delete();
	test_flush();
}
