// Copyright 2018 Schibsted

#include "vtree.h"
#include "bconf.h"
#include "sbp/buf_string.h"
#include "yajl/yajl_parse.h"
#include "json_vtree.h"
#include "sbp/memalloc_functions.h"

#include <stdio.h>
#include <string.h>

#define MAX_DEPTH 40
#define MAX_KEYLEN 256

struct j_stack {
	int count;
	struct bconf_node *node;
};

struct j_ctx {
	int stack;
	struct j_stack st[MAX_DEPTH];
	char next_key[MAX_KEYLEN];
};

static int
json_vtree_null(void *ctx) {
	return 1;
}

static inline const char *
json_vtree_next_key(struct j_ctx *c) {
	if (c->st[c->stack].count >= 0)
		snprintf(c->next_key, sizeof(c->next_key), "%d", c->st[c->stack].count++);
	return *c->next_key ? c->next_key : NULL;
}

static int
json_vtree_boolean(void *ctx, int boolean) {
	struct j_ctx * c = ctx;
	struct j_stack * s = &c->st[c->stack];

	if (boolean)
		bconf_add_datav(&s->node, 1, (const char *[]){ json_vtree_next_key(c) }, "true", BCONF_REF);
	return 1;
}

static int
json_vtree_number(void *ctx, const char *str, size_t l) {
	struct j_ctx * c = ctx;
	struct j_stack * s = &c->st[c->stack];

	bconf_add_datav(&s->node, 1, (const char *[]){ json_vtree_next_key(c) }, xstrndup(str, l), BCONF_OWN);
	return 1;
}

static int
json_vtree_string(void *ctx, const unsigned char *str, size_t l) {
	struct j_ctx * c = ctx;
	struct j_stack * s = &c->st[c->stack];

	bconf_add_datav(&s->node, 1, (const char *[]){ json_vtree_next_key(c) }, xstrndup((const char*)str, l), BCONF_OWN);
	return 1;
}

static int
json_vtree_map_key(void *ctx, const unsigned char *str, size_t l) {
	struct j_ctx * c = ctx;
	size_t dl = l < MAX_KEYLEN - 1 ? l : MAX_KEYLEN - 1;

	/* If we get a key, there is probably a value or object coming next... */
	memcpy(c->next_key, (const char*)str, dl);
	c->next_key[dl] = '\0';
	return 1; 
}

static int
json_vtree_start_map(void *ctx) {
	struct j_ctx * c = ctx;
	struct j_stack * s = &c->st[c->stack];
	struct bconf_node *node;
	const char *key = json_vtree_next_key(c);

	node = bconf_add_listnodev(&s->node, 1, (const char *[]){key});
	struct j_stack * next = &c->st[++c->stack];
	next->node = node;
	next->count = -1;
	return 1;
}

static int
json_vtree_end_map(void *ctx) {
	struct j_ctx * c = ctx;

	c->stack--;
	return 1;
}

static int
json_vtree_start_array(void *ctx) {
	struct j_ctx * c = ctx;
	struct j_stack * s = &c->st[c->stack];
	struct bconf_node *node;

	node = bconf_add_listnodev(&s->node, 1, (const char *[]){json_vtree_next_key(c)});
	struct j_stack * next = &c->st[++c->stack];
	next->node = node;
	next->count = 0;
	return 1;
}

#define json_vtree_end_array json_vtree_end_map

static yajl_callbacks json_vtree_cbs = {
	json_vtree_null,
	json_vtree_boolean,
	NULL,
	NULL,
	json_vtree_number,
	json_vtree_string,
	json_vtree_start_map,
	json_vtree_map_key,
	json_vtree_end_map,
	json_vtree_start_array,
	json_vtree_end_array
};

int
json_vtree(struct vtree_chain *dst, const char *root_name, const char *json_str, ssize_t jsonlen, int validate_utf8) {
	struct bconf_node *bn = NULL;
	int res = json_bconf(&bn, root_name, json_str, jsonlen, validate_utf8);
	if (dst)
		bconf_vtree_own(dst, bn);
	else
		bconf_free(&bn);

	return res;
}

int
json_bconf(struct bconf_node **dst, const char *root_name, const char *json_str, ssize_t jsonlen, int validate_utf8) { 
	struct j_ctx c = {};
	int res = 0;

	if (jsonlen < 0)
		jsonlen = strlen(json_str);

	if (root_name)
		strlcpy(c.next_key, root_name, MAX_KEYLEN);
	c.st[0].count = -1;

	yajl_handle hand = yajl_alloc(&json_vtree_cbs, NULL, &c);
	yajl_config(hand, yajl_allow_comments, 1);
	if (!validate_utf8)
		yajl_config(hand, yajl_dont_validate_strings, 1);

	yajl_status stat = yajl_parse(hand, (const unsigned char*)json_str, jsonlen);
	if (stat == yajl_status_ok)
		stat = yajl_complete_parse(hand);

	if (stat != yajl_status_ok) {
		unsigned char *error = yajl_get_error(hand, 1, (const unsigned char*)json_str, jsonlen);
		/* Check if root_name is a value node, or if root_name.error already exists. Otherwise set it to the error. */
		if (bconf_value(bconf_get(c.st[0].node, root_name))) {
			/* XXX do something */;
		} else if (bconf_vget(c.st[0].node, root_name, "error", NULL)) {
			/* XXX do something */;
		} else {
			if (root_name)
				bconf_add_datav(&c.st[0].node, 2, (const char*[]){ root_name, "error" }, (const char*)error, BCONF_DUP);
			else
				bconf_add_datav(&c.st[1].node, 1, (const char*[]){ "error" }, (const char*)error, BCONF_DUP);
		}
		yajl_free_error(hand, error);
		res = 1;
	}

	if (root_name)
		*dst = c.st[0].node;
	else
		*dst = c.st[1].node;

	yajl_free(hand);

	return res;
}

/* Now, the other way. */
void
vtree_json(struct vtree_chain *n, int use_arrays, int depth, int (*pf)(void *, int, int, const char *, ...), void *cbdata) {
	struct vtree_keyvals keyvals;
	enum vtree_cacheable cc = VTCACHE_UNKNOWN;
	int i;
	int numeric;

	vtree_fetch_keys_and_values_cachev(n, &keyvals, &cc, 1, (const char *[]){ VTREE_LOOP });

	if (use_arrays && keyvals.type == vktUnknown) {
		/* Check for numeric array */
		numeric = 1;
		for (i = 0 ; i < keyvals.len && numeric == 1; i++) {
			const char *s;
			for (s = keyvals.list[i].key; *s; s++) {
				if (*s < '0' || *s > '9') {
					numeric = 0;
					break;
				}
			}
		}
	} else {
		numeric = (keyvals.type == vktList);
	}

	if (numeric)
		pf(cbdata, 0, 1, "[");
	else
		pf(cbdata, 0, 1, "{");
	for (i = 0 ; i < keyvals.len ; i++) {
		if (!numeric) {
			const char *p = keyvals.list[i].key;
			if (p[0] == '_')
				continue;

			const char *ch = strpbrk(p, "\"\\\t");

			pf(cbdata, depth + 1, 0, "\"");
			while (ch) {
				switch (*ch) {
				case '\t':
					pf(cbdata, 0, 0, "%.*s\\t", (int)(ch - p), p);
					break;
				default:
					pf(cbdata, 0, 0, "%.*s\\%c", (int)(ch - p), p, *ch);
					break;
				}
				p = ch + 1;
				ch = strpbrk(p, "\"\\\t");
			}
			pf(cbdata, 0, 0, "%s\": ", p);
		}

		switch (keyvals.list[i].type) {
		case vkvNode:
			vtree_json(&keyvals.list[i].v.node, use_arrays, depth + 1, pf, cbdata);
			if (i < keyvals.len - 1)
				pf(cbdata, 0, 1, ",");
			else
				pf(cbdata, 0, 1, "");
			break;

		case vkvValue:
			{
				const char *p = keyvals.list[i].v.value;
				const char *ch = strpbrk(p, "\"\\\n\t");

				pf(cbdata, 0, 0, "\"");
				while (ch) {
					switch (*ch) {
					case '\t':
						pf(cbdata, 0, 0, "%.*s\\t", (int)(ch - p), p);
						break;
					case '\n':
						pf(cbdata, 0, 0, "%.*s\\n", (int)(ch - p), p);
						break;
					default:
						pf(cbdata, 0, 0, "%.*s\\%c", (int)(ch - p), p, *ch);
						break;
					}
					p = ch + 1;
					ch = strpbrk(p, "\"\\\n\t");
				}
				pf(cbdata, 0, 1, "%s\"%s", p, ((i < keyvals.len - 1) ? "," : ""));
			}
			break;

		case vkvNone:
			pf(cbdata, 0, 0, "null%s", ((i < keyvals.len - 1) ? "," : ""));
			break;
		}
	}
	if (numeric)
		pf(cbdata, depth, 0, "]");
	else
		pf(cbdata, depth, 0, "}");

	if (keyvals.cleanup)
		keyvals.cleanup(&keyvals);
}

int
vtree_json_bscat(void *d, int depth, int newl, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	int r = vbscat(d, fmt, ap);
	va_end(ap);
	return r;
}
