// Copyright 2018 Schibsted

#ifndef PLATFORM_MEMALLOC_FUNCTIONS_H
#define PLATFORM_MEMALLOC_FUNCTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdarg.h>
#include "error_functions.h"
#include "macros.h"

/*
 * Wrappers for memory allocation functions.
 * Guaranteed to return non-NULL.
 */
void *xmalloc(size_t size) ALLOCATOR;
void *xcalloc(size_t count, size_t size) ALLOCATOR;
void *zmalloc(size_t size) ALLOCATOR;
void *xrealloc(void *ptr, size_t size) ALLOCATOR;
char *xstrdup(const char *orig) ALLOCATOR;
char *xstrndup(const char *orig, size_t n) ALLOCATOR;
int xasprintf(char **strp, const char *fmt, ...) FORMAT_PRINTF(2, 3);
int xvasprintf(char **strp, const char *fmt, va_list ap);

/*
 * Memory allocation macros 
 *
*/

/* XXX alloca can probably not return NULL */
/* XXX Please note that this macro use some arguments twice, so watch out for side effects. */
#define ALLOCA_PRINTF(res, str, fmt, args...) do {	\
	int AP_len = snprintf(NULL, 0, fmt, args) + 1;	\
	char *AP_str;					\
	AP_str = alloca(AP_len);			\
	if (!AP_str)					\
		xerr(1, "alloca_printf");		\
	res = snprintf(AP_str, AP_len, fmt, args);	\
	res = res;					\
	str = AP_str;					\
} while(0)

/* XXX Please note that this macro use some arguments twice, so watch out for side effects. */
#define ALLOCA_VPRINTF(res, str, fmt) do {		\
	va_list AP_ap, AP_apc;				\
	int AP_len;					\
	char *AP_str;					\
	va_start(AP_ap, fmt);				\
	va_copy(AP_apc, AP_ap);				\
	AP_len = vsnprintf(NULL, 0, fmt, AP_apc) + 1;	\
	va_end(AP_apc);					\
	AP_str = alloca(AP_len);			\
	if (!AP_str)					\
		xerr(1, "alloca_vprintf");		\
	va_copy(AP_apc, AP_ap); 			\
	res = vsnprintf(AP_str, AP_len, fmt, AP_apc);	\
	res = res;					\
	va_end(AP_apc);					\
	va_end(AP_ap);					\
	str = AP_str;					\
} while(0)


#ifdef __cplusplus
}
#endif

#endif

