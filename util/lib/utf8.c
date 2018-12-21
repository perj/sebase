// Copyright 2018 Schibsted

#include <string.h>
#include <stdlib.h>

#include "utf8.h"
#include "memalloc_functions.h"
#include "string_functions.h"

/*
 * From http://en.wikipedia.org/wiki/Windows-1252
 *
 * Most modern web browsers and e-mail clients treat the MIME charset
 * ISO-8859-1 as Windows-1252 to accommodate such mislabeling. This is
 * now standard behavior in the draft HTML 5 specification, which
 * requires that documents advertised as ISO-8859-1 actually be parsed
 * with the Windows-1252 encoding.
 */
static struct {
	int codepoint;
	unsigned char win1252;
	const char *encoding;
} translit[] = {
	{ 0x20ac, 0x80, "\xe2\x82\xac" }, /* € = EURO SIGN */
	{ 0x0, 0x0, "?" },
	{ 0x201a, 0x82, "\xe2\x80\x9a" }, /* ‚ = SINGLE LOW-9 QUOTATION MARK */
	{ 0x192, 0x83, "\xc6\x92" },      /* ƒ = LATIN SMALL LETTER F WITH HOOK */
	{ 0x201e, 0x84, "\xe2\x80\x9e" }, /* „ = DOUBLE LOW-9 QUOTATION MARK */
	{ 0x2026, 0x85, "\xe2\x80\xa6" }, /* … = HORIZONTAL ELLIPSIS */
	{ 0x2020, 0x86, "\xe2\x80\xa0" }, /* † = DAGGER */
	{ 0x2021, 0x87, "\xe2\x80\xa1" }, /* ‡ = DOUBLE DAGGER */
	{ 0x2c6, 0x88, "\xcb\x86" },      /* ˆ = MODIFIER LETTER CIRCUMFLEX ACCENT */
	{ 0x2030, 0x89, "\xe2\x80\xb0" }, /* ‰ = PER MILLE SIGN */
	{ 0x160, 0x8a, "\xc5\xa0" },      /* Š = LATIN CAPITAL LETTER S WITH CARON */
	{ 0x2039, 0x8b, "\xe2\x80\xb9" }, /* ‹ = SINGLE LEFT-POINTING ANGLE QUOTATION MARK */
	{ 0x152, 0x8c, "\xc5\x92" },      /* Œ = LATIN CAPITAL LIGATURE OE */
	{ 0x0, 0x0, "?" },
	{ 0x17d, 0x8e, "\xc5\xbd" },      /* Ž = LATIN CAPITAL LETTER Z WITH CARON */
	{ 0x0, 0x0, "?" },
	{ 0x0, 0x0, "?" },
	{ 0x2018, 0x91, "\xe2\x80\x98" }, /* ‘ = LEFT SINGLE QUOTATION MARK */
	{ 0x2019, 0x92, "\xe2\x80\x99" }, /* ’ = RIGHT SINGLE QUOTATION MARK */
	{ 0x201c, 0x93, "\xe2\x80\x9c" }, /* “ = LEFT DOUBLE QUOTATION MARK */
	{ 0x201d, 0x94, "\xe2\x80\x9d" }, /* ” = RIGHT DOUBLE QUOTATION MARK */
	{ 0x2022, 0x95, "\xe2\x80\xa2" }, /* • = BULLET */
	{ 0x2013, 0x96, "\xe2\x80\x93" }, /* – = EN DASH */
	{ 0x2014, 0x97, "\xe2\x80\x94" }, /* — = EM DASH */
	{ 0x2dc, 0x98, "\xcb\x9c" },      /* ˜ = SMALL TILDE */
	{ 0x2122, 0x99, "\xe2\x84\xa2" }, /* ™ = TRADE MARK SIGN */
	{ 0x161, 0x9a, "\xc5\xa1" },      /* š = LATIN SMALL LETTER S WITH CARON */
	{ 0x203a, 0x9b, "\xe2\x80\xba" }, /* › = SINGLE RIGHT-POINTING ANGLE QUOTATION MARK */
	{ 0x153, 0x9c, "\xc5\x93" },      /* œ = LATIN SMALL LIGATURE OE */
	{ 0x0, 0x0, "?" },
	{ 0x17e, 0x9e, "\xc5\xbe" },      /* ž = LATIN SMALL LETTER Z WITH CARON */
	{ 0x178, 0x9f, "\xc5\xb8" },      /* Ÿ = LATIN CAPITAL LETTER Y WITH DIAERESIS */
};

char *
latin1_to_utf8(const char *src, int slen, int *dstlen) {
	char *dst;
	const char *s = src;
	const char *end = NULL;
	size_t dlen, utf8len = 0;

	if (slen >= 0)
		end = src + slen;
	else
		slen = 0;

	do {
		if ((*s & 0xC0) == 0xC0 || (*s & 0xA0) == 0xA0)
			utf8len += 2;
		else if (*s & 0x80)
			utf8len += 3;	/* may be too much but that is no issue */
		else
			utf8len++;
		if (end && s >= end)	/* make sure to also count space for the '\0' */
			break;
	} while (*s++);

	dst = xmalloc(utf8len);

	latin1_to_utf8_buf(&dlen, src, slen, dst, utf8len);

	memset(dst + dlen, 0, utf8len - dlen);
	if (dstlen)
		*dstlen = dlen;

	return dst;
}

size_t
latin1_to_utf8_buf(size_t *outlen, const char *src, size_t slen,
    char *dst, size_t dlen)
{
	const char *s = src;
	const char *send = NULL;
	char *d = dst;
	char *dend = dst + dlen;

	if (slen)
		send = s + slen;

	for (; *s; s++) {
		if (send && s >= send)
			break;
		if ((*s & 0xC0) == 0xC0) {
			if (d + 2 >= dend)
				break;
			*d++ = 0xC3;
			*d++ = 0x80 + (*s & 0x3F);
		} else if ((*s & 0xA0) == 0xA0) {
			if (d + 2 >= dend)
				break;
			*d++ = 0xC2;
			*d++ = 0x80 + (*s & 0x3F);
		} else if (*s & 0x80) {
			const char *x = translit[*s & 0x1f].encoding;
			if (d + 3 >= dend)
				break;
			while (*x)
				*d++ = *x++;
		} else {
			if (d + 1 >= dend)
				break;
			*d++ = *s;
		}
	}

	if (outlen)
		*outlen = d - dst;
	if (d < dend)
		*d = '\0';
	return s - src;
}

size_t
utf8_to_latin1_buf(const char *src, size_t slen, char *dst, size_t dlen) {
	const char *end = src + slen;
	const char *dend = dst + dlen - 1;
	char *start = dst;

	while (src < end && dst < dend) {
		int c = utf8_char(&src);
		if (c > 255) {
			unsigned int i;
			for (i = 0; i < sizeof(translit) / sizeof(translit[0]); i++) {
				if (translit[i].codepoint == c) {
					if ((c = translit[i].win1252) == 0)
						c = '?';
					break;
				}
			}
			if (i == sizeof(translit) / sizeof(translit[0]))
				c = '?';
		}
		*dst++ = c;
	}
	*dst = '\0';
	return dst - start;
}

char *
utf8_to_latin1_len(const char *src, size_t len, size_t *outlen) {
	char *buf = xmalloc(len + 1);
	size_t olen = utf8_to_latin1_buf(src, len, buf, len + 1);
	if (outlen)
		*outlen = olen;
	return buf;
}

char *
utf8_to_latin1(const char *src) {
	return utf8_to_latin1_len(src, strlen(src), NULL);
}

/*
 * Latin 2 seems to be the ISO one, not Windows-1250.
 */
static struct {
	int codepoint;
	const char *encoding;
} latin2_translit[0x60] = {
	{ 0, }, /* Non-breaking space */
	{ 0x0104, "\xc4\x84" }, /* Ą */
	{ 0x02D8, "\xcb\x98" }, /* ˘ */
	{ 0x0141, "\xc5\x81" }, /* Ł */
	{ 0, }, /* ¤ */
	{ 0x013D, "\xc4\xbd" }, /* Ľ */
	{ 0x015A, "\xc5\x9a" }, /* Ś */
	{ 0, }, /* § */
	{ 0, }, /* ¨ */
	{ 0x0160, "\xc5\xa0" }, /* Š */
	{ 0x015E, "\xc5\x9e" }, /* Ş */
	{ 0x0164, "\xc5\xa4" }, /* Ť */
	{ 0x0179, "\xc5\xb9" }, /* Ź */
	{ 0, }, /* Soft hyphen */
	{ 0x017D, "\xc5\xbd" }, /* Ž */
	{ 0x017B, "\xc5\xbb" }, /* Ż */

	{ 0, }, /* ° */
	{ 0x0105, "\xc4\x85" }, /* ą */
	{ 0x02DB, "\xcb\x9b" }, /* ˛ */
	{ 0x0142, "\xc5\x82" }, /* ł */
	{ 0, }, /* ´ */
	{ 0x013E, "\xc4\xbe" }, /* ľ */
	{ 0x015B, "\xc5\x9b" }, /* ś */
	{ 0x02C7, "\xcb\x87" }, /* ˇ */
	{ 0, }, /* ¸ */
	{ 0x0161, "\xc5\xa1" }, /* š */
	{ 0x015F, "\xc5\x9f" }, /* ş */
	{ 0x0165, "\xc5\xa5" }, /* ť */
	{ 0x017A, "\xc5\xba" }, /* ź */
	{ 0x02DD, "\xcb\x9d" }, /* ˝ */
	{ 0x017E, "\xc5\xbe" }, /* ž */
	{ 0x017C, "\xc5\xbc" }, /* ż */

	{ 0x0154, "\xc5\x94" }, /* Ŕ */
	{ 0, }, /* Á */
	{ 0, }, /* Â */
	{ 0x0102, "\xc4\x82" }, /* Ă */
	{ 0, }, /* Ä */
	{ 0x0139, "\xc4\xb9" }, /* Ĺ */
	{ 0, }, /* Ć */
	{ 0, }, /* Ç */
	{ 0x010C, "\xc4\x8c" }, /* Č */
	{ 0, }, /* É */
	{ 0x0118, "\xc4\x98" }, /* Ę */
	{ 0, }, /* Ë */
	{ 0x011A, "\xc4\x9a" }, /* Ě */
	{ 0, }, /* Í */
	{ 0, }, /* Î */
	{ 0x010E, "\xc4\x8e" }, /* Ď */

	{ 0x0110, "\xc4\x90" }, /* Đ */
	{ 0x0143, "\xc5\x83" }, /* Ń */
	{ 0x0147, "\xc5\x87" }, /* Ň */
	{ 0, }, /* Ó */
	{ 0, }, /* Ô */
	{ 0x0150, "\xc5\x90" }, /* Ő */
	{ 0, }, /* Ö */
	{ 0, }, /* × */
	{ 0x0158, "\xc5\x98" }, /* Ř */
	{ 0x016E, "\xc5\xae" }, /* Ů */
	{ 0, }, /* Ú */
	{ 0x0170, "\xc5\xb0" }, /* Ű */
	{ 0, }, /* Ü */
	{ 0, }, /* Ý */
	{ 0x0162, "\xc5\xa2" }, /* Ţ */
	{ 0, }, /* ß */

	{ 0x0155, "\xc5\x95" }, /* ŕ */
	{ 0, }, /* á */
	{ 0, }, /* â */
	{ 0x0103, "\xc4\x83" }, /* ă */
	{ 0, }, /* ä */
	{ 0x013A, "\xc4\xba" }, /* ĺ */
	{ 0x0107, "\xc4\x87" }, /* ć */
	{ 0, }, /* ç */
	{ 0x010D, "\xc4\x8d" }, /* č */
	{ 0, }, /* é */
	{ 0x0119, "\xc4\x99" }, /* ę */
	{ 0, }, /* ë */
	{ 0x011B, "\xc4\x9b" }, /* ě */
	{ 0, }, /* í */
	{ 0, }, /* î */
	{ 0x010F, "\xc4\x8f" }, /* ď */

	{ 0x0111, "\xc4\x91" }, /* đ */
	{ 0x0144, "\xc5\x84" }, /* ń */
	{ 0x0148, "\xc5\x88" }, /* ň */
	{ 0, }, /* ó */
	{ 0, }, /* ô */
	{ 0x0151, "\xc5\x91" }, /* ő */
	{ 0, }, /* ö */
	{ 0, }, /* ÷ */
	{ 0x0159, "\xc5\x99" }, /* ř */
	{ 0x016F, "\xc5\xaf" }, /* ů */
	{ 0, }, /* ú */
	{ 0x0171, "\xc5\xb1" }, /* ű */
	{ 0, }, /* ü */
	{ 0, }, /* ý */
	{ 0x0163, "\xc5\xa3" }, /* ţ */
	{ 0x02D9, "\xcb\x99" }, /* ˙ */
};

size_t
latin2_to_utf8_buf(size_t *outlen, const char *src, size_t slen, char *dst, size_t dlen) {
	const char *s = src;
	const char *srcend = src + slen;
	char *d = dst;
	char *dend = dst + dlen;

	for (; s < srcend; s++) {
		unsigned char ch = *s;
		if (ch >= 0xA0) {
			if (d + 2 >= dend)
				break;
			if (latin2_translit[ch - 0xA0].codepoint) {
				memcpy(d, latin2_translit[ch - 0xA0].encoding, 2);
				d += 2;
			} else {
				*d++ = 0xC2 + (ch >> 6 & 1);
				*d++ = 0x80 + (ch & 0x3F);
			}
		} else if (ch & 0x80) {
			if (d + 1 >= dend)
				break;
			*d++ = '?';
		} else {
			if (d + 1 >= dend)
				break;
			*d++ = ch;
		}
	}
	if (outlen)
		*outlen = d - dst;
	if (d < dend)
		*d = '\0';
	return s - src;
}

size_t
utf8_to_latin2_buf(const char *src, size_t slen, char *dst, size_t dlen) {
	const char *end = src + slen;
	const char *dend = dst + dlen - 1;
	char *start = dst;

	while (src < end && dst < dend) {
		int c = utf8_char(&src);
		if (c > 255) {
			unsigned int i;
			for (i = 0; i < sizeof(latin2_translit) / sizeof(latin2_translit[0]); i++) {
				if (latin2_translit[i].codepoint == c) {
					c = 0xA0 + i;
					break;
				}
			}
			if (i == sizeof(translit) / sizeof(translit[0]))
				c = '?';
		}
		*dst++ = c;
	}
	*dst = '\0';
	return dst - start;
}

char *
utf8_to_latin2_len(const char *src, size_t len, size_t *outlen) {
	char *buf = xmalloc(len + 1);
	size_t olen = utf8_to_latin2_buf(src, len, buf, len + 1);
	if (outlen)
		*outlen = olen;
	return buf;
}
