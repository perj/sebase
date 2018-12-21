// Copyright 2018 Schibsted

#ifndef DAEMON_H
#define DAEMON_H

#include "sbp/macros.h"

#include <stdbool.h>
#include <sys/types.h>

enum bos_event {
	bev_prefork,
	bev_postfork_child,
	bev_start,
	bev_healthcheck,
	bev_exit_ok,
	bev_exit_bad,
	bev_crash,
	bev_quick_exit,
};

void set_pidfile(const char *pidfile);
void write_pidfile(void);
void set_switchuid(const char *uid);
void set_coresize(size_t sz);
void set_quick_start(int flag);
void set_respawn_backoff_attrs(int min_s, int max_s, float rate);
void set_healthcheck_url(int interval_s, int unavail_interval_ms, int unavail_limit, const char *fmt, ...) FORMAT_PRINTF(4, 5);
void set_bos_cb(void (*)(enum bos_event ev, int arg, void *cbarg), void *cbarg);
void set_startup_wait_timeout_ms(int);
void set_startup_wait(void);
void startup_ready(const char *daemon_id);

/* Returns false in child, returns true in parent when it's time to exit.
 * When true is returned, *out_rc is the expected exit code, which will be
 * 0 except for a child early exit or in case fork failed.
 */
bool bos_here_until(int *out_rc);

void bos_here(void);
int bos(int (*)(void)) NORETURN;

void do_switchuid(void);

/* Returns value from bos_here_until, or false if nobos is set. */
bool daemonify_here_until(bool nobos, int *out_rc);

void daemonify_here(bool nobos);
void daemonify(int, int (*)(void)) NORETURN;

#endif /*DAEMON_H*/
