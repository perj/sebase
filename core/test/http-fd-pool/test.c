// Copyright 2018 Schibsted

#include "sbp/controller.h"
#include "sbp/bconf.h"
#include "sbp/buf_string.h"
#include "sbp/fd_pool.h"
#include "sbp/http.h"
#include "sbp/http_fd_pool.h"
#include "sbp/logging.h"
#include "sbp/create_socket.h"
#include "sbp/tls.h"

#include <assert.h>
#include <dlfcn.h>
#include <unistd.h>

int (*real_close)(int);

int expect_close = -1;

int
close(int fd) {
	if (expect_close != -1 && fd == expect_close)
		expect_close = -1;
	return real_close(fd);
}

void setup_test(void) __attribute__((constructor));
void
setup_test(void) {
	real_close = dlsym(RTLD_NEXT, "close");
	assert(real_close != NULL);
	assert(real_close != close);
}


static void
test_http(void) {
	struct ctrl_handler handlers[] = {
		CONTROLLER_DEFAULT_HANDLERS,
	};

	char *port;
	int lsock = create_socket_any_port(NULL, &port);
	assert(lsock >= 0);

	struct bconf_node *conf = NULL;
	bconf_add_data(&conf, "https", "0");

	struct ctrl *ctrl = ctrl_setup(conf, handlers, NHANDLERS(handlers), lsock, NULL);
	assert(ctrl);

	struct http_fd_pool_ctx *ctx = http_fd_pool_create_context(NULL);
	assert(ctx);
	struct fd_pool *pool = fd_pool_create_single("test-http", "localhost", port, 1, 5000, NULL);
	assert(pool);

	struct http_fd_pool_conn conn = {
		.fdc = fd_pool_new_conn(pool, "test-http", "port", NULL),
	};
	struct buf_string req = {0};
	bscat(&req, "GET /stats HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"\r\n");
	while (1) {
		if (http_fd_pool_connect(ctx, &conn, &(struct iovec){req.buf, req.pos}, 1, req.pos) != 0)
			abort();
		struct http_fd_pool_response rsp = {};
		http_fd_pool_parse(&conn, &rsp);
		if (rsp.status_code == 200) {
			http_fd_pool_cleanup(ctx, &conn, rsp.keepalive);
			break;
		}
	}

	fd_pool_free(pool);
	ctrl_quit(ctrl);
	bconf_free(&conf);
	close(lsock);
	http_fd_pool_free_context(ctx);
	free(port);
	free(req.buf);
}

static void
test_https(void) {
	struct ctrl_handler handlers[] = {
		CONTROLLER_DEFAULT_HANDLERS,
	};

	char *port;
	int lsock = create_socket_any_port(NULL, &port);
	assert(lsock >= 0);

	struct https_state https = {};
	http_setup_https(&https, "./gencert.sh", NULL, NULL, HTTP_USE_CACERT);

	struct ctrl *ctrl = ctrl_setup(NULL, handlers, NHANDLERS(handlers), lsock, &https);
	assert(ctrl);

	struct http_fd_pool_ctx *ctx = http_fd_pool_create_context(&https);
	assert(ctx);
	struct fd_pool *pool = fd_pool_create_single("test-http", "localhost", port, 1, 5000, NULL);
	assert(pool);

	struct http_fd_pool_conn conn = {
		.fdc = fd_pool_new_conn(pool, "test-http", "port", NULL),
	};
	struct buf_string req = {0};
	bscat(&req, "GET /stats HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"Connection: close\r\n" // For testing session re-use even on connection close.
			"\r\n");
	//char first_sess[1024], second_sess[1024];
	while (1) {
		if (http_fd_pool_connect(ctx, &conn, &(struct iovec){req.buf, req.pos}, 1, req.pos) != 0)
			abort();
		assert(conn.tls);
		struct http_fd_pool_response rsp = {};
		http_fd_pool_parse(&conn, &rsp);
		if (rsp.status_code == 200) {
			//tls_get_session_id(conn.tls, first_sess, sizeof(first_sess));
			// Make sure fd is closed
			expect_close = conn.fd;
			http_fd_pool_cleanup(ctx, &conn, rsp.keepalive);
			assert(expect_close == -1);
			break;
		}
	}

	// Check session re-use.
	req.pos = 0;
	bscat(&req, "GET /stats HTTP/1.1\r\n"
			"Host: localhost\r\n"
			"\r\n");
	conn.fdc = fd_pool_new_conn(pool, "test-http", "port", NULL);
	while (1) {
		if (http_fd_pool_connect(ctx, &conn, &(struct iovec){req.buf, req.pos}, 1, req.pos) != 0)
			abort();
		assert(conn.tls);
		struct http_fd_pool_response rsp = {};
		http_fd_pool_parse(&conn, &rsp);
		if (rsp.status_code == 200) {
			//tls_get_session_id(conn.tls, second_sess, sizeof(second_sess));
			// For now, expect connection to be closed
			expect_close = conn.fd;
			http_fd_pool_cleanup(ctx, &conn, rsp.keepalive);
#ifdef NOT_YET_IMPLEMENTED
			assert(expect_close != -1);
#endif
			break;
		}
	}

	//assert(strcmp(first_sess, second_sess) == 0);

#ifdef NOT_YET_IMPLEMENETED
	// Check keep-alive connection.
	conn.fdc = fd_pool_new_conn(pool, "test-http", "port", NULL);
	while (1) {
		if (http_fd_pool_connect(ctx, &conn, req.buf, req.pos) != 0)
			abort();
		assert(conn.tls);
		assert(conn.fd == expect_close);
		struct http_fd_pool_response rsp = {};
		http_fd_pool_parse(&conn, &rsp);
		if (rsp.status_code == 200) {
			//tls_get_session_id(conn.tls, second_sess, sizeof(second_sess));
			http_fd_pool_cleanup(ctx, &conn, rsp.keepalive);
			break;
		}
	}
#endif

	fd_pool_free(pool);
	assert(expect_close == -1);
	ctrl_quit(ctrl);
	close(lsock);
	http_fd_pool_free_context(ctx);
	free(port);
	free(req.buf);
}

int
main(int argc, char *argv[]) {
	log_setup_perror("http-fd-pool", "debug");
	test_http();
	test_https();
}
