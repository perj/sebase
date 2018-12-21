// Copyright 2018 Schibsted

#include "vtree_value.h"

/*
 * Normally a vtree node is a list of keys of values, but this one is just a single value.
 * Thus most things, e.g. checking for a key or fetching sub values, won't apply.
 */

static int
vtree_value_getlen(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	return 0;
}

static const char *
vtree_value_get(struct vtree_chain *vchain, enum  vtree_cacheable *cc, int argc, const char **argv) {
	if (argc != 0)
		return NULL;
	return vchain->data;
}

static int
vtree_value_haskey(struct vtree_chain *vchain, enum  vtree_cacheable *cc, int argc, const char **argv) {
	return argc == 0;
}

static void
vtree_value_fetch(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	loop->len = 0;
	loop->cleanup = NULL;
}

static void
vtree_value_fetch_keys_by_value(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, int argc, const char **argv) {
	loop->len = 0;
	loop->cleanup = NULL;
}

static struct vtree_chain *
vtree_value_getnode(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, int argc, const char **argv) {
	if (argc != 0)
		return NULL;

	dst->fun = &vtree_value_vtree;
	dst->data = vchain->data;
	dst->next = NULL;
	return dst;
}

static void
vtree_value_fetch_keys_and_values(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	loop->len = 0;
	loop->cleanup = NULL;
}

const struct vtree_dispatch vtree_value_vtree = {
	vtree_value_getlen,
	vtree_value_get,
	vtree_value_haskey,
	vtree_value_fetch, /* fetch_keys, */
	vtree_value_fetch, /* fetch_values, */
	vtree_value_fetch_keys_by_value,
	vtree_value_getnode,
	vtree_value_fetch, /* vtree_value_fetch_nodes, */
	vtree_value_fetch_keys_and_values,
	NULL, /* vtree_value_free */
};

static void
vtree_value_free(struct vtree_chain *vchain) {
	free(vchain->data);
}

const struct vtree_dispatch vtree_value_free_vtree = {
	vtree_value_getlen,
	vtree_value_get,
	vtree_value_haskey,
	vtree_value_fetch, /* fetch_keys, */
	vtree_value_fetch, /* fetch_values, */
	vtree_value_fetch_keys_by_value,
	vtree_value_getnode,
	vtree_value_fetch, /* vtree_value_fetch_nodes, */
	vtree_value_fetch_keys_and_values,
	vtree_value_free
};
