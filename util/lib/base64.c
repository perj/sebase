// Copyright 2018 Schibsted

#include <unistd.h>
#include "base64.h"
#include "memalloc_functions.h"
#include "string.h"

/*
	in = malloc(sizeof(char) * size);
	out = malloc(sizeof(char) * ((size+2)/3*4)+1);
	out[len] = '\0';
	for (i = 0; i < len; i+= 76)
		printf("%.76s\n", out+i);
	** Len will be equal to (size+2)/3*4)
*/

static const char b64table[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char b64urltable[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static const char deb64table[] = ">" /* + */
			   "\0\0\0"
			   "?456789:;<=" /* /, 0 - 9 */
			   "\0\0\0"
			   "\0" /* = */
			   "\0\0\0"
			   "\0\1\2\3\4\5\6\7\10\11\12\13\14\15\16\17\20\21\22\23\24\25\26\27\30\31" /* A - Z */
			   "\0\0\0\0\0\0"
			   "\32\33\34\35\36\37 !\"#$%&'()*+,-./0123"; /* a - z */

static void
base64_encode_triplet(const char *table, char *out, const unsigned char *in, int len) {
	out[0] = table[in[0] >> 2];

	out[1] = table[((in[0] & 0x3) << 4) | (len > 1 ? ((in[1] & 0xF0) >> 4) : 0)];
	out[2] = (len > 1 ? table[((in[1] & 0xF) << 2) | (len > 2 ? ((in[2] & 0xC0) >> 6) : 0)] : '=');
	out[3] = (len > 2 ? table[in[2] & 0x3F] : '=');
}

static ssize_t
base64_encode_table(const char *table, char *dst, const void *src, ssize_t len) {
	const unsigned char* current = (const unsigned char*)src;
	const unsigned char* end = (const unsigned char*)src + len;

	for (; current < end; current += 3) {
		base64_encode_triplet(table, dst, current, (end - current) < 3 ? (end - current) : 3);
		dst += 4;
	}
	*dst = '\0';

	return (end+2-(const unsigned char*)src)/3*4;
}

ssize_t
base64_encode(char *dst, const void *src, ssize_t len) {
	return base64_encode_table(b64table, dst, src, len);
}

ssize_t
base64url_encode(char *dst, const void *src, ssize_t len) {
	return base64_encode_table(b64urltable, dst, src, len) ;
}

ssize_t
base64_decode(char *dst, const char *src, ssize_t len)
{
	int bits, char_count;
	int errors = 0;
	int read_bytes = 0;
	ssize_t written = 0;
	int c = 0;

	char_count = 0;
	bits = 0;
	for (read_bytes = 0; read_bytes < len; ++read_bytes) {
		c = src[read_bytes];
		if (c == '=')
			break;
		if (c < '+' || c > 'z')
			continue;
		if (!deb64table[c - '+'] && c != 'A')
			continue;
		bits += deb64table[c - '+'];
		char_count++;
		if (char_count == 4) {
			dst[written++] = (bits >> 16);
			dst[written++] = ((bits >> 8) & 0xff);
			dst[written++] = (bits & 0xff);
			bits = 0;
			char_count = 0;
		} else {
			bits <<= 6;
		}
	}

	if(c=='=') {
		switch (char_count) {
		case 1:
			/* base64 encoding incomplete: at least 2 bits missing */
			errors++;
			break;
		case 2:
			dst[written++] =  (bits >> 10);
			break;
		case 3:
			dst[written++] = (bits >> 16);
			dst[written++] = ((bits >> 8) & 0xff);
			break;
		}
	} else {
		if (char_count) {
			/* base64 encoding incomplete: at least some bits truncated  */
			errors++;
		}
	}

	return written;
}

char *
base64_encode_new(const char *src, int slen, size_t *len) {
	size_t reslen = 0;
	char *buf = NULL;
	if (src) {
		if (slen < 0)
			slen = strlen(src);
		buf = zmalloc(BASE64_NEEDED(slen));
		reslen = base64_encode(buf, src, slen);
	}
	if (len)
		*len = reslen;
	return buf;
}

char *
base64_decode_new(const char *src, int slen, size_t *len) {
	size_t reslen = 0;
	char *buf = NULL;
	if (src) {
		if (slen < 0)
			slen = strlen(src);
		buf = zmalloc(BASE64DECODE_NEEDED(slen));
		reslen = base64_decode(buf, src, slen);
	}
	if (len)
		*len = reslen;
	return buf;
}

