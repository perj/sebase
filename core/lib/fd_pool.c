// Copyright 2018 Schibsted

#include "sbp/avl.h"
#include "sbp/buf_string.h"
#include "fd_pool.h"
#include "sbp/json_vtree.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sbp/sock_util.h"
#include "sbp/plog.h"
#include "sbp/queue.h"
#include "sbp/sbalance.h"
#include "sd_registry.h"
#include "sbp/string_functions.h"
#include "sbp/timer.h"
#include "sbp/url.h"
#include "sbp/vtree.h"
#include "sbp/vtree_literal.h"
#if __has_include("sbp/xxhash.h")
#include "sbp/xxhash.h"
#else
#include <xxhash.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/un.h>
#include <limits.h>
#include <string.h>

#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif

struct fd_pool_entry {
	SLIST_ENTRY(fd_pool_entry) link;
	int fd;
};

struct fd_pool_port {
	SLIST_ENTRY(fd_pool_entry) link;
	char port_key[32];
	struct sockaddr_storage sockaddr;
	socklen_t addrlen;
	char peer[256];
	struct plog_ctx *count_ctx;
	SLIST_HEAD(, fd_pool_entry) entries;
	struct fd_pool_entry *last_ptr;
};

struct fd_pool_node {
	TAILQ_ENTRY(fd_pool_node) link;
	struct fd_pool *pool;
	int socktype;
	int nports;
	struct fd_pool_port *ports;
	pthread_mutex_t lock;
	int cost;
	int refs;
};

struct fd_pool_service_node {
	char *key;
	struct fd_pool_node *node;
	struct plog_ctx *count_ctx;
};

struct fd_pool_service {
	struct avl_node tree;
	char *service;
	struct sbalance *sb;
	pthread_rwlock_t sblock;
	uint64_t sb_gen;
	bool cycle_last;
	struct addrinfo hints;
	int timeoutms;
	struct sdr_conn *sdconn;
	struct plog_ctx *count_ctx;
};

struct fd_pool {
	SLIST_HEAD(, fd_pool_entry) free_entries;
	pthread_mutex_t lock;
	struct plog_ctx *ports_ctx;
	struct plog_ctx *services_ctx;
	TAILQ_HEAD(, fd_pool_node) all_nodes;
	struct avl_node *services;
	struct sd_registry *sdr;

	struct vtree_chain *upmap;
};

struct fd_pool_conn {
	struct fd_pool *pool;
	struct fd_pool_service *srv;
	struct fd_pool_service_node *sn;
	struct fd_pool_port *port;
	struct fd_pool_entry *entry;
	struct sbalance_connection sc;
	uint64_t sb_gen;
	char *port_key;
	char *pk_ptr;
	int async;
	uint32_t sc_hash;
	struct timer_instance *ti;
	char *node_filter;
	bool silent;
	bool active_fd;
	bool nonblock;
	void *aux;
};

#define LOCK(x) pthread_mutex_lock((x))
#define UNLOCK(x) pthread_mutex_unlock((x))

static const struct addrinfo default_hints = {
	.ai_flags = AI_ADDRCONFIG,
	.ai_socktype = SOCK_STREAM
};

static struct vtree_chain default_upmap = {
	.fun = &vtree_literal_vtree,
	.data = &(struct vtree_keyvals){
		.type = vktDict,
		.len = 6, // Must be in sync with list length.
		.list = (struct vtree_keyvals_elem[]){
			VTREE_LITERAL_VALUE("80", "http_port"),
			VTREE_LITERAL_VALUE("443", "http_port"),
			VTREE_LITERAL_VALUE("8080", "port"),
			VTREE_LITERAL_VALUE("8081", "controller_port"),
			VTREE_LITERAL_VALUE("8082", "keepalive_port,port"),
			VTREE_LITERAL_VALUE("8180", "plog_port"),
		},
	}
};

static const char *
fd_pool_upmap_lookup(struct vtree_chain *upmap, const char *port_key) {
	if (!port_key || !*port_key)
		return "port";
	const char *v = vtree_get(upmap, port_key, NULL);
	return v ?: port_key;
}

static struct fd_pool_node *
fd_pool_fetch_node(const struct fd_pool *pool, int socktype, int nports, struct fd_pool_port ports[nports]) {
	struct fd_pool_node *n;
	TAILQ_FOREACH(n, &pool->all_nodes, link) {
		if (socktype != n->socktype)
			continue;
		struct fd_pool_port *pa, *pb;
		int i;
		for (i = 0 ; i < nports ; i++) {
			pa = &ports[i];
			bool found = false;
			for (int j = 0 ; j < n->nports ; j++) {
				pb = &n->ports[j];
				if (strcmp(pa->port_key, pb->port_key) != 0)
					continue;
				if (pa->addrlen != pb->addrlen)
					continue;
				if (memcmp(&pa->sockaddr, &pb->sockaddr, pa->addrlen) != 0)
					continue;
				found = true;
				break;
			}
			if (!found)
				break;
		}
		if (i < nports)
			continue;
		n->refs++;
		return n;
	}
	return NULL;
}

static char *
fd_pool_mangle_peer(char *buf, size_t bufsz, const char *peer) {
	strlcpy(buf, peer, bufsz);
	for (char *p = buf ; *p ; p++) {
		if (*p == '.' || *p == '#' || isspace(*p))
			*p = '-';
	}
	return buf;
}

static struct plog_ctx *
fd_pool_open_count_ctx(const struct fd_pool *pool, const char *peer, int cost, const char *port_key) {
	char nodebuf[1024];

	fd_pool_mangle_peer(nodebuf, sizeof(nodebuf), peer);

	struct plog_ctx *ctx = plog_open_dict(pool->ports_ctx, nodebuf);
	plog_string_printf(ctx, "cost", "%d", cost);
	plog_string(ctx, "peer", peer);
	plog_string(ctx, "port_key", port_key);
	return ctx;
}

static struct fd_pool_node *
fd_pool_node_new(struct fd_pool *pool, int socktype, int cost, int nports, struct fd_pool_port ports[nports]) {
	struct fd_pool_node *node = zmalloc(sizeof (*node) + sizeof(struct fd_pool_port[nports]));

	node->pool = pool;
	node->socktype = socktype;
	node->cost = cost;
	node->nports = nports;
	node->ports = (struct fd_pool_port*)(node + 1);
	memcpy(node->ports, ports, sizeof(struct fd_pool_port[nports]));

	for (int i = 0 ; i < nports ; i++) {
		SLIST_INIT(&node->ports[i].entries);
		node->ports[i].count_ctx = fd_pool_open_count_ctx(pool, node->ports[i].peer, cost, node->ports[i].port_key);
	}

	pthread_mutex_init(&node->lock, NULL);
	node->refs = 1;

	TAILQ_INSERT_TAIL(&pool->all_nodes, node, link);
	return node;
}

static void
fd_pool_add_node(struct fd_pool *pool, struct fd_pool_service *srv, struct sbalance *sb,
		const char *key, struct vtree_chain *vtree,
		int socktype, int nports, struct fd_pool_port ports[nports]) {
	if (!nports)
		return;

	LOCK(&pool->lock);
	struct fd_pool_node *node = fd_pool_fetch_node(pool, socktype, nports, ports);
	if (!node) {
		int cost = vtree_getint(vtree, "cost", NULL) ?: 1;
		node = fd_pool_node_new(pool, socktype, cost, nports, ports);
	}
	UNLOCK(&pool->lock);

	struct fd_pool_service_node *sn = zmalloc(sizeof(*sn) + (key ? strlen(key) + 1 : 0));
	if (key) {
		sn->key = (char*)(sn + 1);
		strcpy(sn->key, key);
		sn->count_ctx = plog_open_dict(srv->count_ctx, sn->key);
		if (sn->count_ctx) {
			for (int i = 0 ; i < node->nports ; i++) {
				char peerbuf[1024];
				plog_int(sn->count_ctx, fd_pool_mangle_peer(peerbuf, sizeof(peerbuf), node->ports[i].peer), 1);
			}
		}
	} else {
		sn->key = NULL;
	}
	sn->node = node;

	sbalance_add_serv(sb, node->cost, sn);
}

static int
ports_cmp(const struct fd_pool_port *a, const struct fd_pool_port *b) {
	size_t sza = strcspn(a->peer, " ");
	size_t szb = strcspn(b->peer, " ");

	if (sza < szb)
		return -1;
	if (sza > szb)
		return 1;
	return strncmp(a->peer, b->peer, sza);
}

static int
ports_cmp_v(const void *a, const void *b) {
	return ports_cmp(a, b);
}

static void
fd_pool_add(struct fd_pool *pool, struct fd_pool_service *srv, struct sbalance *sb,
		const char *key, struct vtree_chain *vtree,
		int socktype, int nports, struct fd_pool_port ports[nports]) {
	if (!nports)
		return;

	/* Split on IPs, as different IPs should be different nodes. */
	qsort(ports, nports, sizeof(ports[0]), ports_cmp_v);

	int start = 0, i;
	for (i = 1 ; i < nports ; i++) {
		if (ports_cmp(&ports[start], &ports[i]) != 0) {
			fd_pool_add_node(pool, srv, sb, key, vtree, socktype, i - start, ports + start);
			start = i;
		}
	}
	fd_pool_add_node(pool, srv, sb, key, vtree, socktype, i - start, ports + start);
}

static void
fd_ports_add_addrinfo(struct fd_pool_port **restrict ports, int *restrict nports, int *restrict aports, struct addrinfo *res, const char *port_key) {
	struct addrinfo *curr;

	for (curr = res; curr != NULL; curr = curr->ai_next) {
		if (*nports == *aports) {
			*aports *= 2;
			*ports = xrealloc(*ports, *aports * sizeof(**ports));
		}
		struct fd_pool_port *p = *ports + *nports;
		memset(p, 0, sizeof(*p));
		strlcpy(p->port_key, port_key, sizeof(p->port_key));
		memcpy(&p->sockaddr, curr->ai_addr, curr->ai_addrlen);
		p->addrlen = curr->ai_addrlen;

		char h[NI_MAXHOST];
		char s[NI_MAXSERV];
		if (getnameinfo(curr->ai_addr, curr->ai_addrlen, h, sizeof(h), s, sizeof(s), NI_NUMERICHOST|NI_NUMERICSERV) == 0) {
			/* We're OK with truncation here. The use of the return value silences format-truncation warnings */
			int len = snprintf(p->peer, sizeof(p->peer), "%s %s", h, s);
			if (len < 1)
				log_printf(LOG_DEBUG, "fd_pool: Failure to set peer string");
		}

		(*nports)++;
	}
}

static void
fd_ports_add_unix(struct fd_pool_port **restrict ports, int *restrict nports, int *restrict aports, const char *path, const char *port_key) {
	if (*nports == *aports) {
		*aports *= 2;
		*ports = xrealloc(*ports, *aports * sizeof(**ports));
	}
	struct fd_pool_port *p = *ports + *nports;
	memset(p, 0, sizeof(*p));
	strlcpy(p->port_key, port_key, sizeof(p->port_key));

	struct sockaddr_un *addr = (struct sockaddr_un*)&p->sockaddr;
	addr->sun_family = AF_UNIX;
	strlcpy(addr->sun_path, path, sizeof(addr->sun_path));
	p->addrlen = sizeof(*addr);

	char rpbuf[PATH_MAX];
	strlcpy(p->peer, realpath(path, rpbuf) ?: path, sizeof(p->peer));
	(*nports)++;
}

static void
fd_pool_free_node(void *v) {
	struct fd_pool_service_node *sn = v;
	struct fd_pool_node *node = sn->node;

	plog_close(sn->count_ctx);
	free(sn);

	LOCK(&node->pool->lock);
	if (--node->refs > 0) {
		UNLOCK(&node->pool->lock);
		return;
	}
	TAILQ_REMOVE(&node->pool->all_nodes, node, link);
	UNLOCK(&node->pool->lock);

	for (int i = 0 ; i < node->nports ; i++) {
		struct fd_pool_port *p = &node->ports[i];
		while (!SLIST_EMPTY(&p->entries)) {
			struct fd_pool_entry *entry = SLIST_FIRST(&p->entries);

			SLIST_REMOVE_HEAD(&p->entries, link);
			close(entry->fd);
			free(entry);
		}
		plog_close(p->count_ctx);
	}
	pthread_mutex_destroy(&node->lock);
	free(node);
}

static int
fd_pool_populate_from_vtree(struct fd_pool *pool, struct fd_pool_service *srv, struct sbalance *sb, struct vtree_chain *vtree) {
	int start = vtree_getint(vtree, "start", NULL);

	if (start)
		start--; /* 1-indexed in bconf. */

	struct vtree_keyvals loop;
	vtree_fetch_keys_and_values(vtree, &loop, "host", VTREE_LOOP, NULL);

	int err = EFDP_EMPTY_CONFIG;

	for (int cnt = 0 ; cnt < loop.len ; cnt++) {
		int i = (cnt + start) % loop.len;

		if (loop.list[i].type != vkvNode)
			continue;
		if (vtree_getint(&loop.list[i].v.node, "disabled", NULL))
			continue;

		const char *host = vtree_get(&loop.list[i].v.node, "name", NULL);

		/* Collect all values ending with "port" or "path" */

		struct vtree_keyvals nloop;
		vtree_fetch_keys_and_values(&loop.list[i].v.node, &nloop, VTREE_LOOP, NULL);

		int nports = 0, aports = 2;
		struct fd_pool_port *ports = xmalloc(aports * sizeof(*ports));

		for (int j = 0 ; j < nloop.len ; j++) {
			if (nloop.list[j].type != vkvValue)
				continue;
			const char *k = nloop.list[j].key;
			const char *kend = k + strlen(k);
			if (host && kend - k >= 4 && strcmp(kend - 4, "port") == 0) {
				const char *port = nloop.list[j].v.value;
				struct addrinfo *res;
				int eai;

				if ((eai = getaddrinfo(host, port, &srv->hints, &res))) {
					switch (eai) {
					case EAI_NONAME:
						err = EFDP_EAI_NONAME;
						break;
					case EAI_SYSTEM:
						err = EFDP_SYSTEM;
						break;
					default:
						err = EFDP_EAI_OTHER;
						break;
					}
					continue;
				}
				fd_ports_add_addrinfo(&ports, &nports, &aports, res, k);
				freeaddrinfo(res);
			} else if (strcmp(k, "path") == 0) {
				/* This is a "main" port. */
				fd_ports_add_unix(&ports, &nports, &aports, nloop.list[j].v.value, "port");
			}
		}

		if (nloop.cleanup)
			nloop.cleanup(&nloop);

		fd_pool_add(pool, srv, sb, loop.list[i].key, &loop.list[i].v.node, srv->hints.ai_socktype, nports, ports);
		free(ports);
	}

	if (loop.cleanup)
		loop.cleanup(&loop);

	return err;
}

struct fd_pool *
fd_pool_new(struct sd_registry *sdr) {
	struct fd_pool *pool = zmalloc(sizeof (*pool));

	SLIST_INIT(&pool->free_entries);
	pthread_mutex_init(&pool->lock, NULL);
	TAILQ_INIT(&pool->all_nodes);

	pool->upmap = &default_upmap;

	pool->sdr = sdr;
	if (sdr) {
		const char *appl = sd_registry_get_appl(sdr);

		pool->ports_ctx = plog_open_count(NULL, appl, 2, (const char*[]){"fd_pools", "ports"});
		pool->services_ctx = plog_open_count(NULL, appl, 2, (const char*[]){"fd_pools", "services"});
	}

	return pool;
}

static enum sbalance_strat
fd_pool_get_strat(struct vtree_chain *vtree) {
	const char *strat = vtree_get(vtree, "strat", NULL);
	if (strat && *strat) {
		if (strcmp(strat, "hash") == 0)
			return ST_HASH;
		else if (strcmp(strat, "random") == 0)
			return ST_RANDOM;
	} else if (vtree_getint(vtree, "client_hash", NULL)) {
		return ST_HASH;
	} else if (vtree_getint(vtree, "random_pick", NULL)) {
		return ST_RANDOM;
	}
	return ST_SEQ;
}

static int
services_compare(const struct avl_node *an, const struct avl_node *bn) {
	struct fd_pool_service *a = avl_data(an, struct fd_pool_service, tree);
	struct fd_pool_service *b = avl_data(bn, struct fd_pool_service, tree);

	return strcmp(a->service, b->service);
}

static struct fd_pool_service *
fd_pool_find_service(struct fd_pool *pool, const char *service) {
	LOCK(&pool->lock);

	struct fd_pool_service s;
	s.service = (char*)service;
	struct avl_node *n = avl_lookup(&s.tree, &pool->services, services_compare);

	UNLOCK(&pool->lock);

	if (n)
		return avl_data(n, struct fd_pool_service, tree);
	return NULL;
}

static struct fd_pool_service *
fd_pool_get_service(struct fd_pool *pool, const char *service,
		int retries, int failcost, int tempfailcost, enum sbalance_strat st,
		int timeoutms, const struct addrinfo *hints) {
	LOCK(&pool->lock);

	struct fd_pool_service s;
	s.service = (char*)service;
	struct avl_node *n = avl_lookup(&s.tree, &pool->services, services_compare);
	if (n) {
		UNLOCK(&pool->lock);
		return avl_data(n, struct fd_pool_service, tree);
	}

	struct fd_pool_service *srv = zmalloc(sizeof(*srv) + strlen(service) + 1);

	srv->service = (char*)(srv + 1);
	strcpy(srv->service, service);
	srv->timeoutms = timeoutms;
	srv->hints = hints ? *hints : default_hints;
	srv->sb = sbalance_create(retries, failcost, tempfailcost, st);
	srv->sb_gen = 1;
	pthread_rwlock_init(&srv->sblock, NULL);

	if (srv->timeoutms && srv->timeoutms < 1000) {
		log_printf(LOG_INFO, "fd_pool: Ignoring timeout %d ms < 1000", srv->timeoutms);
		srv->timeoutms = 0;
	}
	if (!srv->timeoutms)
		srv->timeoutms = FD_POOL_DEFAULT_TIMEOUT;

	avl_insert(&srv->tree, &pool->services, services_compare);

	UNLOCK(&pool->lock);

	pthread_rwlock_wrlock(&srv->sblock);
	srv->count_ctx = plog_open_dict(pool->services_ctx, service);
	pthread_rwlock_unlock(&srv->sblock);

	return srv;
}

struct fd_pool *
fd_pool_create(const char *service, struct vtree_chain *vtree, const struct addrinfo *hints) {
	if (!vtree || !vtree->fun)
		return NULL;

	enum sbalance_strat st = fd_pool_get_strat(vtree);
	int retries = vtree_getint(vtree, "retries", NULL);

	if (st != ST_SEQ && retries <= 0)
		retries = 1;

	struct fd_pool *pool = fd_pool_new(NULL);
	struct fd_pool_service *srv = fd_pool_get_service(pool, service, retries,
			vtree_getint(vtree, "failcost", NULL) ?: FD_POOL_DEFAULT_FAIL,
			vtree_getint(vtree, "tempfailcost", NULL) ?: FD_POOL_DEFAULT_TEMPFAIL,
			st,
			vtree_getint(vtree, "connect_timeout", NULL) ?: vtree_getint(vtree, "timeout", NULL),
			hints);

	fd_pool_populate_from_vtree(pool, srv, srv->sb, vtree);

	if (!srv->sb->sb_nserv && !vtree_haskey(vtree, "sd", NULL)) {
		sbalance_release(srv->sb, fd_pool_free_node);
		free(srv);
		free(pool);
		return NULL;
	}

	return pool;
}

int
fd_pool_add_config(struct fd_pool *pool, struct vtree_chain *vtree, const struct addrinfo *hints, const char **out_service) {
	if (!vtree || !vtree->fun) {
		if (out_service)
			*out_service = NULL;
		return EFDP_EMPTY_CONFIG;
	}

	if (pool->sdr)
		sd_registry_add_sources(pool->sdr, vtree);

	enum sbalance_strat st = fd_pool_get_strat(vtree);
	int retries = vtree_getint(vtree, "retries", NULL);

	if (st != ST_SEQ && retries <= 0)
		retries = 1;

	char srvbuf[128];
	const char *service = vtree_get(vtree, "service", NULL);
	if (!service) {
		/* Deterministically create a service name based on the given hosts. */
		struct buf_string buf = {};
		struct vtree_chain n = {};
		vtree_json(vtree_getnode(vtree, &n, "host", NULL), true, 0, vtree_json_bscat, &buf);
		unsigned long long hash = XXH64(buf.buf, buf.pos, 0);
		vtree_free(&n);
		snprintf(srvbuf, sizeof(srvbuf), "0x%llx", hash);
		service = srvbuf;
		free(buf.buf);
	}

	struct fd_pool_service *srv = fd_pool_get_service(pool, service, retries,
			vtree_getint(vtree, "failcost", NULL) ?: FD_POOL_DEFAULT_FAIL,
			vtree_getint(vtree, "tempfailcost", NULL) ?: FD_POOL_DEFAULT_TEMPFAIL,
			st,
			vtree_getint(vtree, "connect_timeout", NULL) ?: vtree_getint(vtree, "timeout", NULL),
			hints);

	if (out_service)
		*out_service = srv->service;

	pthread_rwlock_wrlock(&srv->sblock);

	if (srv->sb->sb_nserv != 0) {
		pthread_rwlock_unlock(&srv->sblock);
		return 0; /* Presumed already identically configured. */
	}

	int err = fd_pool_populate_from_vtree(pool, srv, srv->sb, vtree);

	if (!srv->sdconn)
		srv->sdconn = sd_registry_connect_fd_pool(pool->sdr, pool, service, vtree);

	pthread_rwlock_unlock(&srv->sblock);

	if (!srv->sb->sb_nserv && !vtree_haskey(vtree, "sd", NULL)) {
		if (out_service)
			*out_service = NULL;
		return err;
	}

	return 0;
}

#include "fd_pool_url_scheme.h"

int
fd_pool_add_url(struct fd_pool *pool, const char *service, const char *url, int retries, int timeoutms) {
	struct url *u = split_url(url);
	if (!u)
		return EFDP_NOT_URL;

	int ret;

	GPERF_ENUM(fd_pool_url_scheme)
	switch (lookup_fd_pool_url_scheme(u->protocol, -1)) {
	case GPERF_CASE("tcp"):
		ret = fd_pool_add_single(pool, service, u->host, u->port, retries, timeoutms, NULL);
		break;
	case GPERF_CASE("udp"):
		ret = fd_pool_add_single(pool, service, u->host, u->port, retries, timeoutms, &(struct addrinfo){ .ai_flags = AI_ADDRCONFIG, .ai_socktype = SOCK_DGRAM });
		break;
	case GPERF_CASE("unix"):
		ret = fd_pool_add_unix(pool, service, u->path, retries, timeoutms, SOCK_STREAM);
		break;
	case GPERF_CASE("unixgram"):
		ret = fd_pool_add_unix(pool, service, u->path, retries, timeoutms, SOCK_DGRAM);
		break;
	case GPERF_CASE("unixpacket"):
		ret = fd_pool_add_unix(pool, service, u->path, retries, timeoutms, SOCK_SEQPACKET);
		break;
	default:
		ret = EFDP_NOT_URL;
		break;
	}

	free(u);
	return ret;
}

struct fd_pool *
fd_pool_create_single(const char *service, const char *host, const char *port, int retries, int timeoutms, const struct addrinfo *hints) {
	struct addrinfo *res;
	int err;
	if (!hints)
		hints = &default_hints;

	if ((err = getaddrinfo(host, port, hints, &res))) {
		return NULL;
	}

	struct fd_pool *pool = fd_pool_new(NULL);
	struct fd_pool_service *srv = fd_pool_get_service(pool, service, retries,
			FD_POOL_DEFAULT_FAIL, FD_POOL_DEFAULT_TEMPFAIL,
			ST_SEQ, timeoutms, hints);

	int nports = 0, aports = 1;
	struct fd_pool_port *ports = xmalloc(sizeof(*ports));
	fd_ports_add_addrinfo(&ports, &nports, &aports, res, "port");
	freeaddrinfo(res);
	fd_pool_add(pool, srv, srv->sb, NULL, NULL, srv->hints.ai_socktype, nports, ports);
	free(ports);

	if (!srv->sb->sb_nserv) {
		sbalance_release(srv->sb, fd_pool_free_node);
		free(srv);
		free(pool);
		return NULL;
	}

	return pool;
}

int
fd_pool_add_single(struct fd_pool *pool, const char *service, const char *host, const char *port, int retries, int timeoutms, const struct addrinfo *hints) {
	if (!hints)
		hints = &default_hints;

	struct addrinfo *res;
	int err;
	if ((err = getaddrinfo(host, port, hints, &res))) {
		switch (err) {
		case EAI_NONAME:
			return EFDP_EAI_NONAME;
		case EAI_SYSTEM:
			return EFDP_SYSTEM;
		}
		return EFDP_EAI_OTHER;
	}

	struct fd_pool_service *srv = fd_pool_get_service(pool, service, retries,
			FD_POOL_DEFAULT_FAIL, FD_POOL_DEFAULT_TEMPFAIL,
			ST_RANDOM, timeoutms, hints);

	pthread_rwlock_wrlock(&srv->sblock);

	int nports = 0, aports = 1;
	struct fd_pool_port *ports = xmalloc(sizeof(*ports));
	fd_ports_add_addrinfo(&ports, &nports, &aports, res, "port");
	freeaddrinfo(res);
	fd_pool_add(pool, srv, srv->sb, NULL, NULL, srv->hints.ai_socktype, nports, ports);
	free(ports);

	if (!srv->sdconn)
		srv->sdconn = sd_registry_connect_fd_pool(pool->sdr, pool, service, NULL);

	pthread_rwlock_unlock(&srv->sblock);

	if (!srv->sb->sb_nserv)
		return EFDP_EMPTY_CONFIG;

	return 0;
}

struct fd_pool *
fd_pool_create_unix(const char *service, const char *path, int retries, int timeoutms, int socktype) {
	struct fd_pool *pool = fd_pool_new(NULL);
	struct fd_pool_service *srv = fd_pool_get_service(pool, service, retries,
			FD_POOL_DEFAULT_FAIL, FD_POOL_DEFAULT_TEMPFAIL,
			ST_SEQ, timeoutms,
			&(struct addrinfo){ .ai_flags = AI_ADDRCONFIG, .ai_socktype = socktype });

	int nports = 0, aports = 1;
	struct fd_pool_port *ports = xmalloc(sizeof(*ports));
	fd_ports_add_unix(&ports, &nports, &aports, path, "port");

	fd_pool_add(pool, srv, srv->sb, NULL, NULL, socktype, nports, ports);
	free(ports);

	return pool;
}

int
fd_pool_add_unix(struct fd_pool *pool, const char *service, const char *path, int retries, int timeoutms, int socktype) {
	struct fd_pool_service *srv = fd_pool_get_service(pool, service, retries,
			FD_POOL_DEFAULT_FAIL, FD_POOL_DEFAULT_TEMPFAIL,
			ST_RANDOM, timeoutms,
			&(struct addrinfo){ .ai_flags = AI_ADDRCONFIG, .ai_socktype = socktype });

	pthread_rwlock_wrlock(&srv->sblock);

	int nports = 0, aports = 1;
	struct fd_pool_port *ports = xmalloc(sizeof(*ports));
	fd_ports_add_unix(&ports, &nports, &aports, path, "port");

	fd_pool_add(pool, srv, srv->sb, NULL, NULL, socktype, nports, ports);
	free(ports);

	if (!srv->sdconn)
		srv->sdconn = sd_registry_connect_fd_pool(pool->sdr, pool, service, NULL);

	pthread_rwlock_unlock(&srv->sblock);

	return 0;
}

int
fd_pool_update_hosts(struct fd_pool *pool, const char *service, struct vtree_chain *vtree) {
	/* It's assumed that this function is the only one updating pool->sb,
	 * and only one call into here at a time. Thus no lock for read is needed.
	 *
	 * This assumption also makes the node ref couter work without extra locks,
	 * since it can't reach 0 before the sbalance_release below.
	 */

	if (vtree == NULL)
		return EFDP_EMPTY_CONFIG;

	struct fd_pool_service *srv = fd_pool_find_service(pool, service);
	if (!srv)
		return EFDP_NO_SUCH_SERVICE;

	struct sbalance *src_sb = srv->sb;
	struct sbalance *sb = sbalance_create(src_sb->sb_retries, src_sb->sb_failcost, src_sb->sb_softfailcost, sbalance_strat(src_sb));

	int n = fd_pool_populate_from_vtree(pool, srv, sb, vtree);

	if (sb->sb_nserv) {
		pthread_rwlock_wrlock(&srv->sblock);
		if (srv->sb == src_sb) {
			n = sb->sb_nserv;
			srv->sb = sb;
			sb = src_sb;
			srv->sb_gen++;
		} else {
			n = EFDP_RACE_LOST;
		}
		pthread_rwlock_unlock(&srv->sblock);
	}
	sbalance_release(sb, fd_pool_free_node);
	return n;
}

void
fd_pool_free(struct fd_pool *pool) {
	if (!pool)
		return;

	/* Standard avl tree disposal strategy, convert to linked list. */
	struct avl_it it;
	struct avl_node *n;
	struct fd_pool_service *srv, *psrv = NULL;
	avl_it_init(&it, pool->services, NULL, NULL, services_compare);
	while ((n = avl_it_next(&it)) != NULL) {
		srv = avl_data(n, struct fd_pool_service, tree);
		srv->service = (char*)psrv;
		psrv = srv;
	}
	while ((srv = psrv) != NULL) {
		if (srv->sdconn)
			sd_registry_disconnect(srv->sdconn);
		sbalance_release(srv->sb, fd_pool_free_node);
		pthread_rwlock_destroy(&srv->sblock);
		plog_close(srv->count_ctx);
		psrv = (struct fd_pool_service*)(void*)srv->service;
		free(srv);
	}

	/* Nodes should've been freed now. */
	assert(TAILQ_EMPTY(&pool->all_nodes));

	while (!SLIST_EMPTY(&pool->free_entries)) {
		struct fd_pool_entry *entry = SLIST_FIRST(&pool->free_entries);

		SLIST_REMOVE_HEAD(&pool->free_entries, link);
		free(entry);
	}
	plog_close(pool->ports_ctx);
	plog_close(pool->services_ctx);
	free(pool);
}

void
fd_pool_set_portmap(struct fd_pool *pool, struct vtree_chain *upmap) {
	if (upmap == NULL)
		upmap = &default_upmap;
	pool->upmap = upmap;
}

int
fd_pool_timeout(struct fd_pool_conn *conn) {
	if (!conn->srv)
		return FD_POOL_DEFAULT_TIMEOUT;
	return conn->srv->timeoutms;
}

int
fd_pool_socktype(struct fd_pool_conn *conn) {
	if (!conn->srv)
		return SOCK_STREAM;
	return conn->srv->hints.ai_socktype;
}

void
fd_pool_set_cycle_last(struct fd_pool *pool, const char *service, bool cl) {
	struct fd_pool_service *srv = fd_pool_find_service(pool, service);
	if (srv)
		srv->cycle_last = cl;
}

void
fd_pool_set_nonblock(struct fd_pool_conn *conn, bool nb) {
	conn->nonblock = nb;
}

struct fd_pool_conn *
fd_pool_new_conn(struct fd_pool *pool, const char *service, const char *port_key, const char *remote_addr) {
	struct fd_pool_conn *conn = zmalloc(sizeof(*conn));

	conn->pool = pool;
	conn->srv = fd_pool_find_service(pool, service);
	if (!conn->srv && pool->sdr) {
		/* Conjure up a service to see if it's available in SD. */
		conn->srv = fd_pool_get_service(pool, service,
				1, FD_POOL_DEFAULT_FAIL, FD_POOL_DEFAULT_TEMPFAIL, ST_RANDOM,
				FD_POOL_DEFAULT_TIMEOUT, NULL);
		pthread_rwlock_wrlock(&conn->srv->sblock);
		if (!conn->srv->sdconn) {
			conn->srv->sdconn = sd_registry_connect_fd_pool(pool->sdr, pool, service, NULL);
			/* Use a default 1s initial_wait_ms here to allow SD to configure the service. */
			if (conn->srv->sdconn)
				sd_registry_set_initial_wait_ms(conn->srv->sdconn, 1000);
		}
		pthread_rwlock_unlock(&conn->srv->sblock);
	}
	if (conn->srv && conn->srv->sdconn)
		sd_registry_new_conn(conn->srv->sdconn);
	conn->sn = NULL;
	conn->port = NULL;
	conn->sc_hash = remote_addr ? sbalance_hash_string(remote_addr) : 0;
	port_key = fd_pool_upmap_lookup(conn->pool->upmap, port_key);
	conn->port_key = xstrdup(port_key);

	return conn;
}

void
fd_pool_free_conn(struct fd_pool_conn *conn) {
	if (!conn)
		return;

	if (conn->active_fd)
		plog_int(conn->port->count_ctx, "connections", -1);

	sbalance_conn_done(&conn->sc);
	if (conn->sc.sc_sb)
		sbalance_release(conn->sc.sc_sb, fd_pool_free_node);

	if (conn->entry) {
		LOCK(&conn->pool->lock);
		SLIST_INSERT_HEAD(&conn->pool->free_entries, conn->entry, link);
		UNLOCK(&conn->pool->lock);
	}
	free(conn->node_filter);
	free(conn->port_key);
	free(conn);
}

static bool
fd_pool_move_port(struct fd_pool_conn *conn) {
	struct fd_pool_node *n = conn->sn->node;
	while (*conn->pk_ptr) {
		size_t pkl = strcspn(conn->pk_ptr, ",");
		if (conn->port)
			conn->port++;
		else
			conn->port = n->ports;
		for (; conn->port < n->ports + n->nports ; conn->port++) {
			if (strncmp(conn->port->port_key, conn->pk_ptr, pkl) == 0
					&& conn->port->port_key[pkl] == '\0')
				return true;
		}
		conn->port = NULL;
		conn->pk_ptr += pkl;
		if (*conn->pk_ptr == ',')
			conn->pk_ptr++;
	}
	return false;
}

static void
fd_pool_move_node(struct fd_pool_conn *conn, enum sbalance_conn_status status) {
	do {
		do {
			conn->sn = sbalance_conn_next(&conn->sc, status);
			if (!conn->sn)
				return;
			status = SBCS_START;
		} while (conn->node_filter && strcmp(conn->sn->key, conn->node_filter) != 0);
		conn->pk_ptr = conn->port_key;
		conn->port = NULL;
	} while (!fd_pool_move_port(conn));
}

int
fd_pool_get(struct fd_pool_conn *conn, enum sbalance_conn_status status, const char **peer, const char **port_key) {
	int fd = -1;
	int flflags = 0;
	socklen_t sl;
	int error;

	conn->active_fd = false;

	if (!conn->srv) {
		errno = ENOENT;
		return -1;
	}

	/* Restart if we're not on latest gen, to avoid bad node ptrs. */
	if (status == SBCS_START || conn->sb_gen < conn->srv->sb_gen) {
		pthread_rwlock_rdlock(&conn->srv->sblock);
		struct sbalance *sb = conn->srv->sb;
		if (conn->sc.sc_sb) {
			if (status == SBCS_START) /* Only call done if successful. */
				sbalance_conn_done(&conn->sc);
			sbalance_release(conn->sc.sc_sb, fd_pool_free_node);
		}
		conn->sb_gen = conn->srv->sb_gen;
		conn->sn = NULL;
		sbalance_conn_new(&conn->sc, sb, conn->sc_hash);
		pthread_rwlock_unlock(&conn->srv->sblock);
	}

	/* Initialize errno in case we return -1 directly. */
	errno = EAGAIN;

	while (fd == -1) {
		if (conn->ti)
			timer_handover(conn->ti, "connect");

		if (!conn->sn) {
			fd_pool_move_node(conn, status);
		} else if (!conn->entry) {
			/*
			 * conn->entry will be NULL only if the last try was a new connection made to the port.
			 * Thus we retry the same node and port until a new connection fails.
			 */
			if (!fd_pool_move_port(conn))
				fd_pool_move_node(conn, status);
		}
		if (!conn->port) {
			return -1;
		}

		if (conn->ti)
			timer_add_attribute(conn->ti, conn->port->peer);

		if (peer)
			*peer = conn->port->peer;
		if (port_key)
			*port_key = conn->port->port_key;

		if (conn->entry) {
			LOCK(&conn->pool->lock);
			SLIST_INSERT_HEAD(&conn->pool->free_entries, conn->entry, link);
			UNLOCK(&conn->pool->lock);
		}

		if (!SLIST_EMPTY(&conn->port->entries)) {
			LOCK(&conn->sn->node->lock);
			while ((conn->entry = SLIST_FIRST(&conn->port->entries))) {
				struct pollfd pfd;
				int n;

				SLIST_REMOVE_HEAD(&conn->port->entries, link);
				UNLOCK(&conn->sn->node->lock);

				memset(&pfd, 0, sizeof(pfd));

				pfd.fd = conn->entry->fd;
				pfd.events = POLLHUP | POLLRDHUP;

				n = poll(&pfd, 1, 0);

				if (n == 0) {
					if (!conn->silent)
						log_printf(LOG_DEBUG, "fd_pool: using existing fd to %s", conn->port->peer);
					conn->active_fd = true;
					return conn->entry->fd;
				}

				if (!conn->silent) {
					if (n > 0)
						log_printf(LOG_DEBUG, "fd_pool: NOT using existing fd to %s: EOF", conn->port->peer);
					else
						log_printf(LOG_DEBUG, "fd_pool: NOT using existing fd to %s: %m", conn->port->peer);
				}
				plog_int(conn->port->count_ctx, "connections", -1);
				close(conn->entry->fd);

				LOCK(&conn->pool->lock);
				SLIST_INSERT_HEAD(&conn->pool->free_entries, conn->entry, link);
				UNLOCK(&conn->pool->lock);

				LOCK(&conn->sn->node->lock);
			}
			UNLOCK(&conn->sn->node->lock);
		}

		conn->entry = NULL;
		status = SBCS_FAIL;

		if ((fd = socket(conn->port->sockaddr.ss_family, conn->sn->node->socktype, 0)) == -1) {
			if (!conn->silent)
				log_printf(LOG_INFO, "fd_pool: socket(%s): %m", conn->port->peer);
			continue;
		}

		/*
		 * Set the socket to be non-blocking so that we can time out in the connect step.
		 */
		flflags = fcntl(fd, F_GETFL, 0);
		fcntl(fd, F_SETFL, flflags | O_NONBLOCK);
		int flags = fcntl(fd, F_GETFD, 0);
		fcntl(fd, F_SETFD, flags | FD_CLOEXEC);

		if (connect(fd, (struct sockaddr*)&conn->port->sockaddr, conn->port->addrlen) < 0) {
			/*
			 * Connection in progress. poll on it so that we
			 * can time out (except if async).
			 */
			if (errno == EINPROGRESS) {
				struct pollfd pfd;
				int n;

				if (conn->async)
					break;

				memset(&pfd, 0, sizeof(pfd));

				pfd.fd = fd;
				pfd.events = POLLIN | POLLOUT | POLLHUP | POLLRDHUP;

				do {
					n = poll(&pfd, 1, conn->srv->timeoutms);
					/* XXX we restart the timeout on EINTR. */
				} while (n == -1 && errno == EINTR);

				if (n == 1 && (pfd.revents & (POLLIN|POLLOUT)) != 0) {
					/* Almost ok, just check SO_ERROR as well. */
					error = 0;
					sl = sizeof(error);
					if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &sl) == 0 && error == 0)
						break;

					if (error != 0)
						errno = error;
					if (!conn->silent)
						log_printf(LOG_INFO, "fd_pool: getsockopt(%s): %m", conn->port->peer);
				} else if (!conn->silent) {
					switch (n) {
					case 1:
						errno = ECONNREFUSED; /* XXX Not sure this is true. */
						break;
					case 0:
						errno = ETIMEDOUT;
						break;
					}
					log_printf(LOG_INFO, "fd_pool: poll(%s): %m", conn->port->peer);
				}
			} else if (!conn->silent) {
				log_printf(LOG_INFO, "fd_pool: connect(%s): %m", conn->port->peer);
			}
			close(fd);
			fd = -1;
			if (conn->ti)
				timer_add_attribute(conn->ti, "conn_fail");
		}

	}
	if (fd != -1 && !conn->silent)
		log_printf(LOG_DEBUG, "fd_pool: Connected to %s", conn->port->peer);

	if (!conn->async && !conn->nonblock) {
		/*
		 * Restore old flags, the socket shouldn't be non-blocking anymore.
		 */
		fcntl(fd, F_SETFL, flflags);
	}

	if (fd != -1) {
		plog_int(conn->port->count_ctx, "connections", 1);
		conn->active_fd = true;
	}
	return fd;
}

void
fd_pool_put(struct fd_pool_conn *conn, int fd) {
	struct fd_pool_entry *entry = conn->entry;
	static struct rlimit fdlimit;

	conn->active_fd = false;

	/* Assume it doesn't change after first time we're called. */
	if (!fdlimit.rlim_cur)
		getrlimit(RLIMIT_NOFILE, &fdlimit);

	if ((fdlimit.rlim_cur != RLIM_INFINITY && (unsigned)fd >= (fdlimit.rlim_cur * 9 / 10))) {
		/* Safety net while waiting for a better solution. */
		if (!conn->silent)
			log_printf(LOG_DEBUG, "Not keeping fd %d due to rlimit", fd);
		plog_int(conn->port->count_ctx, "connections", -1);
		close(fd);
		return;
	}

	pthread_rwlock_rdlock(&conn->srv->sblock);

	if (entry) {
		conn->entry = NULL;
	} else {
		if (!SLIST_EMPTY(&conn->pool->free_entries)) {
			LOCK(&conn->pool->lock);
			entry = SLIST_FIRST(&conn->pool->free_entries);
			if (entry)
				SLIST_REMOVE_HEAD(&conn->pool->free_entries, link);
			UNLOCK(&conn->pool->lock);
		}
		if (!entry)
			entry = xmalloc(sizeof (*entry));
	}
	entry->fd = fd;
	LOCK(&conn->sn->node->lock);

	if (conn->srv->cycle_last && !SLIST_EMPTY(&conn->port->entries)) {
		SLIST_INSERT_AFTER(conn->port->last_ptr, entry, link);
	} else {
		SLIST_INSERT_HEAD(&conn->port->entries, entry, link);
	}
	conn->port->last_ptr = entry; /* Only used if cycle_last so this is ok. */
	UNLOCK(&conn->sn->node->lock);
	pthread_rwlock_unlock(&conn->srv->sblock);
}

struct fd_pool *
fd_pool_conn_pool(struct fd_pool_conn *conn) {
	return conn->pool;
}

void
fd_pool_set_async(struct fd_pool_conn *conn, int async) {
	conn->async = async;
}

void
fd_pool_set_timer(struct fd_pool_conn *conn, struct timer_instance *ti) {
	conn->ti = ti;
}

void
fd_pool_set_node_key(struct fd_pool_conn *conn, const char *key) {
	conn->node_filter = xstrdup(key);
}

void
fd_pool_set_silent(struct fd_pool_conn *conn) {
	conn->silent = true;
}

void
fd_pool_set_port_key(struct fd_pool_conn *conn, const char *port_key) {
	port_key = fd_pool_upmap_lookup(conn->pool->upmap, port_key);
	free(conn->port_key);
	conn->port_key = xstrdup(port_key);
	conn->pk_ptr = conn->port_key;
}


void *
fd_pool_get_aux(struct fd_pool_conn *conn) {
	if (!conn)
		return NULL;
	return conn->aux;
}

void
fd_pool_set_aux(struct fd_pool_conn *conn, void *aux) {
	conn->aux = aux;
}

int
fd_pool_conn_writev(struct fd_pool_conn *conn, int *fd, struct iovec *iov, int iovcnt, ssize_t iovlen_sum) {

	ssize_t n_total = iovlen_sum;

	if (n_total < 0)
		n_total = get_iovlen_sum(iov, iovcnt);

	int sockfd = *fd;
	int res = 0;

	do {
		if (sockfd < 0 || res < 0) {
			enum sbalance_conn_status status = SBCS_START;
			if (sockfd >= 0) {
				close(sockfd);
				status = SBCS_FAIL;
			}

			sockfd = fd_pool_get(conn, status, NULL, NULL);
		}

		/* No socket available to write to -- this is fatal */
		if (sockfd < 0) {
			*fd = -1;
			return -1;
		}

		res = writev_retry(sockfd, iov, iovcnt, n_total);

	} while (res < 0);

	*fd = sockfd;

	return 0;
}

bool
fd_pool_iter_ports(struct fd_pool *pool, const char *service, int *state, struct fd_pool_port_iter *iter) {
	struct fd_pool_service *srv = fd_pool_find_service(pool, service);
	if (!srv)
		return false;
	if (!*state) {
		pthread_rwlock_rdlock(&srv->sblock);
		if (srv->sb->sb_nserv == 0) {
			pthread_rwlock_unlock(&srv->sblock);
			return false;
		}
		iter->sb_serv = srv->sb->sb_service;
		*state = 1;
	}
	struct fd_pool_service_node *sn = iter->sb_serv->data;
	struct fd_pool_node *node = sn->node;
	while (*state > node->nports) {
		iter->sb_serv++;
		if (iter->sb_serv >= srv->sb->sb_service + srv->sb->sb_nserv) {
			pthread_rwlock_unlock(&srv->sblock);
			return false;
		}
		sn = iter->sb_serv->data;
		node = sn->node;
		*state = 1;
	}
	struct fd_pool_port *port = node->ports + *state - 1;
	iter->key = sn->key;
	iter->peer = port->peer;
	iter->port_key = port->port_key;
	iter->socktype = node->socktype;
	iter->sockaddr = (struct sockaddr*)&port->sockaddr;
	iter->addrlen = port->addrlen;
	pthread_mutex_lock(&node->lock);
	iter->num_stored_fds = 0;
	struct fd_pool_entry *entry;
	SLIST_FOREACH(entry, &port->entries, link) {
		iter->num_stored_fds++;
	}
	pthread_mutex_unlock(&node->lock);
	(*state)++;
	return true;
}

const char *
fd_pool_strerror(int efdp, char *buf, size_t buflen) {
	switch (efdp) {
	case EFDP_EMPTY_CONFIG:
		return "empty config node";
	case EFDP_NOT_URL:
		return "not a valid URL or not a supported URL scheme";
	case EFDP_EAI_NONAME:
		snprintf(buf, buflen, "getaddrinfo: %s", gai_strerror(EAI_NONAME));
		return buf;
	case EFDP_EAI_OTHER:
		return "getaddrinfo: unknown error";
	case EFDP_NO_SUCH_SERVICE:
		return "no such service";
	case EFDP_RACE_LOST:
		return "update race lost";
	case EFDP_SYSTEM:
		return xstrerror(errno);
	default:
		snprintf(buf, buflen, "unknown error %d", efdp);
		return buf;
	}
}

int
fd_pool_split_host_port(char *str, char **host, char **port) {
	if (*str == '[') {
		*host = ++str;
		while (*str && *str != ']')
			str++;
		if (!*str)
			return -1;
		*str++ = '\0';
	} else {
		*host = str;
		while (*str && *str != ':')
			str++;
	}
	if (!*str) {
		*port = NULL;
		return 0;
	}
	if (*str != ':')
		return -1;
	*str++ = '\0';
	*port = str;
	return 0;
}

char *
fd_pool_host_to_service(const char *host, ssize_t hlen, const char *domain, ssize_t dlen) {
	if (hlen < 0)
		hlen = strlen(host);
	if (dlen < 0)
		dlen = strlen(domain);

	if (hlen <= dlen)
		return NULL;
	if (memcmp(host + hlen - dlen, domain, dlen) != 0)
		return NULL;
	hlen -= dlen + 1;
	if (host[hlen] != '.') // Require a . just before the domain suffix.
		return NULL;
	while (hlen > 0 && host[hlen - 1] == '.')
		hlen--;

	// Copy to a new buffer, it's the simple solution.
	struct buf_string dst = {0};
	int start = hlen;
	int end = hlen;
	while (1) {
		while (start > 0 && host[start - 1] != '.')
			start--;
		bswrite(&dst, host + start, end - start);
		if (start == 0)
			break;
		bswrite(&dst, "/", 1);
		while (start > 0 && host[start - 1] == '.')
			start--;
		end = start;
	}
	return dst.buf;
}
