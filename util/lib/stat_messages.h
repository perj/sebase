// Copyright 2018 Schibsted

#ifndef _STAT_MESSAGES_H_
#define _STAT_MESSAGES_H_

#include "linker_set.h"
#include "sbp/atomic.h"
#include "memalloc_functions.h"
#include "queue.h"

struct stat_message {
	const char **name;
	char *msg;
	TAILQ_ENTRY(stat_message) list;
};

#define STAT_MESSAGE_DECLARE(varname, ...) \
	static struct stat_message varname = { \
		(const char *[]){ __VA_ARGS__, NULL }, \
		.msg = NULL, \
	}; \
	LINKER_SET_ADD_DATA(stat_msg, varname)

void stat_messages_foreach(void (*cb)(void *, const char *, const char **), void *);
struct stat_message * stat_message_dynamic_alloc(int namelen, ...);
void stat_message_dynamic_free(struct stat_message *);
void stat_message_printf(struct stat_message *msg, const char *fmt, ...) FORMAT_PRINTF(2, 3);
#endif
