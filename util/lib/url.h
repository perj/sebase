// Copyright 2018 Schibsted

#ifndef COMMON_URL_H
#define COMMON_URL_H

#include <sys/types.h>

#include "macros.h"

struct url {
	char *protocol;
	char *host;
	char *port;
	char *path;
};

#define URL_RE "^([a-z]+)://([A-Za-z0-9.-]*|\\[[A-Za-z0-9.:%-]+\\])(:[0-9]{1,5})?(/.*)?$"

struct url *split_url(const char *url) ALLOCATOR NONNULL_ALL;

/* Destructivly url decode str, stop after max or on one of the stopchars
 * str must be NUL terminated, but can be longer than max.
 * If unsafe is 0, replace all control chars with ?.
 * Returns pointer to the end of the decoded string.
 */
char *url_decode(char *str, int max, const char *stopchars, int unsafe, int *is_utf8);

struct buf_string;
void url_encode(struct buf_string *dst, const char *str, size_t len);
void url_encode_postdata(struct buf_string *dst, const char *str, size_t len);


#endif /*COMMON_URL_H*/
