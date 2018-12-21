// Copyright 2018 Schibsted

#ifndef TIMER_H
#define TIMER_H

#include "macros.h"
#include "queue.h"
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

struct timer_class;

/* Operations on timespecs. */
#define timespecclear(tsp)              (tsp)->tv_sec = (tsp)->tv_nsec = 0
#define timespecisset(tsp)              ((tsp)->tv_sec || (tsp)->tv_nsec)
#define timespeccmp(tsp, usp, cmp)					\
	(((tsp)->tv_sec == (usp)->tv_sec) ?				\
	    ((tsp)->tv_nsec cmp (usp)->tv_nsec) :			\
	    ((tsp)->tv_sec cmp (usp)->tv_sec))
#define timespecadd(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec >= 1000000000L) {			\
			(vsp)->tv_sec++;				\
			(vsp)->tv_nsec -= 1000000000L;			\
		}							\
	} while (0)
#define timespecsub(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec < 0) {				\
			(vsp)->tv_sec--;				\
			(vsp)->tv_nsec += 1000000000L;			\
		}							\
	} while (0)



#define TIMER_MAXATTRIBUTES 8

#define TIMER_MAXCLASSNAME 64

struct timer_instance {
	char ti_class[TIMER_MAXCLASSNAME];
#ifdef __MACH__
	uint64_t ti_start;
	uint64_t ti_stop;
#else
	struct timespec ti_start;
	struct timespec ti_stop;
#endif
	char *ti_attr[TIMER_MAXATTRIBUTES];
	int ti_nattr;
	struct timer_instance *ti_parent;
	uint64_t ti_counter;
	TAILQ_HEAD(,timer_instance) ti_children;
	TAILQ_ENTRY(timer_instance) ti_siblings;
};

struct timer_class_delta {
	long long tc_count;
	uint64_t tc_counter;
	struct timespec tc_total;
	struct timespec tc_children;
};

struct timer_class {
	const char *tc_name;

	long long tc_count;
	uint64_t tc_counter;
	struct timespec tc_max;
	struct timespec tc_min;
	struct timespec tc_total;
	struct timespec tc_children;

	struct {
		long long tc_count;
		uint64_t tc_counter;
		struct timespec tc_total;
		struct timespec tc_children;
	} previous;

	TAILQ_ENTRY(timer_class) tc_list;
};

#ifdef __cplusplus
extern "C" {
#endif

struct timer_instance *timer_start(struct timer_instance *, const char *) ALLOCATOR;
void timer_end(struct timer_instance *, struct timespec *);
void timer_handover(struct timer_instance *, const char *);
void timer_add_attribute(struct timer_instance *, const char *);
void timer_add_counter(struct timer_instance *, uint64_t);
void timer_delta_fetch_reset(struct timer_class *, struct timer_class_delta *, bool);
struct timer_class *timer_getclass(const char *);
void timer_foreach(void (*)(struct timer_class *, void *), void *);
void timer_reset(void);
void timer_clean(void);

#ifdef __cplusplus
}
#endif

#endif
