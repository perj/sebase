// Copyright 2018 Schibsted

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memalloc_functions.h"

void *
xmalloc(size_t size) {
	void *ptr;

	if ((ptr = malloc(size)) == NULL) {
		xwarn("malloc(%lu)", (unsigned long)size);
		abort();
	}

	return ptr;
}

void *
xcalloc(size_t count, size_t size) {
	void *ptr;

	if ((ptr = calloc(count, size)) == NULL) {
		xwarn("calloc(%lu, %lu)", (unsigned long)count, (unsigned long)size);
		abort();
	}

	return ptr;
}

void *
zmalloc(size_t size) {
	return xcalloc(1, size);
}

void *
xrealloc(void *ptr, size_t size) {
	if ((ptr = realloc(ptr, size)) == NULL) {
		xwarn("realloc(%lu)", (unsigned long)size);
		abort();
	}

	return ptr;
}

char *
xstrdup(const char *orig) {
	char *ptr;

	if ((ptr = strdup(orig)) == NULL) {
		xwarn("strdup(%lu)", (unsigned long)strlen(orig));
		abort();
	}

	return ptr;
}

char *
xstrndup(const char *orig, size_t n) {
	char *ptr;

	if ((ptr = strndup(orig, n)) == NULL) {
		xwarn("strndup(%lu)", (unsigned long)strlen(orig));
		abort();
	}

	return ptr;
}


int
xasprintf(char **strp, const char *fmt, ...) {
	int res;
	va_list ap;

	va_start(ap, fmt);
	res = vasprintf(strp, fmt, ap);
	va_end(ap);
	if (res == -1) {
		xwarn("asprintf");
		abort();
	}

	return res;
}

int
xvasprintf(char **strp, const char *fmt, va_list ap) {
	int res;

	res = vasprintf(strp, fmt, ap);
	if (res == -1) {
		xwarn("vasprintf");
		abort();
	}

	return res;
}


