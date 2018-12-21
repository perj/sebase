// Copyright 2018 Schibsted

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "lru.h"
#include "memalloc_functions.h"
#include "spinlock.h"

#include <unistd.h>

#define TH(x) x

static __inline  int
lru_compare(const struct lru_entry *a, const struct lru_entry *b) {
	return strcmp(a->key, b->key);
}

struct lru {
	size_t size;
	size_t max_size;
	TH(pthread_mutex_t mutex;)
	void (*destr)(void*);
	RB_HEAD(lru_tree, lru_entry) tree;
	TAILQ_HEAD(lru_head, lru_entry) lru;
	void (*lru_stat_cb)(struct lru *c, const char *stat);
	uint64_t rindex;
};

RB_GENERATE_STATIC(lru_tree, lru_entry, te, lru_compare);

static void
lru_stat(struct lru *c, const char *stat)
{
	if (c->lru_stat_cb)
		c->lru_stat_cb(c, stat);
}

struct lru *
lru_init(size_t size, void (*destr)(void*), void (*lru_stat_cb)(struct lru *c, const char *stat)) {
	struct lru *c = xmalloc(sizeof(*c));
	c->size = 0;
	c->max_size = size;
	c->destr = destr;
	c->rindex = 0;
	TH(pthread_mutex_init(&c->mutex, NULL));
	RB_INIT(&c->tree);
	TAILQ_INIT(&c->lru);
	c->lru_stat_cb = lru_stat_cb;

	return c;
}

void
lru_flush(struct lru *c) {
	struct lru_entry *o;

	TH(pthread_mutex_lock(&c->mutex));
	while ((o = TAILQ_FIRST(&c->lru))) {
		while (o->users)
			usleep(100000);
		TAILQ_REMOVE(&c->lru, o, tq);
		RB_REMOVE(lru_tree, &c->tree, o);
		if (o->storage && c->destr)
			c->destr(o->storage);
		TH(pthread_mutex_destroy(&o->mutex));
		TH(pthread_cond_destroy(&o->cond));
		free(o);
	}
	c->size = 0;
	TH(pthread_mutex_unlock(&c->mutex));
}

int
lru_invalidate(struct lru *c) {
	TH(pthread_mutex_lock(&c->mutex));
	if (c->rindex++ == UINT64_MAX) {
		c->rindex = 0;
		TH(pthread_mutex_unlock(&c->mutex));
		return 1;
	}
	TH(pthread_mutex_unlock(&c->mutex));
	return 0;
}

void
lru_free(struct lru *c)
{
	lru_flush(c);
	free(c);
}

struct lru_entry *
cache_lru(struct lru *c, const char *key, int klen, int *new_entry, void (*pending_cb)(void *), void *cbarg) {
	struct lru_entry *e;
	struct lru_entry f;

	TH(pthread_mutex_lock(&c->mutex));

	if (klen < 0)
		klen = strlen(key);

	char combined_key[32 + 1 + klen + 1];
	int cklen = snprintf(combined_key, sizeof(combined_key), "%"PRIu64"#%.*s", c->rindex, klen, key);
	f.key = combined_key;

	e = RB_FIND(lru_tree, &c->tree, &f);

	if (!e) {
		/* lru_stat("CACHE MISS"); */
		while (c->size >= c->max_size) {
			struct lru_entry *o = TAILQ_FIRST(&c->lru);
			/* Remove the oldest non-busy entry */

			/* Skip entires with a use count. */
			while (o && o->users) {
				o = TAILQ_NEXT(o, tq);
			}

			if (o == NULL) {
				TH(pthread_mutex_unlock(&c->mutex));
				lru_stat(c, "CACHE FULL");
				*new_entry = 1;
				return NULL;
			}
			lru_stat(c, "CACHE OUT");
			TAILQ_REMOVE(&c->lru, o, tq);
			RB_REMOVE(lru_tree, &c->tree, o);
			if (o->storage && c->destr)
				c->destr(o->storage);
			c->size -= o->storage_size;
			TH(pthread_mutex_destroy(&o->mutex));
			TH(pthread_cond_destroy(&o->cond));
			free(o);
		}

		e = xmalloc(sizeof(*e) + cklen + 1);
		e->key = (char*)(e + 1);
		strcpy(e->key, combined_key);
		TH(pthread_mutex_init(&e->mutex, NULL));
		TH(pthread_cond_init(&e->cond, NULL));
		e->pending = true;
		e->pending_thread = pthread_self();
		e->storage = NULL;
		e->storage_size = 0;
		e->users = 1;
		*new_entry = 1;
		TAILQ_INSERT_TAIL(&c->lru, e, tq);
		RB_INSERT(lru_tree, &c->tree, e);
	} else if (e->pending && pthread_equal(e->pending_thread, pthread_self())) {
		lru_stat(c, "CACHE RECURSE");
		*new_entry = 0;
		e = NULL;
	} else {
		/* lru_stat("CACHE HIT"); */

		spinlock_add_int(&e->users, 1);

		*new_entry = 0;
		TAILQ_REMOVE(&c->lru, e, tq);
		TAILQ_INSERT_TAIL(&c->lru, e, tq);

		if (e->pending) {
			lru_stat(c, "CACHE PENDING");
			pthread_mutex_unlock(&c->mutex);
			if (pending_cb)
				pending_cb(cbarg);
			pthread_mutex_lock(&e->mutex);
			while (e->pending) {
			       pthread_cond_wait(&e->cond, &e->mutex);
			}
			pthread_mutex_unlock(&e->mutex);
		} else {
			pthread_mutex_unlock(&c->mutex);
		}
		return e;
	}

	TH(pthread_mutex_unlock(&c->mutex));

	return e;
}

void
lru_store(struct lru *c, struct lru_entry *e, size_t sz) {
	e->storage_size = sz;
	__sync_fetch_and_add(&c->size, sz);
	TH(pthread_mutex_lock(&e->mutex));
	e->pending = 0;
	TH(pthread_cond_broadcast(&e->cond));
	TH(pthread_mutex_unlock(&e->mutex));
}

void
lru_retain(struct lru *c, struct lru_entry *e) {
	spinlock_add_int(&e->users, 1);
}

void
lru_leave(struct lru *c, struct lru_entry *e) {
	spinlock_add_int(&e->users, -1);
}

void
lru_foreach(struct lru *c, void (*cb)(struct lru_entry *entry, void *cbdata), void *cbdata)
{
	struct lru_entry *e;

	TH(pthread_mutex_lock(&c->mutex));

	TAILQ_FOREACH(e, &c->lru, tq) {
		cb(e, cbdata);
	}

	TH(pthread_mutex_unlock(&c->mutex));
}
