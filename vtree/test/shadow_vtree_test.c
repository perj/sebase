// Copyright 2018 Schibsted

#include "sbp/bconf.h"
#include "sbp/vtree.h"

#include <assert.h>
#include <stdbool.h>

static struct vtree_dispatch bconf_api_check_free;

static int free_called;
static void
check_free(struct vtree_chain *vchain) {
	free_called++;
}

static void
setup(void) {
	struct vtree_chain tmp;
	bconf_vtree_own(&tmp, NULL);
	/* Copy out the function pointers. */
	bconf_api_check_free = *tmp.fun;
	bconf_api_check_free.free = check_free;
	vtree_free(&tmp);
}

static void
test_strong_weak(bool weak) {
	struct vtree_chain defvalues;
	struct shadow_vtree shadow;

	struct bconf_node *defroot = NULL, *shroot = NULL;

	bconf_add_data(&defroot, "a.b", "1");
	bconf_add_data(&defroot, "c", "2");

	bconf_add_data(&shroot, "a.b", "3");
	bconf_add_data(&shroot, "c.d", "4");

	bconf_vtree(&defvalues, defroot);
	bconf_vtree(&shadow.vtree, shroot);
	shadow.vtree.fun = &bconf_api_check_free;
	shadow.free_cb = NULL;

	struct vtree_chain combined;
	shadow_vtree_init(&combined, &shadow, &defvalues);
	if (weak)
		combined.fun = &shadow_vtree_weakref;

	assert(vtree_getint(&combined, "a", "b", NULL) == 3);
	assert(vtree_getint(&combined, "c", "d", NULL) == 4);

	free_called = 0;
	vtree_free(&combined);
	vtree_free(&defvalues);
	assert(free_called == weak ? 0 : 1);
	bconf_free(&defroot);
	bconf_free(&shroot);
}

int
main(int argc, char *argv[]) {
	setup();
	test_strong_weak(false);
	test_strong_weak(true);
}
