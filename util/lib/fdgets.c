// Copyright 2018 Schibsted

#include "fdgets.h"

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define S_OFF state[0]
#define S_NL state[1]

char *
fdgets(char *buf, size_t bufsz, int state[2], int fd) {
	int r;
	int off;

	if (S_NL) {
		if (S_OFF > S_NL) {
			memmove(buf, buf + S_NL, S_OFF - S_NL);
			S_OFF -= S_NL;
		} else {
			S_OFF = 0;
		}
		S_NL = 0;
	}

	off = 0;
	while (1) {
		char *nl;

		if (S_OFF && (nl = strpbrk(buf + off, "\r\n"))) {
			S_NL = nl - buf + 1;
			if (*nl == '\r' && *(nl + 1) == '\n')
				S_NL++;
			*nl = '\0';
			return buf;
		}

		off = S_OFF;

		if ((size_t)off >= bufsz - 1) {
			S_OFF = 0;
			return buf;
		}

		r = read(fd, buf + off, bufsz - off - 1);
		if (r < 0)
			return NULL;
		if (r == 0) {
			errno = 0;

			S_OFF = 0;
			if (off)
				return buf;
			return NULL;
		}

		S_OFF += r;
		buf[S_OFF] = '\0';
	}

}

int
fdgets_copy(char *dst, size_t dstsz, const char *src, int state[2]) {
	size_t n;

	if (S_NL) {
		if (S_OFF > S_NL) {
			n = S_OFF - S_NL;
			if (n >= dstsz)
				n = dstsz - 1;
			memcpy(dst, src + S_NL, n);
		} else {
			n = 0;
		}
		S_NL = 0;
	} else if (S_OFF) {
		n = S_OFF;
		if (n >= dstsz)
			n = dstsz - 1;
		memcpy(dst, src, n);
		dst[n] = '\0';
	} else {
		n = 0;
	}
	S_OFF = n;
	dst[n] = '\0';
	return n;
}

ssize_t
fdgets_read(void *dst, size_t count, char *buf, int state[2], int fd) {
	size_t n;

	if (S_NL) {
		if (S_OFF > S_NL) {
			memmove(buf, buf + S_NL, S_OFF - S_NL);
			S_OFF -= S_NL;
		} else {
			S_OFF = 0;
		}
		S_NL = 0;
	}

	if (count <= (size_t)S_OFF) {
		memcpy(dst, buf, count);
		memmove(buf, buf + count, S_OFF - count);
		S_OFF -= count;
		return count;
	}

	n = S_OFF;
	if (n) {
		memcpy(dst, buf, n);
		S_OFF = 0;
	}

	if (n < count) {
		int r = read(fd, (char*)dst + n, count - n);

		if (r < 0)
			return -1;
		if (r == 0) {
			errno = 0;
			return n;
		}
		n += r;
	}

	return n;
}

