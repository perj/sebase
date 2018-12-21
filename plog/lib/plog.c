// Copyright 2018 Schibsted

#include "plog.h"
#include "plog.pb-c.h"

#include "sbp/atomic.h"
#include "sbp/buf_string.h"
#include "sbp/error_functions.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sbp/string_functions.h"
#include "sbp/utf8.h"
#include "sbp/url.h"

#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#include <assert.h>

/* Shared connection for all context. Closed if refs reaches 0. */
struct plog_conn {
	pthread_mutex_t lock;
	int fd;
	uint64_t refs;
	uint64_t generation;
	time_t last_reconnect;
};

struct plog_ctx {
	struct plog_ctx *pctx;

	pthread_mutex_t lock;
	struct plog_conn *conn;
	uint64_t generation;

	uint64_t id;
	Plogproto__CtxType ctype;
	size_t n_key;
	char **key;

	char *streamtmp;
	int flags;
	int failed_writes;

	Plogproto__Plog buffer;
	size_t msgalloced;
};

struct plog_conn plog_default_conn = { .lock = PTHREAD_MUTEX_INITIALIZER };
static uint64_t plog_ctx_id;

static enum plog_charset plog_charset;

void
plog_set_global_charset(enum plog_charset cs) {
	plog_charset = cs;
}

static bool
plog_reconnect(struct plog_conn *conn) {
	uint64_t gen = conn->generation;
	pthread_mutex_lock(&conn->lock);
	if (gen == conn->generation) {
		// Should perhaps use >= 0 but zero-initialized is nice and fd
		// 0 should be really uncommon.
		if (conn->fd > 0)
			close(conn->fd);
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		if (ts.tv_sec < conn->last_reconnect + 5) {
			conn->fd = -1;
			pthread_mutex_unlock(&conn->lock);
			return false;
		}
		conn->last_reconnect = ts.tv_sec;
		conn->fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (conn->fd < 0) {
			pthread_mutex_unlock(&conn->lock);
			return false;
		}
		struct sockaddr_un unaddr = {0};
		const char *path = getenv("PLOG_SOCKET");
		if (path == NULL)
			path = "/run/plog/plog.sock";
		unaddr.sun_family = AF_UNIX;
		strlcpy(unaddr.sun_path, path, sizeof(unaddr.sun_path));
		if (connect(conn->fd, (struct sockaddr*)&unaddr, sizeof(unaddr)) < 0) {
			close(conn->fd);
			conn->fd = -1;
		} else {
			conn->generation++;
		}
	}
	pthread_mutex_unlock(&conn->lock);

	return conn->fd >= 0;
}

static void
plog_clear_buffer(struct plog_ctx *ctx) {
	free(ctx->buffer.open);
	ctx->buffer.open = NULL;
	for (size_t i = 0 ; i < ctx->buffer.n_msg ; i++) {
		free(ctx->buffer.msg[i]->key);
		free(ctx->buffer.msg[i]->value.data);
	}
	ctx->buffer.n_msg = 0;
	ctx->buffer.has_close = false;
	ctx->buffer.close = false;
}

// Must hold ctx->lock when calling this.
static bool
plog_send(struct plog_ctx *ctx, bool flush) {
	if (!flush && (ctx->flags & PLOG_BUFFERED))
		return true;

	ctx->buffer.has_ctx_id = true;
	ctx->buffer.ctx_id = ctx->id;

	// Disallow fd 0 as it's the "uninitialized" value.
	if (ctx->conn->fd <= 0)
		return false;

	uint8_t buf[1024];
	struct ProtobufCBufferSimple pbuf = PROTOBUF_C_BUFFER_SIMPLE_INIT(buf);
	plogproto__plog__pack_to_buffer(&ctx->buffer, (ProtobufCBuffer*)&pbuf);

	pthread_mutex_lock(&ctx->conn->lock);
	bool ret = false;
	if (ctx->conn->fd <= 0)
		goto out;

	uint32_t sz = htonl(pbuf.len);
	ssize_t l = write(ctx->conn->fd, &sz, sizeof(sz));
	if (l != sizeof(sz))
		goto out;
	ssize_t ssz = pbuf.len;
	for (l = 0 ; l < ssz ; ) {
		ssize_t n = write(ctx->conn->fd, pbuf.data + l, ssz - l);
		if (n == -1) {
			if (errno != EINTR)
				break;
			n = 0;
		}
		l += n;
	}
	ret = l == ssz;
out:
	pthread_mutex_unlock(&ctx->conn->lock);
	PROTOBUF_C_BUFFER_SIMPLE_CLEAR(&pbuf);
	if (ret)
		plog_clear_buffer(ctx);
	return ret;
}

static void
recurse_fallback_session_id(struct buf_string *tgt, struct plog_ctx *ctx) {
	if (!ctx->pctx) {
		// Ignore root context. The appname is added by syslog so adding
		// it here would duplicate it.
		return;
	}
	recurse_fallback_session_id(tgt, ctx->pctx);
	if (tgt->pos > 0)
		bswrite(tgt, ".", 1);
	bscat(tgt, "%llu", (unsigned long long)ctx->id);
	for (size_t i = 0 ; i < ctx->n_key ; i++)
		bscat(tgt, ".%s", ctx->key[i]);
}

static void
plog_fallback(struct plog_ctx *ctx) {
	if (ctx->buffer.n_msg == 0)
		return;

	struct buf_string session_id = {0};
	recurse_fallback_session_id(&session_id, ctx);
	if (session_id.pos > 0)
		bswrite(&session_id, " ", 1);
	for (size_t i = 0 ; i < ctx->buffer.n_msg ; i++) {
		const Plogproto__PlogMessage *msg = ctx->buffer.msg[i];
		int sltype = get_priority_from_level(msg->key, LOG_INFO);
		syslog(LOG_LOCAL0 | sltype, "%s%s: %.*s", session_id.buf ?: "", msg->key, (int)msg->value.len, msg->value.data);
	}
	plog_clear_buffer(ctx);
	free(session_id.buf);
}

static void
plog_fallback_ts(struct plog_ctx *ctx, const char *key, time_t ts) {
	struct tm tm = {};
	tm.tm_isdst = -1;
	gmtime_r(&ts, &tm);
	char buf[256];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
	plog_string(ctx, key, buf);
	plog_fallback(ctx);
}

// Must hold ctx->lock when calling this.
static bool
plog_opencontext(struct plog_ctx *ctx) {
	Plogproto__OpenContext *open = ctx->buffer.open;

	if (!open) {
		ctx->buffer.open = open = malloc(sizeof(*open));
		plogproto__open_context__init(open);
	}
	open->has_ctxtype = true;
	open->ctxtype = ctx->ctype;
	open->n_key = ctx->n_key;
	open->key = ctx->key;
	if (ctx->pctx) {
		open->has_parent_ctx_id = true;
		open->parent_ctx_id = ctx->pctx->id;
	}

	return plog_send(ctx, false);
}

static void
plog_check_generation(struct plog_ctx *ctx) {
	pthread_mutex_lock(&ctx->lock);
	if (ctx->generation != ctx->conn->generation) {
		if (ctx->pctx)
			plog_check_generation(ctx->pctx);
		ctx->generation = ctx->conn->generation;
		plog_opencontext(ctx);
	}
	pthread_mutex_unlock(&ctx->lock);
}

static void
plog_conn_retain(struct plog_ctx *ctx, struct plog_conn *conn) {
	// No need to check refs value, 0 is fine.
	__sync_add_and_fetch(&conn->refs, 1);
	ctx->conn = conn;
	ctx->generation = ctx->conn->generation;
}

static void
plog_conn_release(struct plog_conn *conn) {
	uint64_t refs = __sync_sub_and_fetch(&conn->refs, 1);
	if (refs == 0) {
		pthread_mutex_lock(&conn->lock);
		if (conn->fd > 0)
			close(conn->fd);
		conn->fd = -1;
		conn->last_reconnect = 0;
		pthread_mutex_unlock(&conn->lock);
	}
}

static struct plog_ctx *
plog_open_root(struct plog_conn *conn, const char *appname, Plogproto__CtxType ctype, int npath, const char *path[]) {
	struct plog_ctx *ctx = zmalloc(sizeof(*ctx));
	pthread_mutex_init(&ctx->lock, NULL);
	plog_conn_retain(ctx, conn ?: &plog_default_conn);
	ctx->ctype = ctype;
	ctx->id = __sync_add_and_fetch(&plog_ctx_id, 1);
	ctx->n_key = npath + 1;
	ctx->key = xmalloc(ctx->n_key * sizeof(*ctx->key));
	ctx->key[0] = xstrdup(appname);
	for (int i = 0 ; i < npath ; i++)
		ctx->key[i + 1] = xstrdup(path[i]);
	plogproto__plog__init(&ctx->buffer);

	if (!plog_opencontext(ctx)) {
		plog_reconnect(ctx->conn);
		plog_check_generation(ctx);
	}
	/*
	if (ctx->conn->fd == -1 && ctype == plogproto::log)
		plog_fallback_ts(ctx, "start_timestamp", time(NULL));
	*/
	return ctx;
}

struct plog_ctx *
plog_open_log(struct plog_conn *conn, const char *appname) {
	assert(strchr(appname, '.') == NULL);
	return plog_open_root(conn, appname, PLOGPROTO__CTX_TYPE__log, 0, NULL);
}

struct plog_ctx *
plog_open_state(struct plog_conn *conn, const char *appname) {
	return plog_open_root(conn, appname, PLOGPROTO__CTX_TYPE__state, 0, NULL);
}

struct plog_ctx *
plog_open_count(struct plog_conn *conn, const char *appname, int npath, const char *path[]) {
	return plog_open_root(conn, appname, PLOGPROTO__CTX_TYPE__count, npath, path);
}

static struct plog_ctx *
plog_open_sub(struct plog_ctx *pctx, const char *key, Plogproto__CtxType ctype, int flags) {
	if (!pctx)
		return NULL;
	struct plog_ctx *ctx = zmalloc(sizeof(*ctx));
	pthread_mutex_init(&ctx->lock, NULL);
	plog_conn_retain(ctx, pctx->conn);
	ctx->ctype = ctype;
	ctx->flags = flags;
	ctx->id = __sync_add_and_fetch(&plog_ctx_id, 1);
	ctx->n_key = 1;
	ctx->key = xmalloc(sizeof(*ctx->key));
	ctx->key[0] = xstrdup(key);
	plogproto__plog__init(&ctx->buffer);

	if (!plog_opencontext(ctx)) {
		plog_reconnect(ctx->conn);
		plog_check_generation(ctx);
	}
	if (ctx->conn->fd == -1) {
		// Check root context type.
		for (; pctx->pctx ; pctx = pctx->pctx)
			;
		if (pctx->ctype == PLOGPROTO__CTX_TYPE__log)
			plog_fallback_ts(ctx, "start_timestamp", time(NULL));
	}
	return ctx;
}

struct plog_ctx *
plog_open_dict_flags(struct plog_ctx *pctx, const char *key, int flags) {
	return plog_open_sub(pctx, key, PLOGPROTO__CTX_TYPE__dict, flags);
}

struct plog_ctx *
plog_open_dict(struct plog_ctx *pctx, const char *key) {
	return plog_open_sub(pctx, key, PLOGPROTO__CTX_TYPE__dict, 0);
}

struct plog_ctx *
plog_open_list_flags(struct plog_ctx *pctx, const char *key, int flags) {
	return plog_open_sub(pctx, key, PLOGPROTO__CTX_TYPE__list, flags);
}

struct plog_ctx *
plog_open_list(struct plog_ctx *pctx, const char *key) {
	return plog_open_sub(pctx, key, PLOGPROTO__CTX_TYPE__list, 0);
}

void
plog_flush(struct plog_ctx *ctx) {
	if (!ctx)
		return;

	pthread_mutex_lock(&ctx->lock);
	bool ok = plog_send(ctx, true);
	pthread_mutex_unlock(&ctx->lock);
	if (!ok) {
		plog_reconnect(ctx->conn);
		plog_check_generation(ctx);
		pthread_mutex_lock(&ctx->lock);
		if (!plog_send(ctx, true))
			plog_fallback(ctx);
		pthread_mutex_unlock(&ctx->lock);
	}
}

static void
plog_free(struct plog_ctx *ctx) {
	plog_conn_release(ctx->conn);
	free(ctx->streamtmp);
	for (size_t i = 0 ; i < ctx->msgalloced ; i++)
		free(ctx->buffer.msg[i]);
	free(ctx->buffer.msg);
	for (size_t i = 0 ; i < ctx->n_key ; i++)
		free(ctx->key[i]);
	free(ctx->key);
	free(ctx);
}

void
plog_cancel(struct plog_ctx *ctx) {
	if (!ctx)
		return;
	assert(ctx->flags & PLOG_BUFFERED);
	plog_clear_buffer(ctx);
	plog_free(ctx);
}

int
plog_close(struct plog_ctx *ctx) {
	if (!ctx)
		return 0;
	ctx->buffer.has_close = true;
	ctx->buffer.close = true;
	if (!plog_send(ctx, true)) {
		plog_reconnect(ctx->conn);
		plog_check_generation(ctx);
		if (!plog_send(ctx, true))
			plog_fallback(ctx);
	}
	int r = ctx->failed_writes;
	plog_free(ctx);
	return r;
}

void
plog_move_failed_writes(struct plog_ctx *pctx, struct plog_ctx *ctx) {
	if (!ctx)
		return;
	int fw = __sync_fetch_and_and(&ctx->failed_writes, 0);
	__sync_add_and_fetch(&pctx->failed_writes, fw);
}

int
plog_reset_failed_writes(struct plog_ctx *ctx) {
	return __sync_fetch_and_and(&ctx->failed_writes, 0);
}

void
plog_set_flags(struct plog_ctx *ctx, int flags) {
	if (!ctx)
		return;
	if (!(flags & PLOG_BUFFERED))
		plog_flush(ctx);
	ctx->flags = flags;
}

static char *
json_encode_buf(char *buf, size_t blen, int n, ...) {
	char *end = buf + blen - 1;
	va_list ap;

	assert(n <= 2);
	char *abuf = NULL;
	const char **abufptr[2];
	size_t abufsz;

	va_start(ap, n);
	for (int i = 0 ; i < n ; i++) {
		const char *src = va_arg(ap, const char *);
		const char **dst = va_arg(ap, const char **);
		ssize_t *len = va_arg(ap, ssize_t*);
		char *utf8 = NULL;

		if (!src) {
			*dst = NULL;
			*len = 0;
			abufptr[i] = NULL;
			continue;
		}

		switch (plog_charset) {
		case PLOG_UTF8:
		default:
			break;
		case PLOG_LATIN1:
			{
				int l = *len;
				src = utf8 = latin1_to_utf8(src, l, &l);
				*len = l;
			}
			break;
		case PLOG_LATIN2:
			{
				int l = *len;
				size_t ol;
				if (l < 0)
					l = strlen(src);
				utf8 = xmalloc(l * 3 + 1);
				latin2_to_utf8_buf(&ol, src, l, utf8, l * 3 + 1);
				src = utf8;
				*len = ol;
			}
			break;
		}

		const char *sptr = src;
		char *dptr = buf;
		enum { firstQuote, text, lastQuote, done } state = firstQuote;
		while (state != done) {
			if (__predict_false(dptr >= end - 7)) {
				/* Out of buffer space. Fallback to malloc buffer. */
				if (abuf) {
					char *oldabuf = abuf;
					abuf = xrealloc(abuf, abufsz *= 2);
					if (abuf != oldabuf) {
						/* Update abuf pointers if moved. */
						for (int j = 0 ; j < i ; j++) {
							if (abufptr[j])
								*abufptr[j] = abuf + (*abufptr[j] - oldabuf);
						}
						dptr = abuf + (dptr - oldabuf);
						buf = abuf + (buf - oldabuf);
					}
				} else {
					abuf = xmalloc(abufsz = blen * 2);
					memcpy(abuf, buf, dptr - buf);
					dptr = abuf + (dptr - buf);
					buf = abuf;
				}
				end = abuf + abufsz;
			}
			switch (state) {
			case firstQuote:
				*dptr++ = '"';
				state = text;
				break;
			case text:
				if (*len < 0 ? *sptr == '\0' : sptr - src >= *len) {
					state = lastQuote;
					break;
				}
				dptr += json_encode_char(dptr, end - buf, *sptr++, false);
				break;
			case lastQuote:
				*dptr++ = '"';
				state = done;
				break;
			case done:
				/* quiet warning */
				break;
			}
		}
		*dptr = '\0';
		*dst = buf;
		*len = dptr - buf;
		abufptr[i] = abuf ? dst : NULL;
		buf = dptr + 1;
		free(utf8);
	}
	va_end(ap);
	return abuf;
}

static void
plog_publish(struct plog_ctx *ctx, const char *key, ssize_t klen, const char *coded_value, size_t vlen) {
	if (!key)
		key = PLOG_DEFAULT_KEY;
	pthread_mutex_lock(&ctx->lock);
	if (ctx->buffer.n_msg >= ctx->msgalloced) {
		ctx->msgalloced = ctx->msgalloced * 2 ?: (ctx->flags & PLOG_BUFFERED) ? 4 : 1;
		ctx->buffer.msg = xrealloc(ctx->buffer.msg, ctx->msgalloced * sizeof(*ctx->buffer.msg));
		for (size_t i = ctx->buffer.n_msg ; i < ctx->msgalloced ; i++) {
			ctx->buffer.msg[i] = xmalloc(sizeof (*ctx->buffer.msg[i]));
			plogproto__plog_message__init(ctx->buffer.msg[i]);
		}
	}
	Plogproto__PlogMessage *msg = ctx->buffer.msg[ctx->buffer.n_msg++];
	if (klen >= 0)
		msg->key = xstrdup(key);
	else
		msg->key = xstrndup(key, klen);
	msg->has_value = true;
	msg->value.len = vlen;
	msg->value.data = xmalloc(vlen + 1);
	memcpy(msg->value.data, coded_value, vlen);
	msg->value.data[vlen] = '\0';
	pthread_mutex_unlock(&ctx->lock);
	if (!(ctx->flags & PLOG_BUFFERED))
		plog_flush(ctx);
}

void
plog_string(struct plog_ctx *ctx, const char *key, const char *value) {
	if (ctx == NULL)
		return;
	ssize_t vlen = -1;
	char encbuf[8192], *vbuf;
	char *fb = json_encode_buf(encbuf, sizeof(encbuf), 1, value, &vbuf, &vlen);

	plog_publish(ctx, key, -1, vbuf, vlen);
	free(fb);
}

void
plog_string_klen(struct plog_ctx *ctx, const char *key, ssize_t klen, const char *value, ssize_t vlen) {
	if (ctx == NULL)
		return;
	char encbuf[8192], *vbuf;
	char *fb = json_encode_buf(encbuf, sizeof(encbuf), 1, value, &vbuf, &vlen);

	plog_publish(ctx, key, klen, vbuf, vlen);
	free(fb);
}

void
plog_string_len(struct plog_ctx *ctx, const char *key, const char *value, ssize_t vlen) {
	plog_string_klen(ctx, key, -1, value, vlen);
}

void
plog_string_printf(struct plog_ctx *ctx, const char *key, const char *fmt, ...) {
	if (ctx == NULL)
		return;
	va_list ap;
	char *v;

	va_start(ap, fmt);
	xvasprintf(&v, fmt, ap);
	va_end(ap);

	plog_string(ctx, key, v);
	free(v);
}

void
plog_string_stream(struct plog_ctx *ctx, const char *key, const char *str) {
	if (ctx == NULL)
		return;
	const char *n;
	while ((n = strchr(str, '\n'))) {
		if (ctx->streamtmp) {
			plog_string_printf(ctx, key, "%s%.*s", ctx->streamtmp, (int)(n - str), str);
			free(ctx->streamtmp);
			ctx->streamtmp = NULL;
		} else if (n != str) {
			plog_string_len(ctx, key, str, n - str);
		}
		str = n + 1;
	}
	if (*str) {
		char *news;
		xasprintf(&news, "%s%s", ctx->streamtmp ?: "", str);
		free(ctx->streamtmp);
		ctx->streamtmp = news;
	}
}

void
plog_string_vprintf(struct plog_ctx *ctx, const char *key, const char *fmt, va_list ap) {
	if (ctx == NULL)
		return;
	char *v;
	xvasprintf(&v, fmt, ap);
	plog_string(ctx, key, v);
	free(v);
}

void
plog_int_klen(struct plog_ctx *ctx, const char *key, ssize_t klen, int value) {
	if (ctx == NULL)
		return;
	char vbuf[32];
	int vlen;

	vlen = snprintf(vbuf, sizeof(vbuf), "%d", value);

	plog_publish(ctx, key, klen, vbuf, vlen);
}

void
plog_int(struct plog_ctx *ctx, const char *key, int value) {
	plog_int_klen(ctx, key, -1, value);
}

void
plog_bool_klen(struct plog_ctx *ctx, const char *key, ssize_t klen, bool value) {
	if (ctx == NULL)
		return;
	plog_publish(ctx, key, klen, value ? "true" : "false", value ? 4 : 5);
}

void
plog_bool(struct plog_ctx *ctx, const char *key, bool value) {
	plog_bool_klen(ctx, key, -1, value);
}

const char plog_dictv[4] = "";
const char *plog_dictv_values[3] = {"null", "false", "true"};

void
plog_dict_pairs(struct plog_ctx *ctx, const char *key, ...) {
	if (ctx == NULL)
		return;
	struct buf_string buf = {};
	bswrite(&buf, "{", 1);
	bool once = false;

	va_list ap;
	va_start(ap, key);
	const char *ek;
	while ((ek = va_arg(ap, const char*))) {
		const char *ev = va_arg(ap, const char*);
		ssize_t klen = -1, vlen = -1;
		char encbuf[8192], *fb;
		const char *kbuf, *vbuf;

		if (ev >= plog_dictv && ev < plog_dictv + sizeof(plog_dictv)) {
			fb = json_encode_buf(encbuf, sizeof(encbuf), 1, ek, &kbuf, &klen);
			if (ev == PLOG_DICTV_JSON) {
				vbuf = va_arg(ap, const char*);
				vlen = strlen(vbuf);
			} else {
				vbuf = plog_dictv_values[ev - plog_dictv];
				vlen = strlen(vbuf);
			}
		} else {
			fb = json_encode_buf(encbuf, sizeof(encbuf), 2, ek, &kbuf, &klen, ev, &vbuf, &vlen);
		}

		if (once)
			bswrite(&buf, ",", 1);
		bswrite(&buf, kbuf, klen);
		bswrite(&buf, ":", 1);
		bswrite(&buf, vbuf, vlen);
		once = true;
		free(fb);
	}
	va_end(ap);
	bswrite(&buf, "}", 1);

	plog_publish(ctx, key, -1, buf.buf, buf.pos);
	free(buf.buf);
}

void
plog_json_klen(struct plog_ctx *ctx, const char *key, ssize_t klen, const char *json, ssize_t jsonlen) {
	if (ctx == NULL)
		return;
	if (!json) {
		json = "null";
		jsonlen = strlen("null");
	} else if (jsonlen < 0) {
		jsonlen = strlen(json);
	}

	plog_publish(ctx, key, klen, json, jsonlen);
}

void
plog_json(struct plog_ctx *ctx, const char *key, const char *json, ssize_t jsonlen) {
	plog_json_klen(ctx, key, -1, json, jsonlen);
}

void
plog_json_printf(struct plog_ctx *ctx, const char *key, const char *fmt, ...) {
	if (ctx == NULL)
		return;
	va_list ap;
	va_start(ap, fmt);
	char *json;
	size_t len = xvasprintf(&json, fmt, ap);
	va_end(ap);

	plog_json(ctx, key, json, len);
}

static struct plog_ctx *x_err_ctx;

static void
plog_xerr_print(const char *fmt, va_list ap) {
	size_t l = strlen(fmt) + 6;
	char s[l];

	snprintf(s, l, "%s: %%m", fmt);
	plog_string_vprintf(x_err_ctx, "log", s, ap);
}

static void
plog_xerr_printx(const char *fmt, va_list ap) {
	plog_string_vprintf(x_err_ctx, "log", fmt, ap);
}

void
plog_init_x_err(const char *appname) {
	plog_close(x_err_ctx);
	x_err_ctx = plog_open_log(NULL, appname);
	x_err_init_custom(plog_xerr_print, plog_xerr_printx);
}

void
plog_xerr_close(void) {
	plog_close(x_err_ctx);
	x_err_ctx = NULL;
}
