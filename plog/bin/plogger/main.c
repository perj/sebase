// Copyright 2018 Schibsted

#include "sbp/logging.h"
#include "sbp/plog.h"
#include "sbp/popt.h"

#include <stdio.h>
#include <unistd.h>

const char *appname;
const char *type;

POPT_STRING("appname", NULL, &appname, "Appname to use. Defaults to the current user.");
POPT_STRING("type", NULL, &type, "Message type to use. Defaults to log.");

int
main(int argc, char *argv[]) {
	popt_parse_ptrs(&argc, &argv);

	if (!appname)
		appname = getlogin();
	if (!appname)
		appname = "plogger";

	log_setup_perror(appname, "debug");

	struct plog_ctx *ctx = logging_plog_ctx();

	char *line = NULL;
	size_t n = 0;
	ssize_t l;
	while ((l = getline(&line, &n, stdin)) >= 0) {
		if (l > 0 && line[l - 1] == '\n')
			l--;
		plog_string_len(ctx, type, line, l);
	}
	free(line);

	log_shutdown();
	popt_free(NULL);
}
