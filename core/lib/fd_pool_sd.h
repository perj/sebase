// Copyright 2018 Schibsted

#ifndef FD_POOL_SD_H
#define FD_POOL_SD_H

#include <stdint.h>

struct vtree_chain;
struct fd_pool;
struct fd_pool_sd;
struct sd_queue;

struct fd_pool_sd *fd_pool_sd_create(struct fd_pool *pool, const char *host, const char *appl, const char *service, struct sd_queue *sdq);
void fd_pool_sd_free(struct fd_pool_sd *fps);

void fd_pool_sd_copy_static_config(struct fd_pool_sd *fps, struct vtree_chain *config);

int fd_pool_sd_start(struct fd_pool_sd *fps);
int fd_pool_sd_stop(struct fd_pool_sd *fps);

/*
 * Will wait until the given index, or a higher one, has been fully processed, with a timeout.
 * E.g. waiting for index 1 means at least the inital batch of messages have been processed.
 * Returns 0 on index reached, 1 on timeout.
 */
int fd_pool_sd_wait_index(struct fd_pool_sd *fps, uint64_t index, int timeout_ms);

#endif /*FD_POOL_SD_H*/
