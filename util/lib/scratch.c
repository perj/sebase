// Copyright 2018 Schibsted

#include "memalloc_functions.h"
#include "scratch.h"

#include <string.h>
#include <sys/types.h>
#include <stdint.h>

char *
scratchbuf_new(struct scratch_buffer **buf, size_t minsz) {
	size_t newsz = *buf ? (*buf)->bufsz * 2 : SCRATCH_DEFAULT_BUFSZ;
	if (newsz < minsz)
		newsz = minsz;
	struct scratch_buffer *newb = xmalloc(sizeof (*newb) + newsz);
	newb->prev = *buf;
	newb->bufsz = newsz;
	*buf = newb;
	return newb->buffer;
}

void *
scratchbuf_growarr(struct scratch_buffer **buf, char **pptr, void *array, int *n, size_t sz) {
	/* New arrays can start growing from pptr. */
	if (!array)
		array = (void*)*pptr;
	int nleft = *buf ? scratchbuf_nleft(*buf, *pptr, sz) : 0;
	if (nleft > 0 && (char*)array + *n * sz == *pptr) {
		/* If the array is the one at the end of the scratch, we can grow it directly. */
		if (nleft > *n && nleft > 4)
			nleft = *n ?: 4;
		*n += nleft;
		*pptr += nleft * sz;
	} else {
		/* Otherwise have to copy. */
		*pptr = scratchbuf_new(buf, *n * 2 * sz);
		if (*n) {
			memcpy(*pptr, array, *n * sz);
			*n *= 2;
		} else {
			*n = 4;
		}
		array = *pptr;
		*pptr += *n * sz;
	}
	return array;
}

void *
scratchbuf_memcpy(struct scratch_buffer **buf, char **pptr, const void *val, int vlen, size_t align) {
	off_t al = (((intptr_t)*pptr + (align - 1)) & ~(align - 1)) - ((intptr_t)*pptr);
	int nleft = *buf ? scratchbuf_nleft(*buf, *pptr, 1) : 0;
	if (nleft < vlen + al) {
		*pptr = scratchbuf_new(buf, vlen);
		al = 0;
	}

	/* align */
	memset(*pptr, 0, al);
	*pptr += al;

	memcpy(*pptr, val, vlen);
	void *ret = *pptr;
	*pptr += vlen;
	return ret;
}

char *
scratchbuf_strdup(struct scratch_buffer **buf, char **pptr, const char *str, int len) {
	if (len < 0)
		len = strlen(str);
	int nleft = *buf ? scratchbuf_nleft(*buf, *pptr, 1) : 0;
	if (nleft < len + 1)
		*pptr = scratchbuf_new(buf, len + 1);
	memcpy(*pptr, str, len);
	(*pptr)[len] = '\0';
	char *ret = *pptr;
	*pptr += len + 1;
	return ret;
}

void
scratchbuf_reset(struct scratch_buffer **buf, char **pptr) {
	if (!*buf)
		return;
	size_t totsz = 0;
	struct scratch_buffer *b = *buf;
	if (!b->prev) {
		/* If only one buffer then there's no need to compress. */
		*pptr = b->buffer;
		return;
	}
	while (b) {
		struct scratch_buffer *prev = b->prev;
		totsz += b->bufsz;
		free(b);
		b = prev;
	}
	b = xmalloc(sizeof (*b) + totsz);
	b->bufsz = totsz;
	b->prev = NULL;
	*pptr = b->buffer;
	*buf = b;
}

void
scratchbuf_free(struct scratch_buffer **buf) {
	while (*buf) {
		struct scratch_buffer *prev = (*buf)->prev;
		free(*buf);
		*buf = prev;
	}
}
