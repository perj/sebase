// Copyright 2018 Schibsted

#include <string.h>
#include <unicode/ucnv.h>
#include <unicode/unorm.h>
#include <unicode/ucnv_cb.h>
#include <unicode/utf8.h>
#include <stdlib.h>

#include "unicode.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"

static void
utf8_decode_error_cb(const void *ctx, UConverterToUnicodeArgs *args, const char *badptr, int32_t badlen,
		UConverterCallbackReason reason, UErrorCode *err) {
	UChar subst[badlen < 1 ? 1 : badlen];
	UConverter *dst_cnv = (UConverter *)ctx;

	switch (reason) {
	case UCNV_UNASSIGNED:
	case UCNV_ILLEGAL:
	case UCNV_IRREGULAR:
		/* Use the destination charset */
		/* This won't fail it seems, we'll just get a \uFFFD char on bad values */
		*err = U_ZERO_ERROR;
		int l = ucnv_toUChars(dst_cnv, subst, badlen, badptr, badlen, err);
		if (!U_FAILURE(*err)) {
			for (int i = 0 ; i < l ; i++) {
				if (subst[i] == 0xFFFD)
					subst[i] = '?';
			}
			*err = U_ZERO_ERROR;
			ucnv_cbToUWriteUChars(args, subst, l, 0, err);
		}
		break;

	case UCNV_RESET:
	case UCNV_CLOSE:
	case UCNV_CLONE:
		break;
	}
}

char *
utf8_decode(char *dst, int *dstlen, const char *str, int slen, UConverter *utf8_cnv, UConverter *dst_cnv) {
	int srclen = slen >= 0 ? slen : (int)strlen(str);
	UChar *bufa;
	UChar *bufb;
	int l;
	UErrorCode err = 0;
	UConverterToUCallback old_tou_cb;
	const void *old_tou_ctx;

	ucnv_reset(utf8_cnv);
	ucnv_reset(dst_cnv);

	if (srclen < 1024) {
		bufa = alloca((srclen + 1) * sizeof (*bufa));
		bufb = alloca((srclen + 1) * sizeof (*bufb));
	} else {
		bufa = xmalloc((srclen + 1) * sizeof (*bufa));
		bufb = xmalloc((srclen + 1) * sizeof (*bufb));
	}

	ucnv_setToUCallBack(utf8_cnv, utf8_decode_error_cb, dst_cnv, &old_tou_cb, &old_tou_ctx, &err);
	ucnv_setSubstChars(dst_cnv, "?", 1, &err);

	l = ucnv_toUChars(utf8_cnv, bufa, srclen + 1, str, srclen, &err);
	if (U_FAILURE(err))
		xerrx(1, "utf8_decode(%.*s): error from ucnv_toUChars: %s", srclen, str, u_errorName(err));

	const UNormalizer2 *norm = unorm2_getNFCInstance(&err);
	l = unorm2_normalize(norm, bufa, l, bufb, srclen + 1, &err);
	if (U_FAILURE(err))
		xerrx(1, "utf8_decode(%.*s): error from unorm_normalize: %s", srclen, str, u_errorName(err));

	l = ucnv_fromUChars(dst_cnv, dst, *dstlen, bufb, l, &err);
	if (U_FAILURE(err))
		xerrx(1, "utf8_decode(%.*s): error from ucnv_fromUChars: %s", srclen, str, u_errorName(err));
	if (err == U_STRING_NOT_TERMINATED_WARNING)
		dst[--l] = '\0';

	if (srclen >= 1024) {
		free(bufa);
		free(bufb);
	}

	*dstlen = l;
	return dst;
}


size_t strlen_utf8(const char *str) {
	size_t n = 0, i;
	size_t ln = strlen(str);
	for (i = 0 ; i < ln ; (U8_FWD_1(str, i, ln)) ) {
		n++;
	}
	return n;
}
