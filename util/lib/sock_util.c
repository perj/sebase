// Copyright 2018 Schibsted

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/uio.h>

#include "sock_util.h"
#include "memalloc_functions.h"

size_t
readline(int fd, char *vptr, size_t n) {
	size_t nleft;
	ssize_t nread;
	char *ptr;
	char *tmp;

	ptr = vptr;
	nleft = n - 1;

	while (nleft > 0) {
		if ((nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (nread == 0)
			break;

		nleft -= nread;
		ptr += nread;

		vptr[n - nleft - 1] = '\0';

		if ((tmp = strpbrk(vptr, "\r\n"))) {
			*tmp = '\0';
			return n - nleft;
		}

	}

	vptr[n - nleft - 1] = '\0';
	if ((tmp = strpbrk(vptr, "\r\n")))
		*tmp = '\0';
	if (*vptr == '\0')
		return 0;
	return n - nleft;
}

int
get_local_port(int s, char *buf, size_t buflen, int gni_flags) {
	struct sockaddr_storage ss;
	socklen_t sslen = sizeof(ss);

	if (getsockname(s, (struct sockaddr*)&ss, &sslen))
		return EAI_SYSTEM;
	return getnameinfo((struct sockaddr*)&ss, sslen, NULL, 0, buf, buflen, gni_flags);
}

ssize_t
get_iovlen_sum(const struct iovec *iov, int iovcnt) {
	ssize_t total = 0;

	for (int i=0 ; i < iovcnt ; ++i) {
		total += iov[i].iov_len;
	}

	return total;
}

int writev_retry(int fd, struct iovec *iov, int iovcnt, ssize_t iovlen_sum) {
	ssize_t n_total = iovlen_sum;

	if (n_total < 0)
		n_total = get_iovlen_sum(iov, iovcnt);

	struct iovec *piov = iov;
	struct iovec old_iov = { 0 };
	int old_iov_idx = -1;
	int num_iov_left = iovcnt;
	int hwm_idx = 0;	/* high-water marks */
	ssize_t hwm_total = 0;
	ssize_t n_remaining = n_total;

	/* Write to the fd while there's data left to write, or we detect that we make no progress or get an error */
	while (n_remaining > 0) {
		ssize_t res = writev(fd, piov, num_iov_left);

		/* Restore the iov modified last iteration, if any */
		if (res && old_iov_idx > -1) {
			iov[old_iov_idx] = old_iov;
			old_iov_idx = -1;
		}

		if (res < 0) {
			/* Something went wrong writing to the fd */
			return -1;
		} 

		n_remaining -= res;

		/* Partial non-empty write: advance piov and adjust num_iov_left for next attempt */
		if (res && n_remaining > 0) {
			ssize_t n_left = n_total - n_remaining - hwm_total;
			for (int i=hwm_idx ; i < iovcnt ; ++i) {
				if ((ssize_t)iov[i].iov_len > n_left) {
					/* Backup iov */
					old_iov = iov[i];
					old_iov_idx = i;

					/* Update iov */
					iov[i].iov_base = (char*)iov[i].iov_base + n_left;
					iov[i].iov_len -= n_left;

					num_iov_left = iovcnt - i;
					piov = &iov[i];

					hwm_idx = i;
					break;
				}
				n_left -= iov[i].iov_len;
				hwm_total += iov[i].iov_len;
			}
		}
	}

	return 0;
}

/* func would be bind (reader) or connect (writer) */
int
sd_open_socket(const char *socket_name, int timeout_s, int(*func)(int, const struct sockaddr *, socklen_t)) {
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0 // Not defined on Darwin
#endif
	int fd = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0);
	if (fd < 0) {
		return -1;
	}

	int yes = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		return -1;
	}

	if (timeout_s) {
		struct timeval tv = { timeout_s, 0 };
		if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
			return -1;
		}
	}

	struct sockaddr_un saddr;
	saddr.sun_family = AF_UNIX;
	strncpy(saddr.sun_path, socket_name, sizeof(saddr.sun_path));
	saddr.sun_path[sizeof(saddr.sun_path)-1] = '\0';
	size_t saddr_size = SUN_LEN(&saddr);
	if (*socket_name == '@')
		saddr.sun_path[0] = '\0';

	if (func(fd, (struct sockaddr*)&saddr, saddr_size) < 0) {
		return -1;
	}

	return fd;
}

ssize_t sd_post_message(const char *daemon_id, const char *message) {
	char buf[128];
	char *socket_name = getenv("NOTIFY_SOCKET");

	if (!socket_name && daemon_id && *daemon_id) {
		// Build abstract socket name from daemon_id
		buf[0] = '@';
		strlcpy(buf + 1, daemon_id, sizeof(buf) - 1);
		socket_name = buf;
	}

	if (!socket_name || !*socket_name || !message)
		return -1;

	int fd = sd_open_socket(socket_name, 0, connect);
	if (fd < 0)
		return -1;

	ssize_t written = write(fd, message, strlen(message));
	close(fd);

	return written;
}

