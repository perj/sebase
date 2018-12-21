// Copyright 2018 Schibsted

/*
	sd_start -- Start a program/daemon and wait for a systemd service READY message.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <err.h>
#include <libgen.h>
#include <inttypes.h>

#include "sbp/popt.h"
#include "sbp/sock_util.h"
#include "sbp/buf_string.h"
#include "sbp/memalloc_functions.h"
#include "sbp/string_functions.h"

static bool verbose;
static bool maybe;
static long timeout;
static const char *named_socket;
static const char *pidfile;

POPT_USAGE("[options] <program to start>");
POPT_PURPOSE("Start a program/daemon and wait for a systemd service READY message.");
POPT_SECONDS("timeout", 20, &timeout, "Timeout on waiting for data (0 to disable)");
POPT_BOOL("verbose", false, &verbose, "Verbose output.");
POPT_STRING("socket", NULL, &named_socket, "Socket @abstract|/path/file.sock. If not given, we generate a random abstract socket name.");
POPT_STRING("pidfile", NULL, &pidfile, "Write pid to this file after getting the notification.");
POPT_BOOL("maybe", false, &maybe, "If there is a timeout and the program is still running, consider it a successful start even if nothing was received on the socket.");

static int
wait_for_ready(int fd) {

	if (verbose)
		printf("Waiting for READY=1 on %s (fd=%d)...\n", named_socket, fd);

	int retval = 1;
	struct buf_string res = { 0 };
	char buf[1024] = { 0 };
	ssize_t r;
	while ((r = read(fd, buf, sizeof(buf))) > 0) {
		bswrite(&res, buf, (int)r); /* nul-terminates */
		if (strstr(res.buf, "READY=1") != 0) {
			retval = 0;
			break;
		}
	}
	close(fd);

	if (retval != 0 && r < 0) {
		if (errno == EAGAIN)
			fprintf(stderr, "Read timed out.\n");
		else
			fprintf(stderr, "Read error: %s\n", xstrerror(errno));
	}

	free(res.buf);

	return retval;
}

static int
sd_gen_socket_name(char **ptr, const char *name) {
	char *s = xstrdup(name ?: "unknown");
	char *bname = basename(s);
#ifdef __linux__
	xasprintf(ptr, "@/sd_start/%d/%s/%d/%"PRIu32"", getpid(), bname ?: "unknown", getuid(), arc4random());
#else
	xasprintf(ptr, "/tmp/sd_start.%d.%s.%d.%"PRIu32"", getpid(), bname ?: "unknown", getuid(), arc4random());
#endif
	free(s);

	return 0;
}

int main(int argc, char *argv[], char *env[]) {

	popt_parse_ptrs(&argc, &argv);

	if (argc == 0)
		popt_usage(NULL, false);

	if (!named_socket)
		named_socket = getenv("NOTIFY_SOCKET");

	if (!named_socket || !*named_socket) {
		char *gen_name = NULL;
		if (sd_gen_socket_name(&gen_name, argv[0]) != 0) {
			fprintf(stderr, "Error generating socket name.");
			return EXIT_FAILURE;
		}
		if (setenv("NOTIFY_SOCKET", gen_name, 1) != 0) {
			fprintf(stderr, "Error setting NOTIFY_SOCKET.");
			free(gen_name);
			return EXIT_FAILURE;
		}
		free(gen_name);
		named_socket = getenv("NOTIFY_SOCKET");
	}

	popt_free(NULL);

	int is_abstract = (*named_socket == '@');
	if (!(*named_socket == '/' || is_abstract) || !named_socket[1]) {
		fprintf(stderr, "Socket must be an absolute path, or @abstract");
		return EXIT_FAILURE;
	}

	/* Before we bind we try to remove any existing socket file */
	if (!is_abstract)
		unlink(named_socket);

	int wait_fd = sd_open_socket(named_socket, timeout, bind);
	if (wait_fd < 0) {
		fprintf(stderr, "Error: %s\n", xstrerror(errno));
		return -1;
	}

	int retlvl = 0;
	pid_t pid;
	switch ((pid = fork())) {
	case -1:
		err(1, "fork");
	case 0:
		if (verbose)
			printf("sd_start: Starting %s\n", argv[0]);
		if (setsid() != (pid_t)-1) {
			execvp(argv[0], argv);
			warn("execvp");
		}
		_exit(1);
	default:
		{
			int status = wait_for_ready(wait_fd);
			if (status != 0) {
				if (verbose)
					fprintf(stderr, "sd_start: No READY message received (%d)\n", status);
				if (!maybe || kill(pid, 0) != 0) {
					retlvl = 100;
					break;
				}
			}
			if (pidfile) {
				FILE *pf = fopen(pidfile, "w");
				if (pf == NULL) {
					warn("open(%s)", pidfile);
				} else {
					fprintf(pf, "%d\n", (int)pid);
					fclose(pf);
				}
			}
		}
	}

	if (!is_abstract)
		unlink(named_socket);

	return retlvl;
}
