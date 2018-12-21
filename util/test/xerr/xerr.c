// Copyright 2018 Schibsted

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <syslog.h>
#include "sbp/error_functions.h"

int
main(int argc, char **argv) {
	int ch;
	int e;
	int n;

	e = 0;
	n = 0;

	x_err_init_err("test_xerr");
	while ((ch = getopt(argc, argv, "ens")) != -1) {
		switch(ch) {
		case 'e':
			e = 1;
			break;
		case 'n':
			n = 1;
			break;
		case 's':
			x_err_init_syslog("xerr", LOG_PERROR, LOG_LOCAL0, LOG_DEBUG);
			break;
		default:
			fprintf(stderr, "usage: xerr [-e] [-n] [-s]\n");
			exit(1);
		}
	}

	if (e) {
		xerrx(1, "hej: %s", "hopp");
	} else if (n) {
		errno = EINVAL;
		xwarn("hej");
	} else {
		xwarnx("hej: %s", "hopp");
	}

	return (0);	
}
