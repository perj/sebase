// Copyright 2018 Schibsted

#include <stdbool.h>
#include <sys/types.h>

#include "sbp/macros.h"

#define NHANDLERS(x) (int)(sizeof(x) / sizeof(x[0]))

struct bconf_node;
struct vtree_chain;
struct ctrl_req;
struct stringmap;
struct tls;
struct ctrl_handler {
	const char *url;
	/*
	 * We always parse GET arguments and pass them on.
	 * POST data we either parse as JSON (not yet) or pass to the handler raw if there's a callback for it.
	 */
	void (*start)(struct ctrl_req *, void *);				/* Any necessary setup before things are fully initialized. */
	int (*consume_post)(struct ctrl_req *, struct stringmap *, size_t, const char *, size_t, void *);		/* Handler of POST data. */
	void (*finish)(struct ctrl_req *, struct stringmap *, void *);		/* The main message handler, called after the full request has been read. */
	void (*cleanup)(struct ctrl_req *, void *);				/* In case we need to do any cleanup after everything has been completed. */
	void (*upgrade)(struct ctrl_req *, int fd, struct tls *tls, void *);             /* Post-cleanup upgrade call, when upgrading was negotiated. You must have set status code to 101 for this to be called. Callee owns fd from now on. */
	void *cb_data;
};

struct ctrl;
struct https_state;

struct ctrl *ctrl_setup(struct bconf_node *, const struct ctrl_handler *, int nhandlers, int, struct https_state*);
/*
 * Shutdown controller threads and clean up.
 *
 * It should not be called while processing
 * a request, instead use the two stage quit
 */
void ctrl_quit(struct ctrl *);
int ctrl_get_listen_socket(struct ctrl *);

/*
 * Tell the backend to not keep-alive the connection after this command.
 */
void ctrl_close(struct ctrl_req *);
/*
 * Signal an error for this command.
 */
void ctrl_error(struct ctrl_req *, int, const char *fmt, ...) __attribute__((format (printf, 3, 4)));
/*
 * Signal a status other than 200 that is not an error.
 */
void ctrl_status(struct ctrl_req *, int);
/*
 * Set a callback data pointer for this particular command.  This can
 * be used in raw_post_cb to allocate a buffer or some other context
 * that's needed to handle this particular request. The general
 * cb_data must be NULL when this function is used and it can only be
 * called once per request. Subsequent calls to raw_post_cb and
 * message_cb will be using that pointer for cb_data. The caller is
 * responsible for freeing the data in message_cb (message_cb is only
 * called once and last in the request).
 */
void ctrl_set_handler_data(struct ctrl_req *, void *);

/*
 * Sets the content-type of the successful response.
 */
void ctrl_set_content_type(struct ctrl_req *cr, const char *ct);

/*
 * Makes this request return a raw data blob as the response. It's an
 * internal error to use this and write to the output buf.
 */
void ctrl_set_raw_response_data(struct ctrl_req *, void *, size_t);

/*
 * Returns the Content-Length set by the request or 0 if none has been received.
 */
size_t ctrl_get_content_length(struct ctrl_req *);

/*
 * Get a pointer to the current handler. Note: this is NOT a pointer into the
 * array given to ctrl_setup, but rather a copy.
 */
const struct ctrl_handler *ctrl_get_handler(struct ctrl_req *cr);

/*
 * If an upgrade header was sent, return its value.
 * Only the 32 first bytes are saved.
 */
const char *ctrl_get_upgrade(struct ctrl_req *cr);

/*
 * Calls getpeername then getnameinfo on the socket.
 * Return result from getnameinfo.
 */
int ctrl_get_peer(struct ctrl_req *cr, char *hbuf, size_t hbuflen, char *pbuf, size_t pbuflen, int gni_flags);

/*
 * If HTTPS and the client sent a valid certificate, return the common name,
 * written to cnbuf.
 * Otherwise returns NULL.
 */
char *ctrl_get_peer_commonname(struct ctrl_req *cr, char *cnbuf, size_t cnbuflen);
char *ctrl_get_peer_issuer(struct ctrl_req *cr, char *buf, size_t buflen);

/*
 * Two stage quit. Especially useful when needed to perform a stop from a
 * controller. The first stage will quit the listening thread and no new
 * connections will be received, while maintaining the worker thread and the fd
 * that received the stop request so that it can be still used for writing a
 * response.
 *
 * When the service using the controller has stop the stage two quit can be
 * issued to cleanup properly.
 *
 * ctrl_quit_stage_one() will fail if a stop has been issued from another
 * request.
 */
int ctrl_quit_stage_one(struct ctrl *, bool close_listen);
void ctrl_quit_stage_two(struct ctrl *);

/*
 * Get the root for the per-request bconf.
 */
struct bconf_node;
struct bconf_node **ctrl_get_bconfp(struct ctrl_req *);
/*
 * Called from a callback to output json as our response. "root" is the root of the vtree that we want to output.
 */
void ctrl_output_json(struct ctrl_req *cr, const char *root);

/* In case you want to output plain text. */
void ctrl_output_text(struct ctrl_req *cr, const char *fmt, ...) FORMAT_PRINTF(2, 3);
struct buf_string *ctrl_get_textbuf(struct ctrl_req *cr);

/*
 * Sets custom headers.
 * Only unique headers starting with 'X-' are accepted.
 * The sanity of the keys and values is not checked.
 */
void ctrl_set_custom_headers(struct ctrl_req *cr, const char *key, const char *value) NONNULL_ALL;

void render_json_cb(struct ctrl_req *, struct stringmap *, void *);

/* Default handler for timers, stat_counters and stat_messages */
extern const struct ctrl_handler ctrl_stats_handler;
extern const struct ctrl_handler ctrl_loglevel_handler;

#define CONTROLLER_DEFAULT_HANDLERS	ctrl_stats_handler, ctrl_loglevel_handler
