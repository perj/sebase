// Copyright 2018 Schibsted

#include "sbp/stringmap.h"

#include <assert.h>
#include <string.h>

static void
test_simple(void) {
	struct stringmap *sm = sm_new();

	sm_insert(sm, "foo", -1, "bar", -1);
	sm_insert(sm, "foo", -1, "baz", -1);
	sm_insert(sm, "nfoo", 3, "nbar", 3);
	sm_insert(sm, "nfoo", 4, "nbar", 4);

	assert(strcmp(sm_get(sm, "foo", -1, 0), "bar") == 0);
	assert(strcmp(sm_get(sm, "foo", -1, 1), "baz") == 0);
	assert(strcmp(sm_get(sm, "fooo", 3, 0), "bar") == 0);
	assert(strcmp(sm_get(sm, "nfo", 3, 0), "nba") == 0);
	assert(sm_get(sm, "nfo", 3, 1) == NULL);
	assert(strcmp(sm_get(sm, "nfoo", -1, 0), "nbar") == 0);
	assert(sm_get(sm, "bar", -1, 0) == NULL);
}

int
main(int argc, char *argv[]) {
	test_simple();
}
