// Copyright 2018 Schibsted

#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>

#include "sbp/popt.h"

long timeout;
static bool null_input;

POPT_USAGE("[options] host port");
POPT_ARGUMENT("host", "Host name or ip address to connect to.");
POPT_ARGUMENT("port", "Port name or number to connect to.");
POPT_SECONDS("wait", 0, &timeout, "Wait this long before timing out. Use 0 for infinite.");
POPT_BOOL("null", false, &null_input, "Don't send or read any data, only report if connect was successful.");

int
main(int argc, char *argv[]) {
	struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	struct addrinfo *res;
	struct addrinfo *rp;
	struct pollfd pfds[2];
	int error;
	int fd;
	char buf[1024];
	ssize_t sz;

	popt_parse_ptrs(&argc, &argv);
	if (timeout > 0)
		timeout *= 1000;
	else
		timeout = -1;

	if (argc < 2)
		popt_usage(NULL, false);

	if ((error = getaddrinfo(argv[0], argv[1], &hints, &res)) != 0) {
		printf("getaddrinfo: %s\n", gai_strerror(error));
		exit(EXIT_FAILURE);
	}

	for (rp = res; rp != NULL; rp = rp->ai_next) {
		if ((fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) == -1)
			continue;

		if (connect(fd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;

		close(fd);
	}

	if (rp == NULL) {
		printf("Could not connect\n");
		exit(EXIT_FAILURE);
	}

	if (null_input) {
		close(fd);
		return 0;
	}

	pfds[0].fd = STDIN_FILENO;
	pfds[0].events = POLLIN | POLLHUP;
	pfds[1].fd = fd;
	pfds[1].events = POLLIN;
	struct pollfd *pfdptr = pfds;
	int npfds = 2;

	while (poll(pfdptr, npfds, timeout) > 0) {
		if (pfds[1].revents & POLLIN) {
			sz = read(pfds[1].fd, buf, sizeof(buf));
			if (sz < 0) {
				break;
			} else if (sz == 0) {
				shutdown(fd, SHUT_RD);
				break;
			} else {
				printf("%.*s", (int) sz, buf);
			}
		}

		if (pfds[0].revents & POLLIN) {
			sz = read(pfds[0].fd, buf, sizeof(buf));
			if (sz < 0) {
				break;
			} else if (sz == 0) {
				shutdown(fd, SHUT_WR);
			} else {
				send(fd, buf, sz, 0);
			}
		} else if (pfds[0].revents & POLLHUP) {
			shutdown(fd, SHUT_WR);
			pfdptr++;
			npfds--;
			pfds[0].revents = 0;
		}
	}

	close(fd);
	return 0;
}
