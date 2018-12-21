// Copyright 2018 Schibsted

#include "sbp/memalloc_functions.h"
#include "sbp/vtree_literal.h"

#include <assert.h>

static struct vtree_keyvals static_kvs = {
	.type = vktDict,
	.len = 3,
	.list = (struct vtree_keyvals_elem[]){
		/* Unsorted */
		{ .key = "foo", .type = vkvValue, .v.value = "1" },
		{ .key = "bar", .type = vkvNone },
		{ .key = "baz", .type = vkvValue, .v.value = "2" },
	},
};
static struct vtree_chain static_vtree = {
	.fun = &vtree_literal_vtree,
	.data = &static_kvs,
};

static void
test_static_literal(void) {
	assert(vtree_getint(&static_vtree, "foo", NULL) == 1);
	assert(vtree_getint(&static_vtree, "bar", NULL) == 0);
	assert(vtree_getint(&static_vtree, "baz", NULL) == 2);
}

static void
test_stack_literal(void) {
	/* Note: For static only data, a global variable is typically better, as it's
	 * statically initialized, instead of dynamically on the stack as here. */
	struct vtree_keyvals stack_kvs = {
		.type = vktDict,
		.len = 3,
		.list = (struct vtree_keyvals_elem[]){
			/* Unsorted */
			{ .key = "foo", .type = vkvValue, .v.value = "3" },
			{ .key = "bar", .type = vkvNone },
			{ .key = "baz", .type = vkvNode, .v.node = static_vtree },
		},
	};
	struct vtree_chain stack_vtree = {
		.fun = &vtree_literal_vtree,
		.data = &stack_kvs,
	};

	assert(vtree_getint(&stack_vtree, "foo", NULL) == 3);
	assert(vtree_getint(&stack_vtree, "bar", NULL) == 0);
	assert(vtree_getint(&stack_vtree, "baz", "foo", NULL) == 1);
	assert(vtree_getint(&stack_vtree, "baz", "baz", NULL) == 2);

	vtree_free(&stack_vtree);
}

static void
test_dynamic_literal(void) {
	struct vtree_keyvals *dyn_kvs = vtree_keyvals_create_list(3);
	/* Since the node is static we shouldn't call vtree_free on it. */
	dyn_kvs->cleanup = vtree_keyvals_free_values_only;

	dyn_kvs->list[0].type = vkvValue;
	dyn_kvs->list[0].v.value = xstrdup("3");
	dyn_kvs->list[1].type = vkvNone;
	dyn_kvs->list[2].type = vkvNode;
	dyn_kvs->list[2].v.node = static_vtree;
	struct vtree_chain *dyn_vtree = vtree_literal_create(dyn_kvs);

	assert(vtree_getint(dyn_vtree, "0", NULL) == 3);
	assert(vtree_getint(dyn_vtree, "1", NULL) == 0);
	assert(vtree_getint(dyn_vtree, "2", "foo", NULL) == 1);
	assert(vtree_getint(dyn_vtree, "2", "baz", NULL) == 2);

	vtree_free(dyn_vtree);
	free(dyn_vtree);
}

int
main(int argc, char *argv[]) {
	test_static_literal();
	test_stack_literal();
	test_dynamic_literal();
}
