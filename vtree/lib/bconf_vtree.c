// Copyright 2018 Schibsted

#include <stdlib.h>
#include <string.h>

#include "bconf.h"
#include "sbp/memalloc_functions.h"
#include "vtree.h"

struct ctemplates_vtree_data {
	struct bconf_node *bconf_lowprio;
	struct bconf_node *bconf_highprio;
	enum vtree_cacheable highprio_cachelevel;
};

static struct vtree_chain *_bconf_getnode(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, int argc, const char **argv);
static void vtree_bconf_fetch_nodes(struct bconf_node *node, struct vtree_loop_var *loop, int argc, const char **argv);
static struct vtree_chain *_bconf_node_getnode(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, int argc, const char **argv);
static void vtree_bconf_fetch_keys_and_values(struct bconf_node *node, struct vtree_keyvals *loop, int argc, const char **argv);

static int
_bconf_getlen(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_highprio;

	if (node) {
		node = bconf_vasget(node, NULL, argc, argv);
		if (node) {
			*cc = ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel;
			return bconf_count(node);
		}
	}

	node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_lowprio;
	node = bconf_vasget(node, NULL, argc, argv);
	if (node)
		return bconf_count(node);

	if (((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel < VTCACHE_CAN)
		*cc = VTCACHE_UNKNOWN;
	return 0;
}

static const char*
_bconf_get(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_highprio;

	if (node) {
		node = bconf_vasget(node, NULL, argc, argv);
		if (node) {
			*cc = ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel;
			return bconf_value(node);
		}
	}

	node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_lowprio;
	node = bconf_vasget(node, NULL, argc, argv);
	if (node)
		return bconf_value(node);

	if (((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel < VTCACHE_CAN)
		*cc = VTCACHE_UNKNOWN;
	return NULL;
}

static int
_bconf_haskey(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_highprio;

	if (node) {
		node = bconf_vasget(node, NULL, argc, argv);
		if (node) {
			*cc = ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel;
			return 1;
		}
	}

	node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_lowprio;
	node = bconf_vasget(node, NULL, argc, argv);
	if (node)
		return 1;

	if (((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel < VTCACHE_CAN)
		*cc = VTCACHE_UNKNOWN;
	return 0;
}

static void
_bconf_fetch_cleanup(struct vtree_loop_var *loop) {
	free(loop->l.list);
}

static void
_bconf_fetch_keyvals_cleanup(struct vtree_keyvals *loop) {
	free(loop->list);
}

static void
vtree_bconf_fetch_keys(struct bconf_node *node, struct vtree_loop_var *loop, int argc, const char **argv) {
	int i;

	node = bconf_vasget(node, NULL, argc, argv);

	loop->len = bconf_count(node);
	if (!loop->len) {
		loop->l.list = NULL;
		loop->cleanup = NULL;
		return;
	}

	loop->l.list = xmalloc(loop->len * sizeof(*loop->l.list));
	for (i = 0; i < loop->len; i++) {
		loop->l.list[i] = bconf_key(bconf_byindex(node, i));
	}
	loop->cleanup = _bconf_fetch_cleanup;
}

static void
_bconf_fetch_keys(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_highprio;

	if (node) {
		vtree_bconf_fetch_keys(node, loop, argc, argv);

		if (loop->len) {
			*cc = ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel;
			return;
		}
	}

	node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_lowprio;
	vtree_bconf_fetch_keys(node, loop, argc, argv);
	if (!loop->len && ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel < VTCACHE_CAN)
		*cc = VTCACHE_UNKNOWN;
}

static void
vtree_bconf_fetch_values(struct bconf_node *node, struct vtree_loop_var *loop, int argc, const char **argv) {
	int argoff;
	int i;

	node = bconf_vasget(node, VTREE_LOOP, argc, argv);

	for (argoff = 0; argv[argoff] != VTREE_LOOP && argv[argoff] != NULL; argoff++)
		;
	argoff++;

	loop->len = bconf_count(node);
	if (!loop->len) {
		loop->l.list = NULL;
		loop->cleanup = NULL;
		return;
	}

	loop->l.list = xmalloc(loop->len * sizeof(*loop->l.list));
	for (i = 0; i < loop->len; i++) {
		const char *val = bconf_value(bconf_vasget(bconf_byindex(node, i), NULL, argc - argoff, argv + argoff));
		loop->l.list[i] = val ?: "";
	}
	loop->cleanup = _bconf_fetch_cleanup;
}

static void
_bconf_fetch_values(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_highprio;

	if (node) {
		vtree_bconf_fetch_values(node, loop, argc, argv);

		if (loop->len) {
			*cc = ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel;
			return;
		}
	}

	node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_lowprio;
	vtree_bconf_fetch_values(node, loop, argc, argv);
	if (!loop->len && ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel < VTCACHE_CAN)
		*cc = VTCACHE_UNKNOWN;
}

static void
vtree_bconf_fetch_keys_by_value(struct bconf_node *node, struct vtree_loop_var *loop, const char *value, int argc, const char **argv) {
	int argoff;
	int len;
	int i;

	node = bconf_vasget(node, VTREE_LOOP, argc, argv);

	for (argoff = 0; argv[argoff] != VTREE_LOOP && argv[argoff] != NULL; argoff++)
		;
	argoff++;

	len = bconf_count(node);
	if (!len) {
		loop->len = 0;
		loop->l.list = NULL;
		loop->cleanup = NULL;
		return;
	}

	loop->l.list = xmalloc(len * sizeof(*loop->l.list));
	loop->len = 0;
	for (i = 0; i < len; i++) {
		struct bconf_node *n;
		const char *val;

		val = bconf_value(bconf_vasget((n = bconf_byindex(node, i)), NULL, argc - argoff, argv + argoff));

		if (val && strcmp(value, val) == 0)
			loop->l.list[loop->len++] = bconf_key(n) ?: "";
	}
	loop->cleanup = _bconf_fetch_cleanup;

	if (!loop->len) {
		free(loop->l.list);
		loop->l.list = NULL;
		loop->cleanup = NULL;
	}
}

static void
_bconf_fetch_keys_by_value(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, int argc, const char **argv) {
	struct bconf_node *node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_highprio;

	if (node) {
		vtree_bconf_fetch_keys_by_value(node, loop, value, argc, argv);

		if (loop->len) {
			*cc = ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel;
			return;
		}
	}

	node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_lowprio;
	vtree_bconf_fetch_keys_by_value(node, loop, value, argc, argv);
	if (!loop->len && ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel < VTCACHE_CAN)
		*cc = VTCACHE_UNKNOWN;
}

static void
_bconf_fetch_nodes(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_highprio;

	if (node) {
		vtree_bconf_fetch_nodes(node, loop, argc, argv);

		if (loop->len) {
			*cc = ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel;
			return;
		}
	}

	node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_lowprio;
	vtree_bconf_fetch_nodes(node, loop, argc, argv);
	if (!loop->len && ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel < VTCACHE_CAN)
		*cc = VTCACHE_UNKNOWN;
}

static void
_bconf_fetch_keys_and_values(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_highprio;

	if (node) {
		vtree_bconf_fetch_keys_and_values(node, loop, argc, argv);

		if (loop->len) {
			*cc = ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel;
			return;
		}
	}

	node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_lowprio;
	vtree_bconf_fetch_keys_and_values(node, loop, argc, argv);
	if (!loop->len && ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel < VTCACHE_CAN)
		*cc = VTCACHE_UNKNOWN;
}

static const char*
_bconf_node_get(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *n = vchain->data;

	if (!n) return NULL;

	n = bconf_vasget(n, NULL, argc, argv);

	return n ? bconf_value(n) : NULL;
}

static int
_bconf_node_getlen(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *n = vchain->data;

	if (!n) return 0;

	n = bconf_vasget(n, NULL, argc, argv);

	return n ? bconf_count(n) : 0;
}

static int
_bconf_node_haskey(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	struct bconf_node *n = vchain->data;

	if (!n) return 0;

	n = bconf_vasget(n, NULL, argc, argv);

	return (n) ? 1 : 0;
}

static void
_bconf_node_fetch_keys(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	vtree_bconf_fetch_keys(vchain->data, loop, argc, argv);
}

static void
_bconf_node_fetch_values(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	vtree_bconf_fetch_values(vchain->data, loop, argc, argv);
}

static void
_bconf_node_fetch_byval(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, int argc, const char **argv) {
	vtree_bconf_fetch_keys_by_value(vchain->data, loop, value, argc, argv);
}

static void
_bconf_node_fetch_nodes(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	vtree_bconf_fetch_nodes(vchain->data, loop, argc, argv);
}

static void
_bconf_node_fetch_keys_and_values(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	vtree_bconf_fetch_keys_and_values(vchain->data, loop, argc, argv);
}

static void
_bconf_free(struct vtree_chain *vchain) {
	free(vchain->data);
	vchain->data = NULL;
}

static const struct vtree_dispatch bconf_api_node = {
	_bconf_node_getlen,
	_bconf_node_get,
	_bconf_node_haskey,
	_bconf_node_fetch_keys,
	_bconf_node_fetch_values,
	_bconf_node_fetch_byval,
	_bconf_node_getnode,
	_bconf_node_fetch_nodes,
	_bconf_node_fetch_keys_and_values,
};

static void
_bconf_node_free(struct vtree_chain *vchain){
	struct bconf_node *node = vchain->data;

	bconf_free(&node);

	vchain->data = NULL;
}

static const struct vtree_dispatch bconf_api_node_own = {
	_bconf_node_getlen,
	_bconf_node_get,
	_bconf_node_haskey,
	_bconf_node_fetch_keys,
	_bconf_node_fetch_values,
	_bconf_node_fetch_byval,
	_bconf_node_getnode,
	_bconf_node_fetch_nodes,
	_bconf_node_fetch_keys_and_values,
	_bconf_node_free,
};

static struct vtree_chain *
_bconf_getnode(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, int argc, const char **argv) {
	struct bconf_node *node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_highprio;

	if (node) {
		node = bconf_vasget(node, NULL, argc, argv);

		if (node) {
			*cc = ((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel;
			dst->data = node;
			dst->fun = &bconf_api_node;
			dst->next = NULL;
			return dst;
		}
	}
	node = ((struct ctemplates_vtree_data*)(vchain->data))->bconf_lowprio;
	node = bconf_vasget(node, NULL, argc, argv);

	if (node) {
		dst->data = node;
		dst->fun = &bconf_api_node;
		dst->next = NULL;
		return dst;
	}

	if (((struct ctemplates_vtree_data*)(vchain->data))->highprio_cachelevel < VTCACHE_CAN)
		*cc = VTCACHE_UNKNOWN;

	return NULL;
}

static struct vtree_chain *
_bconf_node_getnode(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, int argc, const char **argv) {
	struct bconf_node *node = bconf_vasget(vchain->data, NULL, argc, argv);

	*cc = VTCACHE_CANT;
	if (node) {
		dst->data = node;
		dst->fun = &bconf_api_node;
		dst->next = NULL;
		return dst;
	}
	return NULL;
}

static void
vtree_bconf_fetch_nodes(struct bconf_node *node, struct vtree_loop_var *loop, int argc, const char **argv) {
	int i;

	node = bconf_vasget(node, NULL, argc, argv);

	loop->len = bconf_count(node);
	if (!loop->len) {
		loop->l.vlist = NULL;
		loop->cleanup = NULL;
		return;
	}

	loop->l.vlist = xmalloc(loop->len * sizeof(*loop->l.vlist));
	for (i = 0; i < loop->len; i++) {
		loop->l.vlist[i].data = bconf_byindex(node, i);
		loop->l.vlist[i].fun = &bconf_api_node;
		loop->l.vlist[i].next = NULL;
	}
	loop->cleanup = _bconf_fetch_cleanup;
}

static void
vtree_bconf_fetch_keys_and_values(struct bconf_node *node, struct vtree_keyvals *loop, int argc, const char **argv) {
	int argoff;
	int i;

	node = bconf_vasget(node, VTREE_LOOP, argc, argv);

	for (argoff = 0; argv[argoff] != VTREE_LOOP && argv[argoff] != NULL; argoff++)
		;
	argoff++;

	loop->type = vktUnknown;
	loop->len = bconf_count(node);
	if (!loop->len) {
		loop->list = NULL;
		loop->cleanup = NULL;
		return;
	}

	loop->list = xmalloc(loop->len * sizeof(*loop->list));
	for (i = 0; i < loop->len; i++) {
		struct bconf_node *n = bconf_byindex(node, i);
		const char *val;

		loop->list[i].key = bconf_key(n);

		n = bconf_vasget(n, NULL, argc - argoff, argv + argoff);
		if (!n)
			loop->list[i].type = vkvNone;
		else if ((val = bconf_value(n))) {
			loop->list[i].type = vkvValue;
			loop->list[i].v.value = val;
		} else {
			loop->list[i].type = vkvNode;
			loop->list[i].v.node.fun = &bconf_api_node;
			loop->list[i].v.node.data = n;
			loop->list[i].v.node.next = NULL;
		}
	}
	loop->cleanup = _bconf_fetch_keyvals_cleanup;
}

static const struct vtree_dispatch bconf_api_vtree = {
	_bconf_getlen,
	_bconf_get,
	_bconf_haskey,
	_bconf_fetch_keys,
	_bconf_fetch_values,
	_bconf_fetch_keys_by_value,
	_bconf_getnode,
	_bconf_fetch_nodes,
	_bconf_fetch_keys_and_values,
	_bconf_free
};

void
bconf_vtree_init(struct vtree_chain *vchain, struct bconf_node *bconf_lowprio, struct bconf_node *bconf_highprio,
		enum vtree_cacheable highprio_cachelevel) {
	if (bconf_highprio) {
		struct ctemplates_vtree_data *d = xmalloc(sizeof(*d));

		vchain->fun = &bconf_api_vtree;
		vchain->data = d;
		d->bconf_lowprio = bconf_lowprio;
		d->bconf_highprio = bconf_highprio;
		d->highprio_cachelevel = highprio_cachelevel;
	} else {
		vchain->fun = &bconf_api_node;
		vchain->data = bconf_lowprio;
	}
}


struct vtree_chain *
bconf_vtree_app(struct vtree_chain *dst, struct bconf_node *host_root, const char *app) {
	bconf_vtree_init(dst, bconf_get(host_root, "*"), bconf_get(host_root, app), VTCACHE_CAN);
	return dst;
}

struct vtree_chain *
bconf_vtree(struct vtree_chain *dst, struct bconf_node *node) {
	bconf_vtree_init(dst, node, NULL, VTCACHE_CAN);
	return dst;
}

struct vtree_chain *
bconf_vtree_own(struct vtree_chain *dst, struct bconf_node *node) {
	dst->fun = &bconf_api_node_own;
	dst->data = node;
	return dst;
}
