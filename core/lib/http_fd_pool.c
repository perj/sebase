// Copyright 2018 Schibsted

#include "fd_pool.h"
#include "sbp/http.h"
#include "http_fd_pool.h"
#include "sbp/lru.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sbp/tls.h"

#include <errno.h>
#include <openssl/x509.h>
#include <string.h>
#include <unistd.h>

struct http_fd_pool_ctx {
	struct tls_context tls_ctx;
	tlskey_t key;
	tlscert_t cert;
	struct lru *sesscache;
};

static void
sesscache_free(void *v) {
	if (v)
		tls_free_session(v);
}

struct http_fd_pool_ctx *
http_fd_pool_create_context(const struct https_state *https) {
	struct http_fd_pool_ctx *ctx = zmalloc(sizeof(*ctx));

	struct buf_string bs = {0};

	if (https && https->cafile[0]) {
		FILE *f = fopen(https->cafile, "r");
		if (!f) {
			free(ctx);
			return NULL;
		}
		bs_fread_all(&bs, f);
		fclose(f);

		ctx->tls_ctx.ncacerts = tls_read_cert_array_buf(bs.buf, bs.pos, &ctx->tls_ctx.cacerts);
	}
	bs.pos = 0;
	if (https && https->certfile[0]) {
		FILE *f = fopen(https->certfile, "r");
		if (!f) {
			free(ctx);
			return NULL;
		}
		bs_fread_all(&bs, f);
		fclose(f);

		ctx->key = tls_read_key_buf(bs.buf, bs.pos);
		ctx->cert = tls_read_cert_buf(bs.buf, bs.pos);
	}
	free(bs.buf);

	ctx->sesscache = lru_init(200, sesscache_free, NULL);
	return ctx;
}

void
http_fd_pool_free_context(struct http_fd_pool_ctx *ctx) {
	if (!ctx)
		return;

	lru_free(ctx->sesscache);
	tls_clear_context(&ctx->tls_ctx);
	tls_free_key(ctx->key);
	tls_free_cert(ctx->cert);
	free(ctx);
}

int
http_fd_pool_send(struct http_fd_pool_ctx *ctx, struct http_fd_pool_conn *conn,
		const struct iovec *iov, int iovcnt, ssize_t reqlen) {
	if (reqlen < 0) {
		reqlen = 0;
		for (int i = 0 ; i < iovcnt ; i++)
			reqlen += iov[i].iov_len;
	}
	/* Should we be using HTTPS? */
	if (ctx && ctx->tls_ctx.ncacerts) {
		conn->tls = tls_open(&ctx->tls_ctx, conn->fd, tlsVerifyPeer, ctx->cert, ctx->key, false);
		if (!conn->tls) {
			log_printf(LOG_ERR, "tls_open failed");
			return -1;
		}

		int new_entry = 0;
		struct lru_entry *sess = NULL;
		sess = cache_lru(ctx->sesscache, conn->peer, -1, &new_entry, NULL, NULL);
		if (sess && sess->storage) {
			tls_set_session(conn->tls, sess->storage);
			log_printf(LOG_DEBUG, "Using cached session");
		}

		tls_start(conn->tls);

		if (tls_connect(conn->tls) == -1) {
			log_printf(LOG_ERR, "tls_connect failed: %s", tls_error(conn->tls, -1));
			if (sess) {
				/* We MUST store on new entry. */
				if (new_entry)
					lru_store(ctx->sesscache, sess, sizeof(struct tls_session*));
				lru_leave(ctx->sesscache, sess);
			}
			return -1;
		}

		if (sess && !sess->storage) {
			tlssess_t tls_sess = tls_get_session(conn->tls);
			log_printf(LOG_DEBUG, "storing session in cache");
			if (!__sync_bool_compare_and_swap(&sess->storage, NULL, tls_sess))
				tls_free_session(tls_sess);
			if (new_entry)
				lru_store(ctx->sesscache, sess, sizeof(tls_sess));
			lru_leave(ctx->sesscache, sess);
		} else if (sess) {
			lru_leave(ctx->sesscache, sess);
		}

		ssize_t r = tls_write_vecs(conn->tls, iov, iovcnt);
		if (r != reqlen) {
			log_printf(LOG_ERR, "tls_write failed");
			tls_free(conn->tls);
			conn->tls = NULL;
		}
	} else {
		ssize_t n = 0;
		struct iovec *iovcopy = NULL;
		while (n < reqlen) {
			ssize_t r = writev(conn->fd, iov, iovcnt);
			if (r < 0 && errno == EINTR)
				continue;
			if (r < 0)
				break;
			n += r;
			while (iovcnt > 0 && (size_t)r >= iov[0].iov_len) {
				r -= iov[0].iov_len;
				iov++;
				iovcnt--;
			}
			if (iovcnt > 0 && r > 0) {
				if (!iovcopy) {
					iovcopy = xmalloc(sizeof(*iovcopy) * iovcnt);
					memcpy(iovcopy, iov, sizeof(*iovcopy) * iovcnt);
					iov = iovcopy;
				}
				((struct iovec*)iov)->iov_base = (char*)iov->iov_base + r;
				((struct iovec*)iov)->iov_len -= r;
			}
		}
		free(iovcopy);
		if (n < reqlen)
			return -1;
	}
	return 0;
}

int
http_fd_pool_connect(struct http_fd_pool_ctx *ctx, struct http_fd_pool_conn *conn,
		const struct iovec *iov, int iovcnt, ssize_t reqlen) {

	while (1) {
		enum sbalance_conn_status status = SBCS_START;
		if (conn->fd > 0) {
			close(conn->fd);
			status = SBCS_FAIL;
		}
		if (conn->tls) {
			tls_free(conn->tls);
			conn->tls = NULL;
		}

		conn->fd = fd_pool_get(conn->fdc, status, &conn->peer, &conn->port_key);
		if (conn->fd == -1)
			return -1;

		if (http_fd_pool_send(ctx, conn, iov, iovcnt, reqlen) == 0)
			return 0;
	}
}

void
http_fd_pool_cleanup(struct http_fd_pool_ctx *ctx, struct http_fd_pool_conn *conn,
		bool keepalive) {
	// TLS connections can't do keep-alive for now since fd_pool_get
	// can't tell us if we need to do tls_open or not.
	// Thus we need to always do it, which is only valid for newly connected
	// sockets.
	if (keepalive && conn->fd > 0 && !conn->tls && conn->fdc) {
		fd_pool_put(conn->fdc, conn->fd);
	} else if (conn->fd > 0) {
		if (conn->tls) {
			tls_stop(conn->tls);
			tls_free(conn->tls);
		}
		close(conn->fd);
	}
	conn->fd = -1;
	conn->tls = NULL;
	if (conn->fdc) {
		fd_pool_free_conn(conn->fdc);
		conn->fdc = NULL;
	}
}

static int
done_cb(http_parser *hp) {
	struct http_fd_pool_response *dst = hp->data;
	dst->complete = true;
	return 0;
}

static int
data_cb(http_parser *hp, const char *at, size_t length) {
	struct http_fd_pool_response *dst = hp->data;
	if (dst->body_cb)
		return dst->body_cb(dst->body_v, at, length);
	if (dst->body_v)
		bswrite(dst->body_v, at, length);
	return 0;
}

static int
header_field_cb(http_parser *hp, const char *at, size_t length) {
	struct http_fd_pool_response *dst = hp->data;

	dst->header_k = at;
	dst->header_klen = length;
	return 0;
}

static int
header_value_cb(http_parser *hp, const char *at, size_t length) {
	struct http_fd_pool_response *dst = hp->data;
	return dst->header_cb(dst->header_v, dst->header_k, dst->header_klen, at, length);
}

void
http_fd_pool_parse(struct http_fd_pool_conn *conn,
		struct http_fd_pool_response *dst) {
	http_parser_settings cbs = {
		.on_message_complete = done_cb,
	};
	if (dst->body_cb || dst->body_v)
		cbs.on_body = data_cb;
	if (dst->header_cb) {
		cbs.on_header_field = header_field_cb;
		cbs.on_header_value = header_value_cb;
	}
	http_parser hp = { .data = dst };
	http_parser_init(&hp, HTTP_RESPONSE);

	while (!dst->complete) {
		char respbuf[1024];
		int r;
		do {
			if (conn->tls)
				r = tls_read(conn->tls, respbuf, sizeof(respbuf));
			else
				r = read(conn->fd, respbuf, sizeof(respbuf));
		} while (r == -1 && errno == EINTR);
		if (r < 0)
			break;
		int l = http_parser_execute(&hp, &cbs, respbuf, r);
		if (l != r || r == 0)
			break;
	}
	if (dst->complete)
		dst->keepalive = http_should_keep_alive(&hp);
	dst->http_errno = hp.http_errno;
	dst->status_code = hp.status_code;
}
