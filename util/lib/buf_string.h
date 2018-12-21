// Copyright 2018 Schibsted

#ifndef BUF_STRING_H
#define BUF_STRING_H

#include "macros.h"

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct buf_string {  
	char *buf; 
	int len;
	int pos;
};

int bufcat(char **buf, int * RESTRICT buflen, int * RESTRICT bufpos, const char * fmt, ...) FORMAT_PRINTF(4, 5) NONNULL_ALL;
int vbufcat(char **buf, int * RESTRICT buflen, int * RESTRICT bufpos, const char * fmt, va_list ap) FORMAT_PRINTF(4, 0) NONNULL_ALL;
int bufwrite(char **buf, int * RESTRICT buflen, int * RESTRICT bufpos, const void * RESTRICT data, size_t len) NONNULL_ALL;

/* Use to preallocate the buf_string to a reasonable size. If not used, the default is BUFCAT_SIZE. */
void bsprealloc(struct buf_string *dst, size_t size) NONNULL_ALL;
int bscat(struct buf_string *dst, const char *fmt, ...) FORMAT_PRINTF(2, 3) NONNULL_ALL;
int vbscat(struct buf_string *dst, const char *fmt, va_list ap) FORMAT_PRINTF(2, 0) NONNULL_ALL;
int bswrite(struct buf_string *dst, const void *data, size_t len) NONNULL_ALL;

/* For use as callback when wanting a void* */
int bswrite_void(void *dst, const void *data, size_t len) NONNULL_ALL;

size_t bs_fread_all(struct buf_string *dst, FILE *f);

#ifdef __cplusplus
}
#endif

#endif /*BUF_STRING_H*/
