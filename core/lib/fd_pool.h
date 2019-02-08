// Copyright 2018 Schibsted

#ifndef FD_POOL_H
#define FD_POOL_H

#include "sbp/sbalance.h"
#include "sbp/macros.h"

#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vtree_chain;
struct iovec;
struct timer_instance;

/*
 * Takes one of our common bconf trees
 * .host.1.name=x
 * .host.1.port=x
 * .host.2.name=x
 * .host.2.port=x
 * and sticks the dns lookup results in an sbalance.
 * Then creates a fd pool per node and each call to
 * fd_pool_get() returns a fd you can use.
 * It returns -1 when you've run out of places to connect.
 * peer is an out parameter on the format "host port", always numeric.
 * port_key is set to the config key of the port used.
 * Use fd_pool_put() when done, or close it if it can't be reused.
 */

#define FD_POOL_DEFAULT_TIMEOUT 5000
#define FD_POOL_DEFAULT_FAIL     100
#define FD_POOL_DEFAULT_TEMPFAIL   0

struct fd_pool;
struct fd_pool_conn;
struct addrinfo;
struct sd_registry;

/* sdr is optional, but this is the only way to have a pool connected to service discovery. */
struct fd_pool *fd_pool_new(struct sd_registry *sdr) ALLOCATOR;
void fd_pool_free(struct fd_pool *pool);

/* New style, add services by name.
 * For add_config, service name is extracted from vtree, and only configures if not already present.
 * The found name is put in the out parameter if not NULL.
 * The others take service as input and add nodes even if config is already there.
 */
int fd_pool_add_config(struct fd_pool *pool, struct vtree_chain *vtree, const struct addrinfo *hints, const char **out_service) NONNULL(1);

int fd_pool_add_url(struct fd_pool *pool, const char *service, const char *url, int retries, int timeoutms);

int fd_pool_add_single(struct fd_pool *pool, const char *service, const char *host, const char *port, int retries, int timeoutms, const struct addrinfo *hints) NONNULL(1, 2, 3);
int fd_pool_add_unix(struct fd_pool *pool, const char *service, const char *path, int retries, int timeoutms, int socktype);

/* Errors return by add functions and update_hosts */
enum {
	/* No valid nodes in the config, and no sd configured. */
	EFDP_EMPTY_CONFIG = -1,

	/* Input string was not an URL (as determined by split_url). */
	EFDP_NOT_URL = -2,

	/* getaddrinfo returned EAI_NONAME */
	EFDP_EAI_NONAME = -3,
	/* getaddrinfo returned some other error (which is now lost) */
	EFDP_EAI_OTHER = -4,

	/* Specified service was not found (update_hosts only) */
	EFDP_NO_SUCH_SERVICE = -5,

	/* Lost a race in update_hosts. Not supposed to happen. */
	EFDP_RACE_LOST = -6,

	/* Inspect errno */
	EFDP_SYSTEM = -100,
};

/* Old style functions, creates a pool with a single service.
 * Always take service as input, use the add functions to parse from vtree.
 * These can't connect to sd registry.
 */
struct fd_pool *fd_pool_create(const char *service, struct vtree_chain *vtree, const struct addrinfo *hints) ALLOCATOR NONNULL(1);
struct fd_pool *fd_pool_create_single(const char *service, const char *host, const char *port, int retries, int timeoutms, const struct addrinfo *hints) ALLOCATOR NONNULL(1, 2);
struct fd_pool *fd_pool_create_unix(const char *service, const char *path, int retries, int timeoutms, int socktype) ALLOCATOR NONNULL(1);

/* Returns an efdp error, or the number of sb nodes found. If 0 or lower is returned nothing was changed. */
int fd_pool_update_hosts(struct fd_pool *pool, const char *service, struct vtree_chain *vtree);

void fd_pool_set_cycle_last(struct fd_pool *pool, const char *service, bool cl) NONNULL(1);

/* Set a custom urlport map, overriding the default. The pointer given is kept as a reference and
 * must be valid as long as the pool is.
 * The main purpose of portmaps is to map port numbers given in e.g. URLs to port keys, but
 * any port_key string can be mapped to any other. URLs formally only allow numbers in the port
 * part, thus not matching our port keys very well. This adds a workaround to map from those
 * numbers to our keys.
 *
 * The default portmap can be seen in fd_pool.c (default_upmap).
 * In particular, "8080" is mapped to "port", while "80" and "443" are mapped to "http_port".
 */
void fd_pool_set_portmap(struct fd_pool *pool, struct vtree_chain *upmap);

struct fd_pool_conn *fd_pool_new_conn(struct fd_pool *pool, const char *service, const char *port_key, const char *remote_addr) ALLOCATOR NONNULL(1, 2);
void fd_pool_free_conn(struct fd_pool_conn *conn);

int fd_pool_get(struct fd_pool_conn *conn, enum sbalance_conn_status status, const char **peer, const char **port_key) NONNULL(1);
void fd_pool_put(struct fd_pool_conn *conn, int fd) NONNULL(1);

struct fd_pool *fd_pool_conn_pool(struct fd_pool_conn *conn) NONNULL(1);

int fd_pool_timeout(struct fd_pool_conn *conn) NONNULL(1);
int fd_pool_socktype(struct fd_pool_conn *conn) NONNULL(1);

void fd_pool_set_nonblock(struct fd_pool_conn *conn, bool nb) NONNULL(1);
void fd_pool_set_async(struct fd_pool_conn *conn, int async) NONNULL(1);
void fd_pool_set_timer(struct fd_pool_conn *conn, struct timer_instance *ti);
void fd_pool_set_node_key(struct fd_pool_conn *conn, const char *key);
void fd_pool_set_silent(struct fd_pool_conn *conn);

/* Allows you to change the used port keys if you chose the wrong ones for some reason.
 * If you already called fd_pool_get, this will restart the ports on the current node.
 */
void fd_pool_set_port_key(struct fd_pool_conn *conn, const char *port_key);

/* Auxiliary pointer set by user. */
void *fd_pool_get_aux(struct fd_pool_conn *conn);
void fd_pool_set_aux(struct fd_pool_conn *conn, void *aux);

/*
	Retrying writev() with fd_pool connection fallback.

	Will write the complete iovecs to the fd passed in, with fallback to the
	connection pool if a write fails, or the passed fd was -1.

	On success, 0 is returned and fd contains a working fd.
	On error, a negative value is returned and fd is -1.

*/
int fd_pool_conn_writev(struct fd_pool_conn *conn, int *fd, struct iovec *iov, int iovcnt, ssize_t iovlen_sum) NONNULL(1,2,3);

struct fd_pool_port_iter {
	const char *key;
	const char *peer;
	const char *port_key;
	int socktype;

	struct sockaddr *sockaddr;
	size_t addrlen;

	struct sb_service *sb_serv;

	int num_stored_fds; /* Stored but currently unused fds. Used ones are not tracked. */
};

/* Debug function, iterate on the nodes with a while loop:
 * while (fd_pool_iter_nodes(pool, &state, &iter)) { ... }
 * will fill out the iter struct for each node inside the pool.
 * Note that you must continue calling the function until it returns false, as there's locks involved.
 * Initialize state to 0.
 */
bool fd_pool_iter_ports(struct fd_pool *pool, const char *service, int *state, struct fd_pool_port_iter *iter);

/* Returns error for efdp. Might use buf for storage, depending on error. */
const char *fd_pool_strerror(int efdp, char *buf, size_t buflen);

/* Utility function to split a host:port address.
 * Will add NUL to the right place in the input string and set host and port.
 * Returns 0 on success, -1 on invalid input.
 * port might be set to NULL if not present in the string.
 */
int fd_pool_split_host_port(char *str, char **host, char **port);

/* Utility function to convert from host name to service name.
 * If the value passed in host has the suffix passed in domain, that part is stripped.
 * The . separated values before the domain are reversed, and . replaced by /
 * E.g. mysearch.search.example.com with domain example.com will be converted to
 * search/mysearch.
 * The return value is malloced and should be freed. A NULL return means host and domain
 * didn't match (or malloc failed).
 */
char *fd_pool_host_to_service(const char *host, ssize_t hlen, const char *domain, ssize_t dlen);

#ifdef __cplusplus
}
#endif

#endif /*FD_POOL_H*/
