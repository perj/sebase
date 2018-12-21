// Copyright 2018 Schibsted

#ifndef BCONF_H
#define BCONF_H
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>

#include "sbp/macros.h"
#include "sbp/queue.h"
#include "vtree.h"

#ifdef __cplusplus
extern "C" {
#endif

enum tristate {
	TRI_UNDEF = 0,
	TRI_FALSE = -1,
	TRI_TRUE = 1
};

struct bconf_node;
struct mempool;

typedef int (*bconf_foreach_cb)(const char *path, size_t plen, struct bconf_node *node, void *cbdata);

void bconf_add_data(struct bconf_node **, const char *, const char *) NONNULL_ALL;
int bconf_add_data_canfail(struct bconf_node **, const char *, const char *) NONNULL_ALL;
void bconf_add_bindata(struct bconf_node **, const char *, void *, size_t) NONNULL(1,2);
struct bconf_node *bconf_add_listnode(struct bconf_node **root, const char *key) NONNULL(1);

void bconf_add_data_pool(struct mempool *, struct bconf_node **, const char *, const char *) NONNULL(2,3,4);

#define BCONF_REF 0
#define BCONF_DUP 1
#define BCONF_OWN 2
/* dup:
 *  0 Dont duplicate, static string that wont be freed or binary data.
 *  1 Duplicate, bconf_add will strdup it.
 *  2 Take ownership, the string is marked to be freed, but wont be copied before added.
 */

/*
 * The add_*v functions can add data with keys that have periods in them. The only way to access those keys currently is
 * with bconf_byindex.
 */
int bconf_add_datav(struct bconf_node **, int argc, const char **argv, const char *value, int dup) NONNULL(1,3,4);
int bconf_add_datav_canfail(struct bconf_node **, int argc, const char **argv, const char *value, int dup) NONNULL(1,3,4);
int bconf_add_bindatav(struct bconf_node **, int argc, const char **keyv, void *, size_t) NONNULL(1,3);
struct bconf_node *bconf_add_listnodev(struct bconf_node **root, int keyc, const char **keyv) NONNULL_ALL;

struct bconf_node* bconf_get(struct bconf_node *, const char *) FUNCTION_PURE;
struct bconf_node* bconf_lget(struct bconf_node *, const char *, size_t) FUNCTION_PURE;
struct bconf_node* bconf_vget(struct bconf_node *root, ...) FUNCTION_PURE SENTINEL(0);
struct bconf_node* bconf_vnget(struct bconf_node *root, int, va_list) FUNCTION_PURE;
struct bconf_node* bconf_vsget(struct bconf_node *root, const char *sentinel, va_list) FUNCTION_PURE;
struct bconf_node* bconf_vasget(struct bconf_node *root, const char *sentinel, int argc, const char **argv) FUNCTION_PURE;
struct bconf_node* bconf_byindex(struct bconf_node *, int) FUNCTION_PURE;
int bconf_count(const struct bconf_node *) FUNCTION_PURE;
const char*  bconf_value(const struct bconf_node *) FUNCTION_PURE;
void*  bconf_binvalue(const struct bconf_node *) FUNCTION_PURE;
int bconf_intvalue(const struct bconf_node *node) FUNCTION_PURE;
const char*  bconf_key(const struct bconf_node *) FUNCTION_PURE;
/* Return length in octets of the key or value respectively */
size_t bconf_klen(const struct bconf_node *) FUNCTION_PURE;
size_t bconf_vlen(const struct bconf_node *) FUNCTION_PURE;
void bconf_free(struct bconf_node **);

const char *bconf_get_string(struct bconf_node*, const char*) FUNCTION_PURE;
const char *bconf_get_string_default(struct bconf_node*, const char*, const char*) FUNCTION_PURE;
int bconf_get_int(struct bconf_node*, const char*) FUNCTION_PURE;
int bconf_get_int_default(struct bconf_node*, const char*, int) FUNCTION_PURE;
int bconf_vget_int(struct bconf_node*, ...) FUNCTION_PURE SENTINEL(0);
/*
 * The default value must be the last argument. The preceeding path must be NULL terminated as usual.
 */
int bconf_vget_int_default(struct bconf_node*, ...) FUNCTION_PURE SENTINEL(1);
const char *bconf_vget_string(struct bconf_node*, ...) FUNCTION_PURE SENTINEL(0);

enum tristate bconf_get_tristate(struct bconf_node *root, const char *key, enum tristate def) FUNCTION_PURE;

int bconf_in_list(const char *value, const char *path, struct bconf_node *root) FUNCTION_PURE;
int bconf_validate_key_conflict(struct bconf_node *root, const char *key) FUNCTION_PURE;

/*
 * Merge src bconf tree into dst. This has the effect of bconf_add_data into dst for each
 * key and value in src. Thus new keys are added, and existing ones are updated, but nothing
 * is removed.
 * Also works as a deep-copy if *dst is NULL.
 * The prefix version adds the prefix to each key before adding (with . separating prefix and
 * the key.
 */
bool bconf_merge(struct bconf_node **dst, struct bconf_node *src);
bool bconf_merge_prefix(struct bconf_node **dst, const char *prefix, struct bconf_node *src);

/* Remove a key and all sub nodes from root. Returns true if it existed, else false. */
/* Note: you can't generally call this on any bconf. Only use it on trees you built locally, e.g. with bconf_merge. */
bool bconf_deletev(struct bconf_node **dst, int argc, const char **argv);

/* Remove keys in root that doesn't exist in filter. Only checks the top level keys. */
/* Note: you can't generally call this on any bconf. Only use it on trees you built locally, e.g. with bconf_merge. */
int bconf_filter_to_keys(struct bconf_node **root, struct bconf_node *filter);

void bconf_json(struct bconf_node *n, int depth, int (*pf)(void *, int, const char *, ...) FORMAT_PRINTF(3, 4), void *cbdata);
int bconf_json_bscat(void *d, int depth, const char *fmt, ...) FORMAT_PRINTF(3, 4);

int bconf_foreach(struct bconf_node *n, int max_depth, bconf_foreach_cb cbfun, void *cbdata);


void bconf_vtree_init(struct vtree_chain *vchain, struct bconf_node *bconf_lowprio, struct bconf_node *bconf_highprio, enum vtree_cacheable highprio_cachelevel);

struct vtree_chain *bconf_vtree_app(struct vtree_chain *dst, struct bconf_node *host_root, const char *app);
struct vtree_chain *bconf_vtree(struct vtree_chain *dst, struct bconf_node *node);

/* Will free bconf_node when vtree is freed. */
struct vtree_chain *bconf_vtree_own(struct vtree_chain *dst, struct bconf_node *node);

#ifdef __cplusplus
}
#endif

#endif
