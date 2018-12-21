// Copyright 2018 Schibsted

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "macros.h"
#include "linker_set.h"

#ifdef __cplusplus
extern "C" {
#endif

struct popt_parser;

enum popt_dsttype {
	POPT_DBOOLP    = 0x100,
	POPT_DLONGP    = 0x200,
	POPT_DSTRINGP  = 0x300,
	POPT_DAUX      = 0x400,
	POPT_DINT      = 0x500,
};

enum popt_opttype {
	POPT_OBOOL,
	POPT_OMSEC,
	POPT_ONUM,
	POPT_OPORT,
	POPT_OSEC,
	POPT_OSTR,
};

#define POPT_DSTMASK 0xF00
#define POPT_OPTMASK 0x0FF

#define POPT_DSTTYPE(t) ((enum popt_dsttype)((t) & POPT_DSTMASK))
#define POPT_OPTTYPE(t) ((enum popt_opttype)((t) & POPT_OPTMASK))

/* Valid combinations. */
enum popt_type {
	POPT_BOOLP     = POPT_DBOOLP | POPT_OBOOL,
	POPT_MSECLONGP = POPT_DLONGP | POPT_OMSEC,
	POPT_NUMLONGP  = POPT_DLONGP | POPT_ONUM,
	POPT_SECLONGP  = POPT_DLONGP | POPT_OSEC,
	POPT_PORTSTRP  = POPT_DSTRINGP | POPT_OPORT,
	POPT_STRP      = POPT_DSTRINGP | POPT_OSTR,
	POPT_BOOLAUX   = POPT_DAUX | POPT_OBOOL,
	POPT_MSECAUX   = POPT_DAUX | POPT_OMSEC,
	POPT_NUMAUX    = POPT_DAUX | POPT_ONUM,
	POPT_PORTAUX   = POPT_DAUX | POPT_OPORT,
	POPT_SECAUX    = POPT_DAUX | POPT_OSEC,
	POPT_STRAUX    = POPT_DAUX | POPT_OSTR,
	POPT_BOOLINT   = POPT_DINT | POPT_OBOOL,
	POPT_MSECINT   = POPT_DINT | POPT_OMSEC,
	POPT_NUMINT    = POPT_DINT | POPT_ONUM,
	POPT_PORTINT   = POPT_DINT | POPT_OPORT,
	POPT_SECINT    = POPT_DINT | POPT_OSEC,
	POPT_STRINT    = POPT_DINT | POPT_OSTR,
};

struct popt {
	const char *name;
	const char *dval;
	const char *desc;
	enum popt_type type;

	union {
		bool *b;
		long *l;
		const char **s;
		intptr_t i;
		void *aux;
	} dst;

	const char *value;
};

/*
 * Linkerset macros.
 *
 * POPT_USAGE is the usage line, it will be prefixed with the program name.
 * The default one is "[options]". You can add more than one.
 *
 * POPT_VERSION is a version line.
 *
 * POPT_PURPOSE is a single line printed directly after the usage.
 *
 * POPT_ARGUMENT lets you document the non-option arguments. E.g. if your
 * usage is "[options] host port" you would use it to document "host" and
 * "port".
 *
 * POPT_DESCRIPTION is added at the very bottom of the help output.
 *
 * The rest defines options. The normal none (POPT_TYPE) takes an option name,
 * default value, pointer to variable and description.
 * The pointer type should be either bool*, long* or const char**, you can
 * see which one is expected in the second argument to POPT.
 *
 * The AUX types typically are for extension libraries, it takes a void*
 * pointer, but it's up to the caller to use it via popt_parse_one or
 * similar.
 *
 * The INT types are meant for apps that want to do their own processing,
 * e.g. to allow the same option multiple times. It takes an intptr_t integer
 * and it's again up to the caller to use it.
 */
#define POPT_USAGE(usage) POPT_USAGEL1(__LINE__, usage)

#define POPT_VERSION(verstr) POPT_VERSIONL1(__LINE__, verstr)

#define POPT_PURPOSE(str) POPT_PURPOSEL1(__LINE__, str)

#define POPT_ARGUMENT(name, desc) POPT_ARGL1(__LINE__, name, desc)

#define POPT_DESCRIPTION(str) POPT_DESCRIPTIONL1(__LINE__, str)

/* Typed pointers. */
#define POPT_BOOL(        name, dval, ptr, desc) \
		static_assert(sizeof(*ptr) == sizeof(bool), "Bool ptr required"); \
		POPT(name, POPT_BOOLP, (dval ? "1" : NULL), ptr, desc)
#define POPT_MILLISECONDS(name, dval, ptr, desc) \
		static_assert(sizeof(*ptr) == sizeof(long), "Long ptr required"); \
		POPT(name, POPT_MSECLONGP, (dval ? #dval : NULL), ptr, desc)
#define POPT_NUMBER(      name, dval, ptr, desc) \
		static_assert(sizeof(*ptr) == sizeof(long), "Long ptr required"); \
		POPT(name, POPT_NUMLONGP, (dval ? #dval : NULL), ptr, desc)
#define POPT_PORT(        name, dval, ptr, desc) \
		static_assert(sizeof(*ptr) == sizeof(const char *), "String ptr required"); \
		POPT(name, POPT_PORTSTRP, dval, ptr, desc)
#define POPT_SECONDS(     name, dval, ptr, desc) \
		static_assert(sizeof(*ptr) == sizeof(long), "Long ptr required"); \
		POPT(name, POPT_SECLONGP, (dval ? #dval : NULL), ptr, desc)
#define POPT_STRING(      name, dval, ptr, desc) \
		static_assert(sizeof(*ptr) == sizeof(const char *), "String ptr required"); \
		POPT(name, POPT_STRP, dval, ptr, desc)

/* Aux pointers. */
#define POPT_BOOL_AUX(        name, dval, aux, desc) POPT(name, POPT_BOOLAUX, (dval ? "1" : NULL), aux, desc)
#define POPT_MILLISECONDS_AUX(name, dval, aux, desc) POPT(name, POPT_MSECAUX, (dval ? #dval : NULL), aux, desc)
#define POPT_NUMBER_AUX(      name, dval, aux, desc) POPT(name, POPT_NUMAUX, (dval ? #dval : NULL), aux, desc)
#define POPT_PORT_AUX(        name, dval, aux, desc) POPT(name, POPT_PORTAUX, dval, aux, desc)
#define POPT_SECONDS_AUX(     name, dval, aux, desc) POPT(name, POPT_SECAUX, (dval ? #dval : NULL), aux, desc)
#define POPT_STRING_AUX(      name, dval, aux, desc) POPT(name, POPT_STRAUX, dval, aux, desc)

/* Integers. */
#define POPT_BOOL_INT(        name, dval, i, desc) POPT(name, POPT_BOOLINT, (dval ? "1" : NULL), i, desc)
#define POPT_MILLISECONDS_INT(name, dval, i, desc) POPT(name, POPT_MSECINT, (dval ? #dval : NULL), i, desc)
#define POPT_NUMBER_INT(      name, dval, i, desc) POPT(name, POPT_NUMINT, (dval ? #dval : NULL), i, desc)
#define POPT_PORT_INT(        name, dval, i, desc) POPT(name, POPT_PORTINT, dval, i, desc)
#define POPT_SECONDS_INT(     name, dval, i, desc) POPT(name, POPT_SECINT, (dval ? #dval : NULL), i, desc)
#define POPT_STRING_INT(      name, dval, i, desc) POPT(name, POPT_STRINT, dval, i, desc)

/* Simple parsing when using only typed pointer dests.
 * Will parse all options and defaults and set the pointers using the functions below.
 * After the call argc is the number of non-option arguments and argv[0] is the first one.
 */
void popt_parse_ptrs(int *argc, char ***argv);

/* Set the value of pointer in p->dst to parsed p->value, based on type in p->argument.
 * Non-ptr dests are ignored, but returns false instead of true.
 */
bool popt_set_dptr(struct popt_parser *pp, struct popt *p);

/* Called by popt_set_dptr to parse value and set the different kind of ptrs.
 * It's an error to call these on the wrong popt type.
 */
void popt_set_bool_ptr(struct popt_parser *pp, struct popt *p);
void popt_set_long_ptr(struct popt_parser *pp, struct popt *p, bool negallowed);
void popt_set_string_ptr(struct popt_parser *pp, struct popt *p);

/* Parses and returns value stored in p->value, calls popt_usage on failures. */
bool popt_parse_bool(struct popt_parser *pp, struct popt *p);
long popt_parse_number(struct popt_parser *pp, struct popt *p, bool negallowed);

struct popt_parser *popt_init(int argc, char **argv);
/*
 * Free a popt_parser and clean up internal allocations.
*/
void popt_free(struct popt_parser *pp);

/* If you wish to do something more special, this function return options one at a time.
 * Call it until it returns NULL, it will then have returned for options and default values.
 * p->value is set to optarg or dval if an argument is required, otherwise "1".
 */
struct popt *popt_parse_one(struct popt_parser *pp);

/* For even more special cases, use these two one at a time until they return NULL. */
struct popt *popt_next_option(struct popt_parser *pp);
struct popt *popt_next_default(struct popt_parser *pp);

/* Adds an option at runtime. The dstv value is the pointer cast to intptr_t for the
 * pointer types.
 */
struct popt *popt_add_option(struct popt_parser *pp, const char *name, enum popt_type type,
		const char *dval, intptr_t dstv, const char *desc);

/* Returns in *argc the number of non-option arguments and in *argv an array of them.
 * Does not read their values as input but rather uses the argc and argv passed to
 * popt_init.
 */
void popt_get_arguments(struct popt_parser *pp, int *argc, char ***argv);

/* Prints usage and exit(1). If verbose is true then options without descriptions are
 * printed, otherwise they're hidden.
 */
void popt_usage(struct popt_parser *pp, bool verbose) __attribute__((noreturn));

/* Prints the version string(s) and exit(0). If no version strings were added,
 * default to the program name followed by the platform version.
 */
void popt_version(struct popt_parser *pp) __attribute__((noreturn));

/* Sets a function to compare aux pointers with in popt_usage. It should return
 * 0 when two options should be considered the same and printed together.
 */
void popt_set_aux_cmp(struct popt_parser *pp, int (*cmp)(void *a, void *b));

/* Implementation of macros above. */
#define POPT_USAGEL1(l, usage) POPT_USAGEL2(l, usage)
#define POPT_USAGEL2(l, usage) \
	static const char *popt_usage_##l = usage; \
	LINKER_SET_ENTRY(poptuse, popt_usage_##l)

#define POPT_VERSIONL1(l, str) POPT_VERSIONL2(l, str)
#define POPT_VERSIONL2(l, str) \
	static const char *popt_version_##l = str; \
	LINKER_SET_ENTRY(poptver, popt_version_##l)

#define POPT_PURPOSEL1(l, str) POPT_PURPOSEL2(l, str)
#define POPT_PURPOSEL2(l, str) \
	static const char *popt_purpose_##l = str; \
	LINKER_SET_ENTRY(poptpur, popt_purpose_##l)

#define POPT_ARGL1(l, name, desc) POPT_ARGL2(l, name, desc)
#define POPT_ARGL2(l, name, desc) \
	static const char *popt_arg_##l[2] = {name, desc}; \
	static const char **popt_ptr_##l = popt_arg_##l; \
	LINKER_SET_ENTRY(poptarg, popt_ptr_##l)

#define POPT(name, type, dval, dstv, desc) POPTL1(__LINE__, name, type, dval, dstv, desc)
#define POPTL1(l, name, type, dval, dstv, desc) POPTL2(l, name, type, dval, dstv, desc)
// Cast the pointer. Assumes union can read that, will work in practice but might be UD.
#define POPTL2(l, name, type, dval, dstv, desc) \
	static struct popt popt_##l = { name, dval, desc, type, { .i = (intptr_t)(dstv) } }; \
	LINKER_SET_ENTRY(popt, popt_##l)

#define POPT_DESCRIPTIONL1(l, str) POPT_DESCRIPTIONL2(l, str)
#define POPT_DESCRIPTIONL2(l, str) \
	static const char *popt_desc_##l = str; \
	LINKER_SET_ENTRY(poptdes, popt_desc_##l)
/* End of macro implementation. */

#ifdef __cplusplus
}
#endif
