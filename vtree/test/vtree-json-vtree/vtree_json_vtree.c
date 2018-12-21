// Copyright 2018 Schibsted

#include <stdio.h>
#include <string.h>

#include "sbp/bconf.h"
#include "sbp/json_vtree.h"
#include "sbp/vtree.h"
#include "sbp/buf_string.h"

static int
p(void *v, int n, int d, const char *fmt, ...) {
	struct buf_string *bs = v;
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = vbscat(bs, fmt, ap);
	va_end(ap);
	return res;
}

int
main(int argc, char **argv) {
	struct bconf_node *org = NULL;
	struct bconf_node *new = NULL;
	struct buf_string obs = {0};
	struct buf_string nbs = {0};
	struct vtree_chain vt;

	bconf_add_data(&org, "kaka.tratt1", "foo");
	bconf_add_data(&org, "kaka.tratt.1", "foo");
	bconf_add_data(&org, "kaka.tratt2", "foo");
	bconf_add_data(&org, "kaka.tratt.2.1", "foo");
	bconf_add_data(&org, "kaka.tratt.2.2", "foo");
	bconf_add_data(&org, "kaka.tratt.2.3", "foo");
	bconf_add_data(&org, "kaka.tratt.2.4", "foo");
	bconf_add_data(&org, "kaka.a\"\\(234)\t", "foo");
	bconf_add_datav(&org, 3, (const char *[]){ "notdot", "dot.dot.dot", "nodot" }, "dot.dot", BCONF_REF);

	bconf_vtree(&vt, org);
	vtree_json(&vt, 0, 0, p, &obs);
	vtree_free(&vt);
	printf("old: %s\n", obs.buf);

	json_bconf(&new, NULL, obs.buf, obs.pos, 0);

	bconf_vtree(&vt, new);
	vtree_json(&vt, 0, 0, p, &nbs);
	vtree_free(&vt);
	printf("new: %s\n", nbs.buf);

	printf("strcmp: %d\n", strcmp(obs.buf, nbs.buf));

	free(obs.buf);
	free(nbs.buf);
	bconf_free(&org);
	bconf_free(&new);

	return 0;
}
