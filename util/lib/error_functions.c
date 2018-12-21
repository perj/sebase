// Copyright 2018 Schibsted

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

#include "error_functions.h"
#include "string_functions.h"

static int xerr_abort;

struct x_handler {
	void (*err_vprint)(const char *fmt, va_list);
	void (*err_vprintx)(const char *fmt, va_list);
};

/*
 * x_handler that logs to syslog
 */
static struct {
	int priority;
} x_syslog_data = {
	LOG_ERR | LOG_DAEMON,
};

static void
err_vsyslogx(const char *fmt, va_list ap) {
	vsyslog(x_syslog_data.priority, fmt, ap);
}

static void
err_vsyslog(const char *fmt, va_list ap) {
	size_t l = strlen(fmt) + 6;
	char *s = alloca(l);

	snprintf(s, l, "%s: %%m", fmt);

	err_vsyslogx(s, ap);
}

static struct x_handler syslog_xhandler = {
	err_vsyslog,
	err_vsyslogx,
};

/*
 * Standard err.h-style. Not using err.h because it's incompatible with
 * ps_display which we commonly use. ps_display clobbers the progname
 * used by err.h functions.
 */
static char err_appname[256];
static void
errx_stderr(const char *fmt, va_list ap) {
	fprintf(stderr, "%s: ", err_appname);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
}

static void
err_stderr(const char *fmt, va_list ap) {
	int save_errno = errno;
	fprintf(stderr, "%s: ", err_appname);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, ": %s\n", xstrerror(save_errno));
	errno = save_errno;
}

static struct x_handler err_xhandler = {
	err_stderr,
	errx_stderr,
};

/*
 * x_err init functions.
 */
struct x_handler *x_handler = &err_xhandler;

void
x_err_init_syslog(const char *ident, int option, int facility, int priority) {
	openlog(ident, option, facility);
	x_syslog_data.priority = priority;
	x_handler = &syslog_xhandler;
}

void
x_err_init_err(const char *appname) {
	x_handler = &err_xhandler;
	strlcpy(err_appname, appname, sizeof(err_appname));
}

void
x_err_init_custom(void (*print)(const char *fmt, va_list), void (*printx)(const char *fmt, va_list)) {
	static struct x_handler err_custom_handler;

	err_custom_handler.err_vprint = print;
	err_custom_handler.err_vprintx = printx;
	x_handler = &err_custom_handler;
}

/*
 * functions conforming to the functions implemented by the
 * semi-standard <err.h>, but using our alternative error
 * logging facilities and with an added 'x' in front of the
 * name.
 */

void
xerr(int ret, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	(*x_handler->err_vprint)(fmt, ap);
	va_end(ap);

	if (xerr_abort)
		abort();
	exit(ret);
}

void
xerrx(int ret, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	(*x_handler->err_vprintx)(fmt, ap);
	va_end(ap);

	if (xerr_abort)
		abort();
	exit(ret);
}

void
xwarn(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	(*x_handler->err_vprint)(fmt, ap);
	va_end(ap);
}

void
xwarnx(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	(*x_handler->err_vprintx)(fmt, ap);
	va_end(ap);
}

/*
 * Auxiliary functions.
 */

void
set_xerr_abort(int flag) {
	xerr_abort = flag;
}
