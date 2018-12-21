// Copyright 2018 Schibsted

/*
	String and formatting functions
*/
#ifndef PLATFORM_STRING_FUNCTIONS_H
#define PLATFORM_STRING_FUNCTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "macros.h"

void ltrim(char *str) NONNULL_ALL;
void rtrim(char *str) NONNULL_ALL;
void trim(char *str) NONNULL_ALL;
char *stristrptrs(const char *haystack, const char *needlestart, const char *needleend, const char *delim) NONNULL(1, 2) FUNCTION_PURE;
char *strstrptrs(const char *haystack, const char *needlestart, const char *needleend, const char *delim) NONNULL(1, 2) FUNCTION_PURE;

char *strmodify(char *, int (*modifier)(int)) NONNULL_ALL;

/* Count the number of occurancies of the charcter (ch) in the string (str) */
int count_chars(const char *str, char ch) FUNCTION_PURE;

char *str_replace(const char* subject, const char* from, const char* to) ALLOCATOR NONNULL_ALL;
char *strtrchr(const char* s, const char* from_set, const char to) ALLOCATOR;
char *remove_subset(const char* s, const char* reject_set, int invert) ALLOCATOR;
char *replace_chars (const char *, int, const char *) ALLOCATOR NONNULL_ALL;
char *replace_chars_utf8 (const char *, int, int (*map)[2], int nmap) ALLOCATOR NONNULL_ALL;
int (*replace_chars_utf8_create_map(const char *char_list, int *nmap))[2] ALLOCATOR NONNULL_ALL;
int is_ws(const char* s) FUNCTION_PURE;

char *pretty_format_number_thousands(int value, int min_length, char thou_sep_char);
/* Escape double quotes */
char *escape_dquotes(const char *uqs);
/* Escape ASCII control characters */
char *escape_control_characters(const char *s);

int json_encode_char(char *dst, size_t dlen, char ch, bool escape_solus);

/* Return the current UTF-8 at *strp and advance the pointer to point at the next one. */
int utf8_char_safe(const char **strp, const char *end) NONNULL(1);
int utf8_char(const char **strp) NONNULL_ALL;

void write_utf8_char(char **str, unsigned int ch) NONNULL_ALL;

const char *xstrerror(int errnum);
char *xstrsignal(int signum, char *buf, size_t bufsz);
char *strwait(int status, char *buf, size_t bufsz);

/*
	Given a string representation of an IP address, populate a buffer with
	the reverse DNS lookup of that address using getnameinfo(). Buffer is
	only valid if call was successful. Flags are passed through to getnameinfo()

	RETURN VALUES:
		1 on success,
		0 if the string couldn't be parsed as a IP address
		or a negative value; the error value from getnameinfo()
*/
int get_hostname_by_addr(const char* remote_addr, char* buf, int buf_size, int flags);

/*
 * Helper functions to convert a string to the corresponding numeric type.
 * The strings have to represent a number and be NULL terminated. Non valid
 * charecters or empty strings are treated as errors.
 *
 * dest is populated only if the conversion succeeds
 *
 * 	RETURN VALUES:
 * 		0; on success,
 * 		1; the string contained non acceptable charecters,
 * 		negative value; Range error
 */
int string_to_int32(const char *s, int32_t *dest) NONNULL_ALL;
int string_to_uint32(const char *s, uint32_t *dest) NONNULL_ALL;
int string_to_float(const char *s, float *dest) NONNULL_ALL;
int string_to_double(const char *s, double *dest) NONNULL_ALL;

#ifdef __cplusplus
}
#endif

#endif

