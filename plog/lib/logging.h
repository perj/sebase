// Copyright 2018 Schibsted

#ifndef LOGGING_H
#define LOGGING_H

#include "sbp/macros.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */


#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>

struct plog_ctx *logging_plog_ctx(void);

const char *level_name(int prio);
int get_priority_from_level(const char* level, int default_priority);

int log_printf(int level, const char* fmt, ...) FORMAT_PRINTF(2, 3) VISIBILITY_HIDDEN;
int vlog_printf(int level, const char *fmt, va_list ap) FORMAT_PRINTF(2, 0) VISIBILITY_HIDDEN;
void log_backtrace(int level, int skip) VISIBILITY_HIDDEN;

int log_setup(const char *appname, const char *level) VISIBILITY_HIDDEN;
int log_setup_perror(const char *appname, const char *level) VISIBILITY_HIDDEN;
int log_shutdown(void) VISIBILITY_HIDDEN;
int log_level() VISIBILITY_HIDDEN;

void log_register_thread(const char *log_string, ...) FORMAT_PRINTF(1, 2) VISIBILITY_HIDDEN;

void log_enable_plog(bool enable);

void log_change_level(const char *level, const char **oldl, const char **newl);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
