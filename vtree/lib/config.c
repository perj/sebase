// Copyright 2018 Schibsted

#include <ctype.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bconfig.h"
#include "bconf.h"
#include "sbp/memalloc_functions.h"
#include "sbp/error_functions.h"

static int
config_init_file(const char *filename, struct bconf_node **rootp, bool allow_env) {
	FILE *fp;
	char buf[1024];
	char *ptr;
	char *key;
	char *value;
	int i;

	if ((fp = fopen(filename, "r")) == NULL) {
		return -1;
	}

	/* Parse the file  */
	while (fgets(buf, sizeof(buf), fp)) {
		/* Check for newline or comment start characters */
		if ((ptr = strpbrk(buf, "\n")))
			*ptr = '\0';

		/* Require whitespace after 'include' to treat as directive */
		if ((strncmp(buf, "include", 7) == 0) && ((i = strspn(buf + 7, " \t")) > 0)) {
			char *n = buf + sizeof("include") - 1 + i;
			char *end = ptr ?: n + strlen(n);
			if (allow_env && strncmp(n, "$ENV{", 5) == 0 && end > n && *(end - 1) == '}') {
				*(end - 1) = '\0';
				n = getenv(n + 5);
				if (!n)
					continue;
			}
			if (n[0] == '/') {
				config_init_file(n, rootp, allow_env);
			} else {
				char *dir = xstrdup(filename);
				int res UNUSED;

				xasprintf(&n, "%s/%s", dirname(dir), n);
				res = config_init_file(n, rootp, allow_env);

				free(dir);
				free(n);
			}
			continue;
		}

		/* Check for separator and split into key and value */
		if ( (ptr = strchr(buf, '=')) == NULL)
			continue;

		*ptr = '\0';

		key = buf;
		value = ptr + 1;

		while (isspace(*key))
			key++;
		/* We're actually a comment, ignore */
		if (*key == '#')
			continue;
		while (ptr > key && isspace(*(ptr - 1)))
			ptr--;
		*ptr = '\0';
		while (isspace(*value))
			value++;
		ptr = value + strlen(value);
		while (ptr > value && isspace(*(ptr - 1)))
			ptr--;
		*ptr-- = '\0';

		if (allow_env && strncmp(value, "$ENV{", 5) == 0 && ptr >= value && *ptr == '}') {
			*ptr = '\0';
			value = getenv(value + 5);
		}

		if (value)
			bconf_add_data(rootp, key, value);
	}

	fclose(fp);

	return 0;
}

struct bconf_node *
config_init(const char *filename) {
	struct bconf_node *config_root = NULL;

	if (config_init_file(filename, &config_root, true) != 0) {
		bconf_free(&config_root);
		return NULL;
	}

	return config_root;
}

int
load_bconf_file(const char *appl, struct bconf_node **root, const char *filename) {
	const char *host = NULL;

	if (bconf_get(*root, "blocket_id"))
		host = bconf_get_string(*root, "blocket_id");

	struct bconf_node *tmproot = NULL;
	if (config_init_file(filename, &tmproot, false) != 0) {
		bconf_free(&tmproot);
		return -1;
	}

	int r = config_merge_bconf(root, tmproot, host, appl);

	bconf_free(&tmproot);
	return r;
}

int
config_merge_bconf(struct bconf_node **root, struct bconf_node *bconf, const char *host, const char *appl) {
	struct bconf_node *starstar, *starappl, *hoststar, *hostappl;
	starstar = bconf_vget(bconf, "*", "*", NULL);
	starappl = appl ? bconf_vget(bconf, "*", appl, NULL) : NULL;
	hoststar = host ? bconf_vget(bconf, host, "*", NULL) : NULL;
	hostappl = (appl && host) ? bconf_vget(bconf, host, appl, NULL) : NULL;

	if (starstar)
		bconf_merge(root, starstar);

	if (starappl && starappl != starstar)
		bconf_merge(root, starappl);
	if (hoststar && hoststar != starstar && hoststar != starappl)
		bconf_merge(root, hoststar);
	if (hostappl && hostappl != starstar && hostappl != starappl && hostappl != hoststar)
		bconf_merge(root, hostappl);

	return 0;
}
