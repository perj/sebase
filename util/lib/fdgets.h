// Copyright 2018 Schibsted

#ifndef COMMON_FDGETS_H
#define COMMON_FDGETS_H

#include <stdlib.h>
#include <sys/types.h>

/*
 * fd version of fgets.
 * The \n is not included, however, nor a \r right before the \n.
 * EOF is signaled by NULL return with errno == 0.
 *
 * Note that the whole buffer is used for state, so you have to
 * use the same buf pointer every time.
 * Initialize state to 0s.
 */
char *fdgets(char *buf, size_t bufsz, int state[2], int fd);

/*
 * Switch from one buffer to another. State is updated.
 * Returns the number of unprocessed bytes that are now
 * the contents of dst.
 */
int fdgets_copy(char *dst, size_t dstsz, const char *src, int state[2]);

/*
 * Like read, but first uses state data from buf, and updates state
 * so fdgets can be used with buf after.
 */
ssize_t fdgets_read(void *dst, size_t count, char *buf, int state[2], int fd);

#endif /*COMMON_FDGETS_H*/
