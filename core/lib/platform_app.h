// Copyright 2018 Schibsted

#pragma once

#include "sbp/http.h"
#include "sbp/macros.h"
#include "sbp/popt.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bconf_node;

enum pappopt_type {
	PAPPOPT_ONE,
	PAPPOPT_MULTI,
	PAPPOPT_KEYVAL,

	PAPPOPT_SPECIAL, /* For internal use only */
};

struct pappopt {
	enum pappopt_type type;
	const char *keypath;
};

struct papp {
	char *appl;
	int flags;
	struct {
		const char *service_prefix;
		const char *healthcheck_port_key;
		const char *healthcheck_path;
	} sd;

	struct https_state https;
	struct sd_registry *sdr;
	struct fd_pool *fd_pool;

	int orig_argc;
	char **orig_argv;
	struct popt_parser *pp;
	struct pappopt extraopts[20];
	unsigned int nextraopts;

	/* Conf keys for logtag and loglevel, if custom. */
	const char *logtag_key, *loglevel_key;
	/* Prefix to form logtags such as indexer+search. */
	const char *logtag_prefix;

	/* If set, used by papp_parse_command_line for POPT_DINT options. */
	void (*intopt_cb)(struct bconf_node **opts, struct popt *p);
};

/* You will probably want one of these. */
#define PAPP_DAEMON      0x1
#define PAPP_NOBOS       0x2

/* Rest are optional. */
#define PAPP_WANT_BCONF  0x4
#define PAPP_NEED_BCONF  0x8
#define PAPP_SMART_START 0x10
#define PAPP_PIDFILE     0x20
#define PAPP_PS_DISPLAY  0x40
#define PAPP_NO_SD_SETUP 0x80

void papp_init(struct papp *app, const char *appl, int flags, int argc, char **argv);
void papp_clean(struct papp *app);

#define PAPPOPT_BOOL(name, dval, keypath, desc) PAPPOPT(name, PAPPOPT_ONE, POPT_BOOLAUX, (dval ? "1" : NULL), keypath, desc)
#define PAPPOPT_MILLISECONDS(name, dval, keypath, desc) PAPPOPT(name, PAPPOPT_ONE, POPT_MSECAUX, (dval ? #dval : NULL), keypath, desc)
#define PAPPOPT_NUMBER(name, dval, keypath, desc) PAPPOPT(name, PAPPOPT_ONE, POPT_NUMAUX, (dval ? #dval : NULL), keypath, desc)
#define PAPPOPT_PORT(name, dval, keypath, desc) PAPPOPT(name, PAPPOPT_ONE, POPT_PORTAUX, dval, keypath, desc)
#define PAPPOPT_SECONDS(name, dval, keypath, desc) PAPPOPT(name, PAPPOPT_ONE, POPT_SECAUX, (dval ? #dval : NULL), keypath, desc)
#define PAPPOPT_STRING(name, dval, keypath, desc) PAPPOPT(name, PAPPOPT_ONE, POPT_STRAUX, dval, keypath, desc)

#define PAPPOPT_KEYVALUE(name, keypath, desc) PAPPOPT(name, PAPPOPT_KEYVAL, POPT_STRAUX, NULL, keypath, desc)

/* Implementation of macros above. */
#define PAPPOPT(name, atype, otype, dval, keypath, desc) PAPPOPTL1(__LINE__, name, atype, otype, dval, keypath, desc)
#define PAPPOPTL1(l, name, atype, otype, dval, keypath, desc) PAPPOPTL2(l, name, atype, otype, dval, keypath, desc)
#define PAPPOPTL2(l, name, atype, otype, dval, keypath, desc) \
	static struct pappopt pappopt_##l = {atype, keypath ?: name}; \
	POPTL2(l, name, otype, dval, &pappopt_##l, desc)
/* End of implementation. */

/*
 * Add an options at runtime. Currently limited to size of app->extraopts.
 * You can change the pappopt type or keypath by writing to the reutred struct.
 */
struct pappopt *papp_add_option(struct papp *app, const char *name, enum popt_opttype otype, const char *dval, const char *desc);

/*
 * Parses command line. Calling this functions is optional as
 * papp_config will call it if you pass a NULL opts.
 * But it can be useful for special processing.
 * Calls papp_usage on command line issues.
 */
struct bconf_node *papp_parse_command_line(struct papp *app);

/*
 * Parses command line (if opts is NULL), config file and possibly bconf.
 * Returns all of them merged.
 * Calls papp_usage on command line issues.
 * argc and argv are modified to point to first non-option argument and the number of them.
 * (argv[0] is the first non-option argument).
 * The original argv[0] is in the returned bconf "progname" key.
 */
struct bconf_node *papp_config(struct papp *app, struct bconf_node *opts);

/*
 * Prints usage and calls exit(1).
 */
void papp_usage(struct papp *app, bool verbose);

/*
 * Daemonizes (if PAPP_DAEMON but no foreground option set).
 * Starts BOS (if neither PAPP_NOBOS nor nobos option set).
 * Configures SD if appropriate (sd.service option set).
 * Starts Go runtime in child if present and will_fork is false.
 * Set will_fork to true if you plan to call papp_fork before starting
 * up the application, otherwise false.
 */
void papp_start(struct papp *app, struct bconf_node *conf, bool will_fork);

/*
 * Forks and starts go runtime in child if present.
 * Also initializes the sdr and fd_pool, which are skipped by start if
 * will_fork is true, due to that it might create threads.
 * It's only valid to call this after papp_start with will_fork true,
 * further forking should be done the normal way.
 * The conf should be the same as that passed to start.
 */
pid_t papp_fork(struct papp *app, struct bconf_node *conf);

/*
 * Set app->appl to appl. Use this function instead of manipulating app->appl
 * directly to avoid memory management issues.
 */
void papp_set_appl(struct papp *app, const char *appl);

#ifdef __cplusplus
}
#endif
