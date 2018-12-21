// Copyright 2018 Schibsted

#ifndef COMMON_SBALANCE_H
#define COMMON_SBALANCE_H

/*
 * Weighted load balancing of a group of services.
 *
 * A group of services is created with sbalance_init and
 * services are added with sbalance_add_serv.
 *
 * The users create new connection with sbalance_conn_new and
 * then iterate over the various services using sbalance_conn_next.
 *
 * The hash argument to sbalance_conn_new specifies a client hash,
 * 0 meaning no hash and that the global connection cycling should
 * be used.
 *
 * The status argument to sbalance_conn_next specifies why the next
 * connection is attempted, the assumption being that if the first
 * service didn't work out, there must be a reason why. Normally
 * the user will cycle over the services multiple times attempting
 * to connect before giving up, but total failures will be limited
 * only to the number of services.
 *
 * For total failures we add a large backoff factor to that service
 * and only retry as often as the cost permits us. When a connection
 * to a failed service succeeds the temporary fail cost is halved.
 *
 * Please notice that the client hashes only work for static weights.
 * When temporary failures start happening client hashes lose their
 * worth.
 *
 * Another thing worth noticing is that the weight doesn't directly
 * cause a service to take more load. It's just that the service is
 * more likely to be chosen for the array of services to try for
 * one connection. 
 *
 * sbalance_conn_done should be called so that we can know when a
 * failed connection has started to respond again.
 */

#include "sbp/rcycle.h"

#include <stdbool.h>

struct sbalance_strategy;

struct sbalance {
	unsigned int sb_nserv;
	unsigned int sb_retries;
	unsigned int sb_failcost;
	unsigned int sb_softfailcost;

	struct sb_service {
		unsigned int cost;
		unsigned int tempfailcost;
		void *data;
	} *sb_service;

	struct sb_strategy *sb_strat;
	int sb_refs;
};

struct sbalance_connection {
	struct sbalance *sc_sb;
	int sc_retries;
	int sc_first;
	uint32_t hash;

	struct rcycle sc_rc;
	int sc_offs;

	unsigned int sc_last;
};

enum sbalance_conn_status {
	SBCS_START,
	SBCS_FAIL,
	SBCS_TEMPFAIL
};

enum sbalance_strat {
	ST_SEQ,
	ST_RANDOM,
	ST_HASH
};

struct sbalance *sbalance_create(unsigned int retries, unsigned int failcost, unsigned int softfailcost, enum sbalance_strat);

struct sbalance *sbalance_retain(struct sbalance *sb);
void sbalance_release(struct sbalance *sb, void (*free_f)(void *));

void sbalance_add_serv(struct sbalance *sb, int cost, void *data);

enum sbalance_strat sbalance_strat(struct sbalance *sb);

uint32_t sbalance_hash_string(const char *);

void sbalance_conn_new(struct sbalance_connection *sc, struct sbalance *sb, uint32_t hash);
void *sbalance_conn_next(struct sbalance_connection *sc, enum sbalance_conn_status status);
void sbalance_conn_done(struct sbalance_connection *sc);

#endif /*COMMON_SBALANCE_H*/
