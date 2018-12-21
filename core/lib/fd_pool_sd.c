// Copyright 2018 Schibsted

#include "fd_pool_sd.h"

#include "sbp/bconf.h"
#include "sbp/bconfig.h"
#include "sbp/buf_string.h"
#include "fd_pool.h"
#include "sbp/logging.h"
#include "sbp/json_vtree.h"
#include "sbp/memalloc_functions.h"
#include "sd_queue.h"
#include "sbp/vtree.h"

#include <assert.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Arbitrarily chosen. */
#define QUEUEWAIT_MS 2000

struct fd_pool_sd {
	struct fd_pool *pool;
	char *host;
	char *appl;
	char *service;

	struct bconf_node *static_conf;

	struct sd_queue *queue;
	uint64_t max_index;
	bool max_index_valid; // Until true, don't expose max_index in e.g. initial_wait.

	pthread_cond_t update_signal;

	volatile bool running;
	pthread_t thread;

	struct bconf_node **confroot;
};

struct fd_pool_sd *
fd_pool_sd_create(struct fd_pool *pool, const char *host, const char *appl, const char *service, struct sd_queue *queue) {
	struct fd_pool_sd *fps = zmalloc(sizeof(*fps));
	fps->pool = pool;
	if (host)
		fps->host = xstrdup(host);
	if (appl)
		fps->appl = xstrdup(appl);
	fps->service = xstrdup(service);
	fps->queue = queue;
	pthread_cond_init(&fps->update_signal, NULL);
	return fps;
}

void
fd_pool_sd_free(struct fd_pool_sd *fps) {
	if (fps->running)
		fd_pool_sd_stop(fps);

	pthread_cond_destroy(&fps->update_signal);
	free(fps->host);
	free(fps->appl);
	free(fps->service);
	bconf_free(&fps->static_conf);
	free(fps);
}

void
fd_pool_sd_copy_static_config(struct fd_pool_sd *fps, struct vtree_chain *config) {
	struct vtree_keyvals hosts;
	vtree_fetch_keys_and_values(config, &hosts, "host", VTREE_LOOP, NULL);

	for (int i = 0 ; i < hosts.len ; i++) {
		if (hosts.list[i].type != vkvNode)
			continue;

		char intbuf[20];
		const char *hk = hosts.list[i].key;
		if (hosts.type == vktList) {
			snprintf(intbuf, sizeof(intbuf), "%d", i);
			hk = intbuf;
		}

		/* Max two levels deep for now. */
		struct vtree_keyvals kvs;
		vtree_fetch_keys_and_values(&hosts.list[i].v.node, &kvs, VTREE_LOOP, NULL);
		if (kvs.len && kvs.type != vktList) {
			for (int j = 0 ; j < kvs.len ; j++) {
				if (kvs.list[j].type != vkvValue)
					continue;
				const char *k = kvs.list[j].key;
				const char *v = kvs.list[j].v.value;
				bconf_add_datav(&fps->static_conf, 3, (const char*[]){"host", hk, k}, v, BCONF_DUP);
			}
		}
		if (kvs.cleanup)
			kvs.cleanup(&kvs);
	}
	if (hosts.cleanup)
		hosts.cleanup(&hosts);
}

static bool
parse_old_bconf_config(struct bconf_node **confdata, const char *value) {
	const char *next;
	for (; *value ; value = next) {
		const char *lend = strchr(value, '\n');
		if (lend)
			next = lend + 1;
		else
			next = lend = value + strlen(value);

		const char *eq = strchr(value, '=');
		if (!eq || eq >= lend) {
			bconf_free(confdata);
			return false;
		}

		char *key = xstrndup(value, eq - value);
		char *val = xstrndup(eq + 1, lend - eq - 1);
		int r = bconf_add_data_canfail(confdata, key, val);
		free(key);
		free(val);

		if (r == -1) {
			bconf_free(confdata);
			return false;
		}
	}
	return true;
}

static bool
parse_config_value(struct bconf_node **hknode, const char *value, const char *host, const char *appl) {
	struct bconf_node *confdata = NULL;

	if (*value == '*')
		parse_old_bconf_config(&confdata, value);
	if (!confdata && json_bconf(&confdata, NULL, value, -1, 0)) {
		log_printf(LOG_ERR, "fd_pool_sd: Error decoding config value: %s", bconf_get_string(confdata, "error"));
		return false;
	}

	struct bconf_node *merged = NULL;

	config_merge_bconf(&merged, confdata, host, appl);

	bool changed = bconf_merge(hknode, merged);

	/* Don't delete the "disabled" key. */
	bconf_add_data(&merged, "disabled", "");
	if (bconf_filter_to_keys(hknode, merged))
		changed = true;

	bconf_free(&confdata);
	bconf_free(&merged);

	return changed;
}

static bool
parse_health_value(struct bconf_node **hknode, const char *value) {
	bool newval = strcmp(value, "up") == 0;

	struct bconf_node *dn;
	if ((dn = bconf_get(*hknode, "disabled"))) {
		bool oldval = !bconf_intvalue(dn);
		if (oldval == newval)
			return false;
	}

	bconf_add_data(hknode, "disabled", newval ? "0" : "1");
	return true;
}

#include "sd_command.h"

static bool
update_config(struct fd_pool_sd *fps, struct bconf_node **dst, struct sd_value *v) {
	struct sd_value *nv;

	bool res = false;
	for ( ; v ; v = nv) {
		nv = SLIST_NEXT(v, list);

		enum sd_command c;
		GPERF_ENUM(sd_command)
		switch ((c = lookup_sd_command(v->keyv[0], -1))) {
		case GPERF_CASE("flush"):
			bconf_free(dst);
			bconf_merge(dst, fps->static_conf);
			res = true;
			break;
		case GPERF_CASE("delete"):
			if (v->keyc >= 2) {
				const char *hostkey = v->keyv[1];
				res = bconf_deletev(dst, 2, (const char *[]){"host", hostkey});
			}
			break;
		case GPERF_CASE("config"):
		case GPERF_CASE("health"):
			if (v->keyc >= 2) {
				const char *hostkey = v->keyv[1];
				const char *value = v->value;
				struct bconf_node *hknode = bconf_add_listnodev(dst, 2, (const char *[]){"host", hostkey});

				bool upd = false;
				if (c == SC_CONFIG)
					upd = parse_config_value(&hknode, value, fps->host, fps->appl);
				else
					upd = parse_health_value(&hknode, value);
				/* Only set res to true if the node is complete. */
				if (upd && bconf_get(hknode, "name") && bconf_get(hknode, "disabled"))
					res = true;
			}
			break;
		case GPERF_CASE_NONE:
			break;
		}

		if (v->index > fps->max_index)
			fps->max_index = v->index; /* Assuming this is an atomic assign. */
		sd_free_value(v);
	}

	return res;
}

static void *
fd_pool_sd_thread(void *v) {
	struct fd_pool_sd *fps = v;

	struct bconf_node *confroot = NULL;

	/* Inspected by tests. */
	fps->confroot = &confroot;

	if (fps->static_conf)
		bconf_merge(&confroot, fps->static_conf);

	while (fps->running) {
		struct sd_value *sv = sd_queue_wait(fps->queue, QUEUEWAIT_MS);
		if (!sv)
			continue;

		bool updated = update_config(fps, &confroot, sv);
		if (updated) {
			struct vtree_chain vtree = {0};
			log_printf(LOG_DEBUG, "fd_pool_sd: Updating service %s", fps->service);
			int n = fd_pool_update_hosts(fps->pool, fps->service, bconf_vtree(&vtree, confroot));
			if (n > 0) {
				// Keep max_index invalid until we have at least one node via SD.
				fps->max_index_valid = true;
			} else if (n < 0) {
				char buf[256];
				log_printf(LOG_ERR, "fd_pool_sd: Updating failed: %s", fd_pool_strerror(n, buf, sizeof(buf)));
			} else {
				log_printf(LOG_DEBUG, "fd_pool_sd: Update failed due to no nodes");
			}
			vtree_free(&vtree);
		}
		pthread_cond_broadcast(&fps->update_signal);
	}

	bconf_free(&confroot);

	return NULL;
}

int
fd_pool_sd_start(struct fd_pool_sd *fps) {
	if (fps->running)
		return 0;

	fps->running = true;
	if (pthread_create(&fps->thread, NULL, fd_pool_sd_thread, fps))
		return -1;
	return 0;
}

int
fd_pool_sd_stop(struct fd_pool_sd *fps) {
	if (!fps->running)
		return 0;

	fps->running = false;
	pthread_join(fps->thread, NULL);
	return 0;
}

int
fd_pool_sd_wait_index(struct fd_pool_sd *fps, uint64_t index, int timeout_ms) {
	if (fps->max_index_valid && fps->max_index >= index)
		return 0;

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_ms / 1000;
	ts.tv_nsec += (timeout_ms % 1000) * 1000000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000;
	}

	int ret = 0;
	pthread_mutex_t dummylock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_lock(&dummylock);
	while (!fps->max_index_valid || fps->max_index < index) {
		if (pthread_cond_timedwait(&fps->update_signal, &dummylock, &ts)) {
			if (!fps->max_index_valid || fps->max_index < index)
				ret = 1;
			break;
		}
	}
	pthread_mutex_unlock(&dummylock);
	pthread_mutex_destroy(&dummylock);

	return ret;
}
