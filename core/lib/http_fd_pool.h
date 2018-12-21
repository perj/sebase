// Copyright 2018 Schibsted

#ifndef HTTP_FD_POOL_H
#define HTTP_FD_POOL_H

#if __has_include("sbp/http_parser.h")
#include "sbp/http_parser.h"
#else
#include <http_parser.h>
#endif

#include <stdbool.h>
#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HTTP over fd pool.
 *
 * Note: This is meant to be used for internal services, it's not very
 * appropriate for talking to third parties.
 *
 * First create a context and store in a global variable.
 * If you want to use both HTTP and HTTPS you will have to create one
 * context for each.
 *
 * XXX: Currently no check of the certificate hostname is done, even
 * when using HTTPS. We only check that the peer has a valid certificate
 * signed by the set CA certificate.
 * Most likely some sort of ACL check should be added, similar to what
 * controllers do.
 *
 * You have to manually create the HTTP request.
 * For each request, initialize conn->fdc, then call the connect function.
 * It will get an fd and send the request, but returns before reading the reply.
 * fd and possibly tls will be set and can be used to parse the reply with
 * the parser function or manually via http_parser.
 * If the request fails and you want to try again simply call connect again.
 *
 * cleanup will close fd and pointers and also call fd_pool_free_conn (set to NULL before calling
 * to avoid).
 * Connect returns 0 if ok, -1 on failure.
 */
struct https_state;
struct tls_context;
struct tls;

struct http_fd_pool_conn {
	struct fd_pool_conn *fdc;

	int fd;
	struct tls *tls;

	/* Filled in from fd_pool_get call. */
	const char *peer;
	const char *port_key;
};

struct http_fd_pool_ctx *http_fd_pool_create_context(const struct https_state *https);
void http_fd_pool_free_context(struct http_fd_pool_ctx *ctx);

int http_fd_pool_connect(struct http_fd_pool_ctx *ctx, struct http_fd_pool_conn *conn,
		const struct iovec *iov, int iovcnt, ssize_t reqlen);
void http_fd_pool_cleanup(struct http_fd_pool_ctx *ctx, struct http_fd_pool_conn *conn,
		bool keepalive);

struct http_fd_pool_response {
	bool complete;              /* Set by parse if a complete response was parsed. */
	bool keepalive;             /* Set by parse if keepalive can be enabled. */
	int status_code;            /* HTTP code */
	enum http_errno http_errno; /* http_parser error code */

	/* Either set both, body_cb will then be called with body_v and data to write,
	 * or set only body_v, in which case is assumed to be a struct buf_string *
	 * to write to.
	 * body_cb should return 0 on success.
	 */
	int (*body_cb)(void *body_v, const void *data, size_t len);
	void *body_v;

	/*
	 * If set, called with header_v, header key and value. They are *NOT* NUL terminated.
	 * Called for each header as they're parsed.
	 * Should return 0 on success.
	 */
	int (*header_cb)(void *header_v, const char *key, size_t klen, const char *value, size_t vlen);
	void *header_v;

	/* Used to store the header key while waiting for the value, not needed to be used by caller. */
	const char *header_k;
	size_t header_klen;
};

/* Utility function to parse response into a simple response struct.
 * Call it after a successful connect.
 */
void http_fd_pool_parse(struct http_fd_pool_conn *conn,
		struct http_fd_pool_response *dst);

/* Send a request. Normally not used, connect will call this for you. */
int http_fd_pool_send(struct http_fd_pool_ctx *ctx, struct http_fd_pool_conn *conn,
		const struct iovec *iov, int iovcnt, ssize_t reqlen);

#ifdef __cplusplus
}
#endif

#endif /*HTTP_FD_POOL_H*/
