// Copyright 2018 Schibsted

#include <pcre.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sbp/parse_query_string.h"
#include "sbp/url.h"
#include "sbp/utf8.h"

static int
match_var(const char *vregex, const char *value, int vlen, enum req_utf8 req_utf8) {
	const char *err;
	int erri;
	int res = 1;

	if (!vregex || !vregex[0])
		return 0;

	/*
	 * URLs are normally UTF-8, but you can override that with %XX characters.
	 * Therefore we only set the PCRE_UTF8 flag if requested.
	 * Note that you're at a disadvantage if you don't use it since we always have to support UTF-8,
	 * but it's very difficult to check for specific characters while doing byte matching.
	 */
	pcre *pregex = pcre_compile(vregex,
			PCRE_DOLLAR_ENDONLY | PCRE_DOTALL | PCRE_NO_AUTO_CAPTURE | (req_utf8 ? PCRE_UTF8 : 0),
			&err, &erri, NULL);

	if (!pregex) {
		syslog(LOG_ERR, "pcre_compile %s", err);
		res = 0;
	} else {
		if ((erri = pcre_exec(pregex, NULL, value, vlen, 0, PCRE_NO_UTF8_CHECK, NULL, 0)) < -1) {
			syslog(LOG_ERR, "pcre_exec: %d", erri);
			res = 0;
		} else if (erri == -1)
			res = 0;
		pcre_free(pregex);
	}
	return res;
}

/* XXX var regexes could be cached for performance. */
/* XXX we only url decode the values here, not the keys. Probably not really a problem though. */
void
parse_query_string(char *qs, void (*parse_cb)(struct parse_cb_data*, char*, int, char*, int), void *cb_data,
		   struct vtree_chain *vars, int unsafe, enum req_utf8 req_utf8, struct parse_qs_options *options) {
	char *ptr;
	char *key;
	char *value;
	char *buf = qs;
	int is_utf8;

	ptr = buf;

	while (ptr && *ptr) {
		int vlen;
		char *tmpval = NULL;
		int tlen;

		/* Split each arg in a key-value-pair */
		key = ptr;
		value = NULL;
		vlen = 0;

		while (*ptr && *ptr != '=' && *ptr != '&')
			ptr++;

		if (*ptr == '=') {
			*ptr++ = '\0';
			value = ptr;

			ptr = url_decode(ptr, -1, "&", unsafe, &is_utf8);

			vlen = ptr - value;
			if (*ptr == '&')
				*ptr++ = '\0';
			if (!is_utf8) {
				switch (req_utf8) {
				case RUTF8_NOCHECK:
					break;
				case RUTF8_REQUIRE:
					value = NULL;
					break;
				case RUTF8_FALLBACK_LATIN1:
					tmpval = latin1_to_utf8(value, vlen, &tlen);
					value = tmpval;
					vlen = tlen;
					break;
				}
			}
		} else if (*ptr == '&') {
			*ptr++ = '\0';
		}

		parse_query_string_value(key, value, vlen, parse_cb, cb_data, vars, req_utf8, options);

		free(tmpval);
	}
}

void
parse_query_string_value(const char *key, char *value, int vlen, void (*parse_cb)(struct parse_cb_data*, char*, int, char*, int),
		void *cb_data, struct vtree_chain *vars, int req_utf8, struct parse_qs_options *options)
{
	struct parse_cb_data pqcb_data = { .cb_data = cb_data, .options = options };

	if (value && vars) {
		int i;
		int value_ok = 0;
		struct vtree_loop_var loop_keys;
		struct vtree_loop_var loop_values;

		if (options) {
			const char *esc_html = vtree_get(vars, key, "escape_html", NULL);

			if (esc_html && *esc_html) {
				options->escape_html = atoi(esc_html) == 0 ? TRI_FALSE : TRI_TRUE;
			} else {
				options->escape_html = TRI_UNDEF; /* template/type will override */
			}
		}

		/* Verify value against a list of strings */
		if (vtree_haskey(vars, key, "allow", NULL)) {

			vtree_fetch_keys(vars, &loop_keys, key, "allow", NULL);
			vtree_fetch_values(vars, &loop_values, key, "allow", VTREE_LOOP, NULL);

			for (i = 0 ; i < loop_keys.len ; ++i) {
				if (strcmp(value, loop_values.l.list[i]) == 0) {
					value_ok = 1;
					break;
				}
			}

			if (loop_keys.cleanup)
				loop_keys.cleanup(&loop_keys);
			if (loop_values.cleanup)
				loop_values.cleanup(&loop_values);
		}

		/* Verify value against a regular expression */
		if (!value_ok) {
			const char *vregex = vtree_get(vars, key, "regex", NULL);
			if (vregex && match_var(vregex, value, vlen, req_utf8)) {
				value_ok = 1;
				vtree_fetch_keys(vars, &loop_keys, key, "match", NULL);
				vtree_fetch_values(vars, &loop_values, key, "match", VTREE_LOOP, "regex", NULL);

				for (i = 0 ; i < loop_keys.len ; ++i) {
					vregex = loop_values.l.list[i];

					if (match_var (vregex, value, vlen, req_utf8)) {
						char one[] = "1";
						char *k = xstrdup(loop_keys.l.list[i]);

						parse_cb(&pqcb_data, k, strlen(loop_keys.l.list[i]), one, 1);
						free(k);
					}
				}

				if (loop_keys.cleanup)
					loop_keys.cleanup(&loop_keys);
				if (loop_values.cleanup)
					loop_values.cleanup(&loop_values);
			}
		}

		if (!value_ok)
			value = NULL;
	}
	if (value) {
		parse_cb(&pqcb_data, (char *)key, strlen(key), value, vlen);
	}
}
