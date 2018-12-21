// Copyright 2018 Schibsted

#include "sbp/string_functions.h"

#include <stdio.h>
#include <string.h>

int
main(int argc, char *argv[]) {
	// One character of each UTF-8 length (with spaces after each).
	char input[] = "A γ € 🌈 ";
	int expect[8] = { 'A', ' ', 0x3B3, ' ', 0x20AC, ' ', 0x1F308, ' ' }, *eptr = expect;
	const char *ptr = input, *pptr = input;
	int ch;
	int ret = 0;
	while ((ch = utf8_char(&ptr))) {
		if (ch != *eptr) {
			fprintf(stderr, "Expected U+%X, got U+%X\n", *eptr, ch);
			ret = 1;
		}
		eptr++;

		char output[5], *wptr = output;
		write_utf8_char(&wptr, ch);

		if (ptr - pptr != wptr - output) {
			fprintf(stderr, "Got different length writing, %d != %d",
					(int)(wptr - output), (int)(ptr - pptr));
			ret = 1;
		} else if (memcmp(pptr, output, wptr - output) != 0) {
			fprintf(stderr, "Wrote different output than input, %.*s != %.*s",
					(int)(wptr - output), output, (int)(ptr - pptr), pptr);
			ret = 1;
		}
		pptr = ptr;
	}
	return ret;
}
