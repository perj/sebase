// Copyright 2018 Schibsted

#include "stat_messages.h"

#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

pthread_mutex_t sm_lock = PTHREAD_MUTEX_INITIALIZER;
TAILQ_HEAD(, stat_message) dyn_msgs = TAILQ_HEAD_INITIALIZER(dyn_msgs);

STAT_MESSAGE_DECLARE(_dummy, "_", "_dummy_to_make_linker_sets_happy");

void
stat_messages_foreach(void (*cb)(void *, const char *, const char **), void *cbarg) {
	LINKER_SET_DECLARE(stat_msg, struct stat_message);
	struct stat_message * const *msgpp;
	struct stat_message *msgp;

	LINKER_SET_FOREACH(msgpp, stat_msg) {
		msgp = *msgpp;
		if (!strcmp(msgp->name[0], "_"))
			continue;
		pthread_mutex_lock(&sm_lock);
		cb(cbarg, msgp->msg, msgp->name);
		pthread_mutex_unlock(&sm_lock);
	}

	pthread_mutex_lock(&sm_lock);
	TAILQ_FOREACH(msgp, &dyn_msgs, list) {
		cb(cbarg, msgp->msg, msgp->name);
	}
	pthread_mutex_unlock(&sm_lock);
}

struct stat_message *
stat_message_dynamic_alloc(int namelen, ...) {
	struct stat_message *msg;
	const char *names[namelen];
	size_t name_totlen = 0;
	int i;
	char *cwp;
	va_list ap;

	va_start(ap, namelen);
	for (i = 0; i < namelen; i++) {
		names[i] = va_arg(ap, const char *);
		name_totlen += strlen(names[i]) + 1;
	}
	va_end(ap);

	msg = xmalloc(sizeof(*msg) + sizeof(const char *) * (namelen + 1) + name_totlen);
	msg->name = (const char **)(msg + 1);
	for (i = 0, cwp = (char *)(msg->name + namelen + 1); i < namelen; i++) {
		size_t nlen = strlen(names[i]) + 1;
		memcpy(cwp, names[i], nlen);
		msg->name[i] = cwp;
		cwp += nlen;
	}
	msg->name[i] = NULL;
	msg->msg = NULL;

	pthread_mutex_lock(&sm_lock);
	TAILQ_INSERT_TAIL(&dyn_msgs, msg, list);
	pthread_mutex_unlock(&sm_lock);

	return msg;
}

void
stat_message_dynamic_free(struct stat_message *msg) {
	pthread_mutex_lock(&sm_lock);
	TAILQ_REMOVE(&dyn_msgs, msg, list);
	pthread_mutex_unlock(&sm_lock);
	free(msg->msg);
	free(msg);
}

void
stat_message_printf(struct stat_message *msg, const char *fmt, ...) {
	char *prev;
	char *new;
	va_list va;

	va_start(va, fmt);
	xvasprintf(&new, fmt, va);
	va_end(va);

	pthread_mutex_lock(&sm_lock);
	prev = msg->msg;
	msg->msg = new;
	pthread_mutex_unlock(&sm_lock);

	free(prev);
}
