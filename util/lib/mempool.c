// Copyright 2018 Schibsted

#include "queue.h"

#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#include "mempool.h"

#define roundup(x, y)   ((((x)+((y)-1))/(y))*(y))

struct mempool_entry
{
	TAILQ_ENTRY(mempool_entry) tq;

	unsigned char *base;
	size_t sz;
	unsigned char *curr;
};

struct mempool
{
	TAILQ_HEAD(, mempool_entry) entries;
	size_t totsz;
};

struct mempool *
mempool_create(size_t firstsz) {
	struct mempool *res;
	struct mempool_entry *entry;

	firstsz = roundup(firstsz, getpagesize());

	unsigned char *newbase = mmap(NULL, firstsz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	if (newbase == MAP_FAILED)
		return NULL;

	res = (struct mempool*)(void*)newbase;
	TAILQ_INIT(&res->entries);
	res->totsz = firstsz;

	entry = (struct mempool_entry*)(void*)(newbase + roundup(sizeof (struct mempool), 16));
	entry->base = newbase;
	entry->sz = firstsz;
	entry->curr = (unsigned char*)entry + roundup(sizeof (struct mempool_entry), 16);

	TAILQ_INSERT_HEAD(&res->entries, entry, tq);
	return res;
}

void *
mempool_alloc(struct mempool *pool, size_t sz) {
	if (!pool)
		return calloc(1, sz);

	struct mempool_entry *entry = TAILQ_FIRST(&pool->entries);

	if (sz <= entry->sz - (entry->curr - entry->base)) {
		void *res = entry->curr;

		entry->curr += roundup(sz, 16);
		return res;
	}

	size_t newsz = pool->totsz;
	while (newsz < sz + sizeof(struct mempool_entry))
		newsz *= 2;

	unsigned char *newbase = mmap(NULL, newsz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	if (newbase == MAP_FAILED)
		return NULL;

	entry = (struct mempool_entry*)(void*)newbase;
	entry->base = newbase;
	entry->sz = newsz;
	entry->curr = newbase + roundup(sizeof (struct mempool_entry), 16);

	TAILQ_INSERT_HEAD(&pool->entries, entry, tq);
	/*pool->totsz += newsz;*/

	void *res = entry->curr;
	entry->curr += roundup(sz, 16);
	return res;
}

const char *
mempool_strdup(struct mempool *pool, const char *str, ssize_t len) {
	char *res;

	if (len < 0)
		len = strlen(str);

	res = mempool_alloc(pool, len + 1);
	if (!res)
		return NULL;
	memcpy(res, str, len);
	res[len] = '\0';

	return res;
}

void
mempool_finalize(struct mempool *pool) {
	struct mempool_entry *entry;

	TAILQ_FOREACH(entry, &pool->entries, tq) {
		mprotect(entry->base, entry->sz, PROT_READ);
	}
}

void
mempool_free(struct mempool *pool) {
	struct mempool_entry *entry, *nentry;

	for (entry = TAILQ_FIRST(&pool->entries); entry ; entry = nentry) {
		nentry = TAILQ_NEXT(entry, tq);

		munmap(entry->base, entry->sz);
	}
}

