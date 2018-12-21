// Copyright 2018 Schibsted

#include "stringpool.h"
#include "avl.h"
#include "memalloc_functions.h"

#include <stdlib.h>
#include <string.h>

/*
 * String interning, currently using AVL tree.
 *
 * Based on previous simplevars.
 * Probably plenty of room for optimizations.
 * Consider using for it for mempool_strdup.
 */

struct stringpool {
	struct avl_node *strs;
	int n;
};

struct stringpool_entry {
	struct avl_node tree;
	char *str;
	ssize_t len;
	int index;
};

static int
stringpool_compare(const struct avl_node *an, const struct avl_node *bn) {
	struct stringpool_entry *a = avl_data(an, struct stringpool_entry, tree);
	struct stringpool_entry *b = avl_data(bn, struct stringpool_entry, tree);

	if (a->len != b->len)
		return a->len - b->len;
	return strncmp(a->str, b->str, a->len);
}

static struct stringpool_entry *
stringpool_lookup(struct stringpool *pool, const char *str, ssize_t *len) {
	struct stringpool_entry s;
	struct avl_node *n;
	if (*len < 0)
		*len = strlen(str);
	s.str = (char*)str;
	s.len = *len;
	n = avl_lookup(&s.tree, &pool->strs, stringpool_compare);
	return n ? avl_data(n, struct stringpool_entry, tree) : NULL;
}

static struct stringpool_entry *
stringpool_add(struct stringpool *pool, const char *str, ssize_t len) {
	struct stringpool_entry *e = xmalloc(sizeof(*e) + len + 1);
	e->str = (char*)(e + 1);
	e->len = len;
	e->index = pool->n++;
	memcpy(e->str, str, len);
	e->str[len] = '\0';
	avl_insert(&e->tree, &pool->strs, stringpool_compare);
	return e;
}

const char *
stringpool_get(struct stringpool *pool, const char *str, ssize_t len) {
	struct stringpool_entry *e;
	if ((e = stringpool_lookup(pool, str, &len)))
		return e->str;
	return stringpool_add(pool, str, len)->str;
}

int
stringpool_get_index(struct stringpool *pool, const char *str, ssize_t len) {
	struct stringpool_entry *e;
	if ((e = stringpool_lookup(pool, str, &len)))
		return e->index;
	return stringpool_add(pool, str, len)->index;
}

int
stringpool_search_index(struct stringpool *pool, const char *str, ssize_t len) {
	struct stringpool_entry *e;
	if ((e = stringpool_lookup(pool, str, &len)))
		return e->index;
	return -1;
}

struct stringpool *
stringpool_new(void) {
	return zmalloc(sizeof (struct stringpool));
}

void
stringpool_free(struct stringpool *pool) {
	struct avl_node *n;
	struct avl_it it;
	struct stringpool_entry *entry, *head = NULL;

	if (!pool)
		return;

	avl_it_init(&it, pool->strs, NULL, NULL, stringpool_compare);

	/*
	 * Removing the elements from the tree would be a pain since it would cause
	 * lots of rebalancing operations on a tree that will be nuked anyway.
	 * So instead of doing this nicely, walk the tree, build a list of elements
	 * to free with the str pointers, then sweep.
	 */
	while ((n = avl_it_next(&it)) != NULL) {
		entry = avl_data(n, struct stringpool_entry, tree);

		entry->str = (char *)head;
		head = entry;
	}

	while ((entry = head) != NULL) {
		head = (struct stringpool_entry *)(void*)entry->str;
		free(entry);
	}

	free(pool);
}
