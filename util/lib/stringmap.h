// Copyright 2018 Schibsted

#ifndef STRINGMAP_H
#define STRINGMAP_H

#include <sys/types.h>

/* Map from string keys to string values.
 * Inserting duplicates are ok, they'll be added at increasing index, which must be specified to get.
 * sm_get returns NULL if key is missing or index invalid.
 * sm_getlist will fill in 0 values on missing keys.
 * Use -1 for lengths to apply strlen.
 */

struct stringmap *sm_new(void);
void sm_free(struct stringmap *sm);

void sm_insert(struct stringmap *sm, const char *key, ssize_t klen, const char *value, ssize_t vlen);
const char *sm_get(struct stringmap *sm, const char *key, ssize_t klen, int index);

struct stringmap_list {
	int n;
	const char *const *list;
};

void sm_getlist(struct stringmap *sm, const char *key, ssize_t klen, struct stringmap_list *out_list);

#endif /*STRINGMAP_H*/
