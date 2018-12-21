// Copyright 2018 Schibsted

#include <stdio.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>

#include "sbp/vtree.h"
#include "log_event.h"
#include "sbp/memalloc_functions.h"

static int setup = 0;
static int event_sock = 0;

static int
event_setup(void) {
	struct sockaddr_un log_addr;
	event_sock = socket(PF_UNIX, SOCK_DGRAM, 0);

	fcntl(event_sock, F_SETFD, FD_CLOEXEC);

	bzero(&log_addr, sizeof(log_addr));
	log_addr.sun_family = AF_UNIX;
	strncpy(log_addr.sun_path, "/dev/log", sizeof(log_addr.sun_path) - 1);

	if (connect(event_sock, (struct sockaddr*)&log_addr, sizeof(log_addr)))
		return 0;

	setup = 1;
	return 1;
}

int
syslog_ident(const char *ident, const char *fmt, ...) {
	char *ev;
	char *logstring;
	int res;
	va_list ap;
	time_t t;
	char tbuf[26];

	int  (*hook_vsyslog_ident)(const char *ident, const char *format, ...) = dlsym(RTLD_DEFAULT, "sysloghook_vsyslog_ident");
	if (hook_vsyslog_ident) {
		va_start(ap, fmt);
		res = hook_vsyslog_ident(ident, fmt, ap);
		va_end(ap);
		return res;
	}

	va_start(ap, fmt);
	UNUSED_RESULT(vasprintf(&logstring, fmt, ap));
	va_end(ap);

	t = time(NULL);
	ctime_r(&t, tbuf);
	/* Cut of timestamp before year. */
	tbuf[19] = '\0';

	/* Skip past week-day of timestamp. */
	res = xasprintf(&ev, "<134>%s %s: %s", tbuf + 4, ident, logstring);

	if (!setup) {
		event_setup();
	}

	if ((res = send(event_sock, ev, strlen(ev), 0)) < 0) {
		event_setup();
		res = send(event_sock, ev, strlen(ev), 0);
	}

	free(logstring);
	free(ev);

	return res;
}

int
log_event(const char *event) {
	return syslog_ident("EVENT", "%s", event);
}

int
stat_log(struct vtree_chain *vtree, const char *event, const char *id) {
	int s;
	char buf[256];
	int ret = 1;
	int i;
	struct vtree_loop_var hostnames;
	struct vtree_loop_var hostports;

	vtree_fetch_values(vtree, &hostnames, "host", VTREE_LOOP, "name", NULL);
	vtree_fetch_values(vtree, &hostports, "host", VTREE_LOOP, "port", NULL);

	if (!hostnames.len) {
		if (hostnames.cleanup)
			hostnames.cleanup(&hostnames);
		if (hostports.cleanup)
			hostports.cleanup(&hostports);

		return 0;
	}

	snprintf(buf, sizeof(buf), "event:%s id:%s\n", event, id);

	for (i = 0 ; i < hostnames.len ; i++) {
		const char *port;
		const char *hostname;
		struct addrinfo hints = { .ai_flags = AI_ADDRCONFIG, .ai_socktype = SOCK_DGRAM};
		struct addrinfo *res;
		struct addrinfo *curr;

		port = hostports.l.list[i];
		hostname = hostnames.l.list[i];

		if (!port[0] || !hostname[0])
			continue;

		if (getaddrinfo(hostname, port, &hints, &res)) {
			/* failed */
			continue;
		}

		for (curr = res; curr; curr = curr->ai_next) {
			s = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol);

			if (sendto(s, buf, strlen(buf), 0, curr->ai_addr, curr->ai_addrlen) >= 0) {
				/* Flag at least one success */
				close(s);
				ret = 0;
				break;
			}
			
			close(s);
		}

		freeaddrinfo(res);
	}

	if (hostnames.cleanup)
		hostnames.cleanup(&hostnames);
	if (hostports.cleanup)
		hostports.cleanup(&hostports);

	return ret;
}
