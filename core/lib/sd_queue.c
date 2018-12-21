// Copyright 2018 Schibsted

#include "sd_queue.h"

#include "sbp/memalloc_functions.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int
sd_queue_init(struct sd_queue *queue) {
	if (pthread_mutex_init(&queue->lock, NULL))
		return -1;
	if (pthread_cond_init(&queue->signal, NULL)) {
		pthread_mutex_destroy(&queue->lock);
		return -1;
	}
	SLIST_INIT(&queue->list);
	return 0;
}

void
sd_queue_destroy(struct sd_queue *queue) {
	struct sd_value *v;

	while ((v = SLIST_FIRST(&queue->list))) {
		SLIST_REMOVE_HEAD(&queue->list, list);
		sd_free_value(v);
	}
	pthread_mutex_destroy(&queue->lock);
	pthread_cond_destroy(&queue->signal);
}

void
sd_queue_begin(struct sd_queue *queue, int *state) {
	pthread_mutex_lock(&queue->lock);
	*state = SLIST_EMPTY(&queue->list);
}

void
sd_queue_insert(struct sd_queue *queue, struct sd_value *value) {
	struct sd_value *v, **nv, **pv;

	/* We do an exhaustive search, because any duplicate keys should be removed,
	 * and the list _should_ be small.
	 */

	bool match = false;
	for (pv = &SLIST_FIRST(&queue->list) ; (v = *pv) ; ) {
		nv = &SLIST_NEXT(v, list);

		if (!match) {
			match = v->keyc == value->keyc;
			for (int i = 0 ; match && i < v->keyc ; i++) {
				match = !strcmp(v->keyv[i], value->keyv[i]);
			}

			if (match) {
				/* Delete the old value. */
				*pv = *nv;
				sd_free_value(v);
				continue;
			}
		}

		pv = nv;
	}

	SLIST_NEXT(value, list) = NULL;
	*pv = value;
}

void
sd_queue_commit(struct sd_queue *queue, int *state) {
	pthread_mutex_unlock(&queue->lock);
	if (*state)
		pthread_cond_broadcast(&queue->signal);
}

struct sd_value *
sd_queue_wait(struct sd_queue *queue, int timeout_ms) {
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_ms / 1000;
	ts.tv_nsec += (timeout_ms % 1000) * 1000000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000;
	}

	pthread_mutex_lock(&queue->lock);
	while (SLIST_EMPTY(&queue->list)) {
		if (pthread_cond_timedwait(&queue->signal, &queue->lock, &ts))
			break;
	}

	struct sd_value *res = SLIST_FIRST(&queue->list);
	SLIST_INIT(&queue->list);
	pthread_mutex_unlock(&queue->lock);
	return res;
}

struct sd_value *
sd_create_value(uint64_t index, int keyc, const char **keyv, ssize_t *klenv, const char *value, ssize_t vlen) {
	size_t totlen = sizeof(struct sd_value) + keyc * sizeof(char *);
	size_t klens[keyc];

	for (int i = 0 ; i < keyc ; i++) {
		klens[i] = klenv && klenv[i] >= 0 ? (size_t)klenv[i] : strlen(keyv[i]);
		totlen += klens[i] + 1;
	}
	if (vlen < 0)
		vlen = strlen(value);
	totlen += vlen + 1;

	struct sd_value *res = xmalloc(totlen);
	res->index = index;
	res->keyc = keyc;
	res->keyv = (const char**)(res + 1);
	char *ptr = (char*)(res->keyv + keyc);
	for (int i = 0 ; i < keyc ; i++) {
		res->keyv[i] = ptr;
		memcpy(ptr, keyv[i], klens[i]);
		ptr[klens[i]] = '\0';
		ptr += klens[i] + 1;
	}
	res->value = ptr;
	memcpy(ptr, value, vlen);
	ptr[vlen] = '\0';
	return res;
}

void
sd_free_value(struct sd_value *v) {
	free(v);
}
