// Copyright 2018 Schibsted

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sbp/bconfig.h"
#include "sbp/bconf.h"
#include "sbp/error_functions.h"

int
main(int argc, char **argv) {
	struct bconf_node *cfg;

	if (argc != 2)
		xerrx(1, "Usage: %s <cfg>", argv[0]);
	if ((cfg = config_init(argv[1])) == NULL)
		xerrx(1, "Error reading config");

	printf("%s\n", bconf_get_string(cfg, "a.x"));
	printf("%s\n", bconf_get_string(cfg, "a.y"));
	printf("%s\n", bconf_get_string(cfg, "a.z"));
	printf("%s\n", bconf_get_string(cfg, "include"));
	printf("%s\n", bconf_get_string(cfg, "b.v"));
	printf("%s\n", bconf_get_string(cfg, "b.w"));

	bconf_free(&cfg);

	return 0;
}
