// Copyright 2018 Schibsted

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "buf_string.h"
#include "cached_regex.h"
#include "memalloc_functions.h"
#include "url.h"

/*
 * Split a URL into components. It's up to the caller to free the return value 
 */
struct url *
split_url(const char *url) {
	int pr_len, host_len, port_len, path_len;
	int re_matches[OV_VSZ(8)];
	struct url *u;
	static struct cached_regex re = { URL_RE, PCRE_CASELESS | PCRE_UTF8 };

	if (!cached_regex_match(&re, url, re_matches, sizeof(re_matches) / sizeof(*re_matches)))
		return NULL;

	pr_len   =  re_matches[OV_END(1)] - re_matches[OV_START(1)];
	host_len =  re_matches[OV_END(2)] - re_matches[OV_START(2)];
	port_len =  re_matches[OV_END(3)] - re_matches[OV_START(3)];
	path_len =  re_matches[OV_END(4)] - re_matches[OV_START(4)];

	bool brackets = host_len > 0 && url[re_matches[OV_START(2)]] == '[';
	if (brackets)
		host_len -= 2;
	if (port_len > 0)
		port_len--; /* Strip : */

	u = xmalloc(sizeof(*u) + pr_len + host_len + port_len + path_len + 4);

	u->protocol = (char *)(u + 1);
	u->host = u->protocol + pr_len + 1;
	u->port = u->host + host_len + 1;
	u->path = u->port + port_len + 1;

	memcpy(u->protocol, url + re_matches[OV_START(1)],   pr_len);
	memcpy(u->host,     url + re_matches[OV_START(2)] + (brackets ? 1 : 0), host_len);
	memcpy(u->port,     url + re_matches[OV_START(3)] + 1, port_len);
	memcpy(u->path,     url + re_matches[OV_START(4)],   path_len);

	u->protocol[pr_len] = '\0';
	u->host[host_len]   = '\0';
	u->port[port_len]   = '\0';
	u->path[path_len]   = '\0';

	return u;
}

char *
url_decode(char *str, int max, const char *stopchars, int unsafe, int *is_utf8) {
	char *ptr = str;
	int len = strlen(str);
	char *end = str + (max >= 0 ? max : len);
	int trailing = 0;
	int minuch = 0, uch = 0;

	if (is_utf8)
		*is_utf8 = 1;

	while (*ptr && ptr < end && (!stopchars || !strchr(stopchars, *ptr))) {
		if (*ptr == '%' && isxdigit(*(ptr + 1)) && isxdigit(*(ptr + 2))) {
			int a = *(unsigned char*)(ptr + 1);
			int b = *(unsigned char*)(ptr + 2);

			if (a <= '9')
				a -= '0';
			else if (a <= 'F')
				a -= 'A' - 10;
			else
				a -= 'a' - 10;
			if (b <= '9')
				b -= '0';
			else if (b <= 'F')
				b -= 'A' - 10;
			else
				b -= 'a' - 10;
			*ptr = a * 16 + b;
			if (!unsafe) {
				if (*ptr == '\t')
					*ptr = ' ';
				else if (*(unsigned char*)ptr < ' ') /* Extra safety check. */
					*ptr = '?';
			}
			memmove(ptr + 1, ptr + 3, str + len - (ptr + 3) + 1);
			end -= 2;
			len -= 2;
		} else if (*ptr == '+') {
			*ptr = ' ';
		}
		if (is_utf8 && *is_utf8) {
			if ((*ptr & 0x80) == 0) {
				if (trailing > 0)
					*is_utf8 = 0;
			} else if ((*ptr & 0xC0) == 0x80) {
				uch = uch << 6 | (*ptr & 0x3F);
				if (--trailing < 0)
					*is_utf8 = 0;
				else if (trailing == 0 && uch < minuch)
					*is_utf8 = 0;
			} else if (trailing > 0) {
				*is_utf8 = 0;
			} else if ((*ptr & 0xE0) == 0xC0) {
				trailing = 1;
				minuch = 0x80;
				uch = *ptr & 0x1F;
			} else if ((*ptr & 0xF0) == 0xE0) {
				trailing = 2;
				minuch = 0x800;
				uch = *ptr & 0x1F;
			} else if ((*ptr & 0xF8) == 0xF0) {
				trailing = 3;
				minuch = 0x10000;
				uch = *ptr & 0x1F;
			} else {
				*is_utf8 = 0;
			}
		}
		ptr++;
	}
	if (is_utf8 && trailing > 0)
		*is_utf8 = 0;
	return ptr;
}

void
url_encode(struct buf_string *dst, const char *str, size_t len) {
	const char *end = str + len;

	for (; str < end ; str++) {
		if ((*str & 0xFF) >= 0x7F || *str < 0x20) {
			bufcat(&dst->buf, &dst->len, &dst->pos, "%%%02X", *str & 0xFF);
		} else switch (*str) {
		/* encode ' ' as %20 instead of the old '+' */
		case ' ':
		/* Possibly reserved: */
		case ';':
		case '/':
		case '?':
		case ':':
		case '@':
		case '&':
		case '=':
		case '+':
		case '$':
		case ',':
		/* Excluded: */
		case '<':
		case '>':
		case '#':
		case '%':
		case '"':
		/* Unwise: */
		case '{':
		case '}':
		case '|':
		case '\\':
		case '^':
		case '[':
		case ']':
		case '`':
		/* Added by rfc 3986 */
		case '\'':
		case '!':
		/* These are also mentioned in rfc 3986, but they seem safe in practice.
		case '(':
		case ')':
		case '*':
		*/
			bufcat(&dst->buf, &dst->len, &dst->pos, "%%%02X", *str & 0xFF);
			break;

		default:
			bufwrite(&dst->buf, &dst->len, &dst->pos, str, 1);
			break;
		}
	}
}

void
url_encode_postdata(struct buf_string *dst, const char *str, size_t len) {
	const char *end = str + len;

	for (; str < end ; str++) {
		if (*str < 0x20) {
			bufcat(&dst->buf, &dst->len, &dst->pos, "%%%02X", *str & 0xFF);
		} else switch (*str) {
		case ' ':
		case '?':
		case '&':
		case '=':
		case '+':
		case '#':
		case '%':
		case '\x7f':
			bufcat(&dst->buf, &dst->len, &dst->pos, "%%%02X", *str & 0xFF);
			break;

		default:
			bufwrite(&dst->buf, &dst->len, &dst->pos, str, 1);
			break;
		}
	}
}
