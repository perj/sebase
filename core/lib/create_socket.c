// Copyright 2018 Schibsted

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>

#include "create_socket.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sbp/sock_util.h"

static int 
create_socket_single_af(const char *host, const char *port, int af, int level) {
	int the_socket;
	int opt;
	const struct addrinfo hints = { .ai_family = af, .ai_flags = AI_PASSIVE | AI_ADDRCONFIG, .ai_socktype = SOCK_STREAM };
	struct addrinfo *res = NULL;
	int r;

	if ((r = getaddrinfo(host, port, &hints, &res))) {
		log_printf(level, "Failed to resolve host \"%s\" and port \"%s\": %s", host ?: "null", port, gai_strerror(r));
		return -1;
	}

	if ((the_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
		log_printf(level, "Server cannot open socket (%m).");
		freeaddrinfo(res);
		return -1;
	}

	opt = 1;
	if (setsockopt(the_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		log_printf(level, "Failed to set reuseaddr (%m).");
		freeaddrinfo(res);
		return -1;
	}
	opt = 0;
	if (res->ai_family == AF_INET6 && setsockopt(the_socket, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0) {
		log_printf(level, "Failed to unset v6only (%m).");
		freeaddrinfo(res);
		return -1;
	}

	if (bind(the_socket, res->ai_addr, res->ai_addrlen)) {
		log_printf(level, "Failed to bind socket (%m).");
		freeaddrinfo(res);
		return -1;
	}

	freeaddrinfo(res);

	if (listen(the_socket, 200) == -1) {
		log_printf(level, "Failed to listen on socket (%m).");
		return -1;
	}

	return the_socket;
}

/*
 * create_socket
 * Creates and returns a socket for listening on TCP-connections on 
 * any IP-address on the specified port.
 */

int
create_socket(const char *host, const char *port) {
	int s;
	if ((s = create_socket_single_af(host, port, AF_INET6, LOG_INFO)) == -1) {
		log_printf(LOG_INFO, "Retrying with IPv4 only socket.");
		s = create_socket_single_af(host, port, AF_INET, LOG_CRIT);
	}
	return s;
}

int
create_socket_any_port(const char *host, char **port) {
	int s = create_socket(host, "0");

	if (s < 0) {
		*port = NULL;
		return s;
	}

	char *buf = xmalloc(NI_MAXSERV);
	if (get_local_port(s, buf, NI_MAXSERV, NI_NUMERICSERV)) {
		free(buf);
		close(s);
		return -1;
	}

	*port = buf;
	return s;
}

int
create_socket_unix(const char *socket_path) {
	int s;
	struct sockaddr_un server;

	if ((s = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		log_printf(LOG_CRIT, "Server cannot open socket (%m).");
		return -1;
	}

	if (strlen(socket_path) >= sizeof(server.sun_path)) {
		log_printf(LOG_CRIT, "Invalid socket path (%s) - too long.", socket_path);
		return -1;
	}

	server.sun_family = AF_UNIX;
	strncpy(server.sun_path, socket_path, sizeof(server.sun_path));

	if (bind(s, (struct sockaddr*)&server, sizeof (server)) != 0) {
		log_printf(LOG_CRIT, "Server cannot bind socket (%m).");
		return -1;
	}

	if (listen(s, 200) == -1) {
		log_printf(LOG_CRIT, "Failed to listen on socket (%m).");
		return -1;
	}

	return s;
}
