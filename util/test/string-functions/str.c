// Copyright 2018 Schibsted

/*
	Tests for string functions.

*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include "sbp/string_functions.h"

const char *res;

static int
test_m(void) {
	const char *haystack = "2006:1,2006:2,2006:3,2006:4";
	const char *needle = "2006:3";
	if ((res = strstrptrs(haystack, needle, needle + strlen(needle), NULL)) != NULL) {
		if (res != haystack + 14)
			return 1;
		return 0;
	}
	return 1;
}

static int
test_n(void) {
	const char *haystack = "2006:1,2006:2,2006:3,2006:4";
	const char *needle = "2006:6";

	if ((res = strstrptrs(haystack, needle + 2, needle + strlen(needle), NULL)) == NULL)
		return 0;
	fprintf(stderr, "Non-match failed (%s)\n", res);
	return 1;
}

static int
test_o(void) {
	const char *haystack = "2006:1,2006:2,2006:3,2006:4";
	const char *needle = "2006";

	if ((res = strstrptrs(haystack, needle, needle + strlen(needle), ",")) == NULL)
		return 0;
	fprintf(stderr, "Non-match failed (%s)\n", res);
	return 1;
}

static int
test_p(void) {
	const char *haystack = "2006:1,2006:2,2006:3,2006:4";
	const char *needle = "2006";

	if ((res = strstrptrs(haystack, needle, needle + strlen(needle), ":")) != NULL)
		return 0;
	fprintf(stderr, "Non-match failed (%s)\n", res);
	return 1;
}

static int
test_q(void) {
	const char *haystack = "2006:6,2006:7,2006:8,2006:9,2006:10,2006:11,2006:12,2006:13,2006:14,2006:15,2006:16,2006:17,2006:18,2006:19,2006:20,2006:21,2006:22,2006:23,2006:24,2006:25,2006:26,2006:27,2006:28,2006:29,2006:30,2006:31,2006:32,2006:33,2006:34,2006:35,2006:36,2006:37,2006:38,2006:39,2006:40,2006:41,2006:42,2006:43,2006:44,2006:45,2006:46,2006:47,2006:48,2006:49,2006:50,2006:51,2006:52,2007:1,2007:2,2007:3,2007:4";
	const char *needle = "2006:5";

	if ((res = strstrptrs(haystack, needle, needle + strlen(needle), ",")) == NULL)
		return 0;
	fprintf(stderr, "Non-match failed (%s)\n", res);
	return 1;
}

static int
test_e(void) {
	const char *haystack = "2006:1,2006:2,2006:3,2006:4";
	const char *needle = ":4";

	if ((res = strstrptrs(haystack, needle, needle + strlen(needle), ",")) == NULL)
		return 0;
	fprintf(stderr, "Non-match failed (%s)\n", res);
	return 1;
}

static int
test_r(void) {
	const char *haystack = "2006:1,2006:2,2006:3,2006:4";
	char *r;
	r = remove_subset(haystack, ":", 0);
	if (strcmp(r, "20061,20062,20063,20064")) {
		fprintf(stderr, "remove_subset failed (%s)\n", r);
		return 1;
	}
	free(r);
	r = remove_subset(haystack, "0123456789", 1);
	if (strcmp(r, "20061200622006320064")) {
		fprintf(stderr, "remove_subset invert failed (%s)\n", r);
		return 1;
	}
	free(r);
	return 0;
}

static int
test_s(void) {
	const char *s = "some \"c:\\file\\path.txt\"";
	char *r = escape_dquotes(s);
	if (strcmp(r, "some \\\"c:\\\\file\\\\path.txt\\\"")) {
		fprintf(stderr, "escape_dquotes failed (%s)\n", r);
		return 1;
	}
	free(r);
	return 0;
}


static int
test_t(void) {
	const char *s = "a world without reason";
	char *r = str_replace(s, "world", "world in flames");
	if (strcmp(r, "a world in flames without reason")) {
		fprintf(stderr, "str_replace failed (%s)\n", r);
		return 1;
	}
	free(r);
	return 0;
}

static int
test_u(void) {
	char buf[127];
	xstrsignal(SIGPIPE, buf, sizeof(buf));
	if (strcmp(buf, "Broken pipe")) {
		fprintf(stderr, "xstrsignal failed (%s)\n", buf);
		return 1;
	}
	return 0;
}

static struct suite {
	const char *name;
	int (*fun)(void);
	const char *desc;
} tests[] = {
	{ "m", test_m, "strstrptrs" },
	{ "n", test_n, "strstrptrs" },
	{ "o", test_o, "strstrptrs" },
	{ "p", test_p, "strstrptrs" },
	{ "q", test_q, "strstrptrs" },
	{ "e", test_e, "strstrptrs" },
	{ "r", test_r, "remove_subset" },
	{ "s", test_s, "escape_dquotes" },
	{ "t", test_t, "str_replace" },
	{ "u", test_u, "xstrsignal" },
	{ NULL, NULL }
};

static void
list_tests(struct suite *s) {
	while (s && s->name) {
		fprintf(stderr, "\t%s\t\t - %s\n", s->name, s->desc ?: "<no description available>");
		++s;
	}
}

static void
usage_exit(char *argv0) {
	fprintf(stderr, "Usage: %s [-a|--all] <test1> <testN> ...\n", argv0);
	fprintf(stderr, "Available tests:\n");
	list_tests(tests);
	exit(2);
}

int
main(int argc, char **argv) {
	int argTest = 1;
	int failed = 0;
	int all = 0;

	if (argc < 2)
		usage_exit(argv[0]);

	if (strcmp(argv[argTest], "--all") == 0 || strcmp(argv[argTest], "-a") == 0) {
		all = 1;
	}

	for ( ; argTest < argc || all ; ++argTest) {
		int run = 0;
		struct suite *s = tests;
		while (s && s->name) {
			if (all || strcmp(argv[argTest], s->name) == 0) {
				if (s->fun()) {
					fprintf(stderr, "FAILED: %s\n", s->name);
					++failed;
				}
				run = 1;
				if (!all)
					break;
			}
			++s;
		}
		if (!run) {
			fprintf(stderr, "Warning: No such test '%s'\n", argv[argTest]);
			++failed;
		}
		if (all)
			break;
	}

	return failed;
}
