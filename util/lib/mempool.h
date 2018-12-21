// Copyright 2018 Schibsted

#ifndef COMMON_MEMPOOL_H
#define COMMON_MEMPOOL_H

#include "macros.h"

struct mempool;

struct mempool *mempool_create(size_t firstsz) ALLOCATOR;
void mempool_finalize(struct mempool *pool) NONNULL_ALL;
void mempool_free(struct mempool *pool) NONNULL_ALL;

/* Always zero initialized */
void *mempool_alloc(struct mempool *pool, size_t sz) ALLOCATOR;

const char *mempool_strdup(struct mempool *pool, const char *str, ssize_t len) ALLOCATOR NONNULL(2);

#endif /*COMMON_MEMPOOL_H*/
