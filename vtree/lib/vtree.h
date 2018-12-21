// Copyright 2018 Schibsted

#ifndef VTREE_H
#define VTREE_H

#include <stdarg.h>

#include "sbp/macros.h"

#define VTREE_LOOP ((const char *)-1)

struct vtree_keyvals;

struct vtree_loop_var {
	int len;
	union {
		const char **list;
		struct vtree_chain *vlist;
	} l;
	void (*cleanup)(struct vtree_loop_var *var);
};

/* Must be ordered by increased cacheability */
enum vtree_cacheable {
	VTCACHE_CANT,
	VTCACHE_UNKNOWN,
	VTCACHE_CAN,
	VTCACHE_USED
};

struct vtree_dispatch {
	int (*getlen)(struct vtree_chain *, enum vtree_cacheable *cc, int, const char **);
	const char* (*get)(struct vtree_chain *, enum vtree_cacheable *cc, int, const char **);
	int (*haskey)(struct vtree_chain *, enum vtree_cacheable *cc, int, const char **);

	void (*fetch_keys)(struct vtree_chain *, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int, const char **);
	void (*fetch_values)(struct vtree_chain *, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int, const char **);
	void (*fetch_byval)(struct vtree_chain *, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, int, const char **);

	struct vtree_chain *(*getnode)(struct vtree_chain *, enum vtree_cacheable *cc, struct vtree_chain *dst, int, const char **);
	void (*fetch_nodes)(struct vtree_chain *, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int, const char **);

	void (*fetch_keys_and_values)(struct vtree_chain *, struct vtree_keyvals *loop, enum vtree_cacheable *cc, int, const char **);

	void (*free)(struct vtree_chain *);
};

struct vtree_chain {
	const struct vtree_dispatch *fun;
#define vtree_free(vchain) ((vchain)->fun && (vchain)->fun->free ? (vchain)->fun->free(vchain) : (void)0)
	void *data;
	struct vtree_chain *next;
};

struct vtree_keyvals {
	enum vtree_keyvals_type {
		vktUnknown, /* Backend doesn't support types. key is set, but might be ignored */
		vktDict, /* key is set and should be used */
		vktList, /* key is NULL, index should be used */
	} type;
	int len;
	struct vtree_keyvals_elem {
		const char *key;
		enum vtree_value_type {
			vkvNone,
			vkvValue,
			vkvNode
		} type;
		union {
			const char *value;
			struct vtree_chain node;
		} v;
	} *list;
	void (*cleanup)(struct vtree_keyvals *);
};

struct shadow_vtree
{
	struct vtree_chain vtree;
	void (*free_cb)(struct shadow_vtree *sv);
	void *cbdata;
};

const char* vtree_get_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, const char **cache, ...) SENTINEL(0);
#define vtree_get(vchain, ...) vtree_get_cache(vchain, NULL, NULL, __VA_ARGS__)
int vtree_getint_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, ...) SENTINEL(0);
#define vtree_getint(vchain, ...) vtree_getint_cache(vchain, NULL, NULL, __VA_ARGS__)
int vtree_haskey_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, ...) SENTINEL(0);
#define vtree_haskey(vchain, ...) vtree_haskey_cache(vchain, NULL, NULL, __VA_ARGS__)
int vtree_getlen_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, ...) SENTINEL(0);
#define vtree_getlen(vchain, ...) vtree_getlen_cache(vchain, NULL, NULL, __VA_ARGS__)
void vtree_fetch_keys_cache(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, ...) SENTINEL(0);
#define vtree_fetch_keys(vchain, loop, ...) vtree_fetch_keys_cache(vchain, loop, NULL, __VA_ARGS__)
void vtree_fetch_values_cache(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, ...) SENTINEL(0);
#define vtree_fetch_values(vchain, loop, ...) vtree_fetch_values_cache(vchain, loop, NULL, __VA_ARGS__)
void vtree_fetch_keys_by_value_cache(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, ...) SENTINEL(0);
#define vtree_fetch_keys_by_value(vchain, loop, value, ...) vtree_fetch_keys_by_value_cache(vchain, loop, NULL, value, __VA_ARGS__)

/* Please not that dst will be freed so it needs to be properly initialized (for example to {0}) */
struct vtree_chain *vtree_getnode_cache(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, struct vtree_chain **cache, ...) SENTINEL(0);
#define vtree_getnode(vchain, dst, ...) vtree_getnode_cache(vchain, NULL, dst, NULL, __VA_ARGS__)

void vtree_fetch_nodes_cache(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, ...) SENTINEL(0);
#define vtree_fetch_nodes(vchain, loop, ...) vtree_fetch_nodes_cache(vchain, loop, NULL, __VA_ARGS__)
void vtree_fetch_keys_and_values_cache(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, ...) SENTINEL(0);
#define vtree_fetch_keys_and_values(vchain, loop, ...) vtree_fetch_keys_and_values_cache(vchain, loop, NULL, __VA_ARGS__)

const char* vtree_get_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, const char **cache, int argc, const char **argv);
int vtree_getint_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, int argc, const char **argv);
int vtree_haskey_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, int argc, const char **argv);
int vtree_getlen_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, int *cache, int argc, const char **argv);
void vtree_fetch_keys_cachev(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv);
void vtree_fetch_values_cachev(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv);
void vtree_fetch_keys_by_value_cachev(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, const char *value, int argc, const char **argv);
struct vtree_chain *vtree_getnode_cachev(struct vtree_chain *vchain, enum vtree_cacheable *cc, struct vtree_chain *dst, struct vtree_chain **cache, int argc, const char **argv);
void vtree_fetch_nodes_cachev(struct vtree_chain *vchain, struct vtree_loop_var *loop, enum vtree_cacheable *cc, int argc, const char **argv);
void vtree_fetch_keys_and_values_cachev(struct vtree_chain *vchain, struct vtree_keyvals *loop, enum vtree_cacheable *cc, int argc, const char **argv);

/* Note: if you don't want shadow_vtree call vtree_free on the contained vtree, override the target
 * fun pointer with &shadow_vtree_weakref after calling shadow_vtree_init. */
void shadow_vtree_init(struct vtree_chain *, struct shadow_vtree *, struct vtree_chain *);
extern const struct vtree_dispatch shadow_vtree;
extern const struct vtree_dispatch shadow_vtree_weakref;

void prefix_vtree_init(struct vtree_chain *, const char *, struct vtree_chain *);
/* To have prefix_vtree call free(prefix), override the fun pointer with the address of this variable
 * after prefix_vtree_init. */
extern const struct vtree_dispatch prefix_vtree_free_prefix;

#endif /*VTREE_H*/
