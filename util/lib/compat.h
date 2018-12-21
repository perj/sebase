// Copyright 2018 Schibsted

#include <stdlib.h>
#if __has_include(<bsd/stdlib.h>)
#include <bsd/stdlib.h>
#endif
#if __has_include(<bsd/string.h>)
#include <bsd/string.h>
#endif
#if __has_include(<bsd/unistd.h>)
#include <bsd/unistd.h>
#endif

#ifndef __GLIBC__

#define strdupa(s) __extension__({ const char *__s = (s); size_t __ss = strlen(__s); char *__v = alloca(__ss + 1); (char*)memcpy(__v, __s, __ss + 1); })
#define strndupa(s, n) __extension__({ const char *__s = (s); size_t __n = (n); char *__v = alloca(__n + 1); strncpy(__v, __s, __n); })

#ifndef __OpenBSD__
#define memrchr(h, n, l) __extension__({ const void *__h = (h), *__e = __h + l; int __n = (n); void *__m = NULL; while ((__h = memchr(__h, __n, __e - __h))) __m = (void*)__h++; __m; })
#endif

#endif
