// Copyright 2018 Schibsted

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sbp/parse_query_string.h"
#include "sbp/buf_string.h"

static void
bs_cb(struct parse_cb_data *cb_data, char *key, int klen, char *value, int vlen) {
	struct buf_string *bs = cb_data->cb_data;
	bscat(bs, "%.*s -> %.*s;", klen, key, vlen, value);
}

struct {
	const char *qs;
	const char *expect;
	enum req_utf8 rutf8;
	int use_vtree;
} tests[] = {
	{
		.qs = "a=b&b=c",
		.expect = "a -> b;b -> c;",
	},
	{
		.qs = "q=räksmörgås",
		.expect = "q -> räksmörgås;",
	       	.rutf8 = RUTF8_REQUIRE,
	},
	{
		.qs = "q=r\xE4ksm\xF6rg\xE5s",
	       	.rutf8 = RUTF8_REQUIRE,
	},
	{
		.qs = "q=r\xE4ksm\xF6rg\xE5s",
		.expect = "q -> räksmörgås;",
	       	.rutf8 = RUTF8_FALLBACK_LATIN1,
	},
	{
		.qs = "q=r\xE4ksm\xF6rg\xE5s",
		.expect = "q -> r\xE4ksm\xF6rg\xE5s;",
	       	.rutf8 = RUTF8_NOCHECK,
	},
	{
		.qs = "a=b&b=%3F",
		.expect = "a -> b;b -> ?;",
	},
	{
		.qs = "ta=b",
		.expect = "ta -> b;",
		.use_vtree = 1,
	},
	{
		.qs = "ta=asdf",
		.use_vtree = 1,
	},
	{
		.qs = "tr=abc123",
		.use_vtree = 1,
	},
	{
		.qs = "tr=abc",
		.expect = "tr -> abc;",
		.use_vtree = 1,
	},
	{
		.qs = "tm=abc",
		.expect = "tm -> abc;",
		.use_vtree = 1,
	},
	{
		.qs = "tm=abc123",
		.expect = "tmatched -> 1;tm -> abc123;",
		.use_vtree = 1,
	},
	{
		.qs = "tm=abc123x",
		.expect = "tm -> abc123x;",
		.use_vtree = 1,
	},
};

int
main(int argc, char **argv) {
	struct bconf_node *root = NULL;
	struct vtree_chain vt;
	unsigned int i;
	int ret = 0;

	/* Testing allow. */
	bconf_add_data(&root, "ta.allow.1", "a");
	bconf_add_data(&root, "ta.allow.2", "b");

	/* Testing regexp */
	bconf_add_data(&root, "tr.regex", "^[a-z]+$");

	/* Testing match */
	bconf_add_data(&root, "tm.regex", "^[a-z0-9]+$");
	bconf_add_data(&root, "tm.match.tmatched.regex", "[0-9]+$");

	bconf_vtree(&vt, root);

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		char *qs = strdup(tests[i].qs);
		struct buf_string bs = { 0 };

		parse_query_string(qs, bs_cb, &bs, tests[i].use_vtree ? &vt : NULL, 0, tests[i].rutf8, NULL);

		if ((!tests[i].expect && bs.buf) || (tests[i].expect && (!bs.buf || strcmp(bs.buf, tests[i].expect) != 0))) {
			fprintf(stderr, "test %u failed: ret[%s] != exp[%s]\n", i, bs.buf, tests[i].expect);
			ret = 1;
		}

		free(bs.buf);
		free(qs);
	}

	vtree_free(&vt);
	bconf_free(&root);

	return ret;
}
