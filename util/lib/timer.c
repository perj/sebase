// Copyright 2018 Schibsted

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "timer.h"
#include "memalloc_functions.h"

#ifdef __MACH__
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

TAILQ_HEAD(,timer_class) timer_classes = TAILQ_HEAD_INITIALIZER(timer_classes);

#include <pthread.h>
pthread_mutex_t tc_mutex = PTHREAD_MUTEX_INITIALIZER;
#define TC_LOCK()   pthread_mutex_lock(&tc_mutex)
#define TC_UNLOCK()   pthread_mutex_unlock(&tc_mutex)

pthread_mutex_t tcd_mutex = PTHREAD_MUTEX_INITIALIZER;
#define TCD_LOCK() pthread_mutex_lock(&tcd_mutex)
#define TCD_UNLOCK() pthread_mutex_unlock(&tcd_mutex)

#ifdef __MACH__
static mach_timebase_info_data_t timebase_info;
static inline void timer_sub(uint64_t *tsp, uint64_t *usp, struct timespec *vsp) {
	if (*usp < *tsp) {
		memset(vsp, 0, sizeof(*vsp));
		return;
	}
	if (timebase_info.denom == 0)
		mach_timebase_info(&timebase_info);
	uint64_t elapsed = (*(tsp) - *(usp)) * timebase_info.numer / timebase_info.denom;
	vsp->tv_sec = elapsed / 1000000000;
	vsp->tv_nsec = elapsed % 1000000000;
}
#else
static inline void timer_sub(struct timespec *tsp, struct timespec *usp, struct timespec *vsp) {
	if (timespeccmp(tsp, usp, <)) {
		memset(vsp, 0, sizeof(*vsp));
		return;
	}
	timespecsub(tsp, usp, vsp);
}
#endif

struct timer_class *
timer_getclass(const char *name) {
	struct timer_class *tc;

	TC_LOCK();
	TAILQ_FOREACH(tc, &timer_classes, tc_list) {
		if (!strcmp(tc->tc_name, name))
			goto out;
	}
	tc = zmalloc(sizeof(*tc));
	TAILQ_INSERT_TAIL(&timer_classes, tc, tc_list);
	tc->tc_name = xstrdup(name);
out:
	TC_UNLOCK();
	return (tc);
}

static void
timer_init(struct timer_instance *ti, struct timer_instance *parent, const char *tc) {
	strlcpy(ti->ti_class, tc, sizeof(ti->ti_class));
	TAILQ_INIT(&ti->ti_children);
	ti->ti_parent = parent;
	if (parent != NULL)
		TAILQ_INSERT_TAIL(&parent->ti_children, ti, ti_siblings);	
}

struct timer_instance *
timer_start(struct timer_instance *parent, const char *tc) {
	struct timer_instance *ti;

	ti = zmalloc(sizeof(*ti));
	timer_init(ti, parent, tc);
#ifdef __MACH__
	ti->ti_start = mach_absolute_time();
#else
	clock_gettime(CLOCK_MONOTONIC, &ti->ti_start);
#endif

	return (ti);
}

void
timer_delta_fetch_reset(struct timer_class *tc, struct timer_class_delta *d, bool reset) {
	TCD_LOCK();

	if (d) {
		d->tc_count = tc->tc_count - tc->previous.tc_count;
		d->tc_counter = tc->tc_counter - tc->previous.tc_counter;
		timespecsub(&tc->tc_total, &tc->previous.tc_total, &d->tc_total);
		timespecsub(&tc->tc_children, &tc->previous.tc_children, &d->tc_children);
	}

	if (reset) {
		tc->previous.tc_count = tc->tc_count;
		tc->previous.tc_counter = tc->tc_counter;
		tc->previous.tc_total = tc->tc_total;
		tc->previous.tc_children = tc->tc_children;
	}

	TCD_UNLOCK();
}

static void
timer_update(struct timer_class *tc, struct timespec *ts, uint64_t counter) {
	TC_LOCK();
	tc->tc_count++;

	tc->tc_counter += counter;
	if (timespeccmp(ts, &tc->tc_max, >))
		tc->tc_max = *ts;
	if (timespeccmp(ts, &tc->tc_min, <) || (tc->tc_min.tv_sec == 0 && tc->tc_min.tv_nsec == 0))
		tc->tc_min = *ts;

	timespecadd(&tc->tc_total, ts, &tc->tc_total);
	TC_UNLOCK();
}

static void
build_name(struct timer_instance *ti, char *namebuf, int bufsiz) {
	if (ti->ti_parent) {
		build_name(ti->ti_parent, namebuf, bufsiz);
		strlcat(namebuf, "#", bufsiz);
	}
	strlcat(namebuf, ti->ti_class, bufsiz);
}

static void
timer_update_children(struct timer_class *tc, struct timespec *ts) {
	TC_LOCK();

	timespecadd(&tc->tc_children, ts, &tc->tc_children);
	TC_UNLOCK();
}

static void
timer_finalize(char *parent_name, struct timer_instance *ti, struct timespec *ts, int freeit) {
	struct timer_instance *child;
	char name[1024];
	int i;
 	struct timer_class *tc = NULL;

	timer_sub(&ti->ti_stop, &ti->ti_start, ts);

	name[0] = '\0';
	if (parent_name != NULL)
		snprintf(name, sizeof(name), "%s#%s", parent_name, ti->ti_class);
	else
		build_name(ti, name, sizeof(name));

	for (i = 0; i < ti->ti_nattr; i++) {
		strlcat(name, "/", sizeof(name));
		strlcat(name, ti->ti_attr[i], sizeof(name));
		free(ti->ti_attr[i]);
		ti->ti_attr[i] = NULL;
	}
	ti->ti_nattr = 0;
	name[sizeof(name) - 1] = '\0';

	if (ts->tv_sec != 0 || ts->tv_nsec != 0)
		timer_update((tc = timer_getclass(name)), ts, ti->ti_counter);

	while ((child = TAILQ_FIRST(&ti->ti_children)) != NULL) {
		struct timespec tss;
		timer_finalize(name, child, &tss, 1);
		if (tss.tv_sec != 0 || tss.tv_nsec != 0)
			timer_update_children(tc, &tss);
	}

	if (ti->ti_parent != NULL)
		TAILQ_REMOVE(&ti->ti_parent->ti_children, ti, ti_siblings);

	if (freeit)
		free(ti);
}

static void
timer_end1(struct timer_instance *ti, struct timespec *ts, int freeit) {
	struct timespec tss;

	if (ts == NULL)
		ts = &tss;
#ifdef __MACH__
	ti->ti_stop = mach_absolute_time();
#else
	clock_gettime(CLOCK_MONOTONIC, &ti->ti_stop);
#endif

	if (ti->ti_parent != NULL && freeit) {
		if (ts) {
			timer_sub(&ti->ti_stop, &ti->ti_start, ts);
		}
		return;
	}

	timer_finalize(NULL, ti, ts, freeit);
}

void
timer_end(struct timer_instance *ti, struct timespec *ts) {
	timer_end1(ti, ts, 1);
}

/*
 * Hand over a timer struct to another timer, using the end time of the
 * old timer as the start time of the new timer.
 */
void
timer_handover(struct timer_instance *ti, const char *tc) {
	struct timer_instance *parent = ti->ti_parent;

	timer_end1(ti, NULL, 0);
	ti->ti_start = ti->ti_stop;
	timer_init(ti, parent, tc);
}


void
timer_add_attribute(struct timer_instance *ti, const char *attr)
{
	int i, n;

	if (ti->ti_nattr == TIMER_MAXATTRIBUTES) {
		/* XXX - how do we handle? */
		return;
	}

	for (i = 0; i <  ti->ti_nattr; i++)
		if (strcmp(attr, ti->ti_attr[i]) < 0)
			break;
	n = i;
	for (i = ti->ti_nattr; i > n ; i--)
		ti->ti_attr[i] = ti->ti_attr[i - 1];

	ti->ti_attr[n] = xstrdup(attr);

	ti->ti_nattr++;
}

void
timer_add_counter(struct timer_instance *ti, uint64_t counter) {
	ti->ti_counter += counter;
}

void
timer_foreach(void (*fn)(struct timer_class *, void *), void *data) {
	struct timer_class *tc;

	TC_LOCK();
	TAILQ_FOREACH(tc, &timer_classes, tc_list)
		(*fn)(tc, data);
	TC_UNLOCK();
}

void
timer_reset(void) {
	struct timer_class *tc;

	TC_LOCK();
	TCD_LOCK();
	TAILQ_FOREACH(tc, &timer_classes, tc_list) {
		tc->tc_count = 0;
		tc->tc_counter = 0;
		tc->tc_total.tv_sec = tc->tc_total.tv_nsec = 0;
		tc->tc_min.tv_sec = tc->tc_min.tv_nsec = 0;
		tc->tc_max.tv_sec = tc->tc_max.tv_nsec = 0;

		tc->previous.tc_count = 0;
		tc->previous.tc_counter = 0;
		tc->previous.tc_total.tv_sec = tc->previous.tc_total.tv_nsec = 0;
	}
	TCD_UNLOCK();
	TC_UNLOCK();
}

void timer_clean(void)
{
    struct timer_class *tc;

    TC_LOCK();


    while ((tc = TAILQ_FIRST(&timer_classes)) != NULL) {
        TAILQ_REMOVE(&timer_classes, tc, tc_list);

        if(tc->tc_name != NULL) free((char *)tc->tc_name);
        free(tc);
    }


    TC_UNLOCK();
}

#ifdef DEBUG
static void __attribute__((destructor)) timer_cleanup_destructor(void) {
	timer_clean();
}
#endif

