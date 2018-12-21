// Copyright 2018 Schibsted

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "sbp/timer.h"

static void
timer_dump(struct timer_class *tc, void *v) {
	struct timespec avgts;
	struct timer_class_delta delta;
	double avg = (double)tc->tc_total.tv_sec + (double)tc->tc_total.tv_nsec / 1000000000.0;

	if (tc->tc_count == 0)
	    return;

	avg /= (double)tc->tc_count;

	avgts.tv_sec = avg;
	avgts.tv_nsec = (avg - (double)avgts.tv_sec) * 1000000000.0;

	printf("Timer: %s.count:%lld\n", tc->tc_name, tc->tc_count);
	printf("Timer: %s.counter:%ld\n", tc->tc_name, (long)tc->tc_counter);
	printf("Timer: %s.total:%d.%03ld\n", tc->tc_name, (int)tc->tc_total.tv_sec, tc->tc_total.tv_nsec / 1000000);
	if (tc->tc_count > 1) {
		printf("Timer: %s.min:%d.%03ld\n", tc->tc_name, (int)tc->tc_min.tv_sec, tc->tc_min.tv_nsec / 1000000);
		printf("Timer: %s.max:%d.%03ld\n", tc->tc_name, (int)tc->tc_max.tv_sec, tc->tc_max.tv_nsec / 1000000);
		printf("Timer: %s.average:%d.%03ld\n", tc->tc_name, (int)avgts.tv_sec, avgts.tv_nsec / 1000000);
	}

	timer_delta_fetch_reset(tc, &delta, true);
	printf("Delta: %s.count:%lld\n", tc->tc_name, delta.tc_count);
	printf("Delta: %s.counter:%ld\n", tc->tc_name, (long)delta.tc_counter);
	printf("Delta: %s.total:%d.%03ld\n", tc->tc_name, (int)delta.tc_total.tv_sec, delta.tc_total.tv_nsec / 1000000);
	printf("\n");
}

int
main(int argc, char **argv) {
	struct timer_instance *ti_root = timer_start(NULL, "test_timer");
	struct timer_instance *ti = timer_start(ti_root, "subtest");

	printf("First run - no delay, no previous delta\n");
	for (int i=0 ; i < 5 ; ++i) {
		timer_handover(ti, "the first function");
		timer_add_attribute(ti, "FIRST");

		timer_handover(ti, "the second function");
		timer_add_attribute(ti, "SECOND");
	}
	timer_add_counter(ti, 42);
	timer_end(ti, NULL);
	timer_end(ti_root, NULL);
	timer_foreach(timer_dump, NULL);
	printf("\n");

	printf("Second run - 500ms delay on SECOND\n");
	ti_root = timer_start(NULL, "test_timer");
	ti = timer_start(ti_root, "subtest");
	for (int i=0 ; i < 3 ; ++i) {
		timer_handover(ti, "the first function");
		timer_add_attribute(ti, "FIRST");

		timer_handover(ti, "the second function");
		timer_add_attribute(ti, "SECOND");
		usleep(500000);
	}
	timer_add_counter(ti, 46);
	timer_end(ti, NULL);
	timer_end(ti_root, NULL);
	timer_foreach(timer_dump, NULL);
	printf("\n");

	printf("Third run - 100ms delay on SECOND\n");
	ti_root = timer_start(NULL, "test_timer");
	ti = timer_start(ti_root, "subtest");
	for (int i=0 ; i < 4 ; ++i) {
		timer_handover(ti, "the first function");
		timer_add_attribute(ti, "FIRST");

		timer_handover(ti, "the second function");
		timer_add_attribute(ti, "SECOND");
		usleep(100000);
	}
	timer_add_counter(ti, 12);
	timer_end(ti, NULL);
	timer_end(ti_root, NULL);
	timer_foreach(timer_dump, NULL);

	timer_clean();

	return 0;
}

