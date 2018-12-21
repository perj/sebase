// Copyright 2018 Schibsted

#ifndef SD_REGISTRY_H
#define SD_REGISTRY_H

#include "daemon.h"
#include "sbp/linker_set.h"

#include <stdint.h>

struct bconf_node;
struct vtree_chain;
struct fd_pool;
struct https_state;
struct sd_queue;
struct sd_registry;
struct sdr_conn;

struct sd_registry *sd_registry_create(const char *host, const char *appl, struct https_state *https);
void sd_registry_free(struct sd_registry *sdr);

const char *sd_registry_get_host(struct sd_registry *sdr);
const char *sd_registry_get_appl(struct sd_registry *sdr);

void sd_registry_add_sources(struct sd_registry *sdr, struct vtree_chain *vtree);

/* Node can be NULL, defaults will be used if so. */
struct sdr_conn *sd_registry_connect_fd_pool(struct sd_registry *sdr, struct fd_pool *pool, const char *service, struct vtree_chain *node);
void sd_registry_disconnect(struct sdr_conn *sdconn);

void sd_registry_set_initial_wait_ms(struct sdr_conn *sdconn, int ms);

/* Poke the sdconn that we'll be using it. Can be called for each new usage. */
void sd_registry_new_conn(struct sdr_conn *sdconn);

/* Call fd_pool_sd_wait_index (or similar). */
int sd_registry_wait_index(struct sdr_conn *sdconn, uint64_t index, int timeout_ms);

void sd_registry_setup_bos_client(struct bconf_node *conf, struct https_state *https);
void sd_registry_setup_bos_multiclient(struct bconf_node *conf, struct https_state *https, const char * const *services);

/* Returns the hostkey to used, if it can be determined. */
struct sd_registry_hostkey;
const char *sd_registry_hostkey(struct sd_registry_hostkey *hk);
void sd_registry_hostkey_free(struct sd_registry_hostkey *hk);

/* Linker set registries */

struct sd_registry_source {
	const char *name;
	const char *node_key;
	int (*setup)(void **srcdata, struct vtree_chain *node, struct https_state *https);
	void (*cleanup)(void *srcdata);
	void *(*connect)(void *srcdata, const char *service, struct vtree_chain *node, struct sd_queue **queue);
	void (*disconnect)(void *srcdata, void *v);
};
#define SD_REGISTRY_ADD_SOURCE(name, node_key, setup_fn, cleanup_fn, connect_fn, disconnect_fn) \
	struct sd_registry_source sdr_source_##name = { \
		#name, \
		node_key, \
		setup_fn, \
		cleanup_fn, \
		connect_fn, \
		disconnect_fn, \
	}; \
	LINKER_SET_ADD_DATA(sdr_source, sdr_source_##name)

struct sd_registry_bos_client {
	const char *name;
	const char *node_key;
	void *(*setup)(const char *service, struct bconf_node *conf, struct bconf_node **sdconf, struct sd_registry_hostkey **hk, struct https_state *https);
	void (*bos_event)(enum bos_event bev, int arg, void *cbarg);
};
#define SD_REGISTRY_ADD_BOS_CLIENT(name, node_key, setup_fn, bos_event_fn) \
	struct sd_registry_bos_client sdr_bos_##name = { \
		#name, \
		node_key, \
		setup_fn, \
		bos_event_fn, \
	}; \
	LINKER_SET_ADD_DATA(sdr_bos, sdr_bos_##name)

#endif /*SD_REGISTRY_H*/
