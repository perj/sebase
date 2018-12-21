// Copyright 2018 Schibsted

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "sbp/buf_string.h"

static int error(const char *msg) {
	printf("Error: %s\n", msg);
	exit(1);
}

int
main(int argc, char *argv[]) {
	struct buf_string bs = { 0 };
	char buf[256];

	memset(buf, 'A', sizeof(buf));

	bswrite(&bs, "B", 1);
	int bufsize = bs.len;
	printf("Initial buffer size=%d\n", bufsize);
	if (bs.pos != 1)
		error("Buffer length invalid");
	if (bs.buf[0] != 'B')
		error("Buffer contents invalid");
	if (bs.buf[1] != 0)
		error("Buffer not terminated after bswrite");

	int fill_count = sizeof(buf)-bs.pos;
	int reallocs_expected = 4;
	int buf_target_size = bufsize * reallocs_expected;
	int written = bs.pos;
	int prev_buflen = bs.len;
	int reallocs = 0;

	while (bs.pos < buf_target_size) {
		int ret = bscat(&bs, "%.*s", fill_count, buf);
		if (bs.len != prev_buflen) {
			printf("Buffer reallocated from %d to %d bytes.\n", prev_buflen, bs.len);
			prev_buflen = bs.len;
			++reallocs;
		}
		printf("Wrote %d bytes, string is %d characters, allocation %d bytes.\n", ret, bs.pos, bs.len);
		fill_count = sizeof(buf);
		written += ret;
	}
	if (written != buf_target_size)
		error("Wrote more data than expected");

	if (reallocs > reallocs_expected)
		error("Unexpectedly many reallocations");

	printf("Buffer length is %d, string length is %zu\n", bs.pos, strlen(bs.buf));
	if ((int)strlen(bs.buf) != bs.pos)
		error("Buffer length invalid");

	for (int i=0 ; i < bs.pos ; ++i) {
		char expected = i == 0 ? 'B' : 'A';
		if (bs.buf[i] != expected) {
			printf("bs[%d] is '%c', expected '%c'\n", i, bs.buf[i], expected);
			error("Invalid buffer contents, buffer corrupt!");
		}
	}

	free(bs.buf);

	printf("buf_string tests complete.\n");

	return 0;
}
