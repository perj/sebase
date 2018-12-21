// Copyright 2018 Schibsted

#include "sbp/bconf.h"
#include "sbp/logging.h"
#include "sbp/sd_registry.h"
#include "sbp/create_socket.h"

#include <assert.h>
#include <curl/curl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void
healthcheck(int sh) {
	int fd = accept(sh, NULL, NULL);
	assert(fd >= 0);
	char buf[1024];
	int r = read(fd, buf, sizeof(buf));
	assert(r > 0);

#define HC_REQUEST "GET /healthcheck"
#define HC_REPLY "HTTP/1.0 200 Ok\r\n\r\n"
	assert(memcmp(buf, HC_REQUEST, strlen(HC_REQUEST)) == 0);
	buf[r] = '\0';

	UNUSED_RESULT(write(fd, HC_REPLY, strlen(HC_REPLY)));
	close(fd);
}

static void
healthcheck_unavail(int sh) {
	int fd = accept(sh, NULL, NULL);
	assert(fd >= 0);
	char buf[1024];
	int r = read(fd, buf, sizeof(buf));
	assert(r > 0);

#define HCU_REPLY "HTTP/1.0 503 Service Unavailable\r\n\r\n"
	assert(memcmp(buf, HC_REQUEST, strlen(HC_REQUEST)) == 0);
	buf[r] = '\0';

	UNUSED_RESULT(write(fd, HCU_REPLY, strlen(HCU_REPLY)));
	close(fd);
}

static void
hostkey_update(int se, int rcode) {
	int fd = accept(se, NULL, NULL);
	assert(fd >= 0);
	char buf[1024];
	int r = read(fd, buf, sizeof(buf));
	assert(r > 0);

#define HOSTKEY_REQUEST "PUT /v2/keys/service/search/asearch/foo"
#define HOSTKEY_REPLY_200 "HTTP/1.0 200 Ok\r\n\r\n"
#define HOSTKEY_REPLY_201 "HTTP/1.0 201 Created\r\n\r\n"
#define HOSTKEY_REPLY_404 "HTTP/1.0 404 Not Found\r\n\r\n"
	assert(memcmp(buf, HOSTKEY_REQUEST, strlen(HOSTKEY_REQUEST)) == 0);
	buf[r] = '\0';

	if (rcode == 201) {
		assert(strstr(buf, "dir=true&ttl=30") != NULL);
	} else {
		assert(strstr(buf, "dir=true&ttl=30&prevExist=true&refresh=true") != NULL);
	}

	switch (rcode) {
	case 200:
		UNUSED_RESULT(write(fd, HOSTKEY_REPLY_200, strlen(HOSTKEY_REPLY_200)));
		break;
	case 201:
		UNUSED_RESULT(write(fd, HOSTKEY_REPLY_201, strlen(HOSTKEY_REPLY_201)));
		break;
	case 404:
		UNUSED_RESULT(write(fd, HOSTKEY_REPLY_404, strlen(HOSTKEY_REPLY_404)));
		break;
	default:
		abort();
	}
	close(fd);
}

static void
config_update(int se, bool withpe) {
	int fd = accept(se, NULL, NULL);
	assert(fd >= 0);
	char buf[1024];
	int r = read(fd, buf, sizeof(buf));
	assert(r > 0);

#define CONFIG_REQUEST "PUT /v2/keys/service/search/asearch/foo/config"
#define CONFIG_REPLY "HTTP/1.0 200 Ok\r\n\r\n"
	assert(memcmp(buf, CONFIG_REQUEST, strlen(CONFIG_REQUEST)) == 0);
	buf[r] = '\0';

	/* XXX depending on vtree_json style here, it seems to add spaces after colon. */
	assert(strstr(buf, "value={\"*\":%20{\"*\":%20{\"cost\":%20\"100\",\"name\":%20\"::1\",\"port\":%20\"1234\"}}}") != NULL ||
		strstr(buf, "value={\"*\":%20{\"*\":%20{\"cost\":%20\"100\",\"name\":%20\"127.0.0.1\",\"port\":%20\"1234\"}}}") != NULL);
	assert(!!strstr(buf, "prevExist=false") == withpe);

	UNUSED_RESULT(write(fd, CONFIG_REPLY, strlen(CONFIG_REPLY)));
	close(fd);
}

static void
health_get(int se) {
	int fd = accept(se, NULL, NULL);
	assert(fd >= 0);
	char buf[1024];
	int r = read(fd, buf, sizeof(buf));
	assert(r > 0);

#define HEALTH_GET_REQUEST "GET /v2/keys/service/search/asearch/foo/health"
#define HEALTH_GET_REPLY "HTTP/1.0 200 Ok\r\n\r\n{\"node\":{\"value\":\"up\"}}"
	assert(memcmp(buf, HEALTH_GET_REQUEST, strlen(HEALTH_GET_REQUEST)) == 0);
	buf[r] = '\0';

	UNUSED_RESULT(write(fd, HEALTH_GET_REPLY, strlen(HEALTH_GET_REPLY)));
	close(fd);
}

static void
health_update(int se, bool up) {
	int fd = accept(se, NULL, NULL);
	assert(fd >= 0);
	char buf[1024];
	int r = read(fd, buf, sizeof(buf));
	assert(r > 0);

#define HEALTH_REQUEST "PUT /v2/keys/service/search/asearch/foo/health"
#define HEALTH_REPLY "HTTP/1.0 200 Ok\r\n\r\n"
	assert(memcmp(buf, HEALTH_REQUEST, strlen(HEALTH_REQUEST)) == 0);
	buf[r] = '\0';

	if (up) {
		assert(strstr(buf, "value=up") != NULL);
	} else {
		assert(strstr(buf, "value=down") != NULL);
	}

	UNUSED_RESULT(write(fd, HEALTH_REPLY, strlen(HEALTH_REPLY)));
	close(fd);
}

int
main(int argc, char *argv[]) {
	log_setup_perror("sd_bos_test", "debug");

	char *porth = NULL;
	int sh = create_socket_any_port(NULL, &porth);
	assert(sh >= 0);
	char *porte = NULL;
	int se = create_socket_any_port(NULL, &porte);
	assert(se >= 0);

	struct bconf_node *root = NULL;
	bconf_add_data(&root, "sd.service", "search/asearch");

	char url[1024];
	snprintf(url, sizeof(url), "http://localhost:%s/healthcheck", porth);
	bconf_add_data(&root, "sd.healthcheck.url", url);
	bconf_add_data(&root, "sd.healthcheck.interval_s", "1");
	bconf_add_data(&root, "sd.healthcheck.unavailble_interval_ms", "100");

	snprintf(url, sizeof(url), "http://localhost:%s", porte);
	bconf_add_data(&root, "sd.etcd_url", url);

	bconf_add_data(&root, "sd.host.key.value", "foo");

	bconf_add_data(&root, "sd.value.*.*.name", "127.0.0.1");
	bconf_add_data(&root, "sd.value.*.*.port", "1234");
	bconf_add_data(&root, "sd.value.*.*.cost", "100");

	sd_registry_setup_bos_client(root, NULL);

	bconf_free(&root);

	bos_here();

	hostkey_update(se, 404);
	hostkey_update(se, 201);
	config_update(se, false);
	health_update(se, false);

	healthcheck(sh);
	hostkey_update(se, 200);
	config_update(se, true);
	health_update(se, true);

	healthcheck(sh);
	hostkey_update(se, 200);
	config_update(se, true);
	health_get(se);

	healthcheck_unavail(sh);
	healthcheck_unavail(sh);
	/* Check that update was not called yet. */
	struct pollfd pfd = {se, POLLIN};
	int n = poll(&pfd, 1, 200);
	assert(n == 0);
	healthcheck_unavail(sh);
	hostkey_update(se, 200);
	config_update(se, true);
	health_update(se, false);

	close(sh);
	free(porth);
	close(se);
	free(porte);
}
