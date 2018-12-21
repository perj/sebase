// Copyright 2018 Schibsted

#ifndef LRU_H
#define LRU_H

#include "macros.h"

#include <pthread.h>
#include <stdbool.h>

#include "tree.h"
#include "queue.h"

struct lru_entry {
	char *key;
	volatile int users;

	bool pending;
	pthread_t pending_thread;

	void *storage;
	size_t storage_size;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
	RB_ENTRY(lru_entry) te;
	TAILQ_ENTRY(lru_entry) tq;
};

#ifdef __cplusplus
extern "C" {
#endif

struct lru *lru_init(size_t size, void (*destr)(void*), void (*lru_stat_cb)(struct lru *c, const char *stat)) ALLOCATOR;
void lru_free(struct lru *) NONNULL_ALL;
void lru_flush(struct lru *) NONNULL_ALL;
int lru_invalidate(struct lru *) NONNULL_ALL;

struct lru_entry *cache_lru(struct lru *, const char *key, int klen, int *new_entry, void (*pending_cb)(void*), void *cbarg) NONNULL(1,2,4);
void lru_retain(struct lru *, struct lru_entry *) NONNULL(2);
void lru_leave(struct lru *, struct lru_entry *) NONNULL(2);
void lru_store(struct lru *, struct lru_entry *, size_t) NONNULL(1,2);

void lru_foreach(struct lru *c, void (*cb)(struct lru_entry *entry, void *cbdat), void *cbdata) NONNULL(1,2);

#ifdef __cplusplus
}
#endif

#endif
