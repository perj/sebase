// Copyright 2018 Schibsted

#include "sd_registry.h"

#include "sbp/bconf.h"
#include "sbp/buf_string.h"
#include "fd_pool.h"
#include "fd_pool_sd.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sbp/plog.h"
#include "sbp/vtree.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct sd_registry {
	char *host;
	char *appl;

	pthread_mutex_t lock;
	struct bconf_node *source_data;
	struct https_state *https;
};

enum sd_dest {
	SDD_FD_POOL,
};

struct sdr_conn {
	struct sd_registry_source *src_type;
	void *srcdata;
	void *src;
	union {
		struct fd_pool_sd *fps;
	} dest;
	enum sd_dest dest_type;
	int initial_wait_ms;
};

struct sd_registry *
sd_registry_create(const char *host, const char *appl, struct https_state *https) {
	struct sd_registry *sdr = zmalloc(sizeof(*sdr));
	char hostbuf[256];

	if (!host || !*host) {
		if (gethostname(hostbuf, sizeof(hostbuf))) {
			host = "unknwon";
		} else {
			char *p = strchr(hostbuf, '.');
			if (p)
				*p = '\0';
			host = hostbuf;
		}
	}

	sdr->host = xstrdup(host);
	sdr->appl = xstrdup(appl);
	sdr->https = https;

	pthread_mutex_init(&sdr->lock, NULL);

	return sdr;
}

void
sd_registry_free(struct sd_registry *sdr) {
	if (!sdr)
		return;
	struct bconf_node *knode, *vnode;
	for (int i = 0 ; (knode = bconf_byindex(sdr->source_data, i)) ; i++) {
		for (int j = 0 ; (vnode = bconf_byindex(knode, j)) ; j++) {
			void *srcdata = bconf_binvalue(bconf_get(vnode, "data"));
			struct sd_registry_source *src_type = bconf_binvalue(bconf_get(vnode, "type"));

			src_type->cleanup(srcdata);
		}
	}
	bconf_free(&sdr->source_data);

	pthread_mutex_destroy(&sdr->lock);

	free(sdr->host);
	free(sdr->appl);
	free(sdr);
}

const char *
sd_registry_get_host(struct sd_registry *sdr) {
	return sdr->host;
}

const char *
sd_registry_get_appl(struct sd_registry *sdr) {
	return sdr->appl;
}

void
sd_registry_add_sources(struct sd_registry *sdr, struct vtree_chain *vtree) {
	pthread_mutex_lock(&sdr->lock);

	LINKER_SET_DECLARE(sdr_source, struct sd_registry_source);
	struct sd_registry_source *const *sdrs;
	LINKER_SET_FOREACH(sdrs, sdr_source) {
		const char *key = (*sdrs)->node_key;
		const char *value = vtree_get(vtree, "sd", key, NULL);

		if (!key || !value)
			continue;

		if (bconf_vget(sdr->source_data, key, value, NULL)) {
			/* Already added. */
			continue;
		}

		void *v = NULL;
		int r = (*sdrs)->setup(&v, vtree, sdr->https);
		if (r != 0)
			continue;

		log_printf(LOG_DEBUG, "%s SD registry at %s added, instance %p", (*sdrs)->name, value, v);
		bconf_add_bindatav(&sdr->source_data, 3, (const char*[]){key, value, "data"}, v, BCONF_REF);
		bconf_add_bindatav(&sdr->source_data, 3, (const char*[]){key, value, "type"}, *sdrs, BCONF_REF);
	}
	pthread_mutex_unlock(&sdr->lock);
}

struct sdr_conn *
sd_registry_connect_fd_pool(struct sd_registry *sdr, struct fd_pool *pool, const char *service, struct vtree_chain *node) {
	if (!sdr || !pool)
		return NULL;

	struct sd_registry_source *src_type = NULL;
	void *srcdata = NULL;
	void *src = NULL;
	struct sd_queue *queue = NULL;

	LINKER_SET_DECLARE(sdr_source, struct sd_registry_source);
	struct sd_registry_source *const *sdrs;

	if (node) {
		/* First look for matching setups. */
		LINKER_SET_FOREACH(sdrs, sdr_source) {
			const char *key = (*sdrs)->node_key;
			const char *value = vtree_get(node, "sd", key, NULL);

			if (!key || !value)
				continue;

			pthread_mutex_lock(&sdr->lock);
			struct bconf_node *sn = bconf_vget(sdr->source_data, key, value, NULL);
			pthread_mutex_unlock(&sdr->lock);
			if (sn) {
				src_type = *sdrs;
				srcdata = bconf_binvalue(bconf_get(sn, "data"));
				src = src_type->connect(srcdata, service, node, &queue);
				if (queue)
					break;
			}
		}
	}
	if (!queue) {
		/* Now let any configured source grab it. */
		pthread_mutex_lock(&sdr->lock);
		struct bconf_node *knode, *vnode;
		for (int i = 0 ; (knode = bconf_byindex(sdr->source_data, i)) ; i++) {
			for (int j = 0 ; (vnode = bconf_byindex(knode, j)) ; j++) {
				srcdata = bconf_binvalue(bconf_get(vnode, "data"));
				src_type = bconf_binvalue(bconf_get(vnode, "type"));

				src = src_type->connect(srcdata, service, node, &queue);
				if (queue)
					break;
			}
		}
		pthread_mutex_unlock(&sdr->lock);
	}
	if (!queue)
		return NULL;
	log_printf(LOG_DEBUG, "FD pool connected to %s SD registry instance %p",
			src_type->name, srcdata);

	struct fd_pool_sd *fps = fd_pool_sd_create(pool, sdr->host, sdr->appl, service, queue);
	if (!fps) {
		log_printf(LOG_CRIT, "sd_registry(%s): Failed to create fd_pool_sd: %m", service);
		goto fail;
	}

	if (vtree_getint(node, "sd", "merge", NULL))
		fd_pool_sd_copy_static_config(fps, node);

	if (fd_pool_sd_start(fps)) {
		log_printf(LOG_CRIT, "sd_registry(%s): Failed to start fd_pool_sd", service);
		goto fail;
	}

	struct sdr_conn *sdconn = zmalloc(sizeof(*sdconn));
	sdconn->src_type = src_type;
	sdconn->srcdata = srcdata;
	sdconn->src = src;
	sdconn->dest_type = SDD_FD_POOL;
	sdconn->dest.fps = fps;

	/* Can't wait here since our thread is probably holding the fd pool sblock.
	 * Set it up but do the wait in new_conn.
	 */
	sdconn->initial_wait_ms = vtree_getint(node, "sd", "initial_wait_ms", NULL);

	return sdconn;

fail:
	if (fps)
		fd_pool_sd_free(fps);
	if (src)
		src_type->disconnect(srcdata, src);
	return NULL;
}

void
sd_registry_disconnect(struct sdr_conn *sdconn) {
	switch (sdconn->dest_type) {
	case SDD_FD_POOL:
		fd_pool_sd_free(sdconn->dest.fps);
		break;
	}
	sdconn->src_type->disconnect(sdconn->srcdata, sdconn->src);
	free(sdconn);
}

void
sd_registry_set_initial_wait_ms(struct sdr_conn *sdconn, int ms) {
	sdconn->initial_wait_ms = ms;
}

void
sd_registry_new_conn(struct sdr_conn *sdconn) {
	if (sdconn->initial_wait_ms)
		sd_registry_wait_index(sdconn, 1, sdconn->initial_wait_ms);
	sdconn->initial_wait_ms = 0;
}

int
sd_registry_wait_index(struct sdr_conn *sdconn, uint64_t index, int timeout_ms) {
	switch (sdconn->dest_type) {
	case SDD_FD_POOL:
		return fd_pool_sd_wait_index(sdconn->dest.fps, index, timeout_ms);
	}
	return -1;
}

static void
lazy_init_sdconf(struct bconf_node **sdconf, struct bconf_node *conf) {
	if (*sdconf)
		return;

	bconf_merge(sdconf, bconf_get(conf, "sd.value"));

	struct bconf_node *dvroot = bconf_get(conf, "sd.dynval");
	struct buf_string dkbuf = {0};
	for (int i = 0 ; i < bconf_count(dvroot) ; i++) {
		struct bconf_node *dv = bconf_byindex(dvroot, i);
		struct bconf_node *knode = bconf_get(dv, "key");

		if (!bconf_count(knode))
			continue;

		const char *v = bconf_get_string(dv, "value");
		if (!v) {
			const char *ref = bconf_get_string(dv, "value_key");
			v = bconf_get_string(conf, ref);
		}
		if (!v)
			continue;

		dkbuf.pos = 0;
		int j;
		for (j = 1 ; j <= bconf_count(knode) ; j++) {
			char jbuf[20];
			snprintf(jbuf, sizeof(jbuf), "%d", j);
			const char *k = bconf_vget_string(knode, jbuf, "value", NULL);
			if (!k)
				break;

			if (j > 1)
				bswrite(&dkbuf, ".", 1);
			bswrite(&dkbuf, k, strlen(k));
		}
		if (j <= bconf_count(knode) || dkbuf.pos == 0)
			continue;

		bconf_add_data(sdconf, dkbuf.buf, v);
	}
	free(dkbuf.buf);
}

struct sd_registry_hostkey {
	char *value;
	char *path;
};

const char *
sd_registry_hostkey(struct sd_registry_hostkey *hk) {
	if (hk->value)
		return hk->value;
	if (hk->path) {
		FILE *f = fopen(hk->path, "r");
		if (!f) {
			if (errno == ENOENT)
				return NULL;
			xerr(1, "SD: fopen(%s)", hk->path);
		}
		char hkbuf[1024];
		bool ok = fgets(hkbuf, sizeof(hkbuf), f) != NULL;
		fclose(f);
		if (!ok)
			return NULL;
		const char *nl = strchr(hkbuf, '\n');
		if (nl == hkbuf)
			return NULL;
		if (nl)
			hk->value = xstrndup(hkbuf, nl - hkbuf);
		else
			hk->value = xstrdup(hkbuf);
		return hk->value;
	}
	abort();
}

void
sd_registry_hostkey_free(struct sd_registry_hostkey *hk) {
	if (!hk)
		return;
	free(hk->value);
	free(hk->path);
	free(hk);
}

static void
lazy_init_hostkey(struct sd_registry_hostkey **hk, struct bconf_node *conf) {
	if (*hk)
		return;
	*hk = zmalloc(sizeof(**hk));
	const char *source = bconf_get_string(conf, "sd.host.key.source");
	if (!source || !*source) {
		if (bconf_get(conf, "sd.host.key.value"))
			source = "value";
		else
			source = "random";
	}

	if (strcmp(source, "value") == 0) {
		const char *v = bconf_get_string(conf, "sd.host.key.value");
		if (!v)
			xerrx(1, "SD: No hostkey value configured");
		(*hk)->value = xstrdup(v);
		return;
	}
	if (strcmp(source, "file") == 0) {
		const char *p = bconf_get_string(conf, "sd.host.key.path");
		if (!p)
			xerrx(1, "SD: No hostkey path configured");
		(*hk)->path = xstrdup(p);
		return;
	}
	xasprintf(&(*hk)->value, "%08x-%04x-4%03x-%04x-%04x%08x",
			(int)arc4random(), (int)arc4random() & 0xFFFF, (int)arc4random() & 0xFFF,
			((int)arc4random() & 0x3FFF) | 0x8000, (int)arc4random() & 0xFFFF, (int)arc4random());
}

void
sd_registry_setup_bos_multiclient(struct bconf_node *conf, struct https_state *https, const char * const *services)
{
	const char *healthcheck_url = bconf_get_string(conf, "sd.healthcheck.url");
	LINKER_SET_DECLARE(sdr_bos, struct sd_registry_bos_client);
	struct sd_registry_bos_client *const *sdb;
	struct sd_registry_hostkey *hk = NULL;
	struct bconf_node *sdconf = NULL;
	bool registered = false;

	if (!healthcheck_url)
		return;

	LINKER_SET_FOREACH(sdb, sdr_bos) {
		if (bconf_vget(conf, "sd", (*sdb)->node_key, NULL)) {
			const char * const *service;
			for (service = services; *service; service++) {
				lazy_init_sdconf(&sdconf, conf);
				lazy_init_hostkey(&hk, conf);
				void *v = (*sdb)->setup(*service, conf, &sdconf, &hk, https);
				if (v) {
					set_bos_cb((*sdb)->bos_event, v);
					registered = true;
				}
			}
		}
		if (registered)
			break;
	}
	if (registered) {
		int hc_int_s = bconf_get_int_default(conf, "sd.healthcheck.interval_s", 10);
		int hc_unavail_s = bconf_get_int_default(conf, "sd.healthcheck.unavailable_interval_ms", 1000);
		int hc_unavail_limit = bconf_get_int_default(conf, "sd.healthcheck.unavailable_limit", 2);
		set_healthcheck_url(hc_int_s, hc_unavail_s, hc_unavail_limit, "%s", healthcheck_url);
	} else {
		bconf_free(&sdconf);
		sd_registry_hostkey_free(hk);
	}
}

void
sd_registry_setup_bos_client(struct bconf_node *conf, struct https_state *https) {
	const char *service = bconf_get_string(conf, "sd.service");
	if (service == NULL)
		return;
	sd_registry_setup_bos_multiclient(conf, https, (const char *const [2]) { service, NULL });
}
