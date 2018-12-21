// Copyright 2018 Schibsted

#include <ctype.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>

#include "sbp/atomic.h"
#include "buf_string.h"
#include "cached_regex.h"
#include "memalloc_functions.h"

int
cached_regex_replace(struct buf_string *result, struct cached_regex *regex, const char* replacement, const char* haystack, int haystack_length, int global) {
	pcre *re = NULL;
	pcre_extra *extra;
	const char* err;
	int retval;
	int alloced = 0;

	int capture_count = 0;
	int *offset = NULL;

	int haystack_offset = 0;
	const char *curr_replacement;

	const char* replacement_start;
	int backref;

	if (haystack_length == -1)
		haystack_length = strlen(haystack);

	/* Handle empty regexes */
	if (regex->regex[0] == '\0') {
		bufwrite(&result->buf, &result->len, &result->pos, haystack, haystack_length);
		return 0;
	}

	/* Compile pattern */
	if (regex->state == 1) {
		re = regex->pe;
		extra = regex->extra;
		capture_count = regex->capture_count;
	} else {
		alloced = 1;

		re = pcre_compile(regex->regex, regex->options, &err, &retval, NULL);

		if (!re) {
			syslog(LOG_ERR, "pcre_compile(%s) %s at %i", regex->regex, err, retval);
			return -1;
		}

		/* Fetch information about the pattern */
		extra = pcre_study(re, 0, &err);

		if (!extra && err) {
			syslog(LOG_ERR, "pcre_study(%s) %s", regex->regex, err);
			pcre_free(re);
			return -1;
		}

		/* Get number of captures */
		if (pcre_fullinfo(re, extra, PCRE_INFO_CAPTURECOUNT, &capture_count) < 0) {
			syslog(LOG_ERR, "pcre_fullinfo(PCRE_INFO_CAPTURECOUNT) failed");
			pcre_free(extra);
			pcre_free(re);
			return -1;
		}

		capture_count += 1;
	}

	/* Make room for capture */
	offset = xmalloc(sizeof(int) * capture_count * 3);

	while (1) {
		replacement_start = replacement;
		curr_replacement = replacement - 1;
		retval = pcre_exec(re, extra, haystack, haystack_length, haystack_offset, 0, offset, capture_count*3);

		if (retval == PCRE_ERROR_NOMATCH) {
			break;
		}
		if (retval < 0) {
			syslog(LOG_ERR, "pcre_exec: %d", retval);
			free(offset);
			if (alloced) {
				pcre_free(extra);
				pcre_free(re);
			}
			return -1;
		}

		/* Print everyting up to the start of the pattern */
		bufwrite(&result->buf, &result->len, &result->pos, haystack + haystack_offset, offset[0] - haystack_offset);
		
		/* Walk through replacement string */
		while (*(++curr_replacement) != '\0') {
			/* Check if current char is a backref-sequence */
			if (*curr_replacement == '$' && (*(curr_replacement + 1) == '$' || isdigit(*(curr_replacement + 1)))) {
				/* Flush if there's any data */
				if (replacement_start < curr_replacement) {
					bufwrite(&result->buf, &result->len, &result->pos, replacement_start, curr_replacement - replacement_start);
				}

				/* Skip first dollar and forward replacement_start */
				++curr_replacement;
				replacement_start = curr_replacement;

				/* If double dollar, update pointers and continue */
				if (*curr_replacement == '$')
					continue;

				/* Back-references */
				backref = *curr_replacement - '0';

				if (backref < capture_count)
					bufwrite(&result->buf, &result->len, &result->pos, haystack + offset[backref*2], offset[backref*2+1] - offset[backref*2]);
				else
					syslog(LOG_WARNING, "regex_replacement pattern uses unknown back-reference (%d) in \"%s\"", backref, regex->regex);

				++replacement_start;
				
				continue;
			}
		}

		if (replacement_start < curr_replacement) {
			bufwrite(&result->buf, &result->len, &result->pos, replacement_start, curr_replacement - replacement_start);
		}
		

		haystack_offset = offset[1];

		if (!global)
			break;
	}

	/* Print remainder of input after last match */
	bufwrite(&result->buf, &result->len, &result->pos, haystack + haystack_offset, haystack_length - haystack_offset);

	if (alloced) {
		if (!atomic_cas_int(&regex->state, 0, -1)) {
			regex->pe = re;
			regex->extra = extra;
			regex->capture_count = capture_count;
			/* This needs to be a cas because of memory ordering. */
			atomic_cas_int(&regex->state, -1, 1);
		} else {
			pcre_free(re);
			if (extra)
				pcre_free(extra);
		}
	}
	free(offset);

	return 0;
}

int
cached_regex_match(struct cached_regex *regex, const char *str, int *ov, int ovlen) {
	int alloced = 0;
	pcre *re;
	pcre_extra *extra;
	int retval;
	const char* err;

	/* Handle empty regexes */
	if (regex->regex[0] == '\0') {
		return 0;
	}

	/* Compile pattern */
	if (regex->state == 1) {
		re = regex->pe;
		extra = regex->extra;
	} else {
		alloced = 1;

		re = pcre_compile(regex->regex,
			(ovlen ? 0 : PCRE_NO_AUTO_CAPTURE) | regex->options,
			&err, &retval, NULL);

		if(!re) {
			syslog(LOG_ERR, "pcre_compile(%s) %s at %i", regex->regex, err, retval);
			return 0;
		}

		/* Fetch information about the pattern */
		extra = pcre_study(re, 0, &err);

		if(!extra && err) {
			syslog(LOG_ERR, "pcre_study(%s) %s", regex->regex, err);
			pcre_free(re);
			return 0;
		}
	}

	retval = pcre_exec(re, extra, str, strlen(str), 0, 0, ov, ovlen);

	if (retval < 0 && retval != PCRE_ERROR_NOMATCH) {
		syslog(LOG_ERR, "pcre_exec < 0");
		if (alloced) {
			pcre_free(extra);
			pcre_free(re);
		}
		return 0;
	}

	if (alloced) {
		if (!atomic_cas_int(&regex->state, 0, -1)) {
			regex->pe = re;
			regex->extra = extra;
			/* This needs to be a cas because of memory ordering. */
			atomic_cas_int(&regex->state, -1, 1);
		} else {
			pcre_free(re);
			if(extra)
				pcre_free(extra);
		}
	}
	
	return retval != PCRE_ERROR_NOMATCH;
}

void
cached_regex_cleanup(struct cached_regex *regex) {
	/* This needs to be a cas because of memory ordering. */
	atomic_cas_int(&regex->state, regex->state, -1);
	if (regex->pe) {
		pcre_free(regex->pe);
		regex->pe = NULL;
	}
	if (regex->extra) {
		pcre_free(regex->extra);
		regex->extra = NULL;
	}
	/* This needs to be a cas because of memory ordering. */
	atomic_cas_int(&regex->state, -1, 0);
}

