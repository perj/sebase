// Copyright 2018 Schibsted

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>

#include "sbp/daemon.h"
#include "sbp/error_functions.h"

static const char *daemon_id = "test_daemon_startup";

static int
allgood(void) {
	startup_ready(daemon_id);

	return 0;
}

static int
timeout(void) {
	sleep(1);

	return 0;
}

static int
died(void) {
	exit(1);
}

static int
notify_twice(void) {
	startup_ready(daemon_id);
	startup_ready(daemon_id);

	return 0;
}

static void
test_startup_wait(int (*func)(void), int expected) {
	pid_t child;
	int status;


	switch (child = fork()) {
	case -1:
		xerr(1, "fork failed");
	case 0:
		set_quick_start(1);
		set_startup_wait();
		set_startup_wait_timeout_ms(500);
		daemonify(true, func);
		break;
	default:
		if (waitpid(child, &status, 0) > 0) {
			int whatexit = 0;
			if (WIFEXITED(status))
				whatexit = WEXITSTATUS(status);
			assert(whatexit == expected);
		} else {
			xerr(1, "waitpid failed");
		}
	}
}

int
main(void) {
	fprintf(stderr, "Testing all ok\n");
	test_startup_wait(allgood, 0);

	fprintf(stderr, "Testing notify twice\n");
	test_startup_wait(notify_twice, 0);

	fprintf(stderr, "Testing child died before signaling\n");
	test_startup_wait(died, 174);

	fprintf(stderr, "Testing child timed out\n");
	test_startup_wait(timeout, 175);

	return 0;
}
