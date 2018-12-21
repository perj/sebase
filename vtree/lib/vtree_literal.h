// Copyright 2018 Schibsted

#ifndef VTREE_LITERAL_H
#define VTREE_LITERAL_H

#include "vtree.h"

extern const struct vtree_dispatch vtree_literal_vtree;
extern const struct vtree_dispatch vtree_literal_free_vtree;

struct vtree_chain* vtree_literal_create(struct vtree_keyvals *data);

/*
 * The difference between these two is that the dict has a different cleanup
 * function that also calls free on the keys. If that's not what you want then
 * use create_list and change the type field manually.
 */
struct vtree_keyvals *vtree_keyvals_create_list(int len);
struct vtree_keyvals *vtree_keyvals_create_dict(int len);

/* Override keyvals->cleanup with one of these to not call vtree_free on the nodes. */
void vtree_keyvals_free_values_only(struct vtree_keyvals *kv);
void vtree_keyvals_free_keys_values_only(struct vtree_keyvals *kv);

#define VTREE_LITERAL_VALUE(kstr, vstr) { \
		.type = vkvValue, \
		.key = (kstr), \
		.v = { .value = (vstr), }, \
	}

#endif /*VTREE_LITERAL_H*/
