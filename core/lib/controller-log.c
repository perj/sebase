// Copyright 2018 Schibsted

#include <stdio.h>

#include "sbp/bconf.h"
#include <controller.h>
#include "sbp/timer.h"
#include "sbp/stat_counters.h"
#include "sbp/stat_messages.h"
#include "sbp/stringmap.h"
#include "sbp/logging.h"

static void
loglevel_finish(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	struct bconf_node **bc = ctrl_get_bconfp(cr);
	const char *old;
	const char *new;

	if (sm_get(qs, "level", -1, 0)) {
		const char *req = sm_get(qs, "level", -1, 0);
		log_change_level(req, &old, &new);
		bconf_add_data(bc, "log.level.old", old);
		if (!strcmp(new, req)) {
			bconf_add_data(bc, "log.level.new", new);
		} else {
			ctrl_error(cr, 400, "unsupported log level requested");
			return;
		}
	} else {
		log_change_level(NULL, &old, &new);
		bconf_add_data(bc, "log.level", old);
	}
	ctrl_output_json(cr, "log");
}

const struct ctrl_handler ctrl_loglevel_handler = {
	.url = "/loglevel",
	.finish = loglevel_finish,
};
