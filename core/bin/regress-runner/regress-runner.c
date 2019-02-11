// Copyright 2018 Schibsted
#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#if __has_include(<sys/prctl.h>)
#include <sys/prctl.h>
#endif
#include <sys/time.h>
#include <dirent.h>


#include "sbp/buf_string.h"
#include "sbp/timer.h"
#include "sbp/bconf.h"
#include "sbp/queue.h"
#include "sbp/memalloc_functions.h"
#include "sbp/popt.h"

/* 60 seconds ought to be enough for everyone. */
#define TEST_TIMEOUT_DEFAULT 60

TAILQ_HEAD(test_case_queue,test_case);

struct test_suite {
	TAILQ_ENTRY(test_suite) link;
	struct test_case_queue depend;
	struct test_case_queue cases;
	struct test_case_queue cleanup;
	char *name;

	int succeeded;
	int failed;
	int skipped;
	struct timespec duration;
};

struct test_case {
	TAILQ_ENTRY(test_case) link;
	char *name;
	char *output;
	int skipped;
	int failure;
	struct timespec ts;
};

extern char **environ;

static const char *outdir = NULL;
static const char *makeargs = NULL;
static const char *logdir = NULL;
static const char *maindir;
static bool exit_on_error = false;
static bool only_fails = false;
static bool travis_fold = false;
static long travis_max_log = 1024;

TAILQ_HEAD(,test_suite) suites = TAILQ_HEAD_INITIALIZER(suites);

POPT_PURPOSE("Helper for running tests and generating a report.");
POPT_STRING("outdir", NULL, &outdir, "Path to write report to.\n"
		"Defaults to $BUILDPATH/dev/tests");
POPT_STRING("make-args", NULL, &makeargs, "Additional arguments to pass to make.");
POPT_STRING("directory", NULL, &maindir, "Run only in this directory. (recursively)");
POPT_BOOL("only-fails", false, &only_fails, "Only write report on failures.");
POPT_STRING("logdir", NULL, &logdir, "Log files will be moved from here.\n"
		"The SYSLOGROOT environment variable will be set to this value if unset.\n"
		"Defaults to $BUILDPATH/dev/logs");
POPT_BOOL("travis-fold", false, &travis_fold, "Print failed tests to stdout with travis folds.");
POPT_NUMBER("travis-max-log", 1024, &travis_max_log, "Maximum number of kbytes to include in log extract.");
POPT_BOOL("exit-on-error", false, &exit_on_error, "Stop running suites on the first error.");

static char *
normalize_path(char *p) {
	static char pwd[MAXPATHLEN];
	static size_t pl;

	if (!pl) {
		if (getcwd(pwd, sizeof(pwd)) == NULL) {
			err(1, "getcwd");
		}
		pl = strlen(pwd);
		if (pl >= sizeof(pwd) - 1)
			errx(1, "pwd too long");
		pwd[pl] = '/';
		pwd[pl+1] = '\0';
		pl++;
	}
	if (!strncmp(p, pwd, pl))
		return strdup(p + pl);
	return strdup(p);
}

volatile sig_atomic_t chld;
volatile sig_atomic_t quit;
volatile sig_atomic_t alrm;

/*
 * We need to handle SIGCHLD and close the pipe forcibly because processes that
 * inherit stdin/stdout/stderr from the pipe can still be running.
 */
static void
sig_handler(int signum) {
	switch (signum) {
	case SIGCHLD:
		chld = 1;
		break;
	case SIGALRM:
		alrm = 1;
		break;
	default:
		quit = 1;
	}
}

static void
out(FILE *f, struct bconf_node *root) {
	const char *name = bconf_get_string(root, "name") ?: "";
	fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n");
	//XXX update the url and re-enable
	//fprintf(f, "<?xml-stylesheet href=\"https://github.com/schibsted/sebase/.../testfile.css\" ?>\n");
	fprintf(f, "<testsuite failures=\"%d\" time=\"%s\" errors=\"0\" skipped=\"%d\" tests=\"%d\" name=\"%s\">\n",
			bconf_get_int(root, "failed"),
			bconf_get_string(root, "duration"),
			bconf_get_int(root, "skipped"),
			bconf_get_int(root, "succeeded") + bconf_get_int(root, "skipped") + bconf_get_int(root, "failed"),
			name);
	struct bconf_node *cases = bconf_get(root, "case");
	int n = bconf_count(cases);
	for (int i = 0 ; i < n ; i++) {
		struct bconf_node *node = bconf_byindex(cases, i);
		const char *result = bconf_get_string(node, "result") ?: "";
		if (strcmp(result, "success") == 0) {
			fprintf(f, "    <testcase time=\"%s\" classname=\"%s.%s\" name=\"%s\"><output xml:space=\"preserve\"><![CDATA[%s]]></output></testcase>\n",
					bconf_get_string(node, "duration"),
					name, bconf_get_string(node, "type"),
					bconf_get_string(node, "name"),
					bconf_get_string(node, "output"));
		} else if (strcmp(result, "failed") == 0) {
			fprintf(f, "    <testcase time=\"%s\" classname=\"%s.%s\" name=\"%s\">\n",
					bconf_get_string(node, "duration"),
					name, bconf_get_string(node, "type"),
					bconf_get_string(node, "name"));
			// XXX might need to massage output first. Template cleaned out esc and converted latin1 => utf8
			fprintf(f, "        <failure message=\"%s\" type=\"fail\" xml:space=\"preserve\"><![CDATA[%s]]></failure>\n",
					bconf_get_string(node, "reason"), bconf_get_string(node, "output"));
			fprintf(f, "    </testcase>\n");
		} else if (strcmp(result, "skipped") == 0) {
			fprintf(f, "    <testcase time=\"%s\" classname=\"%s.%s\" name=\"%s\">\n",
					bconf_get_string(node, "duration"),
					name, bconf_get_string(node, "type"),
					bconf_get_string(node, "name"));
			fprintf(f, "        <error xml:space=\"preserve\"><![CDATA[%s]]></error>\n",
					bconf_get_string(node, "reason"));
			fprintf(f, "    </testcase>\n");
		}
	}
	if (bconf_get_int(root, "failed") > 0) {
		struct bconf_node *logs = bconf_get(root, "logs");
		n = bconf_count(logs);
		for (int i = 0 ; i < n ; i++) {
			fprintf(f, "    <log name=\"%s\" xml:space=\"preserve\"><![CDATA[%s]]></log>\n",
					bconf_key(bconf_byindex(logs, i)),
					bconf_value(bconf_byindex(logs, i)));
		}
	}
	fprintf(f, "</testsuite>\n");
}

static void
travis(struct bconf_node *root) {
	char *name = strdupa(bconf_get_string(root, "name") ?: "");
	char *ptr;
	while ((ptr = strchr(name, '/')))
		*ptr = '-';
	printf("travis_fold:start:%s\n", name);
	printf("Suite %s\n", bconf_get_string(root, "name"));
	struct bconf_node *cases = bconf_get(root, "case");
	int n = bconf_count(cases);
	for (int i = 0 ; i < n ; i++) {
		struct bconf_node *node = bconf_byindex(cases, i);
		printf("%s %s (%s)\n\n",
		       bconf_get_string(node, "type"),
		       bconf_get_string(node, "name"),
		       bconf_get_string(node, "result"));
		printf("%s\n", bconf_get_string(node, "output") ?: "");
	}
	printf("Logs\n");
	struct bconf_node *logs = bconf_get(root, "logs");
	n = bconf_count(logs);
	for (int i = 0 ; i < n ; i++) {
		printf("travis_fold:start:%s\n", bconf_key(bconf_byindex(logs, i)));
		printf("%s\n", bconf_value(bconf_byindex(logs, i)));
		printf("travis_fold:end:%s\n", bconf_key(bconf_byindex(logs, i)));
	}
	printf("travis_fold:end:%s\n", name);
}

static int
make(const char *dir, const char *target, const char *args, char **retbuf) {
	struct itimerval itv = { {0, 0}, {0, 0} };
	size_t rbs = 0;
	size_t rp = 0;
	int status = -1;
	int fds[2];
	char *cmd;
	pid_t pid;
	ssize_t r;

	if (pipe(fds))
		err(1, "pipe");

	chld = 0;
	switch ((pid = fork())) {
	case -1:
		err(1, "fork");
	case 0:
		xasprintf(&cmd, "$(command -v gmake || echo make) -C %s %s %s 2>&1", dir, target, args ? args : "");
		close(fds[0]);
		if (dup2(fds[1], 1) == -1) {
			warn("dup2(1)");
			_exit(1);
		}
		if (dup2(fds[1], 2) == -1) {
			warn("dup2(2)");
			_exit(1);
		}
		execlp("sh", "sh", "-c", cmd, NULL);
		warn("execlp");
		_exit(1);
	default:
		break;
	}

	close(fds[1]);

	itv.it_value.tv_sec = TEST_TIMEOUT_DEFAULT;
	if (setitimer(ITIMER_REAL, &itv, NULL))
		err(1, "setitimer");

	bool running = true;
	rbs = 4096;
	if ((*retbuf = malloc(rbs)) == NULL)
		err(1, "malloc");
	do {
		if (rp == rbs) {
			rbs *= 2;
			if ((*retbuf = realloc(*retbuf, rbs)) == NULL)
				err(1, "realloc");
		}
		r = read(fds[0], *retbuf + rp, rbs - rp);
		if (r > 0)
			rp += r;
		if (chld) {
			chld = 0;
			pid_t epid;
			int wst;
			while ((epid = waitpid(-1, &wst, WNOHANG)) > 0) {
				if (epid == pid) {
					running = false;
					status = wst;
				}
			}
		}
	} while (!alrm && running && r != 0);

	if (rp == rbs) {
		rbs *= 2;
		if ((*retbuf = realloc(*retbuf, rbs)) == NULL)
			err(1, "realloc");
	}
	(*retbuf)[rp] = '\0';

	close(fds[0]);
	itv.it_value.tv_sec = 0;
	if (setitimer(ITIMER_REAL, &itv, NULL))
		err(1, "setitimer 2");

	if (alrm) {
		alrm = 0;
		kill(pid, SIGKILL);
	}

	if (running) {
		pid_t epid;
		int wst;
		while ((epid = wait(&wst)) > 0 || (epid == -1 && errno == EINTR)) {
			if (epid == pid) {
				status = wst;
				break;
			}
		}

		if (epid == -1)
			err(1, "wait");
	}

	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int status_active;
static int ontty;
static int tss;
static int cts;

static void
status_printfnl(const char *fmt, ...) {
	va_list ap;

	if (ontty && status_active) {
		printf("\r\x1B[K");
		fflush(stdout);
		if (cts)
			printf("(Suite %d/%d) ", cts, tss);
	}
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	status_active = 1;
	if (!ontty) {
		printf("\n");
	}
	fflush(stdout);
}

static void
status_printf(const char *fmt, ...) {
	va_list ap;

	if (ontty && status_active) {
		printf("\r\x1B[K");
		fflush(stdout);
		if (cts)
			printf("(Suite %d/%d) ", cts, tss);
	}
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	status_active = 1;
	if (!ontty) {
		printf("\n");
	}
	fflush(stdout);
}

static void
status_errf(const char *fmt, ...) {
	va_list ap;

	if (ontty && status_active) {
		printf("\n");
	}
	if (ontty)
		printf("\x1b[31m");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	status_active = 0;
	if (ontty)
		printf("\x1b[0m");
	printf("\n");
}

static void
collect_suites(const char *pdir, const char *sdir) {
	char *dir;
	if (!sdir) {
		dir = xstrdup(pdir);
		int l = strlen(dir);
		while (l > 0 && dir[l - 1] == '/')
			dir[--l] = '\0';
	} else if (strcmp(pdir, ".") == 0) {
		dir = xstrdup(sdir);
	} else {
		xasprintf(&dir, "%s/%s", pdir, sdir);
	}
	DIR *d = opendir(dir);
	if (!d)
		xerr(1, "Failed to scan directory: %s", dir);
	struct dirent *ent;
	while ((ent = readdir(d))) {
		// Skip ., ..
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;

		bool checkname = false;
		bool fallback = false;
#ifdef DT_REG
		switch (ent->d_type) {
		case DT_REG:
		case DT_LNK:
			checkname = true;
			break;
		case DT_DIR:
			collect_suites(dir, ent->d_name);
			break;
		case DT_UNKNOWN:
			fallback = true;
			break;
		}
#else
		fallback = true;
#endif
		if (fallback) {
			// Fallback DT_UNKNOWN or no d_type. Use lstat.
			struct stat st;
			if (fstatat(dirfd(d), ent->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
				xwarn("Failed to stat %s/%s", dir, ent->d_name);
				continue;
			}
			switch (st.st_mode & S_IFMT) {
			case S_IFDIR:
				collect_suites(dir, ent->d_name);
				break;
			case S_IFLNK:
			case S_IFREG:
				checkname = true;
				break;
			}
		}
		if (checkname && strcmp(ent->d_name, "regress-runner.mk") == 0) {
			struct test_suite *cursuite = calloc(1, sizeof(*cursuite));
			TAILQ_INIT(&cursuite->depend);
			TAILQ_INIT(&cursuite->cases);
			TAILQ_INIT(&cursuite->cleanup);
			cursuite->name = normalize_path(dir);
			TAILQ_INSERT_TAIL(&suites, cursuite, link);
			tss++;
		}
	}
	closedir(d);
	free(dir);
}

static void
generate_tests(const char *dir) {
	struct test_suite *cursuite = NULL;

	status_printfnl("generating list of tests");
	collect_suites(dir, NULL);
	status_printfnl("tests generated");

	struct buf_string buf = {0};
	TAILQ_FOREACH(cursuite, &suites, link) {
		char *mout;
		buf.pos = 0;
		bscat(&buf, "-s -f regress-runner.mk %s", makeargs ?: "");
		if (make(cursuite->name, "print-tests", buf.buf, &mout)) {
			status_errf("%s: make print-tests failed", cursuite->name);
			exit(1);
		}

		char *out = mout;
		char *lp;
		while ((lp = strsep(&out, "\n"))) {
			struct test_case *c;
			char *cmd;
			char *s;

			while (isspace(*lp))
				lp++;
			if (*lp == '\0')
				continue;
			if ((cmd = strchr(lp, ' ')) == NULL) {
				status_errf("invalid line in %s: %s\n", cursuite->name, lp);
				exit(1);
			}
			*cmd++ = '\0';

			if ((s = strchr(lp, ':')))
				*s = '\0';

			struct test_case_queue *tailq;
			if (strcmp(lp, "DEPEND") == 0) {
				tailq = &cursuite->depend;
			} else if (strcmp(lp, "TEST") == 0) {
				tailq = &cursuite->cases;
			} else if (strcmp(lp, "CLEANUP") == 0) {
				tailq = &cursuite->cleanup;
			} else {
				errx(1, "invalid line: %s/%s\n", lp, cmd);
			}
			char *word;
			while ((word = strsep(&cmd, " "))) {
				if (word[0] != '\0') {
					c = calloc(1, sizeof(*c));
					c->name = strdup(word);
					TAILQ_INSERT_TAIL(tailq, c, link);
				}
			}
		}
	}
}

static int
run_case(struct test_case *tc, struct test_suite *ts, int cnum, struct bconf_node *sn, struct timer_instance *gti, const char *skip, const char *type) {
	struct timer_instance *ti;
	char cstr[64];
	int ret = 0;

	snprintf(cstr, sizeof(cstr), "%d", cnum);
	bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "name"}, tc->name, 0);
	bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "type"}, type, 0);
	if (skip == NULL) {
		char tbuf[64];
		double t;

		ti = timer_start(gti, tc->name);
		ret = make(ts->name, tc->name, "-f regress-runner.mk", &tc->output);
		timer_end(ti, &tc->ts);

		bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "output"}, tc->output, 0);

		t = (double)tc->ts.tv_sec + (double)tc->ts.tv_nsec / 1000000000.0;
		snprintf(tbuf, sizeof(tbuf), "%.3f", t);
		bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "duration"}, tbuf, 1);

		if (ret == -2) {
			bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "result"}, "failed", 0);
			bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "reason"}, "timeout", 0);
			tc->failure = 1;
			ts->failed++;
			status_errf("TIMEOUT %.4fs", t);
		} else if (ret != 0) {
			bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "result"}, "failed", 0);
			bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "reason"}, "test failure", 0);
			tc->failure = 1;
			ts->failed++;
			status_errf("FAIL %.4fs", t);
		} else {
			bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "result"}, "success", 0);
			ts->succeeded++;
			status_printfnl("OK %.4fs", t);
		}
	} else {
		ts->skipped++;
		tc->skipped = 1;
		bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "result"}, "skipped", 0);
		bconf_add_datav(&sn, 3, (const char *[]){"case", cstr, "reason"}, skip, 0);
		status_errf("SKIPPED (%s)", skip);
	}
	return ret;
}

static long
get_file_size(const char *filename) {
	struct stat st;
	if (stat(filename, &st) == 0)
		return st.st_size;

	return -1;
}

static int
run_suite(struct test_suite *ts, struct bconf_node *b) {
	struct timer_instance *gti;
	int depend_failed = 0;
	struct test_case *tc;
	char nbuf[64];
	int cnum = 0;
	double t;
	int early_quit = quit;
	int status = 0;

	gti = timer_start(NULL, ts->name);
	TAILQ_FOREACH(tc, &ts->depend, link) {
		status_printf("Depend: %.30s ", tc->name);
		if (run_case(tc, ts, cnum++, b, gti, quit ? "interrupted" : NULL, "depend")) {
			depend_failed = 1;
			status = 1;
			break;
		}
	}
	TAILQ_FOREACH(tc, &ts->cases, link) {
		status_printf("Test case %.30s ", tc->name);
		if (run_case(tc, ts, cnum++, b, gti, depend_failed ? "depend" : (quit ? "interrupted" : NULL), "test"))
			status = 1;
	}
	TAILQ_FOREACH(tc, &ts->cleanup, link) {
		status_printf("Cleanup: %.30s ", tc->name);
		run_case(tc, ts, cnum++, b, gti, early_quit ? "interrupted" : NULL, "cleanup");
	}

	int n = 0;
	int wst;
	while (waitpid(-1, &wst, WNOHANG) != -1 || errno != ECHILD) {
		if (++n > 100) {
			status_errf("Timed out waiting for children to exit.");
			status_errf("Note: Lingering process might be left.");
			status = 2;
			break;
		}
		usleep(100000);
	}
	timer_end(gti, &ts->duration);

	t = (double)ts->duration.tv_sec + (double)ts->duration.tv_nsec / 1000000000.0;
	status_printfnl("Suite duration: %.3fs", t);
	snprintf(nbuf, sizeof(nbuf), "%.3f", t*1000);
	bconf_add_data(&b, "duration", nbuf);
	snprintf(nbuf, sizeof(nbuf), "%d", ts->failed);
	bconf_add_data(&b, "failed", nbuf);
	snprintf(nbuf, sizeof(nbuf), "%d", ts->succeeded);
	bconf_add_data(&b, "succeeded", nbuf);
	snprintf(nbuf, sizeof(nbuf), "%d", ts->skipped);
	bconf_add_data(&b, "skipped", nbuf);

	DIR *log;
	if (logdir && (log = opendir(logdir))) {
		struct dirent *ent;
		while ((ent = readdir(log))) {
			if (ent->d_name[0] == '.')
				continue;

			char path[PATH_MAX];
			snprintf(path, sizeof(path), "%s/%s", logdir, ent->d_name);

			long file_size = get_file_size(path);
			if (file_size < 0)
				continue;
			FILE *f = fopen(path, "rb");
			if (!f)
				continue;

			struct buf_string buf = {0};

			int skip = 0;
			if (travis_fold && file_size > travis_max_log) {
				fseek(f, -travis_max_log, SEEK_END);
				bscat(&buf, "<INITIAL LOG SKIPPED>");
				skip = 1;
			}

			char tmp[8192];
			size_t nr;
			while ((nr = fread(tmp, 1, sizeof(tmp), f)) > 0) {
				if (skip) {
					char *nl = memchr(tmp, '\n', nr);
					if (nl)
						skip = nl - tmp;
					else
						skip = 0;
				}
				bswrite(&buf, tmp + skip, nr - skip);
				skip = 0;
			}
			fclose(f);
			unlink(path);

			if (!buf.buf)
				continue;

			bconf_add_datav(&b, 2, (const char*[]){"logs", ent->d_name}, buf.buf, BCONF_OWN);
		}
		closedir(log);
	}

	return status;
}

static void
exit_program(int level) {
	exit(level);
}

static int
parse_options(int argc, char **argv) {
	popt_parse_ptrs(&argc, &argv);
	travis_max_log *= 1024;

	return 0;
}

// Prepend directory containing argv0 to PATH.
static void
add_path(const char *argv0) {
	if (!argv0 || !strchr(argv0, '/'))
		return;

	char resolved[PATH_MAX];
	if (!realpath(argv0, resolved))
		strlcpy(resolved, argv0, sizeof(resolved));

	char *lastslash = strrchr(resolved, '/');
	if (!lastslash)
		return;
	*lastslash = '\0';

	char *newpath;
	char *p = getenv("PATH");
	if (p == NULL || p[0] == '\0')
		newpath = xstrdup(resolved);
	else
		xasprintf(&newpath, "%s:%s", resolved, p);
	setenv("PATH", newpath, true);
	free(newpath);
}

static void
setup_syslog(void) {
	if (!logdir) {
		const char *bp = getenv("BUILDPATH") ?: "build";
		const char *flavor = getenv("FLAVOR") ?: "dev";
		char *tmp;
		xasprintf(&tmp, "%s/%s/logs", bp, flavor);

		mkdir(tmp, 0777);
		char *resolved = xmalloc(PATH_MAX);
		if (!realpath(tmp, resolved))
			strlcpy(resolved, tmp, PATH_MAX);
		free(tmp);
		logdir = resolved;
		// Leaking resolved here which doesn't really matter.
	}
	if (strcmp(logdir, "") == 0) {
		logdir = NULL;
		return;
	}
	setenv("SYSLOGROOT", logdir, false);
	status_printfnl("Set SYSLOGROOT to %s", logdir);

	if (!getenv("LD_PRELOAD")) {
		const char *bp = getenv("BUILDPATH") ?: "build";
		const char *flavor = getenv("FLAVOR") ?: "dev";
		char *slhp;

		xasprintf(&slhp, "%s/%s/modules/sysloghook.so", bp, flavor);
		struct stat st;
		if (!stat(slhp, &st)) {
			char resolved[PATH_MAX];
			if (!realpath(slhp, resolved))
				strlcpy(resolved, slhp, sizeof(resolved));
			setenv("LD_PRELOAD", resolved, false);
			status_printfnl("Set LD_PRELOAD to %s", resolved);
		}
		free(slhp);
	}
}

static void
setup_outdir(void) {
	if (!outdir) {
		const char *bp = getenv("BUILDPATH") ?: "build";
		const char *flavor = getenv("FLAVOR") ?: "dev";
		char *tmp;
		xasprintf(&tmp, "%s/%s/tests", bp, flavor);

		mkdir(tmp, 0777);
		char *resolved = xmalloc(PATH_MAX);
		if (!realpath(tmp, resolved))
			strlcpy(resolved, tmp, PATH_MAX);
		free(tmp);
		outdir = resolved;
		status_printfnl("Set -outdir to %s", outdir);
		// Leaking resolved here which doesn't really matter.
	}
	if (strcmp(outdir, "") == 0)
		outdir = NULL;
}

int
main(int argc, char **argv, char **envv) {
	struct bconf_node *n, *s, *b = NULL;
	struct test_suite *ts;
	struct sigaction sa;
	int i;
	int status = 0;

	environ = envv;

	parse_options(argc, argv);

	ontty = isatty(1);

	add_path(argv[0]);
	setup_syslog();
	setup_outdir();

	generate_tests(maindir ?: ".");

#if __has_include(<sys/prctl.h>)
#ifndef PR_SET_CHILD_SUBREAPER
	/* CentOS 6 glibc, try anyway in case it's a VM on a newer kernel. */
#define PR_SET_CHILD_SUBREAPER 36
#endif
	if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0))
		warn("prctl");
#endif

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	if (sigaction(SIGCHLD, &sa, NULL))
		err(1, "sigaction");
	if (sigaction(SIGINT, &sa, NULL))
		err(1, "sigaction");
	if (sigaction(SIGQUIT, &sa, NULL))
		err(1, "sigaction");
	if (sigaction(SIGTERM, &sa, NULL))
		err(1, "sigaction");
	if (sigaction(SIGHUP, &sa, NULL))
		err(1, "sigaction");
	if (sigaction(SIGALRM, &sa, NULL))
		err(1, "sigaction");

	i = 0;
	TAILQ_FOREACH(ts, &suites, link) {
		char bkey[64];

		cts++;
		status_printfnl("SUITE: %s", ts->name);

		snprintf(bkey, sizeof(bkey), "suites.%d.name", i);
		bconf_add_data(&b, bkey, ts->name);
		snprintf(bkey, sizeof(bkey), "suites.%d", i);
		int ret = run_suite(ts, bconf_get(b, bkey));
		if (ret > status)
			status = ret;
		if (status > 1)
			break;
		if (ret && exit_on_error)
			break;
		i++;
	}
	cts = 0;

	if (!status && quit)
		status = 1;

	if (outdir == NULL) {
		status_errf("");
		exit_program(status);
	}

	s = bconf_get(b, "suites");
	for (i = 0; (n = bconf_byindex(s, i)) != NULL; i++) {
		char *ofname, *c;
		size_t dl;

		if (only_fails && bconf_get_int(n, "failed") == 0)
			continue;

		xasprintf(&ofname, "%s/%s.xml", outdir, bconf_get_string(n, "name"));
		/* We don't want the test name to have slashes. */
		dl = strlen(outdir) + 1;
		while ((c = strchr(ofname + dl, '/')))
			*c = '_';
		FILE *f = fopen(ofname, "w");
		if (!f) {
			status_errf("Failed to open %s for writing: %s", ofname, strerror(errno));
			exit(1);
		}
		if (bconf_get_int(n, "failed") > 0)
			status_errf("Writing test output for (%s) into %s (%s failed)", bconf_get_string(n, "name"), ofname, bconf_get_string(n, "failed"));
		out(f, n);
		fclose(f);

		if (travis_fold && bconf_get_int(n, "failed") > 0) {
			travis(n);
		}

		free(ofname);
	}
	status_errf("");
	exit_program(status);
}
