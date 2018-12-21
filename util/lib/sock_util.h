// Copyright 2018 Schibsted

#ifndef SOCK_UTIL_H
#define SOCK_UTIL_H

#include <sys/socket.h>
#include <sys/types.h>

#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t readline(int fd, char *vptr, size_t n);

/* Calls getsockname then getnameinfo. Return getnameinfo result. */
int get_local_port(int s, char *pbuf, size_t pbuflen, int gni_flags);

struct iovec;

/*
	Helper to calculate the total size of an array of iovecs
*/
ssize_t
get_iovlen_sum(const struct iovec *iov, int iovcnt) FUNCTION_PURE;

/*
	Wrapper around writev to handle partial writes.

	The iovecs can be modified internally, but will be restored before the function
	returns, allowing a higher-level retry on another fd without re-loading the iovecs.

	If the total iovlen is unknown, pass -1 for iovlen_sum.

	On success, 0 is returned.
	On error, a negative value is returned.
*/
int
writev_retry(int fd, struct iovec *iov, int iovcnt, ssize_t iovlen_sum);

int sd_open_socket(const char *socket_name, int timeout_s, int(*func)(int, const struct sockaddr *, socklen_t));
ssize_t sd_post_message(const char *daemon_id, const char *message);

#ifdef __cplusplus
}
#endif

#endif
