// Copyright 2018 Schibsted

#include <stdlib.h>
#include <string.h>

#include "sbp/memalloc_functions.h"
#include "vtree.h"

/*
 * Pile of helper macros to prevent us from going mad.
 */
#define VTREE_ARGS_COPY(start, argc, argv) do { \
	int VAC_max = 8; \
	va_list VAC_ap; \
	va_start(VAC_ap, start); \
	argv = alloca(sizeof(const char *) * VAC_max); \
	argc = 0; \
	while (1) { \
		const char **VAC_argv; \
		while ((argv[argc++] = va_arg(VAC_ap, const char *)) != NULL && argc < VAC_max) \
			; \
		if (argc < VAC_max) \
			break; \
		VAC_max *= 2; \
		VAC_argv = alloca(sizeof(const char *) * VAC_max); \
		memcpy(VAC_argv, argv, argc * sizeof(const char *)); \
		argv = VAC_argv; \
	} \
	argc--; \
	va_end(VAC_ap); \
} while (0)

#define bpv_getlen(vchain, cc, argc, argv) (vchain)->fun->getlen(vchain, cc, argc, argv)
#define bpv_get(vchain, cc, argc, argv) (vchain)->fun->get(vchain, cc, argc, argv)
#define bpv_haskey(vchain, cc, argc, argv) (vchain)->fun->haskey(vchain, cc, argc, argv)
#define bpv_fetch_keys(vchain, loopvar, cc, argc, argv) (vchain)->fun->fetch_keys(vchain, loopvar, cc, argc, argv)
#define bpv_fetch_values(vchain, loopvar, cc, argc, argv) (vchain)->fun->fetch_values(vchain, loopvar, cc, argc, argv)
#define bpv_fetch_byval(vchain, loopvar, cc, value, argc, argv) (vchain)->fun->fetch_byval(vchain, loopvar, cc, value, argc, argv)
#define bpv_getnode(vchain, cc, dst, argc, argv) (vchain)->fun->getnode(vchain, cc, dst, argc, argv)
#define bpv_fetch_nodes(vchain, loop, cc, argc, argv) (vchain)->fun->fetch_nodes(vchain, loop, cc, argc, argv)
#define bpv_fetch_keys_and_values(vchain, loop, cc, argc, argv) (vchain)->fun->fetch_keys_and_values(vchain, loop, cc, argc, argv)

static inline void
clear_loop_var(struct vtree_loop_var *loop) {
	loop->len = 0;
	loop->l.list = NULL;
	loop->cleanup = NULL;
}

static inline void
clear_vtree_loop_var(struct vtree_keyvals *loop) {
	loop->len = 0;
	loop->list = NULL;
	loop->cleanup = NULL;
}

int 
vtree_getlen_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, ...) {
	int res;
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	int argc = 0;

	if (!vchain || !vchain->fun)
		return 0;

	if (cache && *cache >= 0)
		return *cache;

	VTREE_ARGS_COPY(cache, argc, argv);

	res = bpv_getlen(vchain, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;

	if (cache && c == VTCACHE_CAN)
		*cache = res;

	return res;
}

int 
vtree_getlen_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, int argc, const char **argv) {
	enum vtree_cacheable c = VTCACHE_CAN;
	int res;

	if (!vchain || !vchain->fun)
		return 0;

	if (cache && *cache >= 0)
		return *cache;

	res = bpv_getlen(vchain, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;

	if (cache && c == VTCACHE_CAN)
		*cache = res;

	return res;
}

const char *
vtree_get_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, const char **cache, ...) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	const char *res;
	int argc;

	if (!vchain || !vchain->fun)
		return NULL;

	if (cache && *cache)
		return *cache;

	VTREE_ARGS_COPY(cache, argc, argv);
	res = bpv_get(vchain, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;

	if (cache && c == VTCACHE_CAN)
		*cache = res;

	return res;
}

const char *
vtree_get_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, const char **cache, int argc, const char **argv) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char *res;

	if (!vchain || !vchain->fun)
		return NULL;

	if (cache && *cache)
		return *cache;

	res = bpv_get(vchain, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;

	if (cache && c == VTCACHE_CAN)
		*cache = res;

	return res;
}

int 
vtree_getint_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, ...) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	const char *res;
	int argc;
	int ri;

	if (!vchain || !vchain->fun)
		return 0;

	if (cache && *cache) /* XXX doesn't cache 0 */
		return *cache;

	VTREE_ARGS_COPY(cache, argc, argv);
	res = bpv_get(vchain, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;

	if (res)
		ri = atoi(res);
	else
		ri = 0;

	if (cache && c == VTCACHE_CAN)
		*cache = ri;

	return ri;
}

int 
vtree_getint_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, int argc, const char **argv) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char *res;
	int ri;

	if (!vchain || !vchain->fun)
		return 0;

	if (cache && *cache) /* XXX doesn't cache 0 */
		return *cache;

	res = bpv_get(vchain, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;

	if (res)
		ri = atoi(res);
	else
		ri = 0;

	if (cache && c == VTCACHE_CAN)
		*cache = ri;

	return ri;
}

int
vtree_haskey_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, ...) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	int argc;
	int res;

	if (!vchain || !vchain->fun)
		return 0;

	if (cache && *cache >= 0)
		return *cache;

	VTREE_ARGS_COPY(cache, argc, argv);
	res = bpv_haskey(vchain, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;

	if (cache && c == VTCACHE_CAN)
		*cache = res;

	return res;
}

int
vtree_haskey_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, int argc, const char **argv) {
	enum vtree_cacheable c = VTCACHE_CAN;
	int res;

	if (!vchain || !vchain->fun)
		return 0;

	if (cache && *cache >= 0)
		return *cache;

	res = bpv_haskey(vchain, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;

	if (cache && c == VTCACHE_CAN)
		*cache = res;

	return res;
}

void
vtree_fetch_keys_cache(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, ...) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	int argc;

	if (!vchain || !vchain->fun)
		return clear_loop_var(loop);

	VTREE_ARGS_COPY(cc, argc, argv);
	bpv_fetch_keys(vchain, loop, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

void
vtree_fetch_keys_cachev(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	enum vtree_cacheable c = VTCACHE_CAN;

	if (!vchain || !vchain->fun)
		return clear_loop_var(loop);

	bpv_fetch_keys(vchain, loop, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

void
vtree_fetch_values_cache(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, ...) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	int argc;

	if (!vchain || !vchain->fun)
		return clear_loop_var(loop);

	VTREE_ARGS_COPY(cc, argc, argv);
	bpv_fetch_values(vchain, loop, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

void
vtree_fetch_values_cachev(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	enum vtree_cacheable c = VTCACHE_CAN;

	if (!vchain || !vchain->fun)
		return clear_loop_var(loop);

	bpv_fetch_values(vchain, loop, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

void
vtree_fetch_keys_by_value_cache(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, ...) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	int argc;

	if (!vchain || !vchain->fun)
		return clear_loop_var(loop);

	VTREE_ARGS_COPY(value, argc, argv);
	bpv_fetch_byval(vchain, loop, &c, value, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

void
vtree_fetch_keys_by_value_cachev(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, int argc, const char **argv) {
	enum vtree_cacheable c = VTCACHE_CAN;

	if (!vchain || !vchain->fun)
		return clear_loop_var(loop);

	bpv_fetch_byval(vchain, loop, &c, value, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

struct vtree_chain *
vtree_getnode_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, struct vtree_chain **cache, ...) {
	struct vtree_chain *res;
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	int argc;

	if (!vchain || !vchain->fun)
		return NULL;

	if (cache && *cache) {
		if (*cache == (struct vtree_chain*)-1)
			return NULL;
		return *cache;
	}

	vtree_free(dst);

	VTREE_ARGS_COPY(cache, argc, argv);
	res = bpv_getnode(vchain, &c, dst, argc, argv);

	if (res != dst) {
		/* If they didn't touch dst we should clear it because of the free above. */
		memset(dst, 0, sizeof(*dst));
	}

	if (cc && c < *cc)
		*cc = c;

	if (cache && c == VTCACHE_CAN) {
		if (res == NULL)
			*cache = (struct vtree_chain*)-1;
		else
			*cache = res;
	}

	return res;
}

struct vtree_chain *
vtree_getnode_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, struct vtree_chain **cache, int argc, const char **argv) {
	struct vtree_chain *res;
	enum vtree_cacheable c = VTCACHE_CAN;

	if (!vchain || !vchain->fun)
		return NULL;

	if (cache && *cache) {
		if (*cache == (struct vtree_chain*)-1)
			return NULL;
		return *cache;
	}

	vtree_free(dst);
	res = bpv_getnode(vchain, &c, dst, argc, argv);

	if (cc && c < *cc)
		*cc = c;

	if (cache && c == VTCACHE_CAN) {
		if (res == NULL)
			*cache = (struct vtree_chain*)-1;
		else
			*cache = res;
	}

	return res;
}

void
vtree_fetch_nodes_cache(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, ...) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	int argc;

	if (!vchain || !vchain->fun)
		return clear_loop_var(loop);

	VTREE_ARGS_COPY(cc, argc, argv);
	bpv_fetch_nodes(vchain, loop, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

void
vtree_fetch_nodes_cachev(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	enum vtree_cacheable c = VTCACHE_CAN;

	if (!vchain || !vchain->fun)
		return clear_loop_var(loop);

	bpv_fetch_nodes(vchain, loop, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

void
vtree_fetch_keys_and_values_cache(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, ...) {
	enum vtree_cacheable c = VTCACHE_CAN;
	const char **argv;
	int argc;

	if (!vchain || !vchain->fun)
		return clear_vtree_loop_var(loop);

	VTREE_ARGS_COPY(cc, argc, argv);
	bpv_fetch_keys_and_values(vchain, loop, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

void
vtree_fetch_keys_and_values_cachev(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	enum vtree_cacheable c = VTCACHE_CAN;

	if (!vchain || !vchain->fun)
		return clear_vtree_loop_var(loop);

	bpv_fetch_keys_and_values(vchain, loop, &c, argc, argv);

	if (cc && c < *cc)
		*cc = c;
}

/*
 * Shadow vtree
 */
static int
shadow_vtree_getlen(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct shadow_vtree *subtree = vchain->data;
	int res;

	if (subtree && subtree->vtree.fun) {
		if (argc == 0) {
			*cc = VTCACHE_CANT;
			return bpv_getlen(&subtree->vtree, cc, argc, argv) + bpv_getlen(vchain->next, cc, argc, argv);
		}

		res = bpv_getlen(&subtree->vtree, cc, argc, argv);
		if (res) {
			*cc = VTCACHE_CANT;
			return res;
		}
	}

	res = bpv_getlen(vchain->next, cc, argc, argv);
	if (!res)
		*cc = VTCACHE_UNKNOWN;
	return res;
}

static const char *
shadow_vtree_get(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct shadow_vtree *subtree = vchain->data;
	const char *res;

	if (subtree && subtree->vtree.fun) {
		if (argc == 0) {
			return NULL;
		}

		res = bpv_get(&subtree->vtree, cc, argc, argv);
		if (res) {
			*cc = VTCACHE_CANT;
			return res;
		}
	}

	if (!vchain->next || !vchain->next->fun) {
		*cc = VTCACHE_UNKNOWN;
		return NULL;
	}

	res = bpv_get(vchain->next, cc, argc, argv);
	if (!res)
		*cc = VTCACHE_UNKNOWN;
	return res;
}

static int
shadow_vtree_haskey(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct shadow_vtree *subtree = vchain->data;
	int res;

	if (subtree && subtree->vtree.fun) {
		if (argc == 0) {
			return 0;
		}

		res = bpv_haskey(&subtree->vtree, cc, argc, argv);
		if (res) {
			*cc = VTCACHE_CANT;
			return 1;
		}
	}

	res = bpv_haskey(vchain->next, cc, argc, argv);
	if (!res)
		*cc = VTCACHE_UNKNOWN;
	return res;
}

static int
strcmpp(const void *a, const void *b) {
	return strcmp(*(char * const *)a, *(char * const *)b);
}

static void
shadow_vtree_merged_cleanup(struct vtree_loop_var *loop) {
	free(loop->l.list);
}

static void
shadow_vtree_fetch_keys(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct shadow_vtree *subtree = vchain->data;

	if (subtree && subtree->vtree.fun) {
		if (argc == 0) {
			struct vtree_loop_var l1, l2;
			int i1, i2;

			*cc = VTCACHE_CANT;

			bpv_fetch_keys(&subtree->vtree, &l1, cc, argc, argv);
			bpv_fetch_keys(vchain->next, &l2, cc, argc, argv);

			qsort(l1.l.list, l1.len, sizeof(const char *), strcmpp);
			qsort(l2.l.list, l2.len, sizeof(const char *), strcmpp);

			loop->l.list = xmalloc((l1.len + l2.len) * sizeof(*loop->l.list));
			loop->len = 0;
			i1 = i2 = 0;
			while (i1 < l1.len && i2 < l2.len) {
				int c = strcmp(l1.l.list[i1], l2.l.list[i2]);
				if (c < 0) {
					loop->l.list[loop->len++] = xstrdup(l1.l.list[i1++]);
				} else if (c == 0) {
					loop->l.list[loop->len++] = xstrdup(l1.l.list[i1++]);
					i2++;
				} else {
					loop->l.list[loop->len++] = xstrdup(l2.l.list[i2++]);
				}
			}
			if (l1.cleanup)
				l1.cleanup(&l1);
			if (l2.cleanup)
				l2.cleanup(&l2);
			loop->cleanup = shadow_vtree_merged_cleanup;
			return;
		}

		bpv_fetch_keys(&subtree->vtree, loop, cc, argc, argv);

		if (loop->len) {
			*cc = VTCACHE_CANT;
			return;
		}
	}

	bpv_fetch_keys(vchain->next, loop, cc, argc, argv);
	if (!loop->len)
		*cc = VTCACHE_UNKNOWN;
}

static void
shadow_vtree_fetch_values(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct shadow_vtree *subtree = vchain->data;

	if (subtree && subtree->vtree.fun) {
		bpv_fetch_values(&subtree->vtree, loop, cc, argc, argv);

		if (loop->len) {
			*cc = VTCACHE_CANT;
			return;
		}
	}

	bpv_fetch_values(vchain->next, loop, cc, argc, argv);
	if (!loop->len)
		*cc = VTCACHE_UNKNOWN;
}

static void
shadow_vtree_fetch_keys_by_value(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, int argc, const char **argv) {
	struct shadow_vtree *subtree = vchain->data;

	if (subtree && subtree->vtree.fun) {
		bpv_fetch_byval(&subtree->vtree, loop, cc, value, argc, argv);

		if (loop->len) {
			*cc = VTCACHE_CANT;
			return;
		}
	}

	bpv_fetch_byval(vchain->next, loop, cc, value, argc, argv);
	if (!loop->len)
		*cc = VTCACHE_UNKNOWN;
}

static struct vtree_chain *
shadow_vtree_getnode(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, int argc, const char **argv) {
	struct shadow_vtree *subtree = vchain->data;
	struct vtree_chain *res;

	if (subtree && subtree->vtree.fun) {
		if (argc == 0) {
			*dst = *vchain;
			return dst;
		}

		res = bpv_getnode(&subtree->vtree, cc, dst, argc, argv);

		if (res) {
			*cc = VTCACHE_CANT;
			return res;
		}
	}

	res = bpv_getnode(vchain->next, cc, dst, argc, argv);
	if (!res)
		*cc = VTCACHE_UNKNOWN;
	return res;
}

static void
shadow_vtree_fetch_nodes(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct shadow_vtree *subtree = vchain->data;

	if (subtree && subtree->vtree.fun) {
		bpv_fetch_nodes(&subtree->vtree, loop, cc, argc, argv);

		if (loop->len) {
			*cc = VTCACHE_CANT;
			return;
		}
	}

	bpv_fetch_nodes(vchain->next, loop, cc, argc, argv);
	if (!loop->len)
		*cc = VTCACHE_UNKNOWN;
}

static int
shadow_vtree_kv_cmp(const void *a, const void *b) {
	const struct vtree_keyvals_elem *ae = a;
	const struct vtree_keyvals_elem *be = b;

	return strcmp(ae->key, be->key);
}

static void
shadow_vtree_kv_cleanup(struct vtree_keyvals *loop) {
	int i;

	for (i = 0; i < loop->len; i++) {
		free((void *)loop->list[i].key);
	}
	free(loop->list);
}

static void
shadow_vtree_fetch_keys_and_values(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct shadow_vtree *subtree = vchain->data;

	if (argc == 0) {
		memset(loop, 0, sizeof(*loop));
		return;
	}

	if (subtree && subtree->vtree.fun) {
		if (argc == 1 && argv[0] == VTREE_LOOP) {
			struct vtree_keyvals l1, l2;
			int i1, i2;

			*cc = VTCACHE_CANT;

			bpv_fetch_keys_and_values(&subtree->vtree, &l1, cc, argc, argv);
			bpv_fetch_keys_and_values(vchain->next, &l2, cc, argc, argv);

			qsort(l1.list, l1.len, sizeof(*l1.list), shadow_vtree_kv_cmp);
			qsort(l2.list, l2.len, sizeof(*l2.list), shadow_vtree_kv_cmp);

			loop->list = xmalloc((l1.len + l2.len) * sizeof(*loop->list));
			loop->len = 0;
			i1 = i2 = 0;
			if (l1.type == l2.type)
				loop->type = l1.type;
			else
				loop->type = vktUnknown;
			while (i1 < l1.len || i2 < l2.len) {
				int c = (i1 == l1.len ? 1 : (i2 == l2.len ? -1 : 0));
				if (!c) {
					/* Don't merge lists, concat them. */
					if (l1.type == vktList)
						c = -1;
					else if (l2.type == vktList)
						c = 1;
					else
						c = strcmp(l1.list[i1].key, l2.list[i2].key);
				}

				if (c < 0) {
					loop->list[loop->len] = l1.list[i1++];
				} else if (c == 0) {
					loop->list[loop->len] = l1.list[i1++];
					i2++;
				} else {
					loop->list[loop->len] = l2.list[i2++];
				}
				if (loop->type != vktList) {
					if (loop->list[loop->len].key)
						loop->list[loop->len].key = xstrdup(loop->list[loop->len].key);
					else
						xasprintf((char**)&loop->list[loop->len].key, "%d", loop->len);
				}
				loop->len++;
			}

			if (l1.cleanup)
				l1.cleanup(&l1);
			if (l2.cleanup)
				l2.cleanup(&l2);
			loop->cleanup = shadow_vtree_kv_cleanup;
			return;
		}

		bpv_fetch_keys_and_values(&subtree->vtree, loop, cc, argc, argv);

		if (loop->len) {
			*cc = VTCACHE_CANT;
			return;
		}
	}

	bpv_fetch_keys_and_values(vchain->next, loop, cc, argc, argv);
	if (!loop->len)
		*cc = VTCACHE_UNKNOWN;
}

static void
shadow_vtree_free(struct vtree_chain *vtree) {
	struct shadow_vtree *subtree = vtree->data;

	if (subtree) {
		/* Note: calling vtree_free here is probably a bug, it should be up to the
		 * free_cb function. But we'll keep it for backwards compatibility.
		 * Use shadow_vtree_weakref for the version not doing this call.
		 */
		vtree_free(&subtree->vtree);
		if (subtree->free_cb)
			subtree->free_cb(subtree);
	}
}

static void
shadow_vtree_free_weakref(struct vtree_chain *vtree) {
	struct shadow_vtree *subtree = vtree->data;

	if (subtree && subtree->free_cb)
		subtree->free_cb(subtree);
}

void
shadow_vtree_init(struct vtree_chain *res, struct shadow_vtree *top, struct vtree_chain *bottom) {
	res->fun = &shadow_vtree;
	res->data = top;
	res->next = bottom;
}

const struct vtree_dispatch shadow_vtree = {
	shadow_vtree_getlen,
	shadow_vtree_get,
	shadow_vtree_haskey,
	shadow_vtree_fetch_keys,
	shadow_vtree_fetch_values,
	shadow_vtree_fetch_keys_by_value,
	shadow_vtree_getnode,
	shadow_vtree_fetch_nodes,
	shadow_vtree_fetch_keys_and_values,
	shadow_vtree_free
};

const struct vtree_dispatch shadow_vtree_weakref = {
	shadow_vtree_getlen,
	shadow_vtree_get,
	shadow_vtree_haskey,
	shadow_vtree_fetch_keys,
	shadow_vtree_fetch_values,
	shadow_vtree_fetch_keys_by_value,
	shadow_vtree_getnode,
	shadow_vtree_fetch_nodes,
	shadow_vtree_fetch_keys_and_values,
	shadow_vtree_free_weakref
};

static int
prefix_vtree_getlen(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	const char *prefix = vchain->data;
	size_t plen = strlen(prefix);
	int res = 0;

	if (!vchain->next || !vchain->next->fun)
		return 0;

	if (argc && !strncmp(prefix, argv[0], plen)) {
		if (argv[0][plen] == '\0') {
			res = bpv_getlen(vchain->next, cc, argc - 1, argv + 1);
		} else if (argv[0][plen] == '.') {
			argv[0] += plen + 1;
			res = bpv_getlen(vchain->next, cc, argc, argv);
			argv[0] -= plen + 1;
		}
	}

	return res;
}

static const char *
prefix_vtree_get(struct vtree_chain *vchain, enum  vtree_cacheable *cc, int argc, const char **argv) {
	const char *prefix = vchain->data;
	size_t plen = strlen(prefix);
	const char *res = NULL;

	if (!vchain->next || !vchain->next->fun)
		return NULL;

	if (argc && !strncmp(prefix, argv[0], plen)) {
		if (argv[0][plen] == '\0') {
			res = bpv_get(vchain->next, cc, argc - 1, argv + 1);
		} else if (argv[0][plen] == '.') {
			argv[0] += plen + 1;
			res = bpv_get(vchain->next, cc, argc, argv);
			argv[0] -= plen + 1;
		}
	}

	return res;
}

static int
prefix_vtree_haskey(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	const char *prefix = vchain->data;
	size_t plen = strlen(prefix);
	int res = 0;

	if (!vchain->next || !vchain->next->fun)
		return 0;

	if (argc && !strncmp(prefix, argv[0], plen)) {
		if (argv[0][plen] == '\0') {
			res = bpv_haskey(vchain->next, cc, argc - 1, argv + 1);
		} else if (argv[0][plen] == '.') {
			argv[0] += plen + 1;
			res = bpv_haskey(vchain->next, cc, argc, argv);
			argv[0] -= plen + 1;
		}
	}

	return res;
}

static void
prefix_vtree_keys_cleanup(struct vtree_loop_var *loop) {
	free(loop->l.list);
}

static void
prefix_vtree_fetch_keys(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	const char *prefix = vchain->data;
	size_t plen = strlen(prefix);

	if (argc == 0) {
		loop->len = 1;
		loop->l.list = xmalloc(sizeof(const char *));
		loop->l.list[0] = prefix;
		loop->cleanup = prefix_vtree_keys_cleanup;
		return;
	}

	if (!strncmp(prefix, argv[0], plen)) {
		if (argv[0][plen] == '\0') {
			bpv_fetch_keys(vchain->next, loop, cc, argc - 1, argv + 1);
		} else if (argv[0][plen] == '.') {
			argv[0] += plen + 1;
			bpv_fetch_keys(vchain->next, loop, cc, argc, argv);
			argv[0] -= plen + 1;
		} else {
			memset(loop, 0, sizeof(*loop));
		}
	} else {
		memset(loop, 0, sizeof(*loop));
	}
}

static void
prefix_vtree_fetch_values(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	const char *prefix = vchain->data;
	size_t plen = strlen(prefix);

	if (argc && (argv[0] == VTREE_LOOP || !strncmp(prefix, argv[0], plen))) {
		if (argv[0] == VTREE_LOOP || argv[0][plen] == '\0') {
			bpv_fetch_values(vchain->next, loop, cc, argc - 1, argv + 1);
		} else if (argv[0][plen] == '.') {
			argv[0] += plen + 1;
			bpv_fetch_values(vchain->next, loop, cc, argc, argv);
			argv[0] -= plen + 1;
		} else {
			memset(loop, 0, sizeof(*loop));
		}
	} else {
		memset(loop, 0, sizeof(*loop));
	}
}

static void
prefix_vtree_fetch_keys_by_value(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, int argc, const char **argv) {
	const char *prefix = vchain->data;
	size_t plen = strlen(prefix);

	if (argc && (argv[0] == VTREE_LOOP || !strncmp(prefix, argv[0], plen))) {
		if (argv[0] == VTREE_LOOP || argv[0][plen] == '\0') {
			bpv_fetch_byval(vchain->next, loop, cc, value, argc - 1, argv + 1);
		} else if (argv[0][plen] == '.') {
			argv[0] += plen + 1;
			bpv_fetch_byval(vchain->next, loop, cc, value, argc, argv);
			argv[0] -= plen + 1;
		} else {
			memset(loop, 0, sizeof(*loop));
		}
	} else {
		memset(loop, 0, sizeof(*loop));
	}
}

static struct vtree_chain *
prefix_vtree_getnode(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, int argc, const char **argv) {
	const char *prefix = vchain->data;
	size_t plen = strlen(prefix);
	struct vtree_chain *res = NULL;

	if (!argc) {
		*dst = *vchain;
		return dst;
	}

	if (!strncmp(prefix, argv[0], plen)) {
		if (argv[0][plen] == '\0') {
			res = bpv_getnode(vchain->next, cc, dst, argc - 1, argv + 1);
		} else if (argv[0][plen] == '.') {
			argv[0] += plen + 1;
			res = bpv_getnode(vchain->next, cc, dst, argc, argv);
			argv[0] -= plen + 1;
		}
	}

	return res;
}

static void
prefix_vtree_nodes_cleanup(struct vtree_loop_var *loop) {
	free(loop->l.vlist);
}

static void
prefix_vtree_fetch_nodes(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	const char *prefix = vchain->data;
	size_t plen = strlen(prefix);

	if (argc == 0) {
		loop->len = 1;
		loop->l.vlist = xmalloc(sizeof(*loop->l.vlist));
		*loop->l.vlist = *vchain->next;
		loop->cleanup = prefix_vtree_nodes_cleanup;
	} else if (!strncmp(prefix, argv[0], plen)) {
		if (argv[0][plen] == '\0') {
			bpv_fetch_nodes(vchain->next, loop, cc, argc - 1, argv + 1);
		} else if (argv[0][plen] == '.') {
			argv[0] += plen + 1;
			bpv_fetch_nodes(vchain->next, loop, cc, argc, argv);
			argv[0] -= plen + 1;
		} else {
			memset(loop, 0, sizeof(*loop));
		}
	} else {
		memset(loop, 0, sizeof(*loop));
	}
}

static void
prefix_vtree_keyval_cleanup(struct vtree_keyvals *loop) {
	free(loop->list);
}

static void
prefix_vtree_fetch_keys_and_values(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	const char *prefix = vchain->data;
	size_t plen = strlen(prefix);

	if (argc == 1 && argv[0] == VTREE_LOOP) {
		loop->type = vktDict;
		loop->len = 1;
		loop->list = xmalloc(sizeof(*loop->list));
		loop->list[0].type = vkvNode;
		loop->list[0].v.node = *vchain->next;
		loop->list[0].key = prefix;
		loop->cleanup = prefix_vtree_keyval_cleanup;
		return;
	}

	if (argc && (argv[0] == VTREE_LOOP || !strncmp(prefix, argv[0], plen))) {
		if (argv[0] == VTREE_LOOP || argv[0][plen] == '\0') {
			bpv_fetch_keys_and_values(vchain->next, loop, cc, argc - 1, argv + 1);
		} else if (argv[0][plen] == '.') {
			argv[0] += plen + 1;
			bpv_fetch_keys_and_values(vchain->next, loop, cc, argc, argv);
			argv[0] -= plen + 1;
		} else {
			memset(loop, 0, sizeof(*loop));
		}
	} else {
		memset(loop, 0, sizeof(*loop));
	}
}

static void
prefix_vtree_free(struct vtree_chain *vtree) {
}

static void
prefix_vtree_free_prefix_free(struct vtree_chain *vtree) {
	free(vtree->data);
}

const struct vtree_dispatch prefix_vtree = {
	prefix_vtree_getlen,
	prefix_vtree_get,
	prefix_vtree_haskey,
	prefix_vtree_fetch_keys,
	prefix_vtree_fetch_values,
	prefix_vtree_fetch_keys_by_value,
	prefix_vtree_getnode,
	prefix_vtree_fetch_nodes,
	prefix_vtree_fetch_keys_and_values,
	prefix_vtree_free
};

const struct vtree_dispatch prefix_vtree_free_prefix = {
	prefix_vtree_getlen,
	prefix_vtree_get,
	prefix_vtree_haskey,
	prefix_vtree_fetch_keys,
	prefix_vtree_fetch_values,
	prefix_vtree_fetch_keys_by_value,
	prefix_vtree_getnode,
	prefix_vtree_fetch_nodes,
	prefix_vtree_fetch_keys_and_values,
	prefix_vtree_free_prefix_free
};

void
prefix_vtree_init(struct vtree_chain *res, const char *prefix, struct vtree_chain *tree) {
	res->fun = &prefix_vtree;
	res->data = (void *)prefix;
	res->next = tree;
}
