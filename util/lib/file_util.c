// Copyright 2018 Schibsted

#include "file_util.h"

#include <stdlib.h>
#include <unistd.h>

ssize_t
write_to_tmpfile(char *tmpl, FILE *input) {
	int fd = mkstemp(tmpl);
	if (fd < 0)
		return -1;

	char buf[4096];
	ssize_t tot = 0, r;
	while ((r = fread(buf, 1, sizeof(buf), input)) > 0) {
		ssize_t w = write(fd, buf, r);
		if (w != r) {
			close(fd);
			unlink(tmpl);
			return -1;
		}
		tot += w;
	}
	close(fd);
	return tot;
}
