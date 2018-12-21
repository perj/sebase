// Copyright 2018 Schibsted

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
#include <math.h>
#include <errno.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>

#include "buf_string.h"
#include "memalloc_functions.h"
#include "string_functions.h"

void
write_utf8_char(char **str, unsigned int ch) {
	if (ch < 0x80) {
		*(*str)++ = ch;
	} else if (ch < 0x800) {
		*(*str)++ = 0xC0 | ch >> 6;
		*(*str)++ = 0x80 | (ch & 0x3F);
	} else if (ch < 0x10000) {
		*(*str)++ = 0xE0 | ch >> 12;
		*(*str)++ = 0x80 | ((ch >> 6) & 0x3F);
		*(*str)++ = 0x80 | (ch & 0x3F);
	} else {
		*(*str)++ = 0xF0 | ch >> 18;
		*(*str)++ = 0x80 | ((ch >> 12) & 0x3F);
		*(*str)++ = 0x80 | ((ch >> 6) & 0x3F);
		*(*str)++ = 0x80 | (ch & 0x3F);
	}
}

void
ltrim(char *str) {
	char *ptr;

	/* Find first non space character */
	for (ptr = str; *ptr && isspace(*ptr); ptr++)
		;

	if (*ptr) {
		while (*ptr)
			*str++ = *ptr++;
	}

	*str = '\0';
}

void
rtrim(char *str) {
	char *ptr = NULL;

	while (*str) {
		/* Find first of consecutive trailing spaces */
		if (isspace(*str)) {
			if (ptr == NULL)
				ptr = str;
		} else {
			if (ptr)
				ptr = NULL;
		}

		str++;
	}

	if (ptr)
		*ptr = '\0';
}

void
trim(char *str) {
	ltrim(str);
	rtrim(str);
}

char *
stristrptrs(const char *h, const char *ns, const char *ne, const char *delim) {
	const char *tmp;
	const char *tmp1;
	const char *start = h;

	if (!ne)
		ne = ns + strlen(ns);
	
	while (*h) {
		while (*h && toupper(*h) != toupper(*ns)) {
			h++;
		}

		if (!*h)
			break;
		
		if (h == start || !delim || strchr(delim, *(h - 1))) {
			tmp = h;
			tmp1 = ns;
			while (*tmp && tmp1 != ne && toupper(*tmp1) == toupper(*tmp)) {
				tmp++;
				tmp1++;
			}
			
			if (tmp1 == ne && (!delim || !*tmp || strchr(delim, *tmp)))
				return (char *)h;
		}
		h++;
	}

	return NULL;
}

char *
strstrptrs(const char *h, const char *ns, const char *ne, const char *delim) {
	const char *tmp;
	const char *tmp1;
	const char *start = h;

	if (!ne)
		ne = ns + strlen(ns);
	
	while (*h) {
		while (*h && *h != *ns) {
			h++;
		}

		if (!*h)
			break;
		
		if (h == start || !delim || strchr(delim, *(h - 1))) {
			tmp = h;
			tmp1 = ns;
			while (*tmp && tmp1 != ne && *tmp1 == *tmp) {
				tmp++;
				tmp1++;
			}

			if (tmp1 == ne && (!delim || !*tmp || strchr(delim, *tmp)))
				return (char *)h;
		}
		h++;
	}

	return NULL;
}

char *
strmodify(char *str, int (*modifier)(int)) {
	char *tmp = str;

	while (*tmp) {
		*tmp = modifier(*tmp);
		tmp++;
	}
	return str;
}

int
count_chars(const char *str, char ch) {
	const char *ptr;
	int count = 0;

	if (str) {
		for (ptr = str; *ptr; ptr++) {
			if (*ptr == ch)
				count ++;
		}
	}

	return count;
}

/*
 * Search subject for all occurences of from, and replaces them with to.
 * The result is returned in a new string which must be free'd by the caller.
 */
char *
str_replace(const char *subject, const char *from, const char *to) {
	struct buf_string res = {0};
	size_t flen = strlen(from);
	size_t tlen = strlen(to);

	const char *next = NULL;

	if (tlen <= flen)
		bsprealloc(&res, strlen(subject)+1);
	else
		bsprealloc(&res, strlen(subject)*2);

	while ((next = strstr(subject, from))) {
		bufwrite(&res.buf, &res.len, &res.pos, subject, next - subject);
		bufwrite(&res.buf, &res.len, &res.pos, to, tlen);
		subject = next + flen;
	}
	bufwrite(&res.buf, &res.len, &res.pos, subject, strlen(subject));
	return res.buf;
}

/*
 * Replace any occurence of a character in from_set with a fixed character.
 * Returns copy of string, or NULL if any of s or from_set are NULL.
 * to-do: generalize to 'strtr' and remove replace_chars().
 */
char *
strtrchr(const char *s, const char *from_set, const char to) {
	if (!s || !from_set)
		return NULL;

	char *result = xstrdup(s);
	char *search_from = result;
	char *p;

	while ((p = strpbrk(search_from, from_set))) {
		*p = to;
		search_from = p + 1;
	}

	return result;
}

/*
 * Returns a new string which doesn't include the characters in reject_set,
 * or if invert is set, include ONLY the characters in reject_set.
 */
char *
remove_subset(const char *s, const char *reject_set, int invert) {
	if (!s || !reject_set)
		return NULL;

	char *result_start;
	char *result;

	result_start = result = zmalloc(strlen(s) + 1);

	do {
		if ((!invert) == (strchr(reject_set, *s) == NULL))
			*result++ = *s;
	} while (*s++);

	return result_start;
}

/*
 * Substitute characters in even positions by the odd ones 
 * Returns a new string that *you* must free
 */
char *
replace_chars(const char *string, int len, const char *char_list) {
	if (len < 0)
		len = strlen(string);
        char *output = xmalloc(len + 1);
        const char *in = string, *bad_char;
	const char *end = string + len;
	char *out = output;
	char map[256] = {0};

	for (bad_char = char_list ; *bad_char ; bad_char += 2)
		map[*(unsigned char*)bad_char] = *(bad_char + 1);

        while (in < end) {
		*out++ = map[*(unsigned char*)in] ?: *in;
		in++;
        }
	*out = '\0';

	return output;
}

char *
replace_chars_utf8(const char *string, int len, int (*map)[2], int nmap) {
	if (len < 0)
		len = strlen(string);
	char *output = xmalloc(len*4 + 1);
	const char *in = string;
	const char *end = string + len;
	char *out = output;

	while (in < end) {
		int ch = utf8_char(&in);
		int o = 0, n = nmap;

		while (n > 0) {
			int even = ~n & 1;

			n /= 2;
			int i = o + n;

			int res = map[i][0] - ch;
			if (res == 0) {
				ch = map[i][1];
				break;
			}
			if (res > 0)
				continue;
			o += n + 1;
			n -= even;
		}
		write_utf8_char(&out, ch);
	}
	*out = '\0';

	return output;
}

static int
cmprcu(const void *a, const void *b) {
	return *(const int*)a - *(const int*)b;
}

int (*
replace_chars_utf8_create_map(const char *char_list, int *nmap))[2] {
	const char *bad_char = char_list;
	int bch;
	int n = 0;
	int (*map)[2] = xmalloc(sizeof (*map) * 256); /* XXX dynamic */

	while ((bch = utf8_char(&bad_char)))
	{
		int rch = utf8_char(&bad_char);

		if (n >= 256)
			xerrx(1, "Map overflow in replace_chars_utf8");

		map[n][0] = bch;
		map[n][1] = rch;
		n++;
	}
	qsort(map, n, sizeof(*map), cmprcu);

	*nmap = n;
	return map;
}

int
is_ws(const char* s) {
	if (!s)
		return 1;
	char c;
	while ( (c = *s++) ) {
		switch(c) {
			case ' ':
			case '\t':
			case '\r':
			case '\n':
				continue;
			default:
				return 0;
		}
	}
	return 1;
}

/*
 * adds thousands separators to numbers, with optional padding to a minimum length.
 */
char *
pretty_format_number_thousands(int value, int min_length, char thou_sep_char) {
	char *original;
	char *res;

	/* original number */
	if (min_length > 0) 
		xasprintf(&original, "%0*d", min_length, value);
	else
		xasprintf(&original, "%d", value);
	int o, f;
	int len = strlen(original);
	int final_len = len + (len - 1) / 3;

	/* add separators */
	res = xmalloc(final_len + 1);
	for (o = f = 0; o < len; ++o, ++f)
	{
		res[f] = original[o];
		if ((len - o - 1) % 3 == 0)
			res[++f] = thou_sep_char;
	}
	res[final_len] = 0;
	free(original);
	return res;
}

/*
 * Escape double-quotes (and backslash as a consequence). Returns new string, owned by caller.
 */
char *
escape_dquotes(const char *uqs) {
	const char *t = uqs;
	char *qs, *s;
	size_t len;

	for (len = 0; *t; t++) {
		if (*t == '"' || *t == '\\')
			len++;
		len++;
	}

	s = qs = xmalloc(len + 1);

	do {
		if (*uqs == '"' || *uqs == '\\')
			*s++ = '\\';
	} while ((*s++ = *uqs++));

	return qs;
}

/*
 * Escape ASCII control characters (ASCII < 32)
 * Returns a new string which the caller takes ownership of.
 */
char *
escape_control_characters(const char *s) {
	/* count characters to escape */
	const char *scan = s;
	unsigned char ch;

	int num_escape = 0;
	int len = 0;
	while ((ch = *scan++)) {
		if (ch < 32)
			++num_escape;
		++len;
	}

	if (num_escape <= 0)
		return xstrdup(s);

	/* Allocate memory, room for '\nn' for each escape (worst-case) */
	char *res = xmalloc(1 + len + num_escape*3);

	/* Scan and translate */
	scan = s;
	char *out = res;
	while ((ch = *scan++)) {

		if (ch > 31) {
			*out++ = ch;
		} else {
			switch (ch) {
				case '\n':
					*out++ = '\\';
					*out++ = 'n';
					break;
				case '\f':
					*out++ = '\\';
					*out++ = 'f';
					break;
				case '\r':
					*out++ = '\\';
					*out++ = 'r';
					break;
				case '\t':
					*out++ = '\\';
					*out++ = 't';
					break;
				case '\v':
					*out++ = '\\';
					*out++ = 'v';
					break;
				case '\a':
					*out++ = '\\';
					*out++ = 'a';
					break;
				case '\b':
					*out++ = '\\';
					*out++ = 'b';
					break;
				case '\e':
					*out++ = '\\';
					*out++ = 'e';
					break;
				default:
					*out++ = '\\';
					// XXX this can't be right. Should be octal?
					out += sprintf(out, "%d", ch);
			}
		}
	}
	*out = '\0';

	return res;
}

int
utf8_char_safe(const char **str, const char *end) {
	unsigned int ch;
	unsigned char *src = *(unsigned char**)str;
	int maxl;

	if (!end) {
		maxl = 4;
	} else {
		maxl = end - *str;
		if (maxl <= 0)
			return 0;
	}

	if (src[0] <= 0x80) {
		ch = src[0];
		*str += 1;
	} else if (maxl >= 2 && (src[0] & 0xE0) == 0xC0 && (src[1] & 0xC0) == 0x80) {
		ch = (src[0] & 0x1F) << 6 | (src[1] & 0x3F);
		if (ch < 0x80) /* Need to recheck for security */
			ch = '?';
		*str += 2;
	} else if (maxl >= 3 && (src[0] & 0xF0) == 0xE0 && (src[1] & 0xC0) == 0x80 && (src[2] & 0xC0) == 0x80) {
		ch = (src[0] & 0xF) << 12 | (src[1] & 0x3F) << 6 | (src[2] & 0x3F);
		if (ch < 0x800) /* Need to recheck for security */
			ch = '?';
		*str += 3;
	} else if (maxl >= 4 && (src[0] & 0xF8) == 0xF0 && (src[1] & 0xC0) == 0x80 && (src[2] & 0xC0) == 0x80
			&& (src[3] & 0xC0) == 0x80) {
		ch = (src[0] & 0xE) << 18 | (src[1] & 0x3F) << 12 | (src[2] & 0x3F) << 6 | (src[3] & 0x3F);
		if (ch < 0x10000) /* Need to recheck for security */
			ch = '?';
		*str += 4;
	} else {
		/* UTF-8 supports up to 6 byte encodings, but max for Unicode is 4. */
		ch = src[0];
		*str += 1;
	}

	return ch;
}

int
utf8_char(const char **str) {
	return utf8_char_safe(str, NULL);
}

const char *
xstrerror(int errnum) {
	static __thread char errbuf[256];

	errbuf[0] = '\0';
#ifdef __GLIBC__
	const char *r = strerror_r(errnum, errbuf, sizeof(errbuf));
	if (r != errbuf)
		return r;
#else
	strerror_r(errnum, errbuf, sizeof(errbuf));
#endif
	if (!errbuf[0])
		snprintf(errbuf, sizeof(errbuf), "Unknown (%d)", errnum);
	return errbuf;
}

char *
xstrsignal(int signum, char *buf, size_t bufsz) {
	static pthread_mutex_t strsignal_lock = PTHREAD_MUTEX_INITIALIZER;

	/* strsignal is not thread safe. */
	pthread_mutex_lock(&strsignal_lock);
	char *sig = strsignal(signum);
	if (sig)
		snprintf(buf, bufsz, "%s", sig);
	else
		snprintf(buf, bufsz, "unknown %d", signum);
	pthread_mutex_unlock(&strsignal_lock);
	return buf;
}

char *
strwait(int status, char *buf, size_t bufsz) {
	char sigbuf[256];

	if (WIFEXITED(status)) {
		snprintf(buf, bufsz, "exit status %d", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		snprintf(buf, bufsz, "signal %s%s", xstrsignal(WTERMSIG(status), sigbuf, sizeof (sigbuf)),
#ifdef WCOREDUMP
				WCOREDUMP(status) ? " dumped core" : ""
#else
				""
#endif
				);
	} else if (WIFSTOPPED(status)) {
		snprintf(buf, bufsz, "stopped %s", xstrsignal(WSTOPSIG(status), sigbuf, sizeof (sigbuf)));
#ifdef WIFCONTINUED
	} else if (WIFCONTINUED(status)) {
		snprintf(buf, bufsz, "continued");
#endif
	} else {
		snprintf(buf, bufsz, "unknown %d", status);
	}
	return buf;
}

int
get_hostname_by_addr(const char* remote_addr, char* buf, int buf_size, int flags) {
	struct sockaddr_storage ias;
	int addr_size = 0;
	int retval = 0;

	if (inet_pton(AF_INET, remote_addr, &(((struct sockaddr_in*)&ias)->sin_addr)) == 1) {
		ias.ss_family = AF_INET;
		addr_size = sizeof(struct sockaddr_in);
	} else if (inet_pton(AF_INET6, remote_addr, &(((struct sockaddr_in6*)&ias)->sin6_addr)) == 1) {
		ias.ss_family = AF_INET6;
		addr_size = sizeof(struct sockaddr_in6);
	}

	if (addr_size && (retval = getnameinfo((struct sockaddr*)&ias, addr_size, buf, buf_size, NULL, 0, flags)) == 0)
		retval = 1;

	return retval;
}

int
json_encode_char(char *dst, size_t dlen, char ch, bool escape_solus) {
	if (!dlen)
		return 0;

	static char escapes[256] = {
		['\b'] = 'b',
		['\f'] = 'f',
		['\n'] = 'n',
		['\r'] = 'r',
		['\t'] = 't',
		['\\'] = '\\',
		['"']  = '"',
		['/']  = '/',
	};
	char e;
	if ((e = escapes[(unsigned char)ch]) && (e != '/' || escape_solus)) {
		if (dlen <= 1)
			return 0;
		dst[0] = '\\';
		dst[1] = e;
		return 2;
	}

	if ((unsigned char)ch > 0x1f) {
		dst[0] = ch;
		return 1;
	}
	/* encode other control characters as unicode hex */
	int n = snprintf(dst, dlen, "\\u%04x", (unsigned char)ch);
	if (n < 0)
		return 0;
	if ((size_t)n >= dlen)
		return dlen - 1;
	return n;
}

int
string_to_int32(const char *s, int32_t *dest) {
	char *e;
	long int i;

	errno = 0;
	i = strtol(s, &e, 10);
	if ((errno == ERANGE && (i == LONG_MAX || i == LONG_MIN)) || (errno != 0 && i == 0)
			|| (i < INT32_MIN || i > INT32_MAX)) {
		return -1;
	}

	if (*e != '\0' || e == s) {
		return 1;
	}

	*dest = (int32_t)i;
	return 0;
}

int
string_to_uint32(const char *s, uint32_t *dest) {
	char *e;
	long int i;

	errno = 0;
	i = strtol(s, &e, 10);
	if ((errno == ERANGE && (i == LONG_MAX || i == LONG_MIN)) || (errno != 0 && i == 0)
			|| (i < 0 || i > UINT32_MAX)) {
		return -1;
	}

	if (*e != '\0' || e == s) {
		return 1;
	}

	*dest = (uint32_t)i;
	return 0;
}

int
string_to_float(const char *s, float *dest) {
	char *e;
	float f;

	errno = 0;
	f = strtof(s, &e);
	if ((errno == ERANGE && (f == HUGE_VALF || f == -HUGE_VALF)) || (errno != 0 && f == 0.0)) {
		return -1;
	}

	if (*e != '\0' || e == s) {
		return 1;
	}

	*dest = f;
	return 0;
}

int
string_to_double(const char *s, double *dest) {
	char *e;
	double d;

	errno = 0;
	d = strtod(s, &e);
	if ((errno == ERANGE && (d == HUGE_VAL || d == -HUGE_VAL)) || (errno != 0 && d == 0.0)) {
		return -1;
	}

	if (*e != '\0' || e == s) {
		return 1;
	}

	*dest = d;
	return 0;
}
