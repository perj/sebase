// Copyright 2018 Schibsted

#ifndef COMMON_UNICODE_H
#define COMMON_UNICODE_H
 
#include <unicode/ucnv.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decode a UTF-8 string using the converters.
 * src and dst can be the same pointer.
 */
char *utf8_decode(char *dst, int *dstlen, const char *str, int slen, UConverter *utf8_cnv, UConverter *dst_cnv);

/*
 * Calculate the length (number of chars) of a UTF-8 string
 */
size_t strlen_utf8(const char *str);

#ifdef __cplusplus
}
#endif

#endif /*COMMON_UNICODE_H*/
