// Copyright 2018 Schibsted

#include <pthread.h>
#include "sbp/stat_counters.h"
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <semaphore.h>
#include <stdio.h>

STAT_COUNTER_DECLARE(foo, "x", "a");
STAT_COUNTER_DECLARE(bar, "x", "b");

const unsigned int nthreads = 20;
const unsigned int nrounds = 2000000;

static int stat_seen;

static void
stat_cb(void *v, uint64_t count, const char **name) {
	assert(name[2] == NULL);
	assert(!strcmp(name[0], "x"));

	printf("%s %" PRIu64 "\n", name[1], count);

	assert((!strcmp(name[1], "a") && count == nthreads * nrounds) ||
	    (!strcmp(name[1], "b") && count == nthreads * nrounds * 3) ||
	    (!strcmp(name[1], "c") && count == nthreads * nrounds * 2) ||
	    (!strcmp(name[1], "d") && count == nthreads * nrounds));
	stat_seen++;
}

static sem_t startup_sem;
static pthread_rwlock_t thundering_herd = PTHREAD_RWLOCK_INITIALIZER;

static void *
thr_run(void *v) {
	uint64_t *dyn = v, *d2;
	unsigned int i;
	d2 = stat_counter_dynamic_alloc(2, "x", "d");
	sem_post(&startup_sem);
	pthread_rwlock_rdlock(&thundering_herd);
	for (i = 0; i < nrounds; i++) {
		STATCNT_INC(&foo);
		STATCNT_ADD(&bar, 3);
		STATCNT_ADD(dyn, 2);
		STATCNT_INC(d2);
	}
	stat_counter_dynamic_free(d2);
	pthread_rwlock_unlock(&thundering_herd);
	pthread_exit(NULL);
	return NULL;
}

int
main(int argc, char **argv) {
	pthread_t thr[nthreads];
	unsigned int i;
	uint64_t *dyncnt, *d2;

	dyncnt = stat_counter_dynamic_alloc(2, "x", "c");
	d2 = stat_counter_dynamic_alloc(2, "x", "d");

	sem_init(&startup_sem, 0, 0);
	pthread_rwlock_wrlock(&thundering_herd);

	for (i = 0; i < nthreads; i++) {
		pthread_create(&thr[i], NULL, thr_run, dyncnt);
	}

	/* Wait for all threads to start. */
	for (i = 0; i < nthreads; i++) {
		sem_wait(&startup_sem);
	}
	pthread_rwlock_unlock(&thundering_herd);
	for (i = 0; i < nthreads; i++) {
		void *v;
		pthread_join(thr[i], &v);
	}
	stat_counters_foreach(stat_cb, NULL);
	assert(stat_seen == 4);
	stat_counter_dynamic_free(d2);
	return 0;
}
