// Copyright 2018 Schibsted

#ifndef BASE_SD_H
#define BASE_SD_H

#include "sbp/queue.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

struct sd_value {
	SLIST_ENTRY(sd_value) list;
	uint64_t index;
	int keyc;
	const char **keyv;
	const char *value;
};

struct sd_queue {
	pthread_mutex_t lock;
	pthread_cond_t signal;
	SLIST_HEAD(, sd_value) list;
};

struct sd_value *sd_create_value(uint64_t index, int keyc, const char **keyv, ssize_t *klenv, const char *value, ssize_t vlen);
void sd_free_value(struct sd_value *v);

int sd_queue_init(struct sd_queue *queue);
void sd_queue_destroy(struct sd_queue *queue);

void sd_queue_begin(struct sd_queue *queue, int *state);
void sd_queue_insert(struct sd_queue *queue, struct sd_value *v);
void sd_queue_commit(struct sd_queue *queue, int *state);

/* Returns a linked list of sd_values that should be processed and freed. */
struct sd_value *sd_queue_wait(struct sd_queue *queue, int timeout_ms);

#endif /*BASE_SD_H*/
