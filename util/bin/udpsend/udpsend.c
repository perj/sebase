// Copyright 2018 Schibsted

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <netdb.h>
#include "sbp/macros.h"
#include "sbp/popt.h"

bool ipv4, ipv6, flood;

POPT_USAGE("[options] addr port [ack_fifo]");
POPT_ARGUMENT("addr", "Address to send to.");
POPT_ARGUMENT("port", "Port to send to.");
POPT_ARGUMENT("ack_fifo", "Optionally read acks from this path.");
POPT_BOOL("4", false, &ipv4, "Use only IPv4.");
POPT_BOOL("6", false, &ipv6, "Use only IPv6.");
POPT_BOOL("flood", false, &flood, "Read only one input and send it as fast as possible continuously.");

static void
transmit(int s, char *buf, struct addrinfo *res, int ack_sock) {
	if (sendto(s, buf, strlen(buf), 0, res->ai_addr, res->ai_addrlen) < 0)
		warn("sendto");
	if (ack_sock != -1) {
		char ackbuf[4];
		alarm(4);
		/* signal(SIGALRM, sig_alarm); */
		if (read(ack_sock, ackbuf, 4) == -1) {
			err(1, "reading ACK failed");
		}
		alarm(0);
	}
}

int
main(int argc, char *argv[]) {
	int s;
	char buf[1024];
	int ack_sock = -1;
	struct addrinfo hints = { .ai_socktype = SOCK_DGRAM, .ai_flags = AI_ADDRCONFIG };
	struct addrinfo *res = NULL;
	int r;

	popt_parse_ptrs(&argc, &argv);

	if (argc < 2)
		popt_usage(NULL, false);

	if (ipv4)
		hints.ai_family = AF_INET;
	if (ipv6)
		hints.ai_family = AF_INET6;

	if ((r = getaddrinfo(argv[0], argv[1], &hints, &res)))
		errx(1, "getaddrinfo(%s, %s): %s", argv[0], argv[1], gai_strerror(r));

	s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s < 0)
		err(1, "socket");

	/* Wait for an ack */
	if (argc == 3) {
		ack_sock = open(argv[2], O_RDONLY);
		if (ack_sock == -1)
			err(1, "open");
	}

	if (!flood) {
		while (fgets(buf, sizeof(buf), stdin)) {
			transmit(s, buf, res, ack_sock);
		}
	} else {
		UNUSED_RESULT(fgets(buf, sizeof(buf), stdin));
		while (1) {
			transmit(s, buf, res, ack_sock);
		}
	}

	if (ack_sock != -1)
		close(ack_sock);

	freeaddrinfo(res);

	return 0;
}
