// Copyright 2018 Schibsted

#include "macros.h"

#include <sys/types.h>

/*
 * You can use this scratch space when you do a lot of similar allocations
 * and deallocations in a row, e.g. processing a large set of data one
 * item at a time.
 */

struct scratch {
	struct scratch_buffer *buf;
	char *curptr;
};

struct scratch_buffer {
	struct scratch_buffer *prev;
	size_t bufsz;
	char buffer[] VAR_ALIGN(16);
};

#define SCRATCH_DEFAULT_BUFSZ 1024

/*
 * Create a new buffer. Usually not needed to be called manually, you can
 * start off with a NULL pointer, but provided for custom sizes.
 */
char *scratchbuf_new(struct scratch_buffer **buf, size_t minsz);

static inline void ARTIFICIAL
scratch_init(struct scratch *scratch, size_t minsz) {
	scratch->curptr = scratchbuf_new(&scratch->buf, minsz);
}

/*
 * Helper for calculating how many more elements can fit in current scratch
 * space.
 */
static inline int
scratchbuf_nleft(const struct scratch_buffer *buf, const void *ptr, size_t sz) {
	return (buf->buffer + buf->bufsz - (char*)ptr) / sz;
}

static inline int ARTIFICIAL
scratch_nleft(const struct scratch *scratch, size_t sz) {
	return scratchbuf_nleft(scratch->buf, scratch->curptr, sz);
}

/* Grow (or create) an array. Will add at least one element, updates *n with the new total.
 * Might copy the array, like xrealloc does.
 * You can use the same scratch for multiple arrays, though it's only efficient if you
 * only grow one array at a time, don't interlieve.
 * Also, alignments must match, most likely you should only use a single type element
 * for each scratch.
 * To allocate a new array pass NULL as array and 0 as *n.
 * New elements are NOT zero initialized.
 */
void *scratchbuf_growarr(struct scratch_buffer **buf, char **pptr, void *array, int *n, size_t elemsz) WARN_UNUSED_RESULT;

static inline void * ARTIFICIAL
scratch_growarr(struct scratch *scratch, void *array, int *n, size_t elemsz) {
	return scratchbuf_growarr(&scratch->buf, &scratch->curptr, array, n, elemsz);
}

/* Copy str to the scratch and return a pointer to it. */
char *scratchbuf_strdup(struct scratch_buffer **buf, char **pptr, const char *str, int len);

void *scratchbuf_memcpy(struct scratch_buffer **buf, char **pptr, const void *val, int vlen, size_t align);

static inline char * ARTIFICIAL
scratch_strdup(struct scratch *scratch, const char *str, int len) {
	return scratchbuf_strdup(&scratch->buf, &scratch->curptr, str, len);
}

static inline void * ARTIFICIAL
scratch_memcpy(struct scratch *scratch, const void *val, int vlen, size_t align) {
	return scratchbuf_memcpy(&scratch->buf, &scratch->curptr, val, vlen, align);
}

/* Compact the scratch to a single buffer and reset the curptr to the start.
 * Makes future use more efficient.
 * Invalidates the scratch contents, meant to be used between each item
 * you work on with the scratch.
 */
void scratchbuf_reset(struct scratch_buffer **buf, char **pptr);

static inline void ARTIFICIAL
scratch_reset(struct scratch *scratch) {
	return scratchbuf_reset(&scratch->buf, &scratch->curptr);
}

/* Frees *buf and sets it to NULL. */
void scratchbuf_free(struct scratch_buffer **buf);

static inline void ARTIFICIAL
scratch_clean(struct scratch *scratch) {
	scratchbuf_free(&scratch->buf);
}
