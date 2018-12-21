// Copyright 2018 Schibsted

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "sbalance.h"
#include "sbp/atomic.h"

#include "memalloc_functions.h"


struct sb_strategy {
	void (*strat_init)(struct sbalance_connection *, uint32_t hash);
	void (*strat_reinit)(struct sbalance_connection *);
	int (*strat_next)(struct sbalance_connection *);
};

void sbalance_seq_init(struct sbalance_connection *, uint32_t);
void sbalance_seq_reinit(struct sbalance_connection *);
int sbalance_seq_next(struct sbalance_connection *);

void sbalance_hash_init(struct sbalance_connection *, uint32_t);
void sbalance_rand_init(struct sbalance_connection *, uint32_t);
void sbalance_rand_reinit(struct sbalance_connection *);
int sbalance_rcycle_next(struct sbalance_connection *);

static struct sb_strategy strat_seq = { sbalance_seq_init, sbalance_seq_reinit, sbalance_seq_next };
static struct sb_strategy strat_rand = { sbalance_rand_init, sbalance_rand_reinit, sbalance_rcycle_next };
static struct sb_strategy strat_hash = { sbalance_hash_init, sbalance_rand_reinit, sbalance_rcycle_next };

struct sbalance *
sbalance_create(unsigned int retries, unsigned int failcost, unsigned int softfailcost, enum sbalance_strat strat) {
	struct sbalance *sb = zmalloc(sizeof(*sb));

	sb->sb_retries = retries;
	sb->sb_failcost = failcost;
	sb->sb_softfailcost = softfailcost;
	switch(strat) {
	case ST_SEQ:
		sb->sb_strat = &strat_seq;
		break;
	case ST_RANDOM:
		sb->sb_strat = &strat_rand;
		break;
	case ST_HASH:
		sb->sb_strat = &strat_hash;
		break;
	}

	sb->sb_refs = 1;
	return sb;
}

struct sbalance *
sbalance_retain(struct sbalance *sb) {
	atomic_xadd_int(&sb->sb_refs, 1);
	return sb;
}

void
sbalance_release(struct sbalance *sb, void (*free_f)(void *)) {
	unsigned int i;

	if (atomic_xadd_int(&sb->sb_refs, -1) > 1)
		return;

	if (free_f != NULL) {
		for (i = 0; i < sb->sb_nserv; i++) {
			(*free_f)(sb->sb_service[i].data);
		}
	}
	free(sb->sb_service);
	free(sb);
}

/*
 * Add a service to the pool. Services are sorted in descending cost order.
 */
void
sbalance_add_serv(struct sbalance *sb, int cost, void *data) {
	if (cost == 0)
		cost = 1;

	sb->sb_service = xrealloc(sb->sb_service, sizeof(*sb->sb_service) * (sb->sb_nserv + 1));
	sb->sb_service[sb->sb_nserv].cost = cost;
	sb->sb_service[sb->sb_nserv].tempfailcost = 0;
	sb->sb_service[sb->sb_nserv].data = data;
	sb->sb_nserv++;
}

enum sbalance_strat
sbalance_strat(struct sbalance *sb) {
	if (sb->sb_strat == &strat_seq)
		return ST_SEQ;
	if (sb->sb_strat == &strat_rand)
		return ST_RANDOM;
	return ST_HASH;
}

void
sbalance_conn_new(struct sbalance_connection *sc, struct sbalance *sb, uint32_t hash) {
	sc->sc_sb = sbalance_retain(sb);

	(*sb->sb_strat->strat_init)(sc, hash);
}

void *
sbalance_conn_next(struct sbalance_connection *sc, enum sbalance_conn_status status) {
	struct sbalance *sb = sc->sc_sb;
	int newcost = 0;

	if (sb->sb_nserv == 0)
		return NULL;

	/*
	 * If the last connection failed for some reason, account for it in the temporary
	 * cost. Note, that we don't recalculate anything for this connection, this is
	 * for future use.
	 */
	if (status != SBCS_START) {
		if (status == SBCS_FAIL)
			newcost = sb->sb_failcost;
		else
			newcost = sb->sb_softfailcost;

		sb->sb_service[sc->sc_last].tempfailcost = newcost;
	}


	if (sc->sc_first == 0) {
		if (sc->sc_retries-- == 0)
			return (NULL);
		(*sb->sb_strat->strat_reinit)(sc);
	}

	sc->sc_first--;

	return sc->sc_sb->sb_service[sc->sc_last = (*sb->sb_strat->strat_next)(sc)].data;
}

void
sbalance_conn_done(struct sbalance_connection *sc) {
	struct sbalance *sb = sc->sc_sb;

	/*
	 * If we haven't exhausted retries, it means that we succeeded to
	 * connect to a service. In that case, if the service was under
	 * a temporary failure it means that the failure status can
	 * be cleared and the service can return to the normal cost.
	 */

	if (sb && sb->sb_nserv && sc->sc_retries >= 0 && sb->sb_service[sc->sc_last].tempfailcost) {
		sb->sb_service[sc->sc_last].tempfailcost = 0;
	}
}

void
sbalance_seq_init(struct sbalance_connection *sc, uint32_t hash) {
	sbalance_seq_reinit(sc);
	sc->sc_retries = sc->sc_sb->sb_retries;
}

void
sbalance_seq_reinit(struct sbalance_connection *sc) {
	sc->sc_offs = 0;
	sc->sc_first = sc->sc_sb->sb_nserv;
}

int
sbalance_seq_next(struct sbalance_connection *sc) {
	return sc->sc_offs++;
}

void
sbalance_rand_init(struct sbalance_connection *sc, uint32_t hash) {
	sbalance_hash_init(sc, arc4random());
}

void
sbalance_rand_reinit(struct sbalance_connection *sc) {
	struct sbalance *sb = sc->sc_sb;

	rcycle_init(&sc->sc_rc, sb->sb_nserv, arc4random());
	sc->sc_first = sb->sb_nserv;
	sc->sc_offs = -1;
}

void
sbalance_hash_init(struct sbalance_connection *sc, uint32_t hash) {
	struct sbalance *sb = sc->sc_sb;
	unsigned int pick = 0;
	unsigned int i;
	double tw = 0.0;

#ifdef __GLIBC__
	struct drand48_data rd;
	srand48_r(hash, &rd);
#else
	srand48_deterministic(hash);
#endif

	for (i = 0; i < sb->sb_nserv; i++) {
		double w = 1.0/(double)(sb->sb_service[i].tempfailcost ?: sb->sb_service[i].cost);
		double r;

		tw += w;
#ifdef __GLIBC__
		drand48_r(&rd, &r);
#else
		r = drand48();
#endif

		if ((w / tw) > r)
			pick = i;
	}

	sc->sc_offs = pick;
	sc->sc_first = 1;
	sc->sc_retries = sb->sb_retries;
}

int
sbalance_rcycle_next(struct sbalance_connection *sc) {
	if (sc->sc_offs != -1)
		return sc->sc_offs;
	else
		return rcycle_generate(&sc->sc_rc);
}

uint32_t
sbalance_hash_string(const char *str) {
	uint32_t hash = 2166136261;

	while (*str)
		hash = (hash * 16777619) ^ *str++;

	return hash;
}

