// Copyright 2018 Schibsted

#include <dlfcn.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include <netdb.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#include "sbp/bconf.h"
#include "sbp/bconfig.h"
#include "sbp/error_functions.h"
#include "sbp/fd_pool.h"
#include "sbp/logging.h"
#include "sbp/string_functions.h"
#include "sbp/vtree.h"
#include "sbp/sock_util.h"

#undef writev
/*
	Redefine writev() with a version that selects a random number of bytes to write,
	forcing the calling client code to deal with incomplete writes.
*/
ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
	static int call = 0;
	static ssize_t (*old_writev)(int fd, const struct iovec *iov, int iovcnt) = NULL;

	if (!call++) {
		old_writev = (ssize_t(*)(int, const struct iovec*, int))(dlsym(RTLD_NEXT, "writev"));
		srandom(time(NULL));
	}

	ssize_t n_total = get_iovlen_sum(iov, iovcnt);

	int wlen = random() % (n_total+1);
	int written = wlen;

	for (int i=0 ; i < iovcnt ; ++i) {
		if (wlen <= (int)iov[i].iov_len) {
			old_writev(fd, iov, i);
			struct iovec rest = iov[i];
			rest.iov_len = wlen;
			old_writev(fd, &rest, 1);
			break;
		}
		wlen -= iov[i].iov_len;
	}
	return written;
}

static struct fd_pool *
get_pool(struct bconf_node *config, const char *path, const struct addrinfo *hints) {
	struct vtree_chain node = {};
	struct vtree_chain *pn;

	pn = bconf_vtree(&node, bconf_get(config, path));
	assert(pn);

	struct fd_pool *pool = fd_pool_create("test", &node, hints);

	assert(pool);

	vtree_free(&node);

	return pool;
}


static pid_t
fork_server(const char *port, int socktype, int echo, char *portbuf, size_t portbufsz) {
	const struct addrinfo hints = { .ai_flags = AI_ADDRCONFIG | AI_PASSIVE, .ai_socktype = socktype };
	struct addrinfo *res, *curr;
	int err;
	pid_t server_pid;
	int eventpipe[2];
	char buf[256] = { 0 };
	ssize_t n;

	err = pipe(eventpipe);
	assert(!err);

	if ((err = getaddrinfo("127.0.0.1", port, &hints, &res)))
		xerrx(1, "getaddrinfo: %s", gai_strerror(err));

	switch ((server_pid = fork())) {
	case -1:
		xerr(1, "fork");
	case 0:
		break;
	default:
		close(eventpipe[1]);
		n = read(eventpipe[0], portbuf, portbufsz);
		if (n < 0)
			xwarn("read");
		close(eventpipe[0]);
		freeaddrinfo(res);
		res = NULL;
		return server_pid;
	}

	close(eventpipe[0]);

	for (curr = res ; curr ; curr = curr->ai_next) {
		int s = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol);
		if (s < 0)
			continue;

		if (bind(s, curr->ai_addr, curr->ai_addrlen)) {
			close(s);
			continue;
		}

		char lportbuf[NI_MAXSERV + 1];
		int r = get_local_port(s, lportbuf, sizeof(lportbuf), NI_NUMERICSERV);
		if (r)
			abort();

		if (socktype == SOCK_DGRAM) {
			write(eventpipe[1], lportbuf, strlen(lportbuf));
			close(eventpipe[1]);
			n = recv(s, buf, sizeof(buf), 0);
			if (echo)
				printf("DGRAM>>> '%.*s' (%zd)\n", (int)n, buf, n);
			exit(n > 0 ? 0 : 1);
		} else if (socktype == SOCK_STREAM) {
			if (listen(s, 128) == -1)
				continue;

			write(eventpipe[1], lportbuf, strlen(lportbuf));
			close(eventpipe[1]);

			struct sockaddr_un peer_addr;
			socklen_t peer_addr_size = sizeof(struct sockaddr_un);
			int fd = accept(s, (struct sockaddr *)&peer_addr, &peer_addr_size);

			while ((n = read(fd, buf, sizeof(buf))) > 0) {
				if (echo)
					printf("STREAM>> '%.*s' (%zd)\n", (int)n, buf, n);
			}
			close(fd);
			exit(n >= 0 ? 0 : 1);
		} else {
			xerrx(1, "Unknown socktype");
		}
	}
	xerrx(1, "failed to listen");
}

static void
wait_server(pid_t server) {
	int status;
	pid_t p = waitpid(server, &status, WNOHANG);
	int i;

	for (i = 0; p <= 0 && i < 5 ; i++) {
		sleep(1);
		p = waitpid(server, &status, WNOHANG);
	}

	if (p <= 0) {
		kill(server, SIGTERM);
		p = waitpid(server, &status, 0);
	}

	if (status) {
		char buf[256];
		xerrx(1, "Server exited with error: %s", strwait(status, buf, sizeof(buf)));
	}
}

static void
test_udp(struct bconf_node *config) {
	const struct addrinfo hints = { .ai_flags = AI_ADDRCONFIG, .ai_socktype = SOCK_DGRAM };
	char portbuf[NI_MAXSERV] = {0};

	pid_t server = fork_server(
		bconf_get_string(config, "test_udp.host.1.port"),
		SOCK_DGRAM,
		bconf_get_int_default(config, "test_udp.echo", 0),
		portbuf,
		sizeof(portbuf)
	);

	bconf_add_data(&config, "test_udp.host.1.port", portbuf);

	struct fd_pool *pool = get_pool(config, "test_udp", &hints);
	struct fd_pool_conn *conn = fd_pool_new_conn(pool, "test", "port", NULL);

	assert(conn);

	int fd = fd_pool_get(conn, SBCS_START, NULL, NULL);

	assert(fd >= 0);

	ssize_t n = write(fd, "ok", 2);

	assert(n == 2);

	fd_pool_put(conn, fd);
	fd_pool_free_conn(conn);
	fd_pool_free(pool);

	wait_server(server);
}

static void
test_fd_pool_conn_writev(struct bconf_node *config) {
	const struct addrinfo hints = { .ai_flags = AI_ADDRCONFIG, .ai_socktype = SOCK_STREAM };
	char portbuf[NI_MAXSERV] = {0};

	pid_t server = fork_server(
		bconf_get_string(config, "test_conn_writev.host.2.port"),
		SOCK_STREAM,
		bconf_get_int_default(config, "test_conn_writev.echo", 0),
		portbuf,
		sizeof(portbuf)
	);

	bconf_add_data(&config, "test_conn_writev.host.2.port", portbuf);

	struct fd_pool *pool = get_pool(config, "test_conn_writev", &hints);

	struct fd_pool_conn *conn = fd_pool_new_conn(pool, "test", "port", NULL);

	assert(conn);

	struct iovec iov[4];
	int niov = 0;

	iov[niov].iov_base = (char *)"1111111111";
	iov[niov].iov_len = 10;
	++niov;
	iov[niov].iov_base = (char *)"ABCDEFGHIJ";
	iov[niov].iov_len = 10;
	++niov;
	iov[niov].iov_base = (char *)"2222222222";
	iov[niov].iov_len = 10;
	++niov;
	iov[niov].iov_base = (char *)"0123456789";
	iov[niov].iov_len = 10;
	++niov;

	int fd = -1;
	int status = fd_pool_conn_writev(conn, &fd, iov, niov, -1);

	assert(status == 0);

	fd_pool_put(conn, fd);
	fd_pool_free_conn(conn);
	fd_pool_free(pool);

	wait_server(server);
}

int
main(int argc, char *argv[]) {
	char *config_file = argv[1];

	log_setup_perror("fd_pool_test", "debug");

	assert(config_file);

	struct bconf_node *config = config_init(config_file);

	test_udp(config);

	test_fd_pool_conn_writev(config);

	bconf_free(&config);
}
