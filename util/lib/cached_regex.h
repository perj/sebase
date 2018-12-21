// Copyright 2018 Schibsted

#ifndef COMMON_CACHED_REGEX_H
#define COMMON_CACHED_REGEX_H

/*
 * Thread safe caching of compiled regexes.
 * Note that regex for replace and regex for match are compiled differently,
 * and the same struct pointer should thus not be used for both.
 *
 * Typical usage:
 * static struct cached_regex re = { "foo.*bar", PCRE_CASELESS };
 *
 * ...
 *
 * if (cached_regex_match(&re, "foo", NULL, 0)) ...
 *
 */

/*
 * Refer to https://pcre.org/pcre.txt section
 * "How pcre_exec() returns captured substrings"
 * for details about how the capture arrays work.
 */

#define OV_VSZ(nm) ((nm + 1) * 3)
#define OV_START(x) ((x) * 2)
#define OV_END(x) ((x) * 2 + 1)

#include <pcre.h>

struct cached_regex {
	const char *regex;
	int options;

	/*
	 * The following three fields are protected by state.
	 * state = 0 means that we haven't compiled the regex
	 * yet. state = 1 means that everything is properly
	 * initialized. state = -1 means that someone else won
	 * the initialization race and we should throw away
	 * our compiled regex.
	 */
	pcre *pe;
	pcre_extra *extra;
	int capture_count;

	int state;
};

struct buf_string;

int cached_regex_replace(struct buf_string *result, struct cached_regex *regex, const char *replacement, const char *haystack, int haystack_length, int global);

int cached_regex_match(struct cached_regex *regex, const char *str, int *ovector, int ovecsize);

void cached_regex_cleanup(struct cached_regex *regex);

#endif
