// Copyright 2018 Schibsted

#include "buf_string.h"
#include "memalloc_functions.h"

#include <stdio.h>
#include <string.h>

#define BUFCAT_SIZE 1024

static int
resize(char **buf, int * RESTRICT buf_len, int * RESTRICT buf_pos, int required) {
	int new_size;

	if (*buf) {
		int remaining = *buf_len - *buf_pos;
		if (remaining > required)
			return remaining;
		new_size = *buf_len;
		required += *buf_pos;
	} else {
		new_size = BUFCAT_SIZE;
		*buf_pos = 0;
	}

	/*
	 * We're growing the buf by 1.5x each time to lower memory
	 * fragmentation.
	 */
	while (new_size < required)
		new_size += new_size / 2;

	char *tmp = xrealloc(*buf, new_size);
	*buf = tmp;
	*buf_len = new_size;

	return *buf_len - *buf_pos;
}

int
bufcat(char **buf, int * RESTRICT buf_len, int * RESTRICT buf_pos, const char * fmt, ...) {
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = vbufcat(buf, buf_len, buf_pos, fmt, ap);
	va_end(ap);
	return res;
}

int
vbufcat(char **buf, int * RESTRICT buf_len, int * RESTRICT buf_pos, const char * fmt, va_list ap) {
	int res;
	int remaining;
	va_list apc;

	remaining = resize(buf, buf_len, buf_pos, 0);

	va_copy(apc, ap);
	res = vsnprintf(*buf + *buf_pos, remaining, fmt, apc);
	va_end(apc);
	if (res >= remaining) {
		remaining = resize(buf, buf_len, buf_pos, res + 1); /* Account for terminator */
		va_copy(apc, ap);
		res = vsnprintf(*buf + *buf_pos, remaining, fmt, apc);
		va_end(apc);
	}
	*buf_pos += res;

	return res;
}

int
bufwrite(char **buf, int * RESTRICT buf_len, int * RESTRICT buf_pos, const void * RESTRICT data, size_t len) {
	resize(buf, buf_len, buf_pos, len + 1);
	memcpy(*buf + *buf_pos, data, len);
	*buf_pos += len;
	(*buf)[*buf_pos] = '\0';

	return len;
}

void
bsprealloc(struct buf_string *dst, size_t size) {
	if (!dst->buf) {
		dst->buf = xmalloc(size);
		dst->len = size;
		dst->pos = 0;
	}
	return;
}

int
bscat(struct buf_string *dst, const char *fmt, ...) {
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = vbufcat(&dst->buf, &dst->len, &dst->pos, fmt, ap);
	va_end(ap);

	return res;
}

int
vbscat(struct buf_string *dst, const char *fmt, va_list ap) {
	return vbufcat(&dst->buf, &dst->len, &dst->pos, fmt, ap);
}

int
bswrite(struct buf_string *dst, const void *data, size_t len) {
	return bufwrite(&dst->buf, &dst->len, &dst->pos, data, len);
}

int
bswrite_void(void *dst, const void *data, size_t len) {
	return bswrite(dst, data, len);
}

size_t
bs_fread_all(struct buf_string *dst, FILE *f) {
	size_t tot = 0, r;
	do {
		int remaining = dst->len - dst->pos;
		if (remaining < 512)
			remaining = resize(&dst->buf, &dst->len, &dst->pos, 2048);
		r = fread(dst->buf + dst->pos, 1, remaining, f);
		tot += r;
		dst->pos += r;
	} while (r > 0);
	return tot;
}


