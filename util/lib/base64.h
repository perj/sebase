// Copyright 2018 Schibsted

#ifndef _BASE64_H
#define _BASE64_H

#include <sys/types.h>

#include "macros.h"

/* Includes trailing NUL byte. */
#define BASE64_NEEDED(sz) ((sz + 2) / 3 * 4 + 1)
#define BASE64DECODE_NEEDED(sz) (sz / 4 * 3 + 1)

/* RFC 2045 */
ssize_t
base64_encode(char *dst, const void *src, ssize_t len) NONNULL_ALL;

/* RFC 4648 */
ssize_t
base64url_encode(char *dst, const void *src, ssize_t len) NONNULL_ALL;

ssize_t
base64_decode(char *dst, const char *src, ssize_t len) NONNULL_ALL;

char *
base64_encode_new(const char *src, int slen, size_t *len);

char *
base64_decode_new(const char *src, int slen, size_t *len);

#endif
