// Copyright 2018 Schibsted

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "bconf.h"
#include "sbp/buf_string.h"
#include "sbp/memalloc_functions.h"
#include "sbp/mempool.h"
#include <limits.h>

#define NODE_VAL 1
#define NODE_LIST 2
#define NODE_BIN 3

struct bconf_node {
	char *value;
	char *key;
	size_t klen;
	size_t vlen;
	int type;

	struct bconf_node **sub_nodes;
	struct bconf_node *star;

	int sublen;
	int count;
};

/*
 * Helper function that's a strncmp between a properly nul-terminated
 * string and a "substring" that's not nul-terminated. Properly returns
 * if the string are actually same if the substring is shorter than the
 * string (not equal). Also performs a numeric compare if the both
 * strings happen to be integers.
 */
static __inline int
keycomp(const struct bconf_node *node, const char *substring, size_t substringlen) {
	int res;
	unsigned int i;

	/*
	 * While the algorithm should be:
	 *  - compare the strings numerically if they are integers,
	 *    otherwise compare them as strings,
	 * the algoritm is actually:
	 *  - compare the strings' length if they start with integers,
	 *    then compare them as strings.
	 *
	 * The slightly backwards way of doing this has a point:
	 * It's doesn't break any regression tests that depend on the
	 * bconf strings being in a certain order while still remaining
	 * very efficient. It behaves like the intended algorithm for
	 * everything but strings that start with a digit, but then
	 * contain non-digits.
	 *
	 * The algoritm also depends on node->key being NUL-terminated
	 * because it doesn't check node->klen.
	 */
	if (isdigit(node->key[0]) && isdigit(*substring) &&
	    (res = node->klen - substringlen) != 0)
		return res;

	/* This is a tweaked version, that understands utf8 */
	for (i = 0; i < substringlen; i++) {
		if ((res = (unsigned char) node->key[i] - (unsigned char)substring[i]) != 0)
			return res;
	}

	/*
	 * In case node->key is longer than substring, we return
	 * the last character of node->key as he compare value.
	 * If it's not longer, then this character is NUL, which
	 * means that the strings compared equal. The cast to
	 * unsigned char is necessary to properly handle multibyte
	 * strings, or we'd risk returning a negative value which
	 * breaks the code using the comparator.
	 */

	return (unsigned char)node->key[i];
}


static struct bconf_node *
node_search(const struct bconf_node *node, const char *key, size_t keylen) {
	int i, ns, no;
	int res;

	if (node->type != NODE_LIST)
		return NULL;

	/*
	 * Binary search for the element.
	 */

	no = 0;			/* First node to search */
	ns = node->count;	/* number of nodes to search */

	while (ns > 0) {
		int even = ~ns & 1;

		ns /= 2;
		i = no + ns;

		res = keycomp(node->sub_nodes[i], key, keylen);
		if (res == 0)
			return node->sub_nodes[i];
		if (res > 0)
			continue;
		no += ns + 1;
		ns -= even;
	}

	return NULL;
}

static void
node_insert(struct mempool *pool, struct bconf_node *node, struct bconf_node *n) {
	int i, ns, no;
	int res;
	size_t klen = strlen(n->key);

	if (node->count == node->sublen) {
		if (node->sublen == 0) {
			node->sublen = 2;
			node->sub_nodes = mempool_alloc(pool, sizeof(struct bconf_node *) * node->sublen);
			node->type = NODE_LIST;
		} else {
			size_t oldsublen = node->sublen;
			node->sublen *= 2;

			if (pool) {
				void *oldnodes = node->sub_nodes;
				node->sub_nodes = mempool_alloc(pool, node->sublen * sizeof(struct bconf_node *));
				memcpy(node->sub_nodes, oldnodes, oldsublen * sizeof(struct bconf_node *));
			} else {
				node->sub_nodes = xrealloc(node->sub_nodes, node->sublen * sizeof(struct bconf_node *));
			}

			/* Belt and suspenders. */
			for (i = node->count; i < node->sublen; i++)
				node->sub_nodes[i] = NULL;
		}
	}

	no = 0;			/* First node to search */
	ns = node->count;	/* number of nodes to search */

	/*
	 * Binary search for the slot where we should put this node so that
	 * sub_nodes is always sorted.
	 */
	while (ns > 0) {
		int even = ~ns & 1;

		ns /= 2;
		i = no + ns;

		res = keycomp(node->sub_nodes[i], n->key, klen);
		if (res == 0)
			xerrx(1, "bconf: node_insert: duplicate key %s\n", n->key);
		if (res > 0)
			continue;
		no += ns + 1;
		ns -= even;
	}

	/*
	 * Make space in sub_nodes for the new element. We move
	 * all elements that need moving one step forward.
	 */
	for (i = node->count; i > no; i--) {
		node->sub_nodes[i] = node->sub_nodes[i - 1];
	}
	node->sub_nodes[no] = n;
	node->count++;

	/*
	 * Record if we have a '*' element.
	 */
	if (n->key[0] == '*' && n->key[1] == '\0')
		node->star = n;
}

static inline struct bconf_node *
bconf_get_node(struct mempool *pool, struct bconf_node *node, const char *key, size_t keylen) {
	struct bconf_node *n;

	if (node->type && node->type != NODE_LIST)
		return NULL;

	if ((n = node_search(node, key, keylen)) == NULL) {
		n = mempool_alloc(pool, sizeof(*n) + keylen + 1);
		n->key = (char*)(n + 1);
		memcpy(n->key, key, keylen);
		n->key[keylen] = '\0';
		n->klen = keylen;
		n->vlen = 0;
		node_insert(pool, node, n);
	}
	return n;
}

static struct bconf_node *
bconf_lookup_add(struct mempool *pool, struct bconf_node **root, const char *key) {
	struct bconf_node *node;
	const char *tmp;

	if (!*root)
		*root = mempool_alloc(pool, sizeof (struct bconf_node));

	node = *root;
	do {
		tmp = key;
		while (*tmp != '.' && *tmp)
			tmp++;

		node = bconf_get_node(pool, node, key, tmp - key);
		if (node == NULL)
			return NULL;

		key = tmp + 1;
	} while (*tmp);

	return node;
}

static struct bconf_node *
bconf_lookup_addv(struct mempool *pool, struct bconf_node **root, int argc, const char **argv) {
	struct bconf_node *node;
	int i;

	if (!*root)
		*root = mempool_alloc(pool, sizeof(**root));

	node = *root;
	for (i = 0; i < argc; i++) {
		node = bconf_get_node(pool, node, argv[i], strlen(argv[i]));
		if (node == NULL)
			return NULL;
	}
	return node;
}

static int
bconf_add_dataX(struct mempool *pool, struct bconf_node *node, char *value, size_t vlen, int dup) {
	if ((value || dup) && (!node || node->type == NODE_LIST))
		return -1;

	if (!node->type) {
		if (dup) {
			if (!value)
				return -1;
			node->type = NODE_VAL;
			node->vlen = vlen;
			if (dup == BCONF_OWN)
				node->value = value;
			else
				node->value = (char*)mempool_strdup(pool, value, node->vlen);
		} else if (value) {
			node->type = NODE_BIN;
			node->value = value;
			node->vlen = vlen;
		} else {
			node->type = NODE_LIST;
		}
		return 0;
	}
	int ret = 1;
	if (value || dup) {
		if (dup != BCONF_REF && node->type == NODE_VAL)
			ret = (strcmp(node->value, value) == 0);
		else
			ret = 0;
		if (dup != BCONF_DUP) {
			if (node->type != NODE_BIN && node->value)
				free(node->value);
			node->type = dup ? NODE_VAL : NODE_BIN;
			node->value = value;
			node->vlen = vlen;
		} else if (!value) {
			return -1;
		} else if (!pool && vlen <= node->vlen) {
			/* Copy into existing buffer */
			memcpy(node->value, value, vlen);
			/* zero-terminate new value if shorter and appropriate */
			if (vlen < node->vlen && node->type == NODE_VAL)
				node->value[vlen] = '\0';
			node->vlen = vlen;
		} else {
			/* Allocate new memory for the longer value */
			if (node->value && !pool)
				free(node->value);
			node->vlen = vlen;
			node->value = (char*)mempool_strdup(pool, value, vlen);
		}
	}
	return ret;
}

int
bconf_validate_key_conflict(struct bconf_node *root, const char *key) {
	struct bconf_node *node;
	const char *tmp;

	if (!root)
		return 0;

	node = root;
	do {
		tmp = key;
		while (*tmp != '.' && *tmp)
			tmp++;

		if (node->type && node->type != NODE_LIST)
			return -1;

		node = node_search(node, key, tmp - key);
		if (node == NULL)
			return 0;

		key = tmp + 1;
	} while (*tmp);

	if (node->type && node->type != NODE_VAL)
		return -1;

	return 0;
}

void
bconf_add_data(struct bconf_node **root, const char *key, const char *value) {
	struct bconf_node *node;

	node = bconf_lookup_add(NULL, root, key);
	if (node == NULL)
		xerrx(1, "bconf_add_data: can not add node %s, possible conflict", key);
	if (bconf_add_dataX(NULL, node, (char *)value, strlen(value), BCONF_DUP) == -1)
		xerrx(1, "bconf_add_data: Node list/value conflict");
}

int
bconf_add_data_canfail(struct bconf_node **root, const char *key, const char *value) {
	struct bconf_node *node;

	node = bconf_lookup_add(NULL, root, key);
	if (node == NULL)
		return -1;
	return bconf_add_dataX(NULL, node, (char *)value, strlen(value), BCONF_DUP);
}

void
bconf_add_bindata(struct bconf_node **root, const char *key, void *value, size_t vlen) {
	struct bconf_node *node;

	if (!value)
		return;
	node = bconf_lookup_add(NULL, root, key);
	if (node == NULL)
		xerrx(1, "bconf_add_bindata: can not add node %s, possible conflict", key);
	if (bconf_add_dataX(NULL, node, value, vlen, BCONF_REF) == -1)
		xerrx(1, "bconf_add_bindata: Node list/value conflict");
}

int
bconf_add_bindatav(struct bconf_node **root, int keyc, const char **keyv, void *value, size_t vlen) {
	struct bconf_node *node;

	if (!value)
		return 1;
	node = bconf_lookup_addv(NULL, root, keyc, keyv);
	if (node == NULL)
		xerrx(1, "bconf_add_bindatav: can not add node, possible conflict");
	int r = bconf_add_dataX(NULL, node, value, vlen, BCONF_REF);
	if (r == -1)
		xerrx(1, "bconf_add_bindatav: Node list/value conflict");
	return r;
}

int
bconf_add_datav(struct bconf_node **root, int argc, const char **argv, const char *value, int dup) {
	struct bconf_node *node;

	node = bconf_lookup_addv(NULL, root, argc, argv);
	if (node == NULL)
		xerrx(1, "bconf_add_datav: can not add node, possible conflict");
	int r = bconf_add_dataX(NULL, node, (char *)value, strlen(value), dup);
	if (r == -1)
		xerrx(1, "bconf_add_datav: Node list/value conflict");
	return r;
}

int
bconf_add_datav_canfail(struct bconf_node **root, int argc, const char **argv, const char *value, int dup) {
	struct bconf_node *node;

	node = bconf_lookup_addv(NULL, root, argc, argv);
	if (!node)
		return -1;
	return bconf_add_dataX(NULL, node, (char *)value, strlen(value), dup);
}

struct bconf_node *
bconf_add_listnode(struct bconf_node **root, const char *key) {
	/*
	 * If key is NULL it means we're allocating a new root node.
	 */
	if (key == NULL)
		return mempool_alloc(NULL, sizeof(**root));
	return bconf_lookup_add(NULL, root, key);
}

struct bconf_node *
bconf_add_listnodev(struct bconf_node **root, int keyc, const char **keyv) {
	if (keyv[0] == NULL)
		return mempool_alloc(NULL, sizeof(**root));
	return bconf_lookup_addv(NULL, root, keyc, keyv);
}

void
bconf_add_data_pool(struct mempool *pool, struct bconf_node **root, const char *key, const char *value) {
	struct bconf_node *node;

	node = bconf_lookup_add(pool, root, key);
	if (node == NULL)
		xerrx(1, "bconf_add_data_pool: can not add node %s, possible conflict", key);
	if (bconf_add_dataX(pool, node, (char *)value, strlen(value), BCONF_DUP) == -1)
		xerrx(1, "bconf_add_data_pool: Node list/value conflict");
}


struct bconf_node *
bconf_get(struct bconf_node *root, const char *key) {
	struct bconf_node *n = root; 
	const char *tmp;

	if (key == NULL || root == NULL)
		return NULL;

	do {
		struct bconf_node *star = n->star;

		tmp = key;
		while (*tmp != '.' && *tmp)
			tmp++;

		n = node_search(n, key, tmp - key);

		if (n == NULL) {
			if (star == NULL)
				return NULL;
			n = star;
		}
		key = tmp + 1;
	} while (*tmp);

	return n;
}

struct bconf_node *
bconf_lget(struct bconf_node *root, const char *key, size_t len) {
	struct bconf_node *n = root; 
	const char *tmp;
	const char *end = key + len;

	if (key == NULL || root == NULL || len == 0)
		return NULL;

	do {
		struct bconf_node *star = n->star;

		tmp = key;
		while (*tmp != '.' && tmp < end)
			tmp++;

		n = node_search(n, key, tmp - key);

		if (n == NULL) {
			if (star == NULL)
				return NULL;
			n = star;
		}
		key = tmp + 1;
	} while (tmp < end);

	return n;
}

struct bconf_node * 
bconf_vget(struct bconf_node *root, ...) {
	va_list ap;
	char *key;
	struct bconf_node *res;

	va_start(ap, root);

	res = root;

	while ((key = va_arg(ap, char *)) != NULL && res != NULL) {
		res = bconf_get(res, key);
	}

	va_end(ap);

	return res;
}

struct bconf_node * 
bconf_vnget(struct bconf_node *root, int args, va_list ap) {
	char *key;
	struct bconf_node *res;

	res = root;

	while (args-- && (key = va_arg(ap, char *)) && res != NULL) {
		res = bconf_get(res, key);
	}

	return res;
}

struct bconf_node * 
bconf_vasget(struct bconf_node *root, const char *sentinel, int argc, const char **argv) {
	struct bconf_node *res = root;
	int i;

	for (i = 0; i < argc && argv[i] != sentinel; i++)
		res = bconf_get(res, argv[i]);

	return res;
}

struct bconf_node*
bconf_byindex(struct bconf_node *node, int index) {

	if (node && node->count > index) {
		return (node->sub_nodes[index]);
	}

	return NULL;
}

int 
bconf_count(const struct bconf_node *node) {
	if (node)
		return node->count;
	else
		return 0;
}

const char* 
bconf_value(const struct bconf_node *node) {
	if (node)
		return node->value;
	else 
		return NULL;
}

int
bconf_intvalue(const struct bconf_node *node) {
	if (node && node->value)
		return atoi(node->value);
	else 
		return 0;
}


void* 
bconf_binvalue(const struct bconf_node *node) {
	if (node)
		return (void*)node->value;
	else 
		return NULL;
}

const char *
bconf_key(const struct bconf_node *node) {
	if (node)
		return node->key;
	else
		return NULL;
}

size_t
bconf_klen(const struct bconf_node *node) {
	if (node)
		return node->klen;
	else
		return 0;
}

size_t
bconf_vlen(const struct bconf_node *node) {
	if (node)
		return node->vlen;
	else
		return 0;
}

const char *
bconf_get_string(struct bconf_node *root, const char *key) {
	struct bconf_node *node;
	
	if ((node = bconf_get(root, key)) == NULL)
		return NULL;
	
	return bconf_value(node);
}



const char *
bconf_get_string_default(struct bconf_node *root, const char *key, const char *def) {
	const char *ret = bconf_get_string(root, key);
	
	if (ret)
		return ret;
	
	return def;
}

int
bconf_get_int(struct bconf_node *root, const char *key) {
	struct bconf_node *node;

	if ((node = bconf_get(root, key)) == NULL)
		return 0;
	
	return bconf_intvalue(node);
}

int
bconf_get_int_default(struct bconf_node *root, const char *key, int def) {
	struct bconf_node *node;

	if ((node = bconf_get(root, key)) == NULL)
		return def;

	return bconf_intvalue(node);
}

enum tristate
bconf_get_tristate(struct bconf_node *root, const char *key, enum tristate def) {
	struct bconf_node *node;

	if ((node = bconf_get(root, key)) == NULL)
		return def;

	return bconf_intvalue(node) == 0 ? TRI_FALSE : TRI_TRUE;
}

int
bconf_vget_int(struct bconf_node *root, ...) {
	struct bconf_node *node;
	va_list ap;

	va_start(ap, root);

	node = bconf_vnget(root, INT_MAX, ap);
	va_end(ap);
	
	return bconf_intvalue(node);
}

static struct bconf_node *
bconf_vnget_till_null(struct bconf_node *root, va_list ap) {
	char *key = NULL;
	struct bconf_node *res = root;

	while ((key = va_arg(ap, char *))) {
		if (res)
			res = bconf_get(res, key);
	}

	return res;
}

int
bconf_vget_int_default(struct bconf_node *root, ...) {
	struct bconf_node *node;
	va_list ap;
	int res = 0;

	va_start(ap, root);

	if ((node = bconf_vnget_till_null(root, ap)) != NULL) {
		res = bconf_intvalue(node);
	} else {
		res = va_arg(ap, int);
	}
	va_end(ap);

	return res;
}

const char *
bconf_vget_string(struct bconf_node *root, ...) {
	struct bconf_node *node;
	va_list ap;

	va_start(ap, root);

	node = bconf_vnget(root, INT_MAX, ap);
	va_end(ap);
	
	return node ? bconf_value(node) : NULL;
}

void
bconf_free(struct bconf_node **root) {
	struct bconf_node *node;
	int i;

	node = *root;

	if (node == NULL)
		return;

	if (node->type == NODE_LIST) {
		for (i = 0; i < node->count; i++)
			bconf_free(&node->sub_nodes[i]);
	}

	free(node->sub_nodes);

	if (node->type != NODE_BIN && node->value)
		free(node->value);

	free(node);
	(*root) = NULL;
}

int
bconf_in_list(const char *value, const char *path, struct bconf_node *root) {
	struct bconf_node *node;
	int count;
	int i;

	if (!value)
		return -1;

	if (path)
		node = bconf_get(root, path);
	else
		node = root;

	count = bconf_count(node);

	for (i = 0; i < count; i++) {
		const char *v = bconf_value(bconf_byindex(node, i));
		if (v && !strcmp(v, value)) {
			return i;
		}
	}

	return -1;
}

bool
bconf_merge(struct bconf_node **dst, struct bconf_node *src) {
	int i;
	int n;
	bool ret = false;

	if (!*dst)
		*dst = zmalloc(sizeof (**dst));

	n = bconf_count(src);
	for (i = 0; i < n; i++) {
		struct bconf_node *sn = bconf_byindex(src, i);

		switch (sn->type) {
		case NODE_LIST:
		{
			struct bconf_node *dn = bconf_get_node(NULL, *dst, sn->key, sn->klen);
			if (dn)
				ret |= bconf_merge(&dn, sn);
			break;
		}
		case NODE_VAL:
			if (bconf_add_datav(dst, 1, (const char *[]){sn->key}, sn->value, BCONF_DUP) == 0)
				ret = true;
			break;
		case NODE_BIN:
			if (bconf_add_bindatav(dst, 1, (const char *[]){sn->key}, sn->value, sn->vlen) == 0)
				ret = true;
			break;
		}
	}
	return ret;
}

bool
bconf_merge_prefix(struct bconf_node **dst, const char *prefix, struct bconf_node *src) {
	struct bconf_node *dn;
	if (!src)
		return false;
	if (!*dst)
		*dst = zmalloc(sizeof(**dst));
	dn = bconf_get_node(NULL, *dst, prefix, strlen(prefix));
	if (!dn)
		return false;
	return bconf_merge(&dn, src);
}

bool
bconf_deletev(struct bconf_node **root, int argc, const char **argv) {
	struct bconf_node *n = bconf_lookup_addv(NULL, root, argc - 1, argv);
	if (!n)
		return false;

	/* XXX binary search. */
	for (int i = 0 ; i < n->count ; ) {
		struct bconf_node *sub = bconf_byindex(n, i);

		if (strcmp(argv[argc - 1], bconf_key(sub)) != 0) {
			i++;
			continue;
		}

		n->count--;
		if (n->star == sub)
			n->star = NULL;
		bconf_free(&sub);
		memmove(n->sub_nodes + i, n->sub_nodes + i + 1, (n->count - i) * sizeof(*n->sub_nodes));
		return true;
	}

	return false;
}

int
bconf_filter_to_keys(struct bconf_node **root, struct bconf_node *filter) {
	if (!*root || (*root)->type != NODE_LIST)
		return 0;

	struct bconf_node *n = *root;

	int ret = 0;
	for (int i = 0 ; i < n->count ; ) {
		struct bconf_node *sub = bconf_byindex(n, i);

		if (bconf_get(filter, bconf_key(sub))) {
			i++;
			continue;
		}

		n->count--;
		if (n->star == sub)
			n->star = NULL;
		bconf_free(&sub);
		memmove(n->sub_nodes + i, n->sub_nodes + i + 1, (n->count - i) * sizeof(*n->sub_nodes));
		ret++;
	}
	return ret;
}

static void
bconf_json_value(struct bconf_node *n, int depth, int (*pf)(void *, int, const char *, ...) FORMAT_PRINTF(3, 4), void *cbdata) {
	char *p = n->value;
	char *prev = p;

	pf(cbdata, 0, "\"");
	while (*p) {
		while (*p) {
			if ((0 < *p && *p < 0x20) || *p == '"' || *p == '\\')
				break;
			p++;
		}

		pf(cbdata, 0, "%.*s", (int) (p - prev), prev);
		if (!*p)
			break;

		switch (*p) {
		case '"':
		case '\\':
			pf(cbdata, 0, "\\%c", *p);
			break;
		case '\n':
			pf(cbdata, 0, "\\n");
			break;
		default:
			pf(cbdata, 0, "\\u00%.2x", *p);
			break;
		}

		p++;
		prev = p;
	}
	pf(cbdata, 0, "\"");
}

void
bconf_json(struct bconf_node *n, int depth, int (*pf)(void *, int, const char *, ...) FORMAT_PRINTF(3, 4), void *cbdata) {
	if (!n) {
		pf(cbdata, 0, "{}");
		return;
	}

	if (n->type == NODE_VAL) {
		bconf_json_value(n, depth, pf, cbdata);
		return;
	}

	if (!depth)
		pf(cbdata, depth, "{\n");

	int c = bconf_count(n);
	for (int i = 0 ; i < c ; i++) {
		struct bconf_node *ns = bconf_byindex(n, i);
		pf(cbdata, depth + 1, "\"%s\": ", ns->key);

		switch (ns->type) {
		case NODE_LIST:
			pf(cbdata, 0, "{\n");
			bconf_json(ns, depth + 1, pf, cbdata);
			if (i < c - 1)
				pf(cbdata, 0, ",\n");
			else
				pf(cbdata, 0, "\n");
			break;

		case NODE_VAL:
			bconf_json_value(ns, depth, pf, cbdata);
			pf(cbdata, 0, "%s\n", (i < c - 1) ? "," : "");
			break;
		case NODE_BIN:
			break;
		}
	}
	pf(cbdata, depth, "}");
}

int
bconf_json_bscat(void *d, int depth, const char *fmt, ...) {
	va_list ap;

	bscat(d, "%*s", depth, "");
	va_start(ap, fmt);
	int r = vbscat(d, fmt, ap);
	va_end(ap);
	return r;
}

struct _foreach_state {
	bconf_foreach_cb cbfun;
	void *cbdata;
	int max_depth;
	int pathidx;
	char path[128];
};

static int bconf_foreach_internal(struct bconf_node *n, int depth, struct _foreach_state *state) {
	int c = bconf_count(n);
	int retval = 0;
	int i;

	if (depth > state->max_depth)
		return retval;

	for (i = 0 ; i < c ; i++) {
		struct bconf_node *ns = bconf_byindex(n, i);

		if (ns->type != NODE_LIST && ns->type != NODE_VAL)
			continue;

		int newidx = snprintf(&state->path[state->pathidx], sizeof(state->path) - state->pathidx, "%s%s", ns->key, ns->type == NODE_LIST ? "." : "");
		if (newidx > 0 && newidx < (int)(sizeof(state->path) - state->pathidx)) {
			state->pathidx += newidx;
			if (ns->type == NODE_LIST) {
				if ((retval = bconf_foreach_internal(ns, depth + 1, state)))
					return retval;
			} else if ((retval = state->cbfun(state->path, state->pathidx, ns, state->cbdata)))
				return retval;
			state->pathidx -= newidx;
		} /* else out of bounds */
		state->path[state->pathidx] = '\0';
	}
	return retval;
}

int bconf_foreach(struct bconf_node *n, int max_depth, bconf_foreach_cb cbfun, void *cbdata) {
	struct _foreach_state state = { .cbfun = cbfun, .cbdata = cbdata, .max_depth = max_depth };

	return bconf_foreach_internal(n, 0, &state);
}
