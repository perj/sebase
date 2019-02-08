// Copyright 2018 Schibsted

/*
	Reads one or more bconf-style configuration using config_init() and
	a list of patterns. Matched bconf paths and values are output in a
	format suitable to be evaluated by shell scripts.

	$ eval `getbconfvars -f trans.conf --prefix trans_ -k control_port -k max_connections`
	$ echo $trans_control_port
	20207

	Any periods in the resulting key/path will be replaced with underscores.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sbp/bconfig.h"
#include "sbp/bconf.h"
#include "sbp/memalloc_functions.h"
#include "sbp/string_functions.h"
#include "sbp/cached_regex.h"
#include "sbp/popt.h"

struct match_rec {
	STAILQ_ENTRY(match_rec) next;
	char *prefix;
	char *pattern; /* kept around just in case */
	struct cached_regex re;
};

struct path_rec {
	STAILQ_ENTRY(path_rec) next;
	char *path; /* conf node path */
	STAILQ_HEAD(,match_rec) match_list;
};

struct config_rec {
	STAILQ_ENTRY(config_rec) next;
	char *filename;
	STAILQ_HEAD(,path_rec) path_list;
};

static const int MAX_BCONF_PATH_DEPTH = 10;

STAILQ_HEAD(,config_rec) config_list = STAILQ_HEAD_INITIALIZER(config_list);


static struct config_rec *
new_config(const char *filename) {

	struct config_rec *cr = zmalloc(sizeof(struct config_rec));
	STAILQ_INIT(&cr->path_list);
	cr->filename = xstrdup(filename);
	return cr;
}

static void
free_config(struct config_rec *cr) {
	free(cr->filename);
	free(cr);
}

static struct path_rec *
new_path(const char *path) {

	struct path_rec *pr = zmalloc(sizeof(struct path_rec));
	STAILQ_INIT(&pr->match_list);
	if (path)
		pr->path = xstrdup(path);
	return pr;
}

static void
free_path(struct path_rec *pr) {
	free(pr->path);
	free(pr);
}

static struct match_rec *
new_match(const char *prefix, const char *pattern) {

	struct match_rec *mr = zmalloc(sizeof(struct match_rec));
	if (prefix)
		mr->prefix = xstrdup(prefix);
	if (pattern)
		mr->pattern = xstrdup(pattern);
	memset(&mr->re, 0, sizeof(struct cached_regex));
	mr->re.regex = mr->pattern;
	return mr;
}

static void
free_match(struct match_rec *mr) {
	cached_regex_cleanup(&mr->re);
	free(mr->prefix);
	free(mr->pattern);
	free(mr);
}

POPT_USAGE("--file|-f <name> [--root|-r bconf.path] [--prefix|-p string] --key|-k <regex>");
POPT_PURPOSE("Export subset of blocket config to environment variables.");
POPT_STRING_INT("file", NULL, 'f', "Input file, blocket config/bconf style.");
POPT_STRING_INT("root", NULL, 'r', "Bconf path to process (optional).");
POPT_STRING_INT("prefix", NULL, 'p', "Prefix output (optional)");
POPT_STRING_INT("key", NULL, 'k', "Regex pattern to match in config path.");
POPT_DESCRIPTION("\nArgument order matters, and arguments can be given more than once.\n"
		"\nExample:\n"
		" $ eval `getbconfvars --file trans.conf -p var_ --key control_port`\n"
		" $ echo $var_control_port\n"
		" $ 20207");

static int
match_cb(const char *path, size_t plen, struct bconf_node *node, void *cbdata) {
	struct path_rec *pr = cbdata;

	struct match_rec *match;
	STAILQ_FOREACH(match, &pr->match_list, next) {
		if (cached_regex_match(&match->re, path, NULL, 0)) {
			char *escaped_path = strtrchr(path, ".", '_');
			printf("%s%s=%s\n", match->prefix ?: "", escaped_path, bconf_value(node));
			free(escaped_path);
		}
	}
	return 0;
}

/*
	Build data-structure from arguments, containing all the data we need.

	Note: We will leak small amounts of memory for malformed argument lists,
	such as when given a prefix but no succeeding key to assign it to. While
	easy to fix, it's not worth the effort.
*/
static int parse_options(int argc, char **argv) {
	struct config_rec *curr_config = NULL;
	struct path_rec *curr_path = NULL;
	char *curr_prefix = NULL;

	struct popt_parser *pp = popt_init(argc, argv);
	if (argc < 2)
		popt_usage(pp, false);

	struct popt *p;
	while ((p = popt_parse_one(pp))) {
		switch (p->dst.i) {
			case 'f':
				curr_config = new_config(p->value);
				STAILQ_INSERT_TAIL(&config_list, curr_config, next);
				/* Forget old context */
				curr_prefix = NULL;
				curr_path = NULL;
				break;

			case 'r':
				if (curr_config) {
					curr_path = new_path(p->value);
					STAILQ_INSERT_TAIL(&curr_config->path_list, curr_path, next);
				} else {
					fprintf(stderr, "ERROR: root specified before configuration.\n");
					return 1;
				}
				break;

			case 'p':
				curr_prefix = xstrdup(p->value);
				break;

			case 'k':
				if (curr_config) {
					struct match_rec *mr = new_match(curr_prefix, p->value);

					/* Create a default NULL root path if none exist. */
					if (!curr_path) {
						curr_path = new_path(NULL);
						STAILQ_INSERT_TAIL(&curr_config->path_list, curr_path, next);
					}

					STAILQ_INSERT_TAIL(&curr_path->match_list, mr, next);
				} else {
					fprintf(stderr, "ERROR: key specified before configuration.\n");
					return 1;
				}
				break;
		}
	}

	return 0;
}

int
main(int argc, char **argv) {
	struct bconf_node *cfg_node;
	int retval = 0;

	if ((retval = parse_options(argc, argv)) != 0) {
		return retval;
	}

	struct config_rec *cfg;
	STAILQ_FOREACH(cfg, &config_list, next) {

		if ((cfg_node = config_init(cfg->filename)) == NULL) {
			fprintf(stderr, "Error reading input file '%s'\n", cfg->filename);
			retval = 1;
			break;
		}

		struct path_rec *path;
		STAILQ_FOREACH(path, &cfg->path_list, next) {
			struct bconf_node *path_node = cfg_node;
			if (path->path) {
				path_node = bconf_vget(cfg_node, path->path, NULL);
				if (!path_node) {
					fprintf(stderr, "Path '%s' empty, ignoring.\n", path->path);
					continue;
				}
			}
			bconf_foreach(path_node, MAX_BCONF_PATH_DEPTH, match_cb, path);

		}
		bconf_free(&cfg_node);
	}

	/*
		Spend some time freeing memory.
	*/
	while (!STAILQ_EMPTY(&config_list)) {
		cfg = STAILQ_FIRST(&config_list);

		while (!STAILQ_EMPTY(&cfg->path_list)) {
			struct path_rec *path = STAILQ_FIRST(&cfg->path_list);

			while (!STAILQ_EMPTY(&path->match_list)) {
				struct match_rec *match = STAILQ_FIRST(&path->match_list);
				STAILQ_REMOVE_HEAD(&path->match_list, next);
				free_match(match);
			}

			STAILQ_REMOVE_HEAD(&cfg->path_list, next);
			free_path(path);
		}

		STAILQ_REMOVE_HEAD(&config_list, next);
		free_config(cfg);
	}

	return retval;
}

