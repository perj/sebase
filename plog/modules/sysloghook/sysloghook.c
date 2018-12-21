// Copyright 2018 Schibsted

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>

#pragma GCC visibility push(default)

static const char *syslogroot;
static const char *defaultlog = "messages";

static int _mask = LOG_UPTO(LOG_DEBUG);
static FILE *_log = NULL;
static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;


/*
	forks are copy on write. 
	When a process forks, it leads to keeping the same file pointer until the ident changes
	or re-uses a pointer to a wrong fileno.
	_pid stores the initialized _log pid and when different, openlog will re-open the log file
*/
static int _pid = 0 ;

static char _ident[128] = { 0 };
static char workbuf[1024] = { 0 };

int syslog_ident(const char *ident, const char *format, ...);
int vsyslog_ident(const char *ident, const char *format, va_list ap);
void __syslog_chk(int priority, int flag, const char *format, ...);
void __vsyslog_chk(int priority, int flag, const char *format, va_list ap);

/* Internal helper */
static int
writelogline(FILE* file, const char* ident, const char* line)
{
	char timestr[64]; /* todo: change to use workbuf */
	struct tm tm;
	struct timeval tv;
	char idbuf[30];
	size_t res;

	if (ident) {
		/* It happens that we're passed garbage as ident. */
		const char *ip;

		for (ip = ident ; *ip ; ip++) {
			if (*ip < 0x20 || *ip == 0x7F) {
				snprintf(idbuf, sizeof(idbuf), "(garbageptr:%p)", ident);
				ident = idbuf;
				break;
			}
		}
	}

	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	strftime(timestr, sizeof(timestr), "%F %T", &tm);

#ifdef SYSLOGHOOK_MICROSECONDS
	res = fprintf(file, "%s.%06ld %s: %s\n", timestr, tv.tv_usec, ident, line);
#else
	res = fprintf(file, "%s %s: %s\n", timestr, ident, line);
#endif
	fflush(file);
	return res;
}

/* _mutex must be held when calling this function. */
static void
openlog_nolock(const char *ident, int option, int facility) {
	if (ident) {
		/* It happens that we're passed garbage as ident. Also disallow / */
		const char *ip;

		for (ip = ident ; *ip ; ip++) {
			if (*ip < 0x20 || *ip == 0x7F || *ip == '/') {
				ident = NULL;
				break;
			}
		}
	}

	if (!ident || !*_ident || strcmp(ident, _ident) != 0) {
		if (_log && _pid != getpid()) {
			fclose(_log);
		}
		if (ident && *ident) {
			snprintf(workbuf, sizeof(workbuf)-1, "%s/%s.log", syslogroot, ident);
			_log = fopen(workbuf, "a");
			if (!_log) {
				snprintf(workbuf, sizeof(workbuf)-1, "%s/%s.log", syslogroot, defaultlog);
				_log = fopen(workbuf, "a");
			}
		} else {
			snprintf(workbuf, sizeof(workbuf)-1, "%s/%s.log", syslogroot, defaultlog);
			_log = fopen(workbuf, "a");
		}
		_pid = getpid();
	}
	snprintf(_ident, sizeof(_ident)-1, "%s", ident);
}

void
openlog(const char *ident, int option, int facility) {
	pthread_mutex_lock(&_mutex);
	openlog_nolock(ident, option, facility);
	pthread_mutex_unlock(&_mutex);
}

void
closelog(void) {
	pthread_mutex_lock(&_mutex);
	if (_log)
		fclose(_log);
	_log = NULL;
	pthread_mutex_unlock(&_mutex);
}

static const char *
reformat(const char *fmt, char *buf, size_t buflen) {
	const char *f;
	char errbuf[256];
	int offs[3];
	int noffs = 0;
	char *b, *e;

	for (f = fmt ; *f && noffs < 3 ; f++) {
		if (*f == '%') {
			if (*(f + 1) == 'm') {
				offs[noffs++] = f - fmt;
			} else if (*(f + 1)) {
				f++;
			}
		}
	}

	if (!noffs)
		return fmt;

	errbuf[0] = '\0';
#ifdef __GLIBC__
	const char *err = strerror_r(errno, errbuf, sizeof(errbuf));
	if (err != errbuf)
		snprintf(errbuf, sizeof(errbuf), "%s", err);
#else
	strerror_r(errno, errbuf, sizeof(errbuf));
#endif
	e = errbuf + strlen(errbuf);
	for (b = errbuf ; e - errbuf < (signed)sizeof(errbuf) - 1 && b < e ; b++) {
		if (*b == '%') {
			memmove(b + 1, b, e - b + 1);
			e++;
			b++;
		}
	}

	switch (noffs) {
	case 1:
/* Work around in-file replacing %s% */
#define S "%s"
		snprintf(buf, buflen, "%.*s" S S, offs[0], fmt, errbuf, fmt + offs[0] + 2);
		break;
	case 2:
		snprintf(buf, buflen, "%.*s" S "%.*s" S S, offs[0], fmt, errbuf, offs[1] - offs[0] - 2, fmt + offs[0] + 2, errbuf, fmt + offs[1] + 2);
		break;
	case 3:
		snprintf(buf, buflen, "%.*s" S "%.*s" S "%.*s" S S, offs[0], fmt, errbuf, offs[1] - offs[0] - 2, fmt + offs[0] + 2, errbuf, offs[2] - offs[1] - 2, fmt + offs[1] + 2, errbuf, fmt + offs[2] + 2);
		break;
#undef S
	default:
		return fmt;
	}
	return buf;
}

void
vsyslog(int priority, const char *format, va_list ap) {
	char *line = NULL;
	char fmtbuf[1024];

	pthread_mutex_lock(&_mutex);
	if ((LOG_MASK(priority) & _mask) == 0)
		goto out;

	if (_pid != getpid()){
		char ident[129] = {0};
		// fake a new identity to force renewing the file descriptor
		snprintf(ident, sizeof(ident)-1, "%s", _ident);
		_ident[0] = '\0';
		openlog_nolock(ident, 0, 0);
	}

	if (!_log)
		openlog_nolock("", 0, 0);

	if (!_log)
		goto out;

	if (vasprintf(&line, reformat(format, fmtbuf, sizeof(fmtbuf)), ap) >= 0) {
		writelogline(_log, _ident, line);
		free(line);
	}
 out:
	pthread_mutex_unlock(&_mutex);
}

void
__vsyslog_chk(int priority, int flag, const char *format, va_list ap) {
	/* glibc would use __vfprintf_chk if flag is -1. We ignore it. */
	vsyslog(priority, format, ap);
}

void
syslog(int priority, const char *format, ...) {
	va_list ap;

	va_start(ap, format);
	vsyslog(priority, format, ap);
	va_end(ap);
}

/* logger uses this function. */
void
__syslog_chk(int priority, int flag, const char *format, ...) {
	/* XXX implement flag, probably have to check in glibc sources */
	va_list ap;

	va_start(ap, format);
	vsyslog(priority, format, ap);
	va_end(ap);
}

int
vsyslog_ident(const char *ident, const char *format, va_list ap) {
	char *line = NULL;
	FILE *f;
	char fmtbuf[1024];
	int res = 0;

	pthread_mutex_lock(&_mutex);
	f = _log;
	if (!ident || !*ident)
		goto out;

	/* Reuse existing file-handle if open and same ident, else open new file */
	if (!_log || strcmp(ident, _ident) != 0) {
		snprintf(workbuf, sizeof(workbuf)-1, "%s/%s.log", syslogroot, ident);
		f = fopen(workbuf, "a");
		if (!f) {
			snprintf(workbuf, sizeof(workbuf)-1, "%s/%s.log", syslogroot, defaultlog);
			f = fopen(workbuf, "a");
		}
		if (!f)
			goto out;
	}

	if (vasprintf(&line, reformat(format, fmtbuf, sizeof(fmtbuf)), ap) >= 0) {
		res = writelogline(f, ident, line);
		free(line);
	}
	if (f != _log)
		fclose(f);

 out:
	pthread_mutex_unlock(&_mutex);
	return res;
}

int
setlogmask(int mask)
{
	pthread_mutex_lock(&_mutex);
	int prevmask = _mask;
	_mask = mask;
	pthread_mutex_unlock(&_mutex);
	return prevmask;
}


int
syslog_ident(const char *ident, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int res = vsyslog_ident(ident, format, ap);
	va_end(ap);

	return res;
}

void sysloghook_setup(void) __attribute__((constructor));

void sysloghook_setup(void) {
	const char *dir;
	char buf[PATH_MAX];
	char *bp;
	char *root;

	/*
	 * Heuristics to figure out where to log.
	 * If in platform try to use platform/logs.
	 * Else use regress_final/logs.
	 * If not possible use /tmp/$USER-regress-logs
	 */

	syslogroot = getenv("SYSLOGROOT");
	if (syslogroot) {
		if (!mkdir(syslogroot, 0777) || errno == EEXIST) {
			syslogroot = strdup(syslogroot);
			syslog_ident("sysloghook", "Using 'SYSLOGROOT' => '%s'", syslogroot);
			return;
		}
	}

#if 0
	const char **ekey;
	for (ekey = (const char *[]){ "REGRESSDIR", /*"BDIR",*/ /*"TOPDIR",*/ NULL } ; *ekey ; ekey++) {
		dir = getenv(*ekey);
		if (dir) {

			asprintf(&root, "%s/regress_final/logs", dir);
			if (!mkdir(root, 0777) || errno == EEXIST) {
				syslogroot = root;
				syslog_ident("sysloghook", "Using %s '%s'", *ekey, syslogroot);
				setenv("SYSLOGROOT", syslogroot, 0);
				return;
			}
			free(root);
			asprintf(&root, "%s/logs", dir);
			if (!mkdir(root, 0777) || errno == EEXIST) {
				syslogroot = root;
				syslog_ident("sysloghook", "Using %s '%s'", *ekey, syslogroot);
				setenv("SYSLOGROOT", syslogroot, 0);
				return;
			}
			free(root);
		}
	}
#endif

	bp = getcwd(buf, sizeof(buf));
	if (bp) {
		char *sd;
		char *pp;
		char *np;

		sd = strstr(bp, "/regress_final");
		if (sd) {
			*sd = '\0';
			if (asprintf(&root, "%s/regress_final/logs", bp) >= 0) {
				if (!mkdir(root, 0777) || errno == EEXIST) {
					syslogroot = root;
					*sd = '/';
					syslog_ident("sysloghook", "Using CWD '%s' => '%s'", bp, syslogroot);
					setenv("SYSLOGROOT", syslogroot, 0);
					return;
				}
				free(root);
			}
			*sd = '/';
		}

		/*
		 * If we have <something>/regress and <something>/build, we want the log
		 * directory to be at the same level: <something>/log
		 *
		 * If we have <something>/a/regress and <something>/build, we want the log
		 * directory to be at the "build" level: <something>/log
		 */
		pp = strstr(bp, "/regress");
		while (pp && (np = strstr(pp + 1, "/regress")))
			pp = np;
		if (pp) {
			char restore = *pp;
			char *build_dir;
			*pp = '\0';

			if (asprintf(&build_dir, "%s/build", bp) >= 0) {
				if (access(build_dir, W_OK) == 0) {
					sd = pp;
				} else {
					free(build_dir);
					build_dir = NULL;
					if (asprintf(&build_dir, "%s/../build", bp) >= 0) {
						if (access(build_dir, W_OK) == 0) {
							sd = strrchr(bp, '/');
						}
					}
				}
				free(build_dir);
			}
			*pp = restore;
		}
		if (!sd) {
			sd = strstr(bp, "/build");
			while (sd && (np = strstr(sd + 1, "/build")))
				sd = np;
		}
		if (sd) {
			char restore = *sd;

			*sd = '\0';
			if (asprintf(&root, "%s/logs", bp) >= 0) {
				if (!mkdir(root, 0777) || errno == EEXIST) {
					syslogroot = root;
					*sd = restore;
					syslog_ident("sysloghook", "Using CWD '%s' => '%s'", bp, syslogroot);
					setenv("SYSLOGROOT", syslogroot, 0);
					return;
				}
				free(root);
			}
			*sd = restore;
		}

		if (access("regress_final", W_OK) == 0) {
			if (asprintf(&root, "%s/regress_final/logs", bp) >= 0) {
				if (!mkdir(root, 0777) || errno == EEXIST) {
					syslogroot = root;
					syslog_ident("sysloghook", "Using CWD '%s' => '%s'", bp, syslogroot);
					setenv("SYSLOGROOT", syslogroot, 0);
					return;
				}
				free(root);
			}
		}
	}
	
	dir = getenv("HOME");
	if (dir) {
		if (asprintf(&root, "%s/logs", dir) >= 0) {
			if (!mkdir(root, 0777) || errno == EEXIST) {
				syslogroot = root;
				syslog_ident("sysloghook", "Using HOME '%s' (cwd was '%s')", syslogroot, bp);
				return;
			}
			free(root);
		}
	}

	dir = getenv("USER");
	if (!dir) {
		snprintf(buf, sizeof(buf), "%d", getuid());
		dir = buf;
	}

	if (asprintf(&root, "/tmp/%s-regress-logs", dir) >= 0) {
		if (!mkdir(root, 0777) || errno == EEXIST) {
			syslogroot = root;
			syslog_ident("sysloghook", "Using fallback '%s'", syslogroot);
			return;
		}
		free(root);
	}

	abort();
}

void sysloghook_teardown(void) __attribute__((destructor));
void sysloghook_teardown(void){
	closelog();
}

/* Expose functions with a sysloghook_ prefix for dlsym'ing when the normal symbols are shadowed */

void sysloghook_openlog(const char *ident, int option, int facility) __attribute__ ((alias("openlog")));
void sysloghook_syslog(int priority, const char *format, ...) __attribute__ ((alias("syslog")));
void sysloghook_vsyslog(int priority, const char *format, va_list ap) __attribute__ ((alias("vsyslog")));
void sysloghook_closelog(void) __attribute__ ((alias("closelog")));
// int  sysloghook_setlogmask(int mask) __attribute__ ((alias("setlogmask")));

int  sysloghook_vsyslog_ident(const char *ident, const char *format, va_list ap) __attribute__ ((alias("vsyslog_ident")));

