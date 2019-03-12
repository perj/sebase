// Copyright 2018 Schibsted

#include "vtree_literal.h"

#include "sbp/memalloc_functions.h"
#include "vtree.h"
#include "vtree_value.h"

#include <stdbool.h>
#include <string.h>

static int
get_index(const struct vtree_keyvals *data, int argc, const char **argv) {
	if (argc < 1)
		return -1;

	if (data->type == vktList) {
		char *ep;
		int idx = strtol(argv[0], &ep, 10);
		if (*argv[0] == '\0' || *ep != '\0' || idx < 0 || idx >= data->len)
			return -1;
		return idx;
	} else {
		for (int idx = 0 ; idx < data->len ; idx++) {
			if (strcmp(argv[0], data->list[idx].key) == 0)
				return idx;
		}
		return -1;
	}

}

static int
vtree_literal_getlen(struct vtree_chain *vchain, enum vtree_cacheable *cc, int argc, const char **argv) {
	const struct vtree_keyvals *data = vchain->data;

	*cc = VTCACHE_CANT;

	if (argc == 0)
		return data->len;

	int idx = get_index(data, argc, argv);
	if (idx < 0 || data->list[idx].type != vkvNode)
		return 0;
	return vtree_getlen_cachev(&data->list[idx].v.node, cc, NULL, argc - 1, argv + 1);
}

static const char *
vtree_literal_get(struct vtree_chain *vchain, enum  vtree_cacheable *cc, int argc, const char **argv) {
	const struct vtree_keyvals *data = vchain->data;

	*cc = VTCACHE_CANT;

	int idx = get_index(data, argc, argv);
	if (idx < 0)
		return NULL;

	switch(data->list[idx].type) {
	case vkvNone:
		return NULL;
	case vkvValue:
		if (argc == 1)
			return data->list[idx].v.value;
		return NULL;
	case vkvNode:
		return vtree_get_cachev(&data->list[idx].v.node, cc, NULL, argc - 1, argv + 1);
	}
	return NULL;
}

static int
vtree_literal_haskey(struct vtree_chain *vchain, enum  vtree_cacheable *cc, int argc, const char **argv) {
	const struct vtree_keyvals *data = vchain->data;

	*cc = VTCACHE_CANT;

	int idx = get_index(data, argc, argv);
	if (idx < 0)
		return 0;

	switch(data->list[idx].type) {
	case vkvNone:
	case vkvValue:
		if (argc == 1)
			return 1;
		return 0;
	case vkvNode:
		if (argc == 1)
			return 1;
		return vtree_haskey_cachev(&data->list[idx].v.node, cc, NULL, argc - 1, argv + 1);
	}
	return 0;
}

static void
vtree_literal_cleanup(struct vtree_loop_var *loop) {
	free(loop->l.list);
}

static void
vtree_literal_cleanup_all(struct vtree_loop_var *loop) {
	for (int i = 0 ; i < loop->len ; i++)
		free((void*)loop->l.list[i]);
	free(loop->l.list);
}

static void
vtree_literal_fetch_keys(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	const struct vtree_keyvals *data = vchain->data;

	*cc = VTCACHE_CANT;

	if (argc == 0) {
		loop->len = data->len;
		loop->l.list = xmalloc(loop->len * sizeof(*loop->l.list));
		if (data->type == vktList) {
			for (int i = 0 ; i < loop->len ; i++)
				xasprintf((char**)&loop->l.list[i], "%d", i);
			loop->cleanup = vtree_literal_cleanup_all;
			return;
		}
		for (int i = 0 ; i < data->len ; i++)
			loop->l.list[i] = data->list[i].key;
		loop->cleanup = vtree_literal_cleanup;
		return;
	}

	int idx = get_index(data, argc, argv);
	if (idx >= 0 && data->list[idx].type == vkvNode) {
		vtree_fetch_keys_cachev(&data->list[idx].v.node, loop, cc, argc - 1, argv + 1);
		return;
	}

	loop->len = 0;
	loop->l.vlist = NULL;
	loop->cleanup = NULL;
}

static void
vtree_literal_fetch_values(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	const struct vtree_keyvals *data = vchain->data;

	*cc = VTCACHE_CANT;

	loop->len = 0;
	loop->cleanup = NULL;

	if (argc < 1)
		return;

	if (argv[0] == VTREE_LOOP) {
		loop->len = data->len;
		loop->l.list = xmalloc(sizeof (*loop->l.list) * data->len);
		loop->cleanup = vtree_literal_cleanup;
		for (int i = 0 ; i < data->len ; i++) {
			switch(data->list[i].type) {
			case vkvNone:
				loop->l.list[i] = NULL;
				break;
			case vkvValue:
				if (argc == 1)
					loop->l.list[i] = data->list[i].v.value;
				else
					loop->l.list[i] = NULL;
				break;
			case vkvNode:
				loop->l.list[i] = vtree_get_cachev(&data->list[i].v.node, cc, NULL, argc - 1, argv + 1);
				break;
			}
		}
		return;
	}

	int idx = get_index(data, argc, argv);
	if (idx >= 0 && data->list[idx].type == vkvNode)
		vtree_fetch_values_cachev(&data->list[idx].v.node, loop, cc, argc - 1, argv + 1);
}

static void
vtree_literal_fetch_keys_by_value(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, int argc, const char **argv) {
	const struct vtree_keyvals *data = vchain->data;

	*cc = VTCACHE_CANT;

	loop->len = 0;
	loop->cleanup = NULL;

	if (argc < 1)
		return;

	if (argv[0] == VTREE_LOOP) {
		loop->l.list = xmalloc(data->len * sizeof(*loop->l.list));
		int n = 0;
		for (int i = 0 ; i < data->len ; i++) {
			bool match = false;
			switch(data->list[i].type) {
			case vkvNone:
				break;
			case vkvValue:
				if (argc == 1 && strcmp(value, data->list[i].v.value) == 0)
					match = true;
				break;
			case vkvNode:
				{
					const char *v = vtree_get_cachev(&data->list[i].v.node, cc, NULL, argc - 1, argv + 1);
					if (v && strcmp(value, v) == 0)
						match = true;
				}
				break;
			}
			if (!match)
				continue;
			if (data->type == vktList) {
				xasprintf((char**)&loop->l.list[n], "%d", i);
			} else {
				loop->l.list[n] = data->list[i].key;
			}
			n++;
		}
		if (data->type == vktList)
			loop->cleanup = vtree_literal_cleanup_all;
		else
			loop->cleanup = vtree_literal_cleanup;
		loop->len = n;
		return;
	}

	int idx = get_index(data, argc, argv);
	if (idx >= 0 && data->list[idx].type == vkvNode) {
		vtree_fetch_keys_by_value_cachev(&data->list[idx].v.node, loop, cc, value, argc - 1, argv + 1);
		return;
	}

	loop->len = 0;
	loop->l.vlist = NULL;
	loop->cleanup = NULL;
}

static struct vtree_chain *
vtree_literal_getnode(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, int argc, const char **argv) {
	const struct vtree_keyvals *data = vchain->data;

	*cc = VTCACHE_CANT;

	if (argc == 0) {
		dst->fun = &vtree_literal_vtree;
		dst->data = vchain->data;
		dst->next = NULL;
		return dst;
	}

	int idx = get_index(data, argc, argv);
	if (idx < 0)
		return NULL;

	switch(data->list[idx].type) {
	case vkvNone:
		return NULL;
	case vkvValue:
		if (argc == 1) {
			dst->fun = &vtree_value_vtree;
			dst->data = (void*)data->list[idx].v.value;
			dst->next = NULL;
			return dst;
		}
		return NULL;
	case vkvNode:
		return vtree_getnode_cachev(&data->list[idx].v.node, cc, dst, NULL, argc - 1, argv + 1);
	}
	return NULL;
}

static void
vtree_literal_fetch_nodes(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	const struct vtree_keyvals *data = vchain->data;

	*cc = VTCACHE_CANT;

	if (argc == 0) {
		loop->len = data->len;
		loop->l.vlist = xmalloc(loop->len * sizeof(*loop->l.vlist));
		for (int i = 0 ; i < data->len ; i++) {
			switch(data->list[i].type) {
			case vkvNone:
				memset(&loop->l.vlist[i], 0, sizeof(loop->l.vlist[i]));
				break;
			case vkvValue:
				loop->l.vlist[i].fun = &vtree_value_vtree;
				loop->l.vlist[i].data = (void*)data->list[i].v.value;
				loop->l.vlist[i].next = NULL;
				break;
			case vkvNode:
				loop->l.vlist[i] = data->list[i].v.node;
				break;
			}
		}
		loop->cleanup = vtree_literal_cleanup;
		return;
	}

	int idx = get_index(data, argc, argv);
	if (idx >= 0 && data->list[idx].type == vkvNode) {
		vtree_fetch_nodes_cachev(&data->list[idx].v.node, loop, cc, argc - 1, argv + 1);
		return;
	}

	loop->len = 0;
	loop->l.vlist = NULL;
	loop->cleanup = NULL;
}

static void
vtree_literal_cleanup_keys_and_values(struct vtree_keyvals *loop) {
	struct vtree_chain *dsts = (struct vtree_chain*)(loop->list + loop->len);
	for (int i = 0 ; i < loop->len ; i++)
		vtree_free(&dsts[i]);
	free(loop->list);
}

static void
vtree_literal_fetch_keys_and_values(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, int argc, const char **argv) {
	const struct vtree_keyvals *data = vchain->data;

	*cc = VTCACHE_CANT;

	if (argc == 0) {
		*loop = *data;
		loop->cleanup = NULL;
		return;
	}

	if (argv[0] != VTREE_LOOP) {
		// Defer to the subnode.
		struct vtree_chain vnode = {0};
		if (vchain->fun->getnode(vchain, cc, &vnode, 1, argv)) {
			vnode.fun->fetch_keys_and_values(&vnode, loop, cc, argc - 1, argv + 1);
			vtree_free(&vnode);
		} else {
			loop->len = 0;
			loop->cleanup = NULL;
		}
		return;
	}

	loop->len = data->len;
	loop->type = data->type;

	/* We need to hold on to any getnode destinations until we're freed. */
	loop->list = xmalloc(sizeof (*loop->list) * data->len + sizeof(struct vtree_chain) * data->len);
	struct vtree_chain *dsts = (struct vtree_chain*)(loop->list + data->len);
	memset(dsts, 0, sizeof(struct vtree_chain) * data->len);

	loop->cleanup = vtree_literal_cleanup_keys_and_values;
	for (int i = 0 ; i < data->len ; i++) {
		loop->list[i].key = data->list[i].key;
		switch(data->list[i].type) {
		case vkvNone:
			loop->list[i].type = vkvNone;
			break;
		case vkvValue:
			if (argc == 1) {
				loop->list[i].type = vkvValue;
				loop->list[i].v.value = data->list[i].v.value;
			} else {
				loop->list[i].type = vkvNone;
			}
			break;
		case vkvNode:
			{
				struct vtree_chain *node = vtree_getnode_cachev(&data->list[i].v.node, cc, &dsts[i], NULL, argc - 1, argv + 1);
				const char *v;

				if (!node) {
					loop->list[i].type = vkvNone;
				} else if ((v = vtree_get(node, NULL))) {
					loop->list[i].type = vkvValue;
					loop->list[i].v.value = v;
				} else {
					loop->list[i].type = vkvNode;
					loop->list[i].v.node = *node;
				}
			}
			break;
		}
	}
}

static void
vtree_literal_free(struct vtree_chain *vchain) {
	struct vtree_keyvals *data = vchain->data;
	if (data->cleanup)
		data->cleanup(data);
	free(data);
}

const struct vtree_dispatch vtree_literal_vtree = {
	vtree_literal_getlen,
	vtree_literal_get,
	vtree_literal_haskey,
	vtree_literal_fetch_keys,
	vtree_literal_fetch_values,
	vtree_literal_fetch_keys_by_value,
	vtree_literal_getnode,
	vtree_literal_fetch_nodes,
	vtree_literal_fetch_keys_and_values,
	NULL, //vtree_literal_free
};

const struct vtree_dispatch vtree_literal_free_vtree = {
	vtree_literal_getlen,
	vtree_literal_get,
	vtree_literal_haskey,
	vtree_literal_fetch_keys,
	vtree_literal_fetch_values,
	vtree_literal_fetch_keys_by_value,
	vtree_literal_getnode,
	vtree_literal_fetch_nodes,
	vtree_literal_fetch_keys_and_values,
	vtree_literal_free,
};

struct vtree_chain *
vtree_literal_create(struct vtree_keyvals *data) {
	struct vtree_chain *rnode = xmalloc(sizeof (*rnode));
	rnode->fun = &vtree_literal_free_vtree;
	rnode->data = data;
	rnode->next = NULL;
	return rnode;
}

static void
kv_dictlist_free(struct vtree_keyvals *kv, bool fk, bool fv, bool fn) {
	for (int i = 0 ; i < kv->len ; i++) {
		if (fk)
			free((char*)kv->list[i].key);
		switch (kv->list[i].type) {
		case vkvValue:
			if (fv)
				free((char*)kv->list[i].v.value);
			break;
		case vkvNode:
			if (fn)
				vtree_free(&kv->list[i].v.node);
			break;
		case vkvNone:
			break;
		}
	}
	free(kv->list);
}

static void
kv_list_free(struct vtree_keyvals *kv) {
	kv_dictlist_free(kv, false, true, true);
}

static void
kv_dict_free(struct vtree_keyvals *kv) {
	kv_dictlist_free(kv, true, true, true);
}

void
vtree_keyvals_free_values_only(struct vtree_keyvals *kv) {
	kv_dictlist_free(kv, false, true, false);
}

void
vtree_keyvals_free_keys_values_only(struct vtree_keyvals *kv) {
	kv_dictlist_free(kv, true, true, false);
}

static struct vtree_keyvals *
vtree_keyvals_create(int len) {
	if (len < 0)
		len = 0;
	struct vtree_keyvals *res = xmalloc(sizeof (*res));
	res->len = len;
	res->list = xmalloc(len * sizeof(*res->list));
	return res;
}

struct vtree_keyvals *
vtree_keyvals_create_list(int len) {
	struct vtree_keyvals *res = vtree_keyvals_create(len);
	res->type = vktList;
	res->cleanup = kv_list_free;
	return res;
}

struct vtree_keyvals *
vtree_keyvals_create_dict(int len) {
	struct vtree_keyvals *res = vtree_keyvals_create(len);
	res->type = vktDict;
	res->cleanup = kv_dict_free;
	return res;
}
