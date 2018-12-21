// Copyright 2018 Schibsted

#include "memalloc_functions.h"
#include "stringmap.h"
#include "stringpool.h"

struct stringmap_entry {
	int a, n;
	union {
		const char *one;
		const char **list;
	};
};

struct stringmap {
	struct stringpool *keys;
	struct stringpool *values;
	int nentries, aentries;
	struct stringmap_entry *entries;
};

struct stringmap *
sm_new(void) {
	struct stringmap *sm = zmalloc(sizeof(*sm));
	sm->keys = stringpool_new();
	sm->values = stringpool_new();
	return sm;
}

void
sm_free(struct stringmap *sm) {
	if (!sm)
		return;
	stringpool_free(sm->keys);
	stringpool_free(sm->values);
	for (int i = 0 ; i < sm->nentries ; i++) {
		if (sm->entries[i].n > 1)
			free(sm->entries[i].list);
	}
	free(sm->entries);
	free(sm);
}

void
sm_insert(struct stringmap *sm, const char *key, ssize_t klen, const char *value, ssize_t vlen) {
	int idx = stringpool_get_index(sm->keys, key, klen);
	if (idx < sm->nentries) {
		struct stringmap_entry *e = &sm->entries[idx];
		if (e->n == 1) {
			e->a = 4;
			const char **list = xmalloc(e->a * sizeof(*list));
			list[0] = e->one;
			e->list = list;
		} else if (e->n == e->a) {
			e->a *= 2;
			e->list = xrealloc(e->list, e->a * sizeof(*e->list));
		}
		e->list[e->n++] = stringpool_get(sm->values, value, vlen);
		return;
	}
	if (idx >= sm->aentries) {
		sm->aentries += sm->aentries ?: 4;
		sm->entries = xrealloc(sm->entries, sm->aentries * sizeof(*sm->entries));
	}
	struct stringmap_entry *e = &sm->entries[sm->nentries++];
	e->n = 1;
	e->one = stringpool_get(sm->values, value, vlen);
}

const char *
sm_get(struct stringmap *sm, const char *key, ssize_t klen, int index) {
	int idx = sm ? stringpool_search_index(sm->keys, key, klen) : -1;
	if (idx < 0 || idx >= sm->nentries)
		return NULL;
	struct stringmap_entry *e = &sm->entries[idx];
	if (index < 0 || index >= e->n)
		return NULL;
	if (e->n == 1)
		return e->one;
	return e->list[index];
}

void
sm_getlist(struct stringmap *sm, const char *key, ssize_t klen, struct stringmap_list *out_list) {
	int idx = sm ? stringpool_search_index(sm->keys, key, klen) : -1;
	if (idx < 0 || idx >= sm->nentries) {
		out_list->n = 0;
		out_list->list = NULL;
		return;
	}
	struct stringmap_entry *e = &sm->entries[idx];
	out_list->n = e->n;
	if (e->n == 1)
		out_list->list = &e->one;
	else
		out_list->list = e->list;
}
