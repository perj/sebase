// Copyright 2018 Schibsted

#ifndef CREATE_SOCKET_H
#define CREATE_SOCKET_H

#include <sys/socket.h>
#include <sys/types.h>

#include "sbp/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

int create_socket(const char *host, const char *port);
int create_socket_unix(const char *);

/* Return malloced string with port number in *port */
int create_socket_any_port(const char *host, char **port) NONNULL(2);

#ifdef __cplusplus
}
#endif

#endif
