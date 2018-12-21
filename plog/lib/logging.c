// Copyright 2018 Schibsted

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>

#include <execinfo.h>

#define SYSLOG_NAMES
#include <syslog.h>

#include <pthread.h>


#ifdef DEBUG_STDLIB
#include "debug_stdlib.h"
#endif

#include "plog.h"
#include "logging.h"
#include "sbp/memalloc_functions.h"

static struct {
	int level;
	bool use_plog;
	char appname[256];
} _log = { -1, true };
static pthread_key_t log_tsd;
static pthread_once_t log_key_once = PTHREAD_ONCE_INIT;

inline const char*
level_name(int level) {
	switch(level) {
		case LOG_EMERG: return "EMERG";
		case LOG_ALERT: return "ALERT";
		case LOG_CRIT: return "CRIT";
		case LOG_ERR: return "ERR";
		case LOG_WARNING: return "WARNING";
		case LOG_NOTICE: return "NOTICE";
		case LOG_INFO: return "INFO";
		case LOG_DEBUG: return "DEBUG";
		default: return "";
	}
}

static inline const char*
level_name_lc(int level) {
	switch(level) {
		case LOG_EMERG: return "emerg";
		case LOG_ALERT: return "alert";
		case LOG_CRIT: return "crit";
		case LOG_ERR: return "err";
		case LOG_WARNING: return "warning";
		case LOG_NOTICE: return "notice";
		case LOG_INFO: return "info";
		case LOG_DEBUG: return "debug";
		default: return "";
	}
}

int
get_priority_from_level(const char* level, int default_priority) {
	if (level) {
		CODE *pri_code = prioritynames;
		while (pri_code->c_name) {
			if (strcasecmp(pri_code->c_name, level) == 0)
				return pri_code->c_val;
			++pri_code;
		}
	}
	return default_priority;
}

static struct plog_ctx *logging_ctx;
static pthread_mutex_t logging_ctx_lock = PTHREAD_MUTEX_INITIALIZER;

struct plog_ctx *
logging_plog_ctx(void) {
	if (!logging_ctx) {
		/* Keep errno in case of %m */
		int save_errno = errno;
		pthread_mutex_lock(&logging_ctx_lock);
		if (!logging_ctx)
			logging_ctx = plog_open_log(NULL, _log.appname);
		pthread_mutex_unlock(&logging_ctx_lock);
		errno = save_errno;
	}
	return logging_ctx;
}

int
vlog_printf(int level, const char *fmt, va_list ap) {
	int res = 0;
	char *ptr;
	char *log_string = NULL;
	char *logfmt;

	if (level > _log.level)
		return 0;

	log_string = pthread_getspecific(log_tsd);

	if (log_string)
		ALLOCA_PRINTF(res, logfmt, "(%s): %s", log_string, fmt);
	else
		logfmt = (char*)fmt;

	if ((ptr = strchr(logfmt, '\r'))) {
		*ptr = '\0';
	}

	if (_log.use_plog)
		plog_string_vprintf(logging_plog_ctx(), level_name(level), logfmt, ap);
	else
		vsyslog(level, logfmt, ap);

	return 1;
}

int
log_printf(int level, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	int res = vlog_printf(level, fmt, ap);
	va_end(ap);

	return res;
}

void
log_backtrace(int level, int skip) {
	void *btbuf[50];
	int btsiz = 50;
	int bti;
	char **bts;

	if (skip < -1)
		return;

	btsiz = backtrace(btbuf, btsiz);
	if (btsiz > 0) {
		bts = backtrace_symbols(btbuf, btsiz);
		for (bti = skip + 1 ; bti < btsiz ; bti++)
			log_printf(level, " bt: %s", bts[bti]);
		free(bts);
	}
}


static
void
key_free(void *ptr) {
	free(ptr);
}

static
void
log_thread_once(void) {
	pthread_key_create(&log_tsd, key_free);
}

void
log_register_thread(const char *fmt, ...) {
	char *old_log_string;
	char *log_string;
	va_list ap;

	old_log_string = pthread_getspecific(log_tsd);
	if (old_log_string)
		free(old_log_string);

	va_start(ap, fmt);
	if (vasprintf(&log_string, fmt, ap) == -1)
		;
	va_end(ap);

	pthread_setspecific(log_tsd, log_string);
}


int
log_level(void) {
	return _log.level;
}

static int
log_setup_options(const char *appname, const char *level, int options) {

	pthread_once(&log_key_once, log_thread_once);

	_log.level = get_priority_from_level(level, LOG_INFO);
	strlcpy(_log.appname, appname, sizeof(_log.appname));

	openlog(_log.appname, options, LOG_LOCAL0);

	return 1;
}

int
log_setup(const char *appname, const char *level) {
	return log_setup_options(appname, level, 0);
}

int
log_setup_perror(const char *appname, const char *level) {
	return log_setup_options(appname, level, LOG_PERROR);
}

int
log_shutdown(void) {

	if (logging_ctx) {
		int fw = plog_reset_failed_writes(logging_ctx);
		if (fw)
			plog_int(logging_ctx, "plog_failed_writes", fw);
		plog_close(logging_ctx);
		logging_ctx = NULL;
	}

	void (*hook_closelog)(void) = dlsym(RTLD_DEFAULT, "sysloghook_closelog");
	if (hook_closelog)
		hook_closelog();
	else
		closelog();

	return 1;
}

void
log_enable_plog(bool enable) {
	_log.use_plog = enable;
}

void
log_change_level(const char *level, const char **oldl, const char **newl) {
	*oldl = level_name_lc(_log.level);
	if (level != NULL)
		_log.level = get_priority_from_level(level, _log.level);
	*newl = level_name_lc(_log.level);
}
