// Copyright 2018 Schibsted

#ifndef STRINGPOOL_H
#define STRINGPOOL_H

#include <sys/types.h>

struct stringpool *stringpool_new(void);
void stringpool_free(struct stringpool *pool);

const char *stringpool_get(struct stringpool *pool, const char *str, ssize_t len);

/* Returns 0-based index of str based on inserted order.
 * Can be used as array index for a string mapping.
 */
int stringpool_get_index(struct stringpool *pool, const char *str, ssize_t len);

/* Like get_index but doesn't insert. */
int stringpool_search_index(struct stringpool *pool, const char *str, ssize_t len);


#endif /*STRINGPOOL_H*/
