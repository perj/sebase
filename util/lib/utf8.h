// Copyright 2018 Schibsted

#ifndef BASECOMMON_UTF8_H
#define BASECOMMON_UTF8_H
 
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Simple encoder and decoder. Returned string is allocated and needs to be free()-ed.
 *
 * Please notice that this isn't strictly ISO-8859-1, but rather Windows-1252, see the comment
 * above the function implementations for the reason why.
 */
char *latin1_to_utf8(const char *src, int slen, int *dstlen);
char *utf8_to_latin1(const char *src);
char *utf8_to_latin1_len(const char *src, size_t len, size_t *outlen);
size_t utf8_to_latin1_buf(const char *src, size_t slen, char *dst, size_t dlen);

char *utf8_to_latin2_len(const char *src, size_t len, size_t *outlen);
size_t utf8_to_latin2_buf(const char *src, size_t slen, char *dst, size_t dlen);

/*
 * Encode string from src to dst. Returning the number of converted
 * latin encoding chars and if outlen is non NULL the length of the UTF-8 string.
 */
size_t latin1_to_utf8_buf(size_t *outlen, const char *src, size_t slen, char *dst, size_t dlen);
size_t latin2_to_utf8_buf(size_t *outlen, const char *src, size_t slen, char *dst, size_t dlen);


#ifdef __cplusplus
}
#endif

#endif /*BASECOMMON_UTF8_H*/
