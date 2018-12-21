// Copyright 2018 Schibsted

#include <stdio.h>
#include <stdlib.h>

#include "sbp/avl.h"

struct node {
	struct avl_node tree;
	int key;
};

static int
cmp(const struct avl_node *an, const struct avl_node *bn) {
	const struct node *a = avl_data(an, const struct node, tree);
	const struct node *b = avl_data(bn, const struct node, tree);

	return a->key - b->key;
}

/* Recursive, may not be suitable for all tree sizes */
static void avl_free(struct avl_node *n) {
	if (!n)
		return;
	avl_free(n->link[0]);
	avl_free(n->link[1]);
	struct node *e = avl_data(n, struct node, tree);
	free(e);
}

int
main(int argc, char **argv) {
	struct avl_node *root = NULL;
	int nrounds;
	int ins, del;
	int i;

	if (argc > 1)
		nrounds = atoi(argv[1]);
	else
		nrounds = 10000;

	ins = del = 0;
	for (i = 0; i < nrounds; i++) {
		struct node s;
		struct avl_node *n;

		s.key = rand() % (nrounds / 2);		/* jajaja */
		if ((n = avl_lookup(&s.tree, &root, cmp)) == NULL) {
			struct node *d;
			d = malloc(sizeof(*d));
			d->key = s.key;
			avl_insert(&d->tree, &root, cmp);
			ins++;
		} else {
			avl_delete(n, &root, cmp);
			del++;
			free(n);
		}
		avl_check(root, cmp);
		if (i % (nrounds / 10) == (nrounds / 10) - 1)
			printf("inserts: %d, deletes: %d\n", ins, del);
	}

	avl_free(root);

	return 0;
}
