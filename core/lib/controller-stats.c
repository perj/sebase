// Copyright 2018 Schibsted

#include <stdio.h>

#include "sbp/bconf.h"
#include <controller.h>
#include "sbp/timer.h"
#include "sbp/stat_counters.h"
#include "sbp/stat_messages.h"

static void
stats_stat_counter_cb(void *v, uint64_t cnt, const char **name) {
	struct bconf_node **bc = v;
	char numbuf[32];
	int namelen = 0;
	int i;

	if (cnt == 0)
		return;

	for (i = 0; name[i] != NULL; i++) {
		namelen++;
	}

	const char *newname[namelen + 1];
	newname[0] = "stats";
	for (i = 0; i < namelen; i++) {
		newname[i + 1] = name[i];
	}
	snprintf(numbuf, sizeof(numbuf), "%llu", (unsigned long long)cnt);
	bconf_add_datav(bc, namelen + 1, newname, numbuf, BCONF_DUP);
}

static void
stats_stat_message_cb(void *v, const char *msg, const char **name) {
	struct bconf_node **bc = v;
	int namelen = 0;
	int i;

	if (msg == NULL)
		return;

	for (i = 0; name[i] != NULL; i++) {
		namelen++;
	}

	const char *newname[namelen + 1];
	newname[0] = "stats";
	for (i = 0; i < namelen; i++) {
		newname[i + 1] = name[i];
	}
	bconf_add_datav(bc, namelen + 1, newname, msg, BCONF_DUP);
}

static void
timer_dump(struct timer_class *tc, void *v) {
	struct bconf_node **bc = v;
	const char *name[4] = {
		"stats",
		"timers",
		tc->tc_name
	};
	char numbuf[32];
	double avg = (double)tc->tc_total.tv_sec + (double)tc->tc_total.tv_nsec / 1000000000.0;
	struct timespec avgts;

	if (tc->tc_count == 0)
		return;

	avg /= (double)tc->tc_count;

	avgts.tv_sec = avg;
	avgts.tv_nsec = (avg - (double)avgts.tv_sec) * 1000000000.0;

	snprintf(numbuf, sizeof(numbuf), "%lld", tc->tc_count);
	name[3] = "count";
	bconf_add_datav(bc, 4, name, numbuf, BCONF_DUP);

	snprintf(numbuf, sizeof(numbuf), "%llu", (unsigned long long)tc->tc_counter);
	name[3] = "bytes";
	bconf_add_datav(bc, 4, name, numbuf, BCONF_DUP);

	snprintf(numbuf, sizeof(numbuf), "%lld.%03ld", (long long)tc->tc_total.tv_sec, tc->tc_total.tv_nsec / 1000000);
	name[3] = "total";
	bconf_add_datav(bc, 4, name, numbuf, BCONF_DUP);

	snprintf(numbuf, sizeof(numbuf), "%lld.%03ld", (long long)tc->tc_min.tv_sec, tc->tc_min.tv_nsec / 1000000);
	name[3] = "min";
	bconf_add_datav(bc, 4, name, numbuf, BCONF_DUP);

	snprintf(numbuf, sizeof(numbuf), "%lld.%03ld", (long long)tc->tc_max.tv_sec, tc->tc_max.tv_nsec / 1000000);
	name[3] = "max";
	bconf_add_datav(bc, 4, name, numbuf, BCONF_DUP);

	snprintf(numbuf, sizeof(numbuf), "%lld.%03ld", (long long)avgts.tv_sec, avgts.tv_nsec / 1000000);
	name[3] = "average";
	bconf_add_datav(bc, 4, name, numbuf, BCONF_DUP);
}

static void
stats_start(struct ctrl_req *cr, void *v) {
	struct bconf_node **bconfp = ctrl_get_bconfp(cr);
	stat_counters_foreach(stats_stat_counter_cb, bconfp);
	stat_messages_foreach(stats_stat_message_cb, bconfp);
	timer_foreach(timer_dump, bconfp);
}

static void
stats_finish(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	ctrl_output_json(cr, "stats");
}

const struct ctrl_handler ctrl_stats_handler = {
	.url = "/stats",
	.start = stats_start,
	.finish = stats_finish,
};
