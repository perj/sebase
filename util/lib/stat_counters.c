// Copyright 2018 Schibsted

#include "stat_counters.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>

static pthread_mutex_t dyn_mtx = PTHREAD_MUTEX_INITIALIZER;
static TAILQ_HEAD(,stat_counter) dyn_counters = TAILQ_HEAD_INITIALIZER(dyn_counters);

STAT_COUNTER_DECLARE(ph, "_", "init");

void
stat_counters_foreach(void (*cb)(void *, uint64_t, const char **), void *cbarg) {
	LINKER_SET_DECLARE(stat_cnt, struct stat_counter);
	struct stat_counter * const *cntpp;
	struct stat_counter *cntp;
	LINKER_SET_FOREACH(cntpp, stat_cnt) {
		cntp = *cntpp;
		if (!strcmp(cntp->name[0], "_"))
			continue;
		(*cb)(cbarg, *cntp->cnt, cntp->name);
	}
	pthread_mutex_lock(&dyn_mtx);
	TAILQ_FOREACH(cntp, &dyn_counters, list) {
		(*cb)(cbarg, *cntp->cnt, cntp->name);
	}
	pthread_mutex_unlock(&dyn_mtx);
}

/*
 * No attempt is made to check that the static counter names with the dynamic counter names. Don't do that or foreach will
 * be confusing.
 */
uint64_t *
stat_counter_dynamic_alloc(int namelen, ...) {
	size_t names_totlen = 0;
	const char *names[namelen];
	char *n;
	struct stat_counter *sc;
	uint64_t *ret = NULL;
	va_list ap;
	int i;

	va_start(ap, namelen);

	for (i = 0; i < namelen; i++) {
		names[i] = va_arg(ap, const char *);
		names_totlen += strlen(names[i]) + 1;
	}
	va_end(ap);

	pthread_mutex_lock(&dyn_mtx);
	TAILQ_FOREACH(sc, &dyn_counters, list) {
		for (i = 0; i < namelen; i++) {
			if (sc->name[i] == NULL || strcmp(names[i], sc->name[i]))
				break;
		}
		if (i == namelen) {
			sc->refs++;	/* protected by the mutex above */
			ret = sc->cnt;
			goto out;
		}
	}

	ret = malloc(sizeof(uint64_t) + sizeof(*sc) + sizeof(const char *) * (namelen + 1) + names_totlen);
	if (!ret)
		goto out;
	sc = (struct stat_counter *)(ret + 1);
	sc->cnt = ret;
	*sc->cnt = 0;
	sc->refs = 1;
	sc->name = (const char **)(sc + 1);
	n = (char *)(sc->name + namelen + 1);
	for (i = 0; i < namelen; i++) {
		size_t nlen = strlen(names[i]);
		memcpy(n, names[i], nlen + 1);
		sc->name[i] = n;
		n += nlen + 1;
	}
	sc->name[i] = NULL;

	TAILQ_INSERT_TAIL(&dyn_counters, sc, list);
out:
	pthread_mutex_unlock(&dyn_mtx);

	return ret;
}

void
stat_counter_dynamic_free(uint64_t *ret) {
	struct stat_counter *sc = (struct stat_counter *)(ret + 1);

	pthread_mutex_lock(&dyn_mtx);
	if (--sc->refs == 0) {
		TAILQ_REMOVE(&dyn_counters, sc, list);
		free(sc->cnt);
	}
	pthread_mutex_unlock(&dyn_mtx);
}
