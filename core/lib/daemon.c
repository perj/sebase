// Copyright 2018 Schibsted

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <pwd.h>
#ifdef __GLIBC__
#include <sys/prctl.h>
#endif
#include <sys/resource.h>
#include <sys/poll.h>

#include "daemon.h"
#include "sbp/http.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sbp/plog.h"
#include "sbp/queue.h"
#include "sbp/sock_util.h"

char *pidfile;
char *switchuid;
char *healthcheck_url;
size_t coresize;
int quick_start;
int respawn_delay_min = 2;
int respawn_delay_max = 2;
int healthcheck_interval_s;
int healthcheck_unavail_ms;
int healthcheck_unavail_limit;
float respawn_delay_backoff_rate = 1.0f;

bool startup_wait = false;
static int startup_wait_timeout_ms = 5000;
static int pfd[2];

int hup = 0;
static void bos_sighup(int signum) {
	hup = 1;
}

int usr1 = 0;
static void bos_sigusr1(int signum) {
	usr1 = 1;
}

int usr2 = 0;
static void bos_sigusr2(int signum) {
	usr2 = 1;
}

int term = 0;
static void bos_sigterm(int signum) {
	term = 1;
}

int alrm = 0;
static void bos_sigalrm(int signum) {
	alrm = 1;
}

static void
cleanup(void) {
	if (pidfile) {
		int fd = open(pidfile, O_RDONLY);
		char pidbuf[13];

		if (fd >= 0) {
			ssize_t len = read(fd, pidbuf, sizeof(pidbuf) - 1);

			close (fd);

			if (len >= 0) {
				pidbuf[len] = 0;
				if (atoi(pidbuf) == getpid())
					unlink(pidfile);
			}
		}

		free(pidfile);
	}
	free(healthcheck_url);
	free(switchuid);
}

void
set_startup_wait(void) {
	startup_wait = true;
}

void
set_startup_wait_timeout_ms(int timeout) {
	startup_wait_timeout_ms = timeout;
}

static void
set_startup_wait_cleanup(void) {
	if (!startup_wait)
		return;

	startup_wait = false;
	close(pfd[0]);
	close(pfd[1]);
}

void set_pidfile(const char *path) {
	if (pidfile)
		free(pidfile);
	pidfile = xstrdup(path);
}

void write_pidfile(void) {
	if (!pidfile)
		return;
	int fd;
	char pidbuf[13];

	if ((fd = open(pidfile, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
		xerr(1, "open(pidfile %s)", pidfile);
	sprintf(pidbuf, "%d\n", (int)getpid());
	UNUSED_RESULT(write(fd, pidbuf, strlen(pidbuf)));
	close(fd);
}

void set_switchuid(const char *uid) {
	if (switchuid)
		free(switchuid);
	switchuid = uid ? xstrdup(uid) : NULL;
}

void set_coresize(size_t sz) {
	coresize = sz;
}

void
set_quick_start(int flag) {
	quick_start = flag;
}

void
set_respawn_backoff_attrs(int min_s, int max_s, float rate) {
	respawn_delay_backoff_rate = rate;
	respawn_delay_min = min_s;
	respawn_delay_max = max_s;
}

void
do_switchuid(void) {
	if (switchuid) {
		struct passwd *pw = getpwnam(switchuid);

		if (!pw)
			xerr(1, "getpwnam(%s)", switchuid);
		if (setgid(pw->pw_gid))
			xerr(1, "setgid(%s)", switchuid);
		if (setuid(pw->pw_uid))
			xerr(1, "setuid(%s)", switchuid);
#ifdef __GLIBC__
		if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1)
			log_printf(LOG_INFO, "prctl failed");
#endif
	}
}

static struct http *
healthcheck_setup(void) {
	if (!healthcheck_url)
		return NULL;
	struct http *hc = http_create(NULL);
	if (!hc)
		xerr(1, "http_create");
	hc->url = healthcheck_url;
	hc->method = "GET";
	curl_easy_setopt(hc->ch, CURLOPT_TIMEOUT_MS, 1000l);
	return hc;
}

static int
healthcheck(struct http *hc) {
	return http_perform(hc);
}

static void
healthcheck_cleanup(struct http *hc) {
	http_free(hc);
	hc = NULL;
}

void
set_healthcheck_url(int interval_s, int unavail_interval_ms, int unavail_limit, const char *fmt, ...) {
	free(healthcheck_url);
	if (fmt == NULL) {
		healthcheck_url = NULL;
		return;
	}
	healthcheck_interval_s = interval_s;
	healthcheck_unavail_ms = unavail_interval_ms;
	healthcheck_unavail_limit = unavail_limit;
	va_list ap;
	va_start(ap, fmt);
	xvasprintf(&healthcheck_url, fmt, ap);
	va_end(ap);
}

struct bos_event_cb {
	TAILQ_ENTRY(bos_event_cb) list;
	void (*cb)(enum bos_event ev, int arg, void *cbarg);
	void *cbarg;	
};

static TAILQ_HEAD(,bos_event_cb) bos_events = TAILQ_HEAD_INITIALIZER(bos_events);

void
set_bos_cb(void (*cb)(enum bos_event ev, int arg, void *cbarg), void *cbarg) {
	struct bos_event_cb *bec = xmalloc(sizeof(*bec));
	bec->cb = cb;
	bec->cbarg = cbarg;
	TAILQ_INSERT_TAIL(&bos_events, bec, list);
}

static void
announce_event(enum bos_event ev, int arg) {
	struct bos_event_cb *bec;
	TAILQ_FOREACH(bec, &bos_events, list) {
		bec->cb(ev, arg, bec->cbarg);
	}
}

bool
bos_here_until(int *out_rc) {
	pid_t child;
	int status;
	int ret;
	int respawn = 0;
	float respawn_delay = respawn_delay_min;
	int unavail = 0;

	while (1) {
		time_t restart_time = time(NULL);
		struct sigaction sa;
		int waiterr;

		announce_event(bev_prefork, 0);

		log_enable_plog(true);

		switch ((child = fork())) {
		case -1:
			/* Fork failed */
			*out_rc = 1;
			cleanup();
			return true;
		case 0:
			announce_event(bev_postfork_child, 0);
			/* In child */
			free(pidfile);
			pidfile = NULL;
			free(healthcheck_url);
			healthcheck_url = NULL;
			signal(SIGHUP, SIG_DFL);
			signal(SIGUSR1, SIG_DFL);
			signal(SIGUSR2, SIG_DFL);
			signal(SIGINT, SIG_DFL);
			signal(SIGTERM, SIG_DFL);
			signal(SIGPIPE, SIG_IGN);
			if (respawn) {
				/* In child it's ok to use plog. */
				plog_string_printf(logging_plog_ctx(), PLOG_CRIT, "BOS restarting main in %d seconds. Attempt #%d", (int)respawn_delay, respawn);
				sleep(respawn_delay);
			} else  {
				log_printf(LOG_INFO, "(INFO) BOS starting");
			}
			return false;
		}

		log_enable_plog(false);

		/* Parent - handle signals */

		memset(&sa, 0, sizeof(sa));
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = bos_sighup;
		if (sigaction(SIGHUP, &sa, NULL))
			xerr(1, "sigaction");
		sa.sa_handler = bos_sigusr1;
		if (sigaction(SIGUSR1, &sa, NULL))
			xerr(1, "sigaction");
		sa.sa_handler = bos_sigusr2;
		if (sigaction(SIGUSR2, &sa, NULL))
			xerr(1, "sigaction");
		sa.sa_handler = bos_sigterm;
		if (sigaction(SIGTERM, &sa, NULL))
			xerr(1, "sigaction");
		if (sigaction(SIGINT, &sa, NULL))
			xerr(1, "sigaction");
		sa.sa_handler = bos_sigalrm;
		if (sigaction(SIGALRM, &sa, NULL))
			xerr(1, "sigaction");

		announce_event(bev_start, child);

		struct timespec next_cb = {0, 0};
		struct http *hc = healthcheck_setup();

		/*
		 * Forward SIGHUP, SIGUSR1, SIGUSR2 and SIGTERM to the child.
		 */
		do {
			hup = 0;
			usr1 = 0;
			usr2 = 0;
			term = 0;
			alrm = 0;

			if (hc) {
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				if (now.tv_sec > next_cb.tv_sec || (now.tv_sec == next_cb.tv_sec && now.tv_nsec >= next_cb.tv_nsec)) {
					int r = healthcheck(hc);
					if (r == 503) {
						if (++unavail > healthcheck_unavail_limit) {
							announce_event(bev_healthcheck, r);
							unavail = 0;
						}
					} else {
						unavail = 0;
						announce_event(bev_healthcheck, r);
					}
					next_cb = now;
					if (r < 200 || r >= 400) {
						next_cb.tv_sec += healthcheck_unavail_ms / 1000;
						next_cb.tv_nsec += (healthcheck_unavail_ms % 1000) * 1000000;
						if (next_cb.tv_nsec >= 1000000000) {
							next_cb.tv_sec++;
							next_cb.tv_nsec -= 1000000000;
						}
					} else {
						next_cb.tv_sec += healthcheck_interval_s;
					}
				}
				now.tv_sec = next_cb.tv_sec - now.tv_sec;
				if (now.tv_nsec > next_cb.tv_nsec) {
					now.tv_sec--;
					now.tv_nsec += 1000000000;
				}
				now.tv_nsec = next_cb.tv_nsec - now.tv_nsec;
				setitimer(ITIMER_REAL, &(struct itimerval){ .it_value = { .tv_sec = now.tv_sec, .tv_usec = now.tv_nsec / 1000 } }, NULL);
			}
			ret = waitpid(child, &status, 0);
			waiterr = errno;
			if (ret == -1 && errno == EINTR)
				log_printf(alrm ? LOG_DEBUG : LOG_INFO, "BOS signalled (%s)", hup ? "hup" : usr1 ? "usr1" : usr2 ? "usr2" : term ? "term" : alrm ? "alrm" : "unknown");
			if (hup) {
				if (kill(child, SIGHUP))
					log_printf(LOG_WARNING, "kill(%d, SIGHUP): %m", child);
			}
			if (usr1) {
				if (kill(child, SIGUSR1))
					log_printf(LOG_WARNING, "kill(%d, SIGUSR1): %m", child);
			}
			if (usr2) {
				if (kill(child, SIGUSR2))
					log_printf(LOG_WARNING, "kill(%d, SIGUSR2): %m", child);
			}
			if (term) {
				if (kill(child, SIGTERM))
					log_printf(LOG_WARNING, "kill(%d, SIGTERM): %m", child);
			}
			if (alrm) {
				next_cb.tv_sec = 0;
			}
		} while (ret == -1 && waiterr == EINTR && term == 0 && (hup || usr1 || usr2 || alrm));

		healthcheck_cleanup(hc);
		set_startup_wait_cleanup();

		if (ret == -1 && waiterr == EINTR && term) {
			int stat = 0;
			int res;
			int done = 0;
			res = kill(child, SIGTERM);

			announce_event(bev_exit_ok, SIGTERM);

			while (!done) {
				stat++;
				switch ((res = waitpid(child, &status, WNOHANG))) {
				case -1:
					*out_rc = 0;
					cleanup();
					return true;
				case 0:
					switch (stat) {
					case 1:
					case 2:
					case 3:
					case 4:
					case 5:
						sleep(1);
						break;
					case 6:
						log_printf(LOG_INFO, "Child nonresponsive, sending SIGINT");
						fprintf(stderr, "Child nonresponsive, sending SIGINT\n");
						kill(child, SIGINT);
						sleep(20);
						break;
					case 7:
					default:
						log_printf(LOG_WARNING, "Child nonresponsive, sending SIGKILL");
						fprintf(stderr, "Child nonresponsive, sending SIGKILL\n");
						kill(child, SIGKILL);
						sleep(1);
						break;
					}
					break;
				default:
					*out_rc = 0;
					cleanup();
					return true;
				}
			}
		} else {
			if (WIFEXITED(status)) {
				/* Child exited normally */
				log_printf(LOG_INFO, "BOS Child %u exit status: %d", ret, WEXITSTATUS(status));
				if (WEXITSTATUS(status) == 0) {
					announce_event(bev_exit_ok, 0);
					*out_rc = 0;
					cleanup();
					return true;
				}
				announce_event(bev_exit_bad, WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				/* Child terminated by signal */
				log_printf(LOG_CRIT, "BOS Child %u term signal: %s (%d)", ret, strsignal(WTERMSIG(status)), WTERMSIG(status));
				announce_event(bev_crash, WTERMSIG(status));
			}

#ifdef WCOREDUMP
			if (WCOREDUMP(status)) {
				/* Child dumped core */
				log_printf(LOG_INFO, "BOS Child %u dumped core", ret);
			}
#endif
		}

		time_t time_client_lifetime = time(NULL) - restart_time - respawn_delay; /* Subtract delay we know was spent sleeping */

		/* If we fail at the first attempt, probably operator near. */
		if (respawn == 0 && time_client_lifetime <= 5) {
			log_printf(LOG_CRIT, "Child died within 5 seconds, shutting down BOS");
			announce_event(bev_quick_exit, 0);
			*out_rc = 1;
			cleanup();
			return true;
		}

		if ((int)respawn_delay != respawn_delay_min && time_client_lifetime >= 60*5) {
			log_printf(LOG_INFO, "Child lived longer than five minutes, resetting restart delay from %d to %d", (int)respawn_delay, respawn_delay_min);
			respawn_delay = respawn_delay_min;
		}

		if (respawn_delay*respawn_delay_backoff_rate >= respawn_delay_max)
			respawn_delay = respawn_delay_max;
		else
			respawn_delay *= respawn_delay_backoff_rate;

		respawn++;

	}
}

void
bos_here(void) {
	int rc;
	if (bos_here_until(&rc))
		exit(rc);
}

int
bos(int (*func)(void)) {
	bos_here();
	int rc = func();
	cleanup();
	exit(rc);
}

void
startup_ready(const char *daemon_id) {
	if (!startup_wait)
		return;

	close(pfd[0]);

	char b = 1;
	if (write(pfd[1], &b, 1) != 1)
		exit(2);
	close(pfd[1]);

	startup_wait = false;

	if (sd_post_message(daemon_id, "READY=1") < (ssize_t)sizeof("READY=1")) {
		log_printf(LOG_WARNING,"startup_ready: Failed to post READY to daemon handler");
	}

}

bool
daemonify_here_until(bool nobos, int *out_rc) {
	pid_t child;
	int fd;
	int status;

	if (startup_wait) {
		if (pipe(pfd) == -1)
			exit(1);
	}

	switch (child = fork()) {
	case -1:
		xerr(1, "fork");
	case 0:
		break;
	default:
		/*
		 * The main process forks out here.
		 * Sleep a while and check that child is still running.
		 */
		if (pidfile)
			free(pidfile);

		if (!quick_start)
			sleep(5);
		if (waitpid(child, &status, WNOHANG) > 0) {
			if (WIFEXITED(status))
				exit(WEXITSTATUS(status));
			exit(1);
		}

		/*
		 * We have to guarantee that it will not return successfully
		 * unless notified. If we get a timeout then we assume failure
		 * and kill the child.
		 */
		if (startup_wait) {
			struct pollfd pollfd = { .fd = pfd[0], .events = POLLIN };
			int res;

			close(pfd[1]);
			res = poll(&pollfd, 1, startup_wait_timeout_ms);
			if (res == 0) {
				kill(child, SIGTERM);
				exit(175); /* timeout */
			} else if (res == -1) {
				kill(child, SIGTERM);
				exit(errno);
			}

			char b;
			if (read(pfd[0], &b, 1) != 1) {
				kill(child, SIGTERM);
				exit(174);
			}
			close(pfd[0]);
		}

		exit(0);
	}

	if (pidfile) {
		write_pidfile();
	}

	// 'change_user' is used to enable/disable this part.... 
	if (switchuid) {
		do_switchuid();
	}

	if (coresize) {
		struct rlimit rlim;

		if (getrlimit(RLIMIT_CORE, &rlim) == -1)
			xerr(1, "getrlimit()");
		rlim.rlim_cur = coresize;
		if (setrlimit(RLIMIT_CORE, &rlim) == -1)
			xerr(1, "setrlimit()");
	}

	if ((fd = open("/dev/null", O_RDWR, 0)) == -1)
		xerr(1, "open(/dev/null)");

	if (setsid() == -1)
		xerr(1, "setsid");

	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);

	if (fd > 2)
		close(fd);

	if (!nobos)
		return bos_here_until(out_rc);
	return false;
}

void
daemonify_here(bool nobos) {
	int rc;
	if (daemonify_here_until(nobos, &rc))
		exit(rc);
}

void
daemonify(int nobos, int (*func)(void)) {
	daemonify_here(nobos);
	int rc = func();
	cleanup();
	exit(rc);
}
