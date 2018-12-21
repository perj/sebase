// Copyright 2018 Schibsted

#include "memalloc_functions.h"
#include "popt.h"

#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

static bool help_aux, verbose_help_aux, version_aux;
POPT_BOOL("help", false, &help_aux, "Print help.");
POPT_BOOL("h", false, &help_aux, NULL);
POPT_BOOL("help-verbose", false, &verbose_help_aux, "Print verbose help.");
POPT_BOOL("version", false, &version_aux, "Print version.");

struct popt_parser {
	int nopts;
	struct popt *options;
	struct option *longopts;
	int argc;
	char **argv;
	const char *progname;
	int dvalidx;

	int (*aux_cmp)(void *a, void *b);
};

static void
popt_set_longopt(struct option *longopt, const struct popt *popt) {
	longopt->name = popt->name;
	longopt->has_arg = (POPT_OPTTYPE(popt->type) != POPT_OBOOL || popt->dval != NULL);
	longopt->flag = NULL;
	longopt->val = 0;
}

struct popt *
popt_add_option(struct popt_parser *pp, const char *name, enum popt_type type,
		const char *dval, intptr_t dstv, const char *desc) {
	LINKER_SET_DECLARE(popt, struct popt);
	int nlsopts = LINKER_SET_END(popt) - LINKER_SET_START(popt);

	int idx = pp->nopts++;
	pp->options = xrealloc(pp->options, pp->nopts * sizeof(*pp->options));
	pp->longopts = xrealloc(pp->longopts, (nlsopts + pp->nopts + 1) * sizeof(*pp->longopts));

	pp->options[idx].name = name;
	pp->options[idx].dval = dval;
	pp->options[idx].desc = desc;
	pp->options[idx].type = type;
	pp->options[idx].dst.i = dstv;
	pp->options[idx].value = NULL;

	popt_set_longopt(&pp->longopts[idx + nlsopts], &pp->options[idx]);
	memset(&pp->longopts[idx + nlsopts + 1], 0, sizeof(*pp->longopts));
	return &pp->options[idx];
}

struct popt_parser *
popt_init(int argc, char **argv) {
	LINKER_SET_DECLARE(popt, struct popt);
	int nlsopts = LINKER_SET_END(popt) - LINKER_SET_START(popt);

	struct popt_parser *pp = xmalloc(sizeof(*pp));
	pp->nopts = 0;
	pp->options = NULL;
	pp->longopts = xmalloc((nlsopts + 1) * sizeof(*pp->longopts));
	pp->argc = argc;
	pp->argv = argv;
	pp->progname = argv[0];

	struct popt *const *p;
	int i = 0;
	LINKER_SET_FOREACH(p, popt) {
		popt_set_longopt(&pp->longopts[i], (*p));
		i++;
	}
	memset(&pp->longopts[i], 0, sizeof(pp->longopts[i]));
	pp->dvalidx = -1;

	return pp;
}

static struct popt_parser *static_pp;

void
popt_free(struct popt_parser *pp) {
	if (pp) {
		free(pp->options);
		free(pp->longopts);
		free(pp);
	}
	if (static_pp && pp != static_pp)
		popt_free(static_pp);
}

struct popt *
popt_next_option(struct popt_parser *pp) {
	extern char *optarg;

	int idx;
	int v = getopt_long_only(pp->argc, pp->argv, "", pp->longopts, &idx);
	if (v < 0)
		return NULL;
	/* Unknown option or returned '?' for an ambiguous match, show usage. */
	if (v != 0)
		popt_usage(pp, false);
	struct popt *p;

	LINKER_SET_DECLARE(popt, struct popt);
	int nlsopts = LINKER_SET_END(popt) - LINKER_SET_START(popt);

	if (idx < nlsopts)
		p = LINKER_SET_START(popt)[idx];
	else
		p = &pp->options[idx - nlsopts];

	if (POPT_DSTTYPE(p->type) == POPT_DBOOLP && p->dst.b == &help_aux)
		popt_usage(pp, false);
	if (POPT_DSTTYPE(p->type) == POPT_DBOOLP && p->dst.b == &verbose_help_aux)
		popt_usage(pp, true);
	if (POPT_DSTTYPE(p->type) == POPT_DBOOLP && p->dst.b == &version_aux)
		popt_version(pp);
	const char *value = pp->longopts[idx].has_arg ? optarg : "1";
	p->value = value;
	return p;
}

struct popt *
popt_next_default(struct popt_parser *pp) {
	LINKER_SET_DECLARE(popt, struct popt);
	int nlsopts = LINKER_SET_END(popt) - LINKER_SET_START(popt);
	while (++pp->dvalidx < pp->nopts + nlsopts) {
		struct popt *p;
		if (pp->dvalidx < nlsopts)
			p = LINKER_SET_START(popt)[pp->dvalidx];
		else
			p = &pp->options[pp->dvalidx - nlsopts];
		if (!p->value && p->dval) {
			p->value = p->dval;
			return p;
		}
	}
	return NULL;
}

struct popt *
popt_parse_one(struct popt_parser *pp) {
	if (pp->dvalidx >= 0)
		return popt_next_default(pp);
	struct popt *p = popt_next_option(pp);
	if (p)
		return p;
	return popt_next_default(pp);
}

#include "popt_boolval.h"

bool
popt_parse_bool(struct popt_parser *pp, struct popt *p) {
	if (!p->dval)
		return true;
	GPERF_ENUM_NOCASE(popt_boolval)
	switch (lookup_popt_boolval(p->value, -1)) {
	case GPERF_CASE("true"):
	case GPERF_CASE("1"):
	case GPERF_CASE("on"):
	case GPERF_CASE("yes"):
		return true;
	case GPERF_CASE("false"):
	case GPERF_CASE("0"):
	case GPERF_CASE("off"):
	case GPERF_CASE("no"):
		return false;
	default:
		fprintf(stderr, "Bad boolean value `%s'\n", p->value);
		popt_usage(pp, false);
	}
}

void
popt_set_bool_ptr(struct popt_parser *pp, struct popt *p) {
	assert(POPT_DSTTYPE(p->type) == POPT_DBOOLP);
	*p->dst.b = popt_parse_bool(pp, p);
}

long
popt_parse_number(struct popt_parser *pp, struct popt *p, bool negallowed) {
	char *end;
	long l = strtol(p->value, &end, 0);
	if (*p->value == '\0' || *end != '\0') {
		fprintf(stderr, "Bad number `%s'\n", p->value);
		popt_usage(pp, false);
	}
	if (!negallowed && l < 0) {
		fprintf(stderr, "Negative number `%s' not allowed\n", p->value);
		popt_usage(pp, false);
	}
	return l;
}

void
popt_set_long_ptr(struct popt_parser *pp, struct popt *p, bool negallowed) {
	assert(POPT_DSTTYPE(p->type) == POPT_DLONGP);
	*p->dst.l = popt_parse_number(pp, p, negallowed);
}

void
popt_set_string_ptr(struct popt_parser *pp, struct popt *p) {
	assert(POPT_DSTTYPE(p->type) == POPT_DSTRINGP);
	*p->dst.s = p->value;
}

bool
popt_set_dptr(struct popt_parser *pp, struct popt *p) {
	switch (p->type) {
	case POPT_BOOLP:
		popt_set_bool_ptr(pp, p);
		break;
	case POPT_NUMLONGP:
		popt_set_long_ptr(pp, p, true);
		break;
	case POPT_MSECLONGP:
	case POPT_SECLONGP:
		popt_set_long_ptr(pp, p, false);
		break;
	case POPT_PORTSTRP:
	case POPT_STRP:
		popt_set_string_ptr(pp, p);
		break;
	default:
		return false;
	}
	return true;
}

void
popt_parse_ptrs(int *argc, char ***argv) {
	struct popt_parser *pp = static_pp = popt_init(*argc, *argv);
	struct popt *p;
	while ((p = popt_parse_one(pp))) {
		popt_set_dptr(pp, p);
	}
	popt_get_arguments(pp, argc, argv);
}

void
popt_get_arguments(struct popt_parser *pp, int *argc, char ***argv) {
	*argc = pp->argc - optind;
	*argv = pp->argv + optind;
}

static const char *
popt_argname(enum popt_type type) {
	switch (POPT_OPTTYPE(type)) {
	case POPT_OBOOL:
		return "boolean";
	case POPT_OMSEC:
		return "milliseconds";
	case POPT_ONUM:
		return "number";
	case POPT_OPORT:
		return "port";
	case POPT_OSEC:
		return "seconds";
	case POPT_OSTR:
		return "string";
	}
	abort();
}

static void
print_option(bool dash, int nkeys, const char *keys[nkeys], enum popt_type type, const char *dval, const char *desc) {
	if (!desc || !*desc)
		return;
	char keybuf[1024];
	unsigned keyoff;
	for (keyoff = 0 ; keyoff < 8 ; keyoff++)
		keybuf[keyoff] = ' ';
	for (int i = 0 ; i < nkeys ; i++) {
		const char *key = keys[i];
		const char *dstr = !dash ? "" : strlen(key) == 1 ? "-" : "--";
		int r = snprintf(keybuf + keyoff, sizeof(keybuf) - keyoff, "%s%s%s",
				i ? "|" : "", dstr, key);
		if (r <= 0)
			break;
		keyoff += r;
	}
	if (POPT_OPTTYPE(type) != POPT_OBOOL || dval) {
		int r = snprintf(keybuf + keyoff, sizeof(keybuf) - keyoff, " %s", popt_argname(type));
		if (r > 0)
			keyoff += r;
	}

	if (dval)
		fprintf(stderr, "%-40s [Default: %s]\n", keybuf, dval);
	else
		fprintf(stderr, "%s\n", keybuf);

	while (1) {
		size_t spn = strcspn(desc, "\n");
		fprintf(stderr, "%-10s%.*s\n", "", (int)spn, desc);
		if (!desc[spn])
			break;
		desc += spn + 1;
	}
}

void
popt_usage(struct popt_parser *pp, bool verbose) {
	if (!pp)
		pp = static_pp;

	int idx = 0;
	LINKER_SET_DECLARE(poptuse, const char *);
	const char ***use;
	LINKER_SET_FOREACH(use, poptuse) {
		fprintf(stderr, "%-8s%s %s\n", idx == 0 ? "Usage:" : "", pp->progname, **use);
		idx++;
	}
	if (idx == 0)
		fprintf(stderr, "%-8s%s [options]\n", "Usage:", pp->progname);

	idx = 0;
	LINKER_SET_DECLARE(poptpur, const char *);
	const char ***purp;
	LINKER_SET_FOREACH(purp, poptpur) {
		if (idx == 0)
			fprintf(stderr, "Purpose:\n");
		fprintf(stderr, "        %s\n", **purp);
		idx++;
	}

	LINKER_SET_DECLARE(poptarg, const char **);
	if (LINKER_SET_END(poptarg) > LINKER_SET_START(poptarg))
		fprintf(stderr, "Arguments:\n");
	const char ****arg;
	LINKER_SET_FOREACH(arg, poptarg) {
		print_option(false, 1, &(**arg)[0], POPT_BOOLP, NULL, (**arg)[1]);
	}

	LINKER_SET_DECLARE(popt, struct popt);
	int nlsopts = LINKER_SET_END(popt) - LINKER_SET_START(popt);

	fprintf(stderr, "Options:\n");
	bool already[pp->nopts + nlsopts];
	memset(already, 0, sizeof(already));
	const char *keys[pp->nopts + nlsopts];
	int nkeys;
	for (idx = 0 ; idx < pp->nopts + nlsopts ; idx++) {
		if (already[idx])
			continue;
		struct popt *p;
		if (idx < nlsopts)
			p = LINKER_SET_START(popt)[idx];
		else
			p = &pp->options[idx - nlsopts];
		const char *dval = p->dval;
		const char *desc = p->desc;

		keys[0] = p->name;
		nkeys = 1;
		if (p->dst.aux != NULL) {
			for (int j = idx + 1 ; j < pp->nopts + nlsopts ; j++) {
				struct popt *p2;
				if (j < nlsopts)
					p2 = LINKER_SET_START(popt)[j];
				else
					p2 = &pp->options[j - nlsopts];
				bool same = p->type == p2->type;
				if (same && pp->aux_cmp && POPT_DSTTYPE(p->type) == POPT_DAUX)
					same = pp->aux_cmp(p->dst.aux, p2->dst.aux) == 0;
				else if (same)
					same = p->dst.i == p2->dst.i;
				if (same) {
					keys[nkeys] = p2->name;
					nkeys++;
					already[j] = true;
					if (!dval)
						dval = p2->dval;
					if (!desc)
						desc = p2->desc;
				}
			}
		}
		if (!desc && verbose)
			desc = "<No description>";
		print_option(true, nkeys, keys, p->type, dval, desc);
	}

	LINKER_SET_DECLARE(poptdes, const char *);
	const char ***desc;
	LINKER_SET_FOREACH(desc, poptdes) {
		fprintf(stderr, "%s\n", **desc);
	}

	exit(1);
}

void
popt_version(struct popt_parser *pp) {
	if (!pp)
		pp = static_pp;

	int idx = 0;
	LINKER_SET_DECLARE(poptver, const char *);
	const char ***verstr;
	LINKER_SET_FOREACH(verstr, poptver) {
		fprintf(stderr, "%s\n", **verstr);
		idx++;
	}
	/*
	if (idx == 0) {
		fprintf(stderr, "%s %s\n", pp->progname, PLATFORM_VERSION_STRING);
	}
	*/
	exit(0);
}

void
popt_set_aux_cmp(struct popt_parser *pp, int (*cmp)(void *a, void *b)) {
	pp->aux_cmp = cmp;
}
