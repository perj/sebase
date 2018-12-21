// Copyright 2018 Schibsted

#ifndef _STAT_COUNTERS_H_
#define _STAT_COUNTERS_H_

#include "linker_set.h"
#include <inttypes.h>

#include "queue.h"

struct stat_counter {
	const char **name;
	uint64_t *cnt;
	int refs;
	TAILQ_ENTRY(stat_counter) list;
};

#define STAT_COUNTER_DECLARE(varname, ...) \
	static uint64_t varname; \
	static struct stat_counter scnt##varname = { \
		(const char *[]){ __VA_ARGS__, NULL }, \
		&varname, \
	}; \
	LINKER_SET_ADD_DATA(stat_cnt, scnt##varname)

uint64_t *stat_counter_dynamic_alloc(int namelen, ...);
void stat_counter_dynamic_free(uint64_t *);

#define STATCNT_ADD(cntp, n) __sync_fetch_and_add(cntp, (uint64_t)n)
#define STATCNT_INC(cntp) STATCNT_ADD(cntp, 1)
#define STATCNT_SET(cntp, n) do { (*(cntp)) = (uint64_t)(n); } while (0)
#define STATCNT_RESET(cntp) STATCNT_SET(cntp, 0)

void stat_counters_foreach(void (*cb)(void *, uint64_t, const char **), void *);

#endif
