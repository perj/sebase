// Copyright 2018 Schibsted

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sbp/bconf.h"


struct bconf_node *root;

static int
foreach_example(const char *path, size_t plen, struct bconf_node *node, void *cbdata) {

	if (strlen(path) != plen) {
		printf("ERROR: bconf_foreach callback path length incorrect: %zu != %zu", strlen(path), plen);
		return 1;
	}

	if (strlen(bconf_value(node)) != bconf_vlen(node)) {
		printf("ERROR: bconf_vlen %s(%zu)\n", bconf_value(node), bconf_vlen(node));
		return 1;
	}

	return 0;
}

static int
pf(void *a, int d, const char *fmt, ...) {
	int res;
	va_list ap;

	while (d--) {
		putchar('\t');
	}


	va_start(ap, fmt);
	res = vprintf(fmt, ap);
	va_end(ap);
	return res;
}

int
main(void) {
	int count;
	struct bconf_node *b;
	struct bconf_node *n;
	int i;

	/* Test initialization of the API */

	/*
	if (bconf_init(&b, "10.0.0.0", "test")) {
		fprintf(stderr, "bconf_init failed\n");
		exit(1);
	}
	*/

	bconf_add_data(&root, "host.fnargel.category.7.price", "20");

	if (bconf_vget_int(root, "host", "fnargel", "category", "7", "price", NULL) != 20) {
		fprintf(stderr, "bconf_vget_int() call failed.\n");
		exit(1);
	}

	if (bconf_vget_int_default(root, "does", "not", "exist", NULL, 1234) != 1234) {
		fprintf(stderr, "bconf_vget_int_default() default-value call failed.\n");
		exit(1);
	}

	if (bconf_vget_int_default(root, "host", "fnargel", "category", "7", "price", NULL, 1234) != 20) {
		fprintf(stderr, "bconf_vget_int_default() call failed.\n");
		exit(1);
	}

	/* Should not crash when supplied a non-leaf node */
	if (!(bconf_get_int(root, "host.fnargel") == 0 && bconf_vget_int(root, "host", "fnargel", NULL) == 0)) {
		fprintf(stderr, "Check that we shouldn't crash int-getters on non-leaf-node failed.\n");
		exit(1);
	}

	bconf_add_data(&root, "host.common.category.7.price", "20");
	bconf_add_data(&root, "host.common.category.8.price", "20");
	bconf_add_data(&root, "host.common.category.8.test", "apa");
	bconf_add_data(&root, "host.common.category.9.price", "20");

	bconf_add_data(&root, "*.common.category.7.price", "20");
	bconf_add_data(&root, "*.common.category.8.price", "20");
	bconf_add_data(&root, "*.common.category.8.test", "apa");
	bconf_add_data(&root, "*.common.category.9.price", "20");

	bconf_add_data(&root, "*.common.seo.cat.number.Дома", "3");
	bconf_add_data(&root, "*.common.seo.cat.number.Домашні_тварини", "4030");

	b = bconf_vget(root, "*", "common", "seo", "cat", "number", "Дома", NULL);
	if (b) {
		/* Regression: Test for issue #347 found on shotgun, error in keycomp-function on utf-8 strings */
		if (strcmp("3", bconf_value(b)) != 0) {
			fprintf(stderr, "bconf_value incorrect for utf-8 prefix node\n");
			exit(1);
		}
	} else {
		fprintf(stderr, "bconf_vget failed on node with utf-8\n");
		exit(1);
	}

	b = bconf_get(root, "host1.common.category");

	if (!b) {
		fprintf(stderr, "bconf_get failed\n");
		exit(1);
	}

	if (bconf_count(b) != 3) {
		fprintf(stderr, "bconf_count failed\n");
		exit(1);
	}

	if (strcmp(bconf_value(bconf_get(b, "7.price")), "20") != 0) {
		fprintf(stderr, "bconf_value failed\n");
		exit(1);
	}


	b = bconf_vget(root, "host.common.category", "7", NULL);
	if (!b) {
		fprintf(stderr, "bconf_byname failed\n");
		exit(1);
	}

	if (strcmp(bconf_value(bconf_get(b, "price")), "20") != 0) {
		fprintf(stderr, "bconf_value failed\n");
		exit(1);
	}

	struct bconf_node *cnode = bconf_get(root, "host.common.category");
	b = bconf_byindex(cnode, 0);

	if (!b) {
		fprintf(stderr, "bconf_byindex failed\n");
		exit(1);
	}

	for (count = 0 ; count < 3 ; count++) {
		b = bconf_byindex(cnode, count);

		if (!b) {
			fprintf(stderr, "bconf_byindex failed\n");
			exit(1);
		}

		if (strcmp(bconf_value(bconf_get(b, "price")), "20") != 0) {
			fprintf(stderr, "bconf_value failed\n");
			exit(1);
		}
		if (bconf_klen(b) != strlen(bconf_key(b))) {
			fprintf(stderr, "bconf_klen failed\n");
			exit(1);
		}
		if (bconf_vlen(b) != strlen(bconf_value(b) ?: "")) {
			fprintf(stderr, "bconf_vlen failed\n");
			exit(1);
		}

		if (count == 1 && !bconf_get(b, "test")) {
			fprintf(stderr, "bconf_value failed\n");
			exit(1);
		}
	}

	bconf_add_data(&root, "merge.merge.a", "1");
	bconf_add_data(&root, "merge.merge.b", "2");
	bconf_add_data(&root, "merge.merge.sub.a", "1");
	bconf_add_data(&root, "merge.merge.sub.b", "2");
	bconf_add_data(&root, "merge.merge.sub.c", "3");
	bconf_add_data(&root, "*.merge.b", "4");
	bconf_add_data(&root, "*.merge.c", "5");
	bconf_add_data(&root, "*.merge.sub.b", "4");
	bconf_add_data(&root, "*.merge.sub.c", "5");
	bconf_add_data(&root, "*.merge.sub.d", "6");
	bconf_add_data(&root, "weird_characters_in_value", "Test JSON escaping \n\r\t\"\\");
	bconf_add_data(&root, "utf8_value", "åäö");

	n = NULL;
	bconf_merge(&n, bconf_get(root, "*.merge"));
	if (!n) {
		fprintf(stderr, "bconf_merge(*.merge) failed\n");
		exit(1);
	}
	bconf_merge(&n, bconf_get(root, "merge.merge"));

	for (i = 0, b = n; b; b = bconf_get(b, "sub"), i++) {
		count = bconf_count(b);
		if (count != 4) {
			fprintf(stderr, "bconf_merge() wrong size, %d != 4\n", count);
			exit(1);
		}

		count = bconf_get_int(n, "b");
		if (count != 2) {
			fprintf(stderr, "bconf_merge() override failed. %d != 2\n", count);
			exit(1);
		}
	}
	if (i != 2) {
		fprintf(stderr, "bconf_merge() wrong loop count, %d != 2\n", i);
		exit(1);
	}

	bconf_json(root, 0, pf, NULL);
	printf("\nhost.common.category.8.test as JSON: ");
	bconf_json(bconf_get(root, "host.common.category.8.test"), 0, pf, NULL);
	putchar('\n');

	bconf_foreach(root, 64, foreach_example, NULL);

	bconf_free(&n);

	return 0;
}
