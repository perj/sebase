// Copyright 2018 Schibsted

#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>

#include "sbp/logging.h"
#include "sbp/http.h"
#if __has_include("sbp/http_parser.h")
#include "sbp/http_parser.h"
#else
#include <http_parser.h>
#endif

#include "sbp/bconf.h"
#include "sbp/date_functions.h"
#include "sbp/parse_query_string.h"
#include "controller.h"
#include "create_socket.h"
#include "sbp/stat_counters.h"
#include "sbp/stat_messages.h"
#include "sbp/stringmap.h"
#include "sbp/tls.h"

#define MIN_NTHREADS 5

#include "controller-events.h"

struct job {
	bool initial;
	int fd;
	struct tls *tls;
	TAILQ_ENTRY(job) entry_list;
};

struct worker;
struct ctrl_handler_int {
	struct ctrl_handler hand;
	uint64_t *cnt;
};

/* This should be struct controller or something, this doesn't describe one thread (as the code was in the beginning), but all the threads. */
struct ctrl {
	struct ctrl_handler_int *handlers;
	struct bconf_node *ctrl_conf;
	int nhandlers;
	int listen_socket;
	bool acl_disabled;
	bool quit;
	pthread_mutex_t quit_mutex;

	struct {
		bool enabled;
		struct tls_context ctx;
		tlskey_t key;
		int ncerts;
		tlscert_t *cert_arr;
	} tls;

	pthread_t listen_thread;
	union ctrl_event_e event_e;

	/* Trigger event on closing */
	int closefd[2];

	pthread_mutex_t queue_lock;
	TAILQ_HEAD(, worker) worker_threads;

	pthread_mutex_t job_lock;
	TAILQ_HEAD(, job) job_list;
	pthread_cond_t job_cond;

	pthread_mutex_t event_lock;
	TAILQ_HEAD(, event_handler) event_list;

	const char *stat_counters_prefix;

	uint64_t *num_accept;
};

struct worker {
	struct ctrl *ctrl;
	pthread_t worker_thread;

	struct stat_message *thr_state;
	struct stat_message *handler_name;
	uint64_t *handler_data_total;
	uint64_t *handler_data_current;

	TAILQ_ENTRY(worker) worker_list;
};

struct ctrl_req {
	struct worker *worker;
	struct http_parser hp;
	int fd;
	struct tls *tls;

	enum {
		HS_NONE,
		HS_VALUE,
		HS_FIELD
	} header_state;		/* the recommended state machine for catching headers. */
	struct buf_string current_header_name;
	struct buf_string current_header_value;
	char upgrade[32];

	const struct ctrl_handler_int *handler;
	void *handler_data;

	struct stringmap *qs;
	struct bconf_node *cr_bconf;
	struct bconf_node *custom_headers;
	size_t content_length;
	struct buf_string text;

	bool message_completed;
	bool keepalive;
	int close_conn;
	int status;
	int in_handler;
	const char *response_content_type;
	void *raw_response_data;
	size_t raw_response_data_sz;
};

#define WORKER_STATE(worker, ...) do { if (worker->thr_state) { stat_message_printf(worker->thr_state, __VA_ARGS__); } } while (0)

void
render_json_cb(struct ctrl_req *cr, struct stringmap *qs, void *data) {
	const char *root = data;
	bconf_json(bconf_get(cr->cr_bconf, root), 0, bconf_json_bscat, &cr->text);
	// Append a newline to match previous code, and for not breaking prompts after curl.
	bswrite(&cr->text, "\n", 1);
}

static int
error_consume_post(struct ctrl_req *cr, struct stringmap *qs, size_t totlen, const char *data, size_t len, void *v) {
	return 0;
}

static struct ctrl_handler_int error_handler = {
	.hand.url = "",
	.hand.consume_post = error_consume_post,
	.hand.finish = render_json_cb,
	.hand.cb_data = (char *)"error"
};

void
ctrl_output_json(struct ctrl_req *cr, const char *root) {
	ctrl_set_content_type(cr, "application/json");
	render_json_cb(cr, NULL, (void*)root);
}

void
ctrl_output_text(struct ctrl_req *cr, const char *fmt, ...) {
	ctrl_set_content_type(cr, "text/plain");
	va_list ap;
	va_start(ap, fmt);
	vbscat(&cr->text, fmt, ap);
	va_end(ap);
}

struct buf_string *
ctrl_get_textbuf(struct ctrl_req *cr) {
	return &cr->text;
}

void
ctrl_set_custom_headers(struct ctrl_req *cr, const char *key, const char *value) {
	if ((strncmp(key, "X-", 2) != 0) || bconf_get(cr->custom_headers, key))
		xerrx(1,"Only unique 'X-' headers allowed, \"%s\"", key);

	bconf_add_data(&cr->custom_headers, key, value);
}

void
ctrl_close(struct ctrl_req *cr) {
	cr->close_conn = 1;
}

void
ctrl_status(struct ctrl_req *cr, int status) {
	cr->status = status;
}

void
ctrl_error(struct ctrl_req *cr, int error, const char *fmt, ...) {
	char buf[32];
	va_list ap;
	char *msg = NULL;

	snprintf(buf, sizeof(buf), "%d", error);
	bconf_add_data(&cr->cr_bconf, "error.status", buf);

	va_start(ap, fmt);
	int res = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (res == -1)
		msg = (char*)"<error message lost>";

	bconf_add_data(&cr->cr_bconf, "error.message", msg);

	/* Generally this is only accessed by internal services so things shouldn't cause errors, so log everything as CRIT.
	 * Except for 404, which is used in normal flow.
	 */
	if (error == 404)
		log_printf(LOG_INFO, "controller: ctrl_error called: %d (%s) (handler: %s)", error, msg, cr->handler ? cr->handler->hand.url : "<none>");
	else
		log_printf(LOG_CRIT, "controller: ctrl_error called: %d (%s) (handler: %s)", error, msg, cr->handler ? cr->handler->hand.url : "<none>");

	cr->handler = &error_handler;
	cr->status = error;
	ctrl_close(cr);		/* We don't know if it's a fatal protocol error or just some misunderstanding, always close. */

	if (cr->in_handler == 1) {
		cr->in_handler = 2;
		(*error_handler.hand.finish)(cr, cr->qs, error_handler.hand.cb_data);
	}

	if (res != -1)
		free(msg);
}

static void
quit_listen_thread_and_broadcast(struct ctrl *ctrl, bool close_listen) {
	ctrl->quit = true;

	/* Wake up event listener */
	if (ctrl->closefd[1] >= 0) {
		close(ctrl->closefd[1]);
		ctrl->closefd[1] = -1;
	}

	if (close_listen) {
		close(ctrl->listen_socket);
		ctrl->listen_socket = -1;
	} else {
		/* Calling close removes the fd automatically. In this case we still
		 * remove it to stop handling connections. They'll instead start
		 * queuing up on the socket.
		 */
		event_e_remove(&ctrl->event_e, ctrl->listen_socket);
	}
	pthread_join(ctrl->listen_thread, NULL);

	pthread_mutex_lock(&ctrl->job_lock);
	pthread_cond_broadcast(&ctrl->job_cond);
	pthread_mutex_unlock(&ctrl->job_lock);
}

int
ctrl_quit_stage_one(struct ctrl *ctrl, bool close_listen) {
	log_printf(LOG_DEBUG, "ctrl_quit_stage_one");

	if (pthread_mutex_trylock(&ctrl->quit_mutex)) {
		log_printf(LOG_CRIT, "Another quit request is ongoing");
		return -1;
	}

	quit_listen_thread_and_broadcast(ctrl, close_listen);

	struct worker *executing_worker = NULL;
	while(!TAILQ_EMPTY(&ctrl->worker_threads)) {
		pthread_mutex_lock(&ctrl->queue_lock);
		struct worker *worker = TAILQ_FIRST(&ctrl->worker_threads);
		TAILQ_REMOVE(&ctrl->worker_threads, worker, worker_list);
		pthread_mutex_unlock(&ctrl->queue_lock);

		if (pthread_equal(worker->worker_thread, pthread_self())) {
			executing_worker = worker;
			continue;
		}
		pthread_join(worker->worker_thread, NULL);

		if (ctrl->stat_counters_prefix) {
			stat_message_dynamic_free(worker->thr_state);
			stat_message_dynamic_free(worker->handler_name);
			stat_counter_dynamic_free(worker->handler_data_total);
			stat_counter_dynamic_free(worker->handler_data_current);
		}

		free(worker);
	}

	if (executing_worker) {
		TAILQ_INSERT_TAIL(&ctrl->worker_threads, executing_worker, worker_list);
	}

	return 0;
}

void
ctrl_quit_stage_two(struct ctrl *ctrl) {
	log_printf(LOG_DEBUG, "ctrl_quit stage two");
	if (ctrl->listen_socket != -1)
		close(ctrl->listen_socket);

	/* There can be at most one, otherwise something went horribly wrong */
	if (!TAILQ_EMPTY(&ctrl->worker_threads)) {
		struct worker *worker = TAILQ_FIRST(&ctrl->worker_threads);
		TAILQ_REMOVE(&ctrl->worker_threads, worker, worker_list);
		if (!TAILQ_EMPTY(&ctrl->worker_threads)) {
			log_printf(LOG_CRIT, "controller: Improper termination while on stage two of quiting exiting");
			exit(1);
		}

		pthread_join(worker->worker_thread, NULL);

		if (ctrl->stat_counters_prefix) {
			stat_message_dynamic_free(worker->thr_state);
			stat_message_dynamic_free(worker->handler_name);
			stat_counter_dynamic_free(worker->handler_data_total);
			stat_counter_dynamic_free(worker->handler_data_current);
		}

		free(worker);
	}
	pthread_mutex_destroy(&ctrl->queue_lock);
	pthread_mutex_destroy(&ctrl->job_lock);
	pthread_cond_destroy(&ctrl->job_cond);

	for (int i = 0; i < ctrl->nhandlers; i++) {
		if (ctrl->handlers[i].cnt)
			stat_counter_dynamic_free(ctrl->handlers[i].cnt);
	}
	free(ctrl->handlers);

	/* We are the only ones hadling here, no need to lock */
	while (!TAILQ_EMPTY(&ctrl->event_list)) {
		struct event_handler *event_handler = TAILQ_FIRST(&ctrl->event_list);
		TAILQ_REMOVE(&ctrl->event_list, event_handler, event_entry);
		if (event_handler->tls) {
			tls_stop(event_handler->tls);
			tls_free(event_handler->tls);
		}
		close(event_handler->fd);
		free(event_handler);
	}

	pthread_mutex_destroy(&ctrl->event_lock);
	pthread_mutex_destroy(&ctrl->quit_mutex);

	stat_counter_dynamic_free(ctrl->num_accept);

	if (ctrl->tls.key)
		tls_free_key(ctrl->tls.key);
	tls_free_cert_array(ctrl->tls.ncerts, ctrl->tls.cert_arr);
	tls_clear_context(&ctrl->tls.ctx);

	if (ctrl->closefd[0] >= 0)
		close(ctrl->closefd[0]);
	if (ctrl->closefd[1] >= 0)
		close(ctrl->closefd[1]);

	free(ctrl);
}

void
ctrl_quit(struct ctrl *ctrl) {
	if (!ctrl)
		return;
	log_printf(LOG_DEBUG, "ctrl_quit");
	if (ctrl_quit_stage_one(ctrl, true)) {
		log_printf(LOG_CRIT, "controller: ctrl_quit() called while another quit is in progress");
		return;
	}

	ctrl_quit_stage_two(ctrl);
}

void
ctrl_set_handler_data(struct ctrl_req *cr, void *v) {
	cr->handler_data = v;
}

void
ctrl_set_content_type(struct ctrl_req *cr, const char *ct) {
	cr->response_content_type = ct;
}

void
ctrl_set_raw_response_data(struct ctrl_req *cr, void *d, size_t sz) {
	cr->raw_response_data = d;
	cr->raw_response_data_sz = sz;
}

size_t
ctrl_get_content_length(struct ctrl_req *cr) {
	return cr->content_length;
}

struct bconf_node **
ctrl_get_bconfp(struct ctrl_req *cr) {
	return &cr->cr_bconf;
}

const struct ctrl_handler *
ctrl_get_handler(struct ctrl_req *cr) {
	return &cr->handler->hand;
}

const char *
ctrl_get_upgrade(struct ctrl_req *cr) {
	if (!cr->hp.upgrade)
		return NULL;
	if (cr->upgrade[0])
		return cr->upgrade;
	return NULL;
}

int
ctrl_get_peer(struct ctrl_req *cr, char *hbuf, size_t hbuflen, char *pbuf, size_t pbuflen, int gni_flags) {
	struct sockaddr_storage ss;
	socklen_t sslen = sizeof(ss);

	if (getpeername(cr->fd, (struct sockaddr*)&ss, &sslen))
		return EAI_SYSTEM;
	int r = getnameinfo((struct sockaddr*)&ss, sslen, hbuf, hbuflen, pbuf, pbuflen, gni_flags);
	if (r)
		return r;
	/* Remove V4MAPPED prefix, if any. */
	if (strncmp(hbuf, "::ffff:", strlen("::ffff:")) == 0)
		memmove(hbuf, hbuf + strlen("::ffff:"), strlen(hbuf) + 1 - strlen("::ffff:"));
	return 0;
}

char *
ctrl_get_peer_commonname(struct ctrl_req *cr, char *cnbuf, size_t cnbuflen) {
	if (!cr->tls)
		return NULL;

	tlscert_t peercert = tls_get_peer_cert(cr->tls);
	if (!peercert) {
		log_printf(LOG_DEBUG, "No peer certificate");
		return NULL;
	}

	tls_get_cn(peercert, cnbuf, cnbuflen);
	tls_free_cert(peercert);
	return cnbuf;
}

char *
ctrl_get_peer_issuer(struct ctrl_req *cr, char *buf, size_t buflen) {
	if (!cr->tls)
		return NULL;

	tlscert_t peercert = tls_get_peer_cert(cr->tls);
	if (!peercert) {
		log_printf(LOG_DEBUG, "No peer certificate");
		return NULL;
	}

	tls_get_issuer_cn(peercert, buf, buflen);
	tls_free_cert(peercert);
	return buf;
}

static int
on_message_begin(struct http_parser *hp) {
	struct ctrl_req *cr = hp->data;

	cr->status = 200;	/* Assume success */

	return 0;
}

static int
on_message_complete(struct http_parser *hp) {
	struct ctrl_req *cr = hp->data;

	if (cr->handler) {
		struct buf_string hdrs = { 0 };
		char *data;
		size_t data_sz;
		ssize_t r;
		char *hdr_data;
		size_t hdr_sz;
		char buf[128];

		WORKER_STATE(cr->worker, "handler_finish");

		cr->in_handler = 1;		/* For error bailouts. */
		(*cr->handler->hand.finish)(cr, cr->qs, cr->handler_data ?: cr->handler->hand.cb_data);

		if (cr->raw_response_data != NULL) {
			data = cr->raw_response_data;
			data_sz = cr->raw_response_data_sz;
			if (cr->text.pos != 0)
				log_printf(LOG_CRIT, "controller: [%s]: raw data with buf output, using raw data", cr->handler->hand.url);
		} else {
			data = cr->text.buf;
			data_sz = cr->text.pos;
		}

		static const char * const statusmsg[] = {
			"Error",
			"Continue",
			"Success",
		};

		bscat(&hdrs, "HTTP/1.1 %d %s\r\n", cr->status, cr->status >= 100 && cr->status < 300 ? statusmsg[cr->status / 100] : "Error");
		date_format_rfc1123(buf, sizeof(buf), time(NULL));
		bscat(&hdrs, "Date: %s\r\n", buf);
		if (cr->close_conn)
			bscat(&hdrs, "Connection: close\r\n");
		bscat(&hdrs, "Content-Length: %llu\r\n", (unsigned long long)data_sz);
		if (cr->response_content_type)
			bscat(&hdrs, "Content-Type: %s\r\n", cr->response_content_type);
		if (cr->status == 101)
			bscat(&hdrs, "Upgrade: %s\r\nConnection: Upgrade\r\n", cr->upgrade);

		for (int i = 0; i < bconf_count(cr->custom_headers); i++)
			bscat(&hdrs, "%s: %s\r\n", bconf_key(bconf_byindex(cr->custom_headers, i)), bconf_value(bconf_byindex(cr->custom_headers, i)));
		bscat(&hdrs, "\r\n");

		WORKER_STATE(cr->worker, "sending_result (%llu + %zd bytes)", (unsigned long long)hdrs.pos, data_sz);

		hdr_data = hdrs.buf;
		hdr_sz = hdrs.pos;
		do {
			if (cr->tls)
				r = tls_write(cr->tls, hdr_data, hdr_sz);
			else
				r = write(cr->fd, hdr_data, hdr_sz);
			if (r < 1) {
				log_printf(LOG_CRIT, "controller: Failed to write response header: %m");
			} else {
				hdr_data += r;
				hdr_sz -= r;
			}
		} while (r > 0 && hdr_sz > 0);
		free(hdrs.buf);
		if (data_sz && hdr_sz == 0) {
			do {
				if (cr->tls)
					r = tls_write(cr->tls, data, data_sz);
				else
					r = write(cr->fd, data, data_sz);
				if (r < 1) {
					log_printf(LOG_CRIT, "controller: Failed to write response data: %m");
				} else {
					data += r;
					data_sz -= r;
				}
			} while (r > 0 && data_sz > 0);
		}
		free(cr->text.buf);
		cr->text.buf = NULL;
		cr->text.pos = 0;

		if (cr->handler->hand.cleanup)
			(*cr->handler->hand.cleanup)(cr, cr->handler_data ?: cr->handler->hand.cb_data);

		WORKER_STATE(cr->worker, "result sent");
	}

	if (cr->worker->handler_name) {
		stat_message_printf(cr->worker->handler_name, "<none>");
		STATCNT_SET(cr->worker->handler_data_total, 0);
		STATCNT_SET(cr->worker->handler_data_current, 0);
	}

	cr->message_completed = true;

	return 0;
}

static void
parse_qs_cb(struct parse_cb_data *d, char *key, int klen, char *val, int vlen) {
	struct stringmap *qs = d->cb_data;
	sm_insert(qs, key, klen, val, vlen);
}

struct path_param {
	const char *key;
	size_t key_len;
	const char *value;
	size_t value_len;
};

static bool
match_handler(const char *handler_url, size_t handler_url_len, const char *request_url, size_t request_url_len, struct path_param **p, int *num_params) {
	bool match = false;
	const char *hup = handler_url;
	const char *hend = handler_url + handler_url_len;
	const char *rup = request_url;
	const char *rend = request_url + request_url_len;
	int paramidx = 0;
	int num_vars = 0, num_var_ends = 0;
	struct path_param *params = NULL;

	for (const char *hp = hup; hp < hend; hp++) {
		int diff;
		if (*hp == '<')
			num_vars++;
		if (*hp == '>')
			num_var_ends++;

		diff = num_vars - num_var_ends;
		if (diff != 0 && diff != 1) {
			log_printf(LOG_CRIT, "Malformed handler url found: %.*s", (int)handler_url_len, handler_url);
			goto out;
		}
	}

	params = xcalloc(num_vars, sizeof(*params));
	for (; hup != hend && rup != rend; hup++, rup++) {
		if (*hup == *rup) {
			continue;
		} else if (*hup == '<') {
			params[paramidx].key = ++hup;
			while (*hup++ != '>') {
				params[paramidx].key_len++;
			}

			params[paramidx].value = rup;
			while (*rup != '/' && rup != rend) {
				rup++;
				params[paramidx].value_len++;
			}
			paramidx++;

			if (rup == rend || hup == hend)
				break;
		} else if (*hup != *rup) {
			break;
		}
	}

	if (hup != hend || rup != rend) {
		goto out;
	}

	log_printf(LOG_DEBUG, "Found matching handler for with url: %.*s", (int)handler_url_len, handler_url);
	match = true;

out:
	if (!match) {
		free(params);
		params = NULL;
		num_vars = 0;
	}
	*p = params;
	*num_params = num_vars;
	return match;
}

static struct bconf_node *
get_default_acl(void) {
	static struct bconf_node *default_acl;
	if (!default_acl) {
		static pthread_mutex_t default_acl_lock = PTHREAD_MUTEX_INITIALIZER;
		pthread_mutex_lock(&default_acl_lock);
		if (!default_acl) {
			static struct bconf_node *tmproot;
			bconf_add_data(&tmproot, "0.method", "*");
			bconf_add_data(&tmproot, "0.path", "/");
			bconf_add_data(&tmproot, "0.remote_addr", "::1");
			bconf_add_data(&tmproot, "0.action", "allow");
			bconf_add_data(&tmproot, "1.method", "*");
			bconf_add_data(&tmproot, "1.path", "/");
			bconf_add_data(&tmproot, "1.remote_addr", "127.0.0.1");
			bconf_add_data(&tmproot, "1.action", "allow");
			bconf_add_data(&tmproot, "2.method", "*");
			bconf_add_data(&tmproot, "2.path", "/");
			bconf_add_data(&tmproot, "2.cert.cn", "*");
			bconf_add_data(&tmproot, "2.action", "allow");
			default_acl = tmproot;
		}
		pthread_mutex_unlock(&default_acl_lock);
	}
	return default_acl;
}

static char *
ctrl_get_peer_check(struct ctrl_req *cr, char *buf, size_t buflen) {
	int r = ctrl_get_peer(cr, buf, buflen, NULL, 0, NI_NUMERICHOST);
	return r == 0 ? buf : NULL;
}

static bool
check_acl(struct ctrl_req *cr, const char *url, size_t urllen, struct bconf_node *acl) {
	if (!acl)
		acl = get_default_acl();

	struct {
		const char *key;
		char *(*get)(struct ctrl_req *cr, char *buf, size_t buflen);
		char buf[1024];
		bool tried;
	} *curr, checks[] = {
		{"remote_addr", ctrl_get_peer_check},
		{"cert.cn", ctrl_get_peer_commonname},
		{"issuer.cn", ctrl_get_peer_issuer},
		{NULL},
	};

	struct bconf_node *aclnode;
	for (int i = 0 ; (aclnode = bconf_byindex(acl, i)) ; i++) {
		const char *method = bconf_get_string(aclnode, "method");
		if (!method)
			continue;
		if (strcmp(method, "*") && strcmp(method, http_method_str(cr->hp.method)))
			continue;

		const char *path = bconf_get_string(aclnode, "path");
		if (!path || *path == 0)
			continue;
		size_t pathlen = strlen(path);
		if (pathlen > urllen)
			continue;
		if (strncmp(url, path, pathlen) != 0)
			continue;
		if (path[pathlen-1] != '/' && pathlen != urllen)
			continue;

		for (curr = checks ; curr->key ; curr++) {
			const char *value = bconf_get_string(aclnode, curr->key);
			if (!value)
				continue;

			if (!curr->tried && !curr->get(cr, curr->buf, sizeof(curr->buf))) {
				curr->tried = true;
				log_printf(LOG_ERR, "check_acl: Failed to get peer %s", curr->key);
				break;
			}
			curr->tried = true;

			if (curr->buf[0] == '\0')
				break;

			if (strcmp(value, "*") != 0 && strcmp(value, curr->buf) != 0)
				break;
		}
		if (curr->key)
			continue;

		/* All test succeeded, check for action */
		const char *value = bconf_get_string(aclnode, "action");
		if (!value)
			continue;

		if (strcmp(value, "allow") == 0)
			return true;
		return false;
	}
	return false;
}

static int
on_url(struct http_parser *hp, const char *at, size_t length) {
	struct ctrl_req *cr = hp->data;
	struct http_parser_url hpu;
	struct path_param *params = NULL;
	int num_path_params = 0;
	int res;

	if ((res = http_parser_parse_url(at, length, 0, &hpu)) != 0) {
		log_printf(LOG_CRIT, "handle_command: url parse failed %d", res);
		return res;
	}

	/*
	 * We only care about path and query.
	 */
	if (!(hpu.field_set & (1 << UF_PATH))) {
		ctrl_error(cr, 400, "on_url: no path");
		return 0;
	}
	for (int i = 0; i < cr->worker->ctrl->nhandlers; i++) {
		const struct ctrl_handler_int *hi = &cr->worker->ctrl->handlers[i];

		if (match_handler(hi->hand.url, strlen(hi->hand.url), at + hpu.field_data[UF_PATH].off, hpu.field_data[UF_PATH].len, &params, &num_path_params)) {
			cr->handler = hi;
			if (hi->cnt)
				STATCNT_INC(hi->cnt);
			break;
		}
	}

	if (!check_acl(cr, at + hpu.field_data[UF_PATH].off, hpu.field_data[UF_PATH].len,
			bconf_get(cr->worker->ctrl->ctrl_conf, "acl"))) {
		if (cr->worker->ctrl->acl_disabled) {
			log_printf(LOG_WARNING, "controller: ACL check failed, but ACL disabled");
		} else {
			ctrl_error(cr, 403, "Forbidden (%.*s)", hpu.field_data[UF_PATH].len, at + hpu.field_data[UF_PATH].off);
			free(params);
			return 0;
		}
	}

	if (cr->handler == NULL) {
		ctrl_error(cr, 404, "unknown url (%.*s)", hpu.field_data[UF_PATH].len, at + hpu.field_data[UF_PATH].off);
		free(params);
		return 0;
	}

	if (cr->worker->handler_name) {
		stat_message_printf(cr->worker->handler_name, "%s", &cr->handler->hand.url[1]);
	}

	cr->custom_headers = NULL;

	if (num_path_params) {
		cr->qs = sm_new();
		for (int i = 0; i < num_path_params; i++)
			sm_insert(cr->qs, params[i].key, params[i].key_len, params[i].value, params[i].value_len);
	}

	if (cr->handler->hand.start) {
		(*cr->handler->hand.start)(cr, cr->handler->hand.cb_data);
	}

	if (hpu.field_set & (1 << UF_QUERY)) {
		char *qs = strndup(at + hpu.field_data[UF_QUERY].off, hpu.field_data[UF_QUERY].len); /* GRR. parse_query_string should be smarter. */

		/* Parse query string will just silently ignore arguments that it doesn't like. I'd like to see some error handling too. */
		if (!cr->qs)
			cr->qs = sm_new();
		parse_query_string(qs, parse_qs_cb, cr->qs, NULL, 0, RUTF8_REQUIRE, NULL);
		free(qs);
	}

	free(params);
	return 0;
}

/* For now we limit the body size to 100GB */
#define MAX_BODY_SIZE (100LL*1024*1024*1024)

#include "ctrl_header.h"

static void
finalize_header(struct ctrl_req *cr) {
	if (cr->current_header_name.pos != 0) {
		const char *nameb = cr->current_header_name.buf;
		int namep = cr->current_header_name.pos;
		const char *valueb = cr->current_header_value.buf;
		int valuep = cr->current_header_value.pos;

		/* Be safe. Even though http_parser probably deals with this already. */
		if (valueb == NULL || valuep == 0) {
			ctrl_error(cr, 400, "bad header value");
			goto out;
		}

		/*
		 * There are only a few headers we care about at this moment. All other headers are dropped.
		 */
		GPERF_ENUM_NOCASE(ctrl_header)
		switch (lookup_ctrl_header(nameb, namep)) {
		case GPERF_CASE("Content-Length"):
			{
				char *endptr = NULL;
				long long v;
				errno = 0;
				v = strtoll(valueb, &endptr, 10);
				if (errno == ERANGE || v < 0 || v > MAX_BODY_SIZE || endptr != valueb + valuep) {
					ctrl_error(cr, 400, "bad content-length");
					goto out;
				}
				cr->content_length = v;
				if (cr->worker->handler_data_total)
					STATCNT_SET(cr->worker->handler_data_total, v);
			}
			break;
		case GPERF_CASE("Connection"):
			if (!strncmp(valueb, "close", valuep))
				ctrl_close(cr);
			break;
		case GPERF_CASE("Upgrade"):
			strlcpy(cr->upgrade, valueb, sizeof(cr->upgrade));
			break;
		case GPERF_CASE_NONE:
			break;
		}
	}
out:
	free(cr->current_header_name.buf);
	cr->current_header_name.buf = NULL;
	cr->current_header_name.len = cr->current_header_name.pos = 0;
	free(cr->current_header_value.buf);
	cr->current_header_value.buf = NULL;
	cr->current_header_value.len = cr->current_header_value.pos = 0;
}

static int
on_header_field(struct http_parser *hp, const char *at, size_t length) {
	struct ctrl_req *cr = hp->data;

	switch (cr->header_state) {
	case HS_VALUE:
		finalize_header(cr);
		break;
	case HS_NONE:
	case HS_FIELD:
		break;
	}
	cr->header_state = HS_FIELD;
	bswrite(&cr->current_header_name, at, length);
	return 0;
}

static int
on_header_value(struct http_parser *hp, const char* at, size_t length) {
	struct ctrl_req *cr = hp->data;

	switch (cr->header_state) {
	case HS_VALUE:
	case HS_FIELD:
		break;
	default:
	case HS_NONE:
		log_printf(LOG_CRIT, "on_header_value: wrong state %d", cr->header_state);
		return 1;
	}
	cr->header_state = HS_VALUE;
	bswrite(&cr->current_header_value, at, length);
	return 0;
}

static int
on_headers_complete(struct http_parser *hp) {
	struct ctrl_req *cr = hp->data;

	finalize_header(cr);
	return 0;
}

static int
on_body(struct http_parser *hp, const char* at, size_t length) {
	struct ctrl_req *cr = hp->data;
	int ret;

	/* In the future we want to decode things here with json, for now, we just error out. */
	if (cr->handler == NULL || cr->handler->hand.consume_post == NULL) {
		ctrl_error(cr, 400, "POST data with no data handler.");
		return 0;
	}

	ret = (*cr->handler->hand.consume_post)(cr, cr->qs, cr->content_length, at, length, cr->handler_data ?: cr->handler->hand.cb_data);
	if (!ret && cr->worker->handler_data_current) {
		STATCNT_ADD(cr->worker->handler_data_current, length);
	}
	return ret;
}

static struct http_parser_settings hp_settings = {
	.on_message_begin = on_message_begin,
	.on_url = on_url,
	.on_header_field = on_header_field,
	.on_header_value = on_header_value,
	.on_headers_complete = on_headers_complete,
	.on_body = on_body,
	.on_message_complete = on_message_complete,

};

static void
handle_request(struct ctrl_req *cr, int initbyte) {
	char buf[65536];
	size_t nparsed;
	ssize_t len;

	http_parser_init(&cr->hp, HTTP_REQUEST);
	cr->hp.data = cr;

	if (initbyte >= 0) {
		char ch = initbyte;
		nparsed = http_parser_execute(&cr->hp, &hp_settings, &ch, 1);
		if (nparsed != 1) {
			log_printf(LOG_CRIT, "handle_command: request parse error: %s (%s)",
					http_errno_description(HTTP_PARSER_ERRNO(&cr->hp)),
					http_errno_name(HTTP_PARSER_ERRNO(&cr->hp)));
			return;
		}
	}

	do {
		if (cr->tls)
			len = tls_read(cr->tls, buf, sizeof(buf));
		else
			len = read(cr->fd, buf, sizeof(buf));

		if (len < 0) {
			log_printf(LOG_CRIT, "handle_request: read %m");
			return;
		}

		if (len == 0) {
			WORKER_STATE(cr->worker, "closed, empty read");
			return;
		}

		nparsed = http_parser_execute(&cr->hp, &hp_settings, buf, len);
		if (nparsed != (size_t)len) {
			log_printf(LOG_CRIT, "handle_command: request parse error: %s (%s)",
					http_errno_description(HTTP_PARSER_ERRNO(&cr->hp)),
					http_errno_name(HTTP_PARSER_ERRNO(&cr->hp)));
			return;
		}
	} while (!cr->message_completed);

	if (cr->hp.upgrade && cr->handler->hand.upgrade && cr->status == 101) {
		WORKER_STATE(cr->worker, "upgraded");
		cr->keepalive = false;
		(*cr->handler->hand.upgrade)(cr, cr->fd, cr->tls, cr->handler_data ?: cr->handler->hand.cb_data);
		cr->fd = -1;
		cr->tls = NULL;
	} else if (cr->close_conn || !http_should_keep_alive(&cr->hp)) {
		/* Always close if asked to */
		WORKER_STATE(cr->worker, "closed");
	} else {
		WORKER_STATE(cr->worker, "keepalive");
		cr->keepalive = true;
	}
	sm_free(cr->qs);
	bconf_free(&cr->custom_headers);
	bconf_free(&cr->cr_bconf);
	cr->handler = NULL;
	cr->handler_data = NULL;
	cr->content_length = 0;
	cr->status = 0;
	cr->in_handler = 0;
	cr->response_content_type = NULL;
}

static int
check_for_tls(struct ctrl *ctrl, struct ctrl_req *cr) {
	char ch;
	int r = read(cr->fd, &ch, 1);

	if (r <= 0) {
		if (r < 0)
			log_printf(LOG_CRIT, "controller: initial read failed: %m");
		return -2;
	}

	if (ch != 0x16) /* All HTTPS connections start with this byte. */
		return ch & 0xff;

	if (!ctrl->tls.enabled) {
		log_printf(LOG_CRIT, "controller: HTTPS disabled");
		return -2;
	}

	cr->tls = tls_open(&ctrl->tls.ctx, cr->fd, tlsVerifyPeer|tlsVerifyOptional, ctrl->tls.cert_arr[0], ctrl->tls.key, false);
	if (!cr->tls) {
		log_printf(LOG_CRIT, "controller: failed to initalize TLS");
		return -2;
	}

	tls_inject_read(cr->tls, &ch, 1);
	tls_start(cr->tls);
	do {
		r = tls_accept(cr->tls);
	} while (r > 0);
	if (r < 0) {
		log_printf(LOG_CRIT, "controller: tls_accept: %s", tls_error(cr->tls, r));
		tls_free(cr->tls);
		cr->tls = NULL;
		return -2;
	}

	return -1;
}


int
ctrl_get_listen_socket(struct ctrl *ctrl) {
	return ctrl->listen_socket;
}

static inline void
queue_job_and_signal(struct ctrl *ctrl, int fd, bool initial, struct tls *tls) {
	struct job *job = calloc(1, sizeof(*job));
	job->initial = initial;
	job->fd = fd;
	job->tls = tls;

	pthread_mutex_lock(&ctrl->job_lock);
	TAILQ_INSERT_TAIL(&ctrl->job_list, job, entry_list);
	pthread_cond_signal(&ctrl->job_cond);
	pthread_mutex_unlock(&ctrl->job_lock);
}

static void
read_event(struct event_handler *event_handler, struct ctrl *ctrl) {
	/*
	 * Remove the event from the control so that it does not wake
	 * on multiple reads on the same request
	 */
	int fd = event_handler->fd;
	struct tls *tls = event_handler->tls;

	event_e_triggered(&ctrl->event_e, fd);

	pthread_mutex_lock(&ctrl->event_lock);
	TAILQ_REMOVE(&ctrl->event_list, event_handler, event_entry);
	pthread_mutex_unlock(&ctrl->event_lock);
	free(event_handler);

	queue_job_and_signal(ctrl, fd, false, tls);
}

static void
close_event(struct event_handler *event_handler, struct ctrl *ctrl) {
	pthread_mutex_lock(&ctrl->event_lock);
	TAILQ_REMOVE(&ctrl->event_list, event_handler, event_entry);
	pthread_mutex_unlock(&ctrl->event_lock);

	event_e_remove(&ctrl->event_e, event_handler->fd);

	/* Drain */
	uint64_t unused;
	if (read(event_handler->fd, &unused, sizeof(unused)) != sizeof(unused))
		log_printf(LOG_WARNING, "Failed to drain closefd, ignoring: %m");

	free(event_handler);
}

static void
accept_event(struct event_handler *event_handler, struct ctrl *ctrl) {
	struct sockaddr_storage sas;
	socklen_t slen = sizeof(sas);

	int fd = accept(ctrl->listen_socket, (struct sockaddr *)&sas, &slen);
	if (fd < 0) {
		log_printf(LOG_CRIT, "Error accepting in listen socket: %m");
		/* XXX: is it in any way recoverable??? */
		return;
	}
	STATCNT_INC(ctrl->num_accept);

	queue_job_and_signal(ctrl, fd, true, NULL);
}

static void
event_add(struct ctrl *ctrl, void (*cb)(struct event_handler *, struct ctrl *), int fd, struct tls *tls) {
	struct event_handler *event_handler = calloc(1, sizeof(*event_handler));
	if (!event_handler) {
		log_printf(LOG_CRIT, "failed to calloc event_handler: %m");
		exit(1);
	}

	event_handler->cb = cb;
	event_handler->fd = fd;
	event_handler->tls = tls;

	pthread_mutex_lock(&ctrl->event_lock);
	TAILQ_INSERT_TAIL(&ctrl->event_list, event_handler, event_entry);
	pthread_mutex_unlock(&ctrl->event_lock);

	/*
	 * We might have closed the engine but still be processing
	 * jobs from keepalive connections, close the fd and log the errors
	 */
	if (!ctrl->quit && event_e_add(&ctrl->event_e, event_handler, fd) < 0) {
		log_printf(LOG_CRIT, "Error adding socket to event set: %m");

		pthread_mutex_lock(&ctrl->event_lock);
		TAILQ_REMOVE(&ctrl->event_list, event_handler, event_entry);
		pthread_mutex_unlock(&ctrl->event_lock);
		free(event_handler);
		if (tls) {
			tls_stop(tls);
			tls_free(tls);
		}

		close(fd);
		return;
	}
}

static void *
listen_thread(void *v) {
	struct ctrl *ctrl = (struct ctrl *)v;

	event_e_init(&ctrl->event_e);
	event_add(ctrl, accept_event, ctrl->listen_socket, NULL);
	if (ctrl->closefd[0] >= 0)
		event_add(ctrl, close_event, ctrl->closefd[0], NULL);

	while (!ctrl->quit) {
		if (event_e_handle(&ctrl->event_e, ctrl) < 0) {
			if (errno == EINTR)
				continue;
			log_printf(LOG_CRIT, "Error handling events: %m");
			ctrl->quit = true;
			break;
		}
	}

	event_e_close(&ctrl->event_e);
	pthread_exit(NULL);
}

static void *
worker_thread(void *v) {
	struct worker *worker = (struct worker *)v;
	struct ctrl *ctrl = worker->ctrl;

	WORKER_STATE(worker, "idle");
	pthread_mutex_lock(&ctrl->job_lock);
	while (!ctrl->quit) {
		if (TAILQ_EMPTY(&ctrl->job_list))
			pthread_cond_wait(&ctrl->job_cond, &ctrl->job_lock);
		while (!TAILQ_EMPTY(&ctrl->job_list)) {

			struct job *job = TAILQ_FIRST(&ctrl->job_list);
			if (!job)
				continue;

			TAILQ_REMOVE(&ctrl->job_list, job, entry_list);
			pthread_mutex_unlock(&ctrl->job_lock);

			int fd = job->fd;
			bool initial = job->initial;
			struct tls *tls = job->tls;
			free(job);
			if (fd < 0) {
				pthread_mutex_lock(&ctrl->job_lock);
				continue;
			}

			WORKER_STATE(worker, "handling");
			struct ctrl_req cr = {
				.worker = worker,
				.fd	= fd,
				.keepalive = false,
				.tls = tls,
			};

			int r = -1;
			if (initial)
				r = check_for_tls(ctrl, &cr);

			if (r != -2)
				handle_request(&cr, r);

			if (cr.keepalive) {
				event_add(ctrl, read_event, fd, cr.tls);
			} else {
				if (cr.tls) {
					tls_stop(cr.tls);
					tls_free(cr.tls);
				}
				if (cr.fd != -1)
					close(cr.fd);
			}

			WORKER_STATE(worker, "idle");
			pthread_mutex_lock(&ctrl->job_lock);
		}
	}
	pthread_mutex_unlock(&ctrl->job_lock);

	pthread_exit(NULL);
}

static int
ctrl_setup_https_server(struct ctrl *ctrl, struct bconf_node *ctrl_conf, const char *cert_host,
		struct https_state *https) {
	FILE *f = NULL;
	const char *p = bconf_get_string(ctrl_conf, "cert.command");
	if (p && *p)
		f = popen(p, "r");
	else if ((p = bconf_get_string(ctrl_conf, "cert.path")) && *p)
		f = fopen(p, "r");
	else if (https && https->certfile[0]) {
		p = https->certfile;
		f = fopen(p, "r");
	}

	tlskey_t key = NULL;
	tlscert_t *certs = NULL;
	int ncerts = 0;
	if (f) {
		struct buf_string bs = {0};
		bs_fread_all(&bs, f);
		fclose(f);

		key = tls_read_key_buf(bs.buf, bs.pos);
		ncerts = tls_read_cert_array_buf(bs.buf, bs.pos, &certs);
		free(bs.buf);
		if (ncerts < 0) {
			log_printf(LOG_CRIT, "Failed to parse a certificate, tried %s", p);
			tls_free_key(key);
			return -1;
		}
	}
	int ret = 0;
	if (!key && !ncerts && !p) {
		if (!cert_host)
			cert_host = "localhost";
		log_printf(LOG_WARNING, "controller: Generating self signed certificate");
		key = tls_generate_key(2048);
		certs = xmalloc(sizeof(*certs));
		certs[0] = tls_generate_selfsigned_cert(key, cert_host);
		ncerts = 1;
		ret = 1;
	}
	if (!key || !ncerts) {
		if (!p)
			errno = EINVAL;
		log_printf(LOG_CRIT, "HTTPS enabled but failed to get key or certificate (%m), tried: %s", p);
		return -1;
	}
	ctrl->tls.enabled = true;
	ctrl->tls.ncerts = ncerts;
	ctrl->tls.cert_arr = certs;
	ctrl->tls.key = key;
	if (ncerts > 1)
		tls_add_ca_chain(&ctrl->tls.ctx, ncerts - 1, certs + 1);
	return ret;
}

static int
ctrl_setup_https_cacert(struct ctrl *ctrl, struct bconf_node *ctrl_conf, struct https_state *https) {
	FILE *f = NULL;
	const char *p = bconf_get_string(ctrl_conf, "cacert.command");
	if (p && *p)
		f = popen(p, "r");
	else if ((p = bconf_get_string(ctrl_conf, "cacert.path")) && *p)
		f = fopen(p, "r");
	else if (https && https->cafile[0]) {
		p = https->cafile;
		f = fopen(p, "r");
	}
	tlscert_t *cacerts = NULL;
	int ncacerts = 0;
	if (f) {
		struct buf_string bs = {0};
		bs_fread_all(&bs, f);
		fclose(f);
		ncacerts = tls_read_cert_array_buf(bs.buf, bs.pos, &cacerts);
		free(bs.buf);
	}
	if (!ncacerts) {
		log_printf(LOG_WARNING, "controller: No CA certificate, client authentication not possible.");
	}
	ctrl->tls.ctx.cacerts = cacerts;
	ctrl->tls.ctx.ncacerts = ncacerts;
	return 0;
}

struct ctrl *
ctrl_setup(struct bconf_node *ctrl_conf, const struct ctrl_handler *handlers, int nhandlers, int listen_socket,
		struct https_state *https) {
	const char *host = bconf_get_string(ctrl_conf, "host");
	const char *port = bconf_get_string(ctrl_conf, "port");
	struct ctrl *ctrl = calloc(1, sizeof *ctrl);
	int nthr;
	int r;
	int i;
	const char *cert_host = host;

	/* XXX: sysloghook necessity */
	log_printf(LOG_INFO, "controller: setting up controller");

	if (!bconf_get_int(ctrl_conf, "bind_host"))
		host = NULL;

	if (listen_socket == -1) {
		if ((ctrl->listen_socket = create_socket(host, port)) == -1) {
			free(ctrl);
			return NULL;
		}
	} else {
		/* There already is a listen socket. */
		ctrl->listen_socket = listen_socket;
	}

	if (bconf_get_int_default(ctrl_conf, "https", 1)) {
		if (ctrl_setup_https_cacert(ctrl, ctrl_conf, https)) {
			close(ctrl->listen_socket);
			free(ctrl);
			return NULL;
		}
		int rs = ctrl_setup_https_server(ctrl, ctrl_conf, cert_host, https);
		if (rs == -1) {
			close(ctrl->listen_socket);
			free(ctrl);
			return NULL;
		}
		if (rs == 1 && !ctrl->tls.ctx.ncacerts) {
			/* Disable the default ACL if no https is setup. */
			ctrl->acl_disabled = true;
		}
	} else {
		ctrl->acl_disabled = true;
	}
	if (bconf_get(ctrl_conf, "acl"))
		ctrl->acl_disabled = false;
	if (bconf_get_int(ctrl_conf, "acl_disabled")) {
		ctrl->acl_disabled = true;
		log_printf(LOG_INFO, "controller: ACL explicitely disabled");
	} else if (ctrl->acl_disabled) {
		log_printf(LOG_WARNING, "controller: Default ACL disabled");
	}

	pthread_mutex_init(&ctrl->queue_lock, NULL);
	pthread_mutex_init(&ctrl->quit_mutex, NULL);
	TAILQ_INIT(&ctrl->worker_threads);

	pthread_mutex_init(&ctrl->job_lock, NULL);
	pthread_cond_init(&ctrl->job_cond, NULL);
	TAILQ_INIT(&ctrl->job_list);

	pthread_mutex_init(&ctrl->event_lock, NULL);
	TAILQ_INIT(&ctrl->event_list);

	ctrl->ctrl_conf = ctrl_conf;
	ctrl->stat_counters_prefix = bconf_get_string(ctrl->ctrl_conf, "stat_counters_prefix");
	ctrl->nhandlers = nhandlers;
	ctrl->handlers = calloc(nhandlers, sizeof(*ctrl->handlers));
	for (i = 0; i < nhandlers; i++) {
		ctrl->handlers[i].hand = handlers[i];
		if (ctrl->stat_counters_prefix) {
			/* We skip the first character of the url because it's always a '/'. */
			ctrl->handlers[i].cnt = stat_counter_dynamic_alloc(3, ctrl->stat_counters_prefix, &handlers[i].url[1], "calls");
		}
	}

	/* Non fatal */
	if (pipe(ctrl->closefd) < 0) {
		log_printf(LOG_WARNING, "Failed to create closefd %m");
		ctrl->closefd[0] = ctrl->closefd[1] = -1;
	}

	ctrl->num_accept = stat_counter_dynamic_alloc(2, "controller", "accept");
	if ((r = pthread_create(&ctrl->listen_thread, NULL, listen_thread, ctrl)) != 0) {
		if (listen_socket == -1)
			close(ctrl->listen_socket);

		if (ctrl->closefd[0] >= 0)
			close(ctrl->closefd[0]);
		if (ctrl->closefd[1] >= 0)
			close(ctrl->closefd[1]);

		if (ctrl->stat_counters_prefix) {
			for (i = 0; i < ctrl->nhandlers; i++)
				stat_counter_dynamic_free(ctrl->handlers[i].cnt);
		}

		stat_counter_dynamic_free(ctrl->num_accept);

		pthread_mutex_destroy(&ctrl->queue_lock);
		pthread_mutex_destroy(&ctrl->event_lock);
		pthread_mutex_destroy(&ctrl->quit_mutex);
		pthread_mutex_destroy(&ctrl->job_lock);
		pthread_cond_destroy(&ctrl->job_cond);

		free(ctrl->handlers);
		free(ctrl);
		return NULL;
	}

	nthr = bconf_get_int(ctrl_conf, "nthreads");
	if (nthr < MIN_NTHREADS)
		nthr = MIN_NTHREADS;

	for (i = 0; i < nthr; i++) {
		struct worker *worker = calloc(1, sizeof(*worker));
		if (!worker) {
			log_printf(LOG_CRIT, "failed to allocate worke #%d", i);
			ctrl_quit(ctrl);
			return NULL;
		}

		worker->ctrl = ctrl;

		if (ctrl->stat_counters_prefix) {
			char buf[16];
			snprintf(buf, sizeof(buf), "%d", i);
			worker->thr_state = stat_message_dynamic_alloc(4, ctrl->stat_counters_prefix, "thread", buf, "thread_state");
			worker->handler_name = stat_message_dynamic_alloc(4, ctrl->stat_counters_prefix, "thread", buf, "current_handler");
			worker->handler_data_total = stat_counter_dynamic_alloc(5, ctrl->stat_counters_prefix, "thread", buf, "post_data", "total");
			worker->handler_data_current = stat_counter_dynamic_alloc(5, ctrl->stat_counters_prefix, "thread", buf, "post_data", "current");
		}

		if ((r = pthread_create(&worker->worker_thread, NULL, worker_thread, worker)) != 0) {
			log_printf(LOG_CRIT, "failed to create worker_thread #%d, %d, %s", i, r, strerror(r));
			if (ctrl->stat_counters_prefix) {
				stat_message_dynamic_free(worker->thr_state);
				stat_message_dynamic_free(worker->handler_name);
				stat_counter_dynamic_free(worker->handler_data_total);
				stat_counter_dynamic_free(worker->handler_data_current);
			}
			free(worker);
			ctrl_quit(ctrl);
			return NULL;
		}

		TAILQ_INSERT_TAIL(&ctrl->worker_threads, worker, worker_list);
	}

	return ctrl;
}
