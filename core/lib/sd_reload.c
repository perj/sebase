// Copyright 2018 Schibsted

#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sd_registry.h"
#include "sd_queue.h"
#include "sbp/vtree.h"

#include <pthread.h>
#include <unistd.h>

struct sd_reload
{
	int index;
	int sleep_s;

	struct sd_queue queue;

	pthread_t thread;
};

static void *
sd_reload_thread(void *v) {
	struct sd_reload *data = v;

	while (1) {
		sleep(data->sleep_s);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		int qst;
		sd_queue_begin(&data->queue, &qst);
		struct sd_value *flush = sd_create_value(++data->index, 1, (const char*[]){"flush"}, NULL, "", 0);
		sd_queue_insert(&data->queue, flush);
		sd_queue_commit(&data->queue, &qst);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	}

	return NULL;
}

static int
sd_reload_setup(void **srcdata, struct vtree_chain *node, struct https_state *https) {
	return 0;
}

static void
sd_reload_cleanup(void *srcdata) {
}

static void *
sd_reload_connect(void *srcdata, const char *service, struct vtree_chain *node, struct sd_queue **queue) {
	if (!vtree_haskey(node, "sd", "reload_s", NULL)) {
		/* Silently reject "free for all" grabs, don't make sense for us. */
		return NULL;
	}

	if (!vtree_getint(node, "sd", "merge", NULL)) {
		log_printf(LOG_CRIT, "sd_registry(%s): sd.reload_s set without sd.merge", service);
		return NULL;
	}
	if (!vtree_getlen(node, "host", NULL)) {
		log_printf(LOG_CRIT, "sd_registry(%s): sd.reload_s set but no nodes configured", service);
		return NULL;
	}

	struct sd_reload *data = zmalloc(sizeof(*data));
	data->sleep_s = vtree_getint(node, "sd", "reload_s", NULL);
	if (data->sleep_s < 5) {
		log_printf(LOG_WARNING, "sd_registry(%s): Ignoring reload_s %d. Using minimum of 5", service, data->sleep_s);
		data->sleep_s = 5;
	}
	sd_queue_init(&data->queue);
	if (pthread_create(&data->thread, NULL, sd_reload_thread, data)) {
		log_printf(LOG_CRIT, "sd_registry(%s): Failed to start reload thread", service);
		sd_queue_destroy(&data->queue);
		free(data);
		return NULL;
	}

	*queue = &data->queue;
	return data;
}

static void
sd_reload_disconnect(void *srcdata, void *v) {
	struct sd_reload *data = v;

	pthread_cancel(data->thread);
	pthread_join(data->thread, NULL);
	free(data);
}

SD_REGISTRY_ADD_SOURCE(sd_reload, "reload_s", sd_reload_setup, sd_reload_cleanup, sd_reload_connect, sd_reload_disconnect);
