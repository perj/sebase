// Copyright 2018 Schibsted

#ifndef PLATFORM_ERROR_FUNCTIONS_H
#define PLATFORM_ERROR_FUNCTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "macros.h"

#include <stdarg.h>

/*
 * Misc functions to init the error logging functions for the x* family
 * of functions.
 *
 * Default is to log to syslog with LOG_ERR | LOG_DAEMON
 * the x{err,warn}{,x} functions conform to the {err,warn}{,x} functions
 * documented in err(3).
 */
void x_err_init_syslog(const char *ident, int option, int facility, int priority);
void x_err_init_err(const char *appname);
void x_err_init_custom(void (*print)(const char *fmt, va_list), void (*printx)(const char *fmt, va_list));

void xerr(int ret, const char *fmt, ...) NORETURN FORMAT_PRINTF(2, 3);
void xerrx(int ret, const char *fmt, ...) NORETURN FORMAT_PRINTF(2, 3);
void xwarn(const char *fmt, ...) FORMAT_PRINTF(1, 2);
void xwarnx(const char *fmt, ...) FORMAT_PRINTF(1, 2);

void set_xerr_abort(int flag);

#ifdef __cplusplus
}
#endif

#endif

