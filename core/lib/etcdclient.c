// Copyright 2018 Schibsted

#include "etcdclient.h"

#include "sbp/bconf.h"
#include "sbp/buf_string.h"
#include "sbp/json_vtree.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sd_queue.h"
#include "sbp/string_functions.h"
#include "sbp/http.h"

#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

/* Arbitrarily chosen. */
#define MULTIWAIT_MS 2000
#define INITIAL_BACKOFF_US 500000
#define MAX_BACKOFF_US 8000000

enum ec_ev {
	ec_ev_new_listener
};

#define EC_EV_NEW_LISTENER (&(enum ec_ev){ec_ev_new_listener})
#define EC_EVSZ sizeof(enum ec_ev)

#define FOREACH_QUEUE(ec) \
	for (struct ec_tailq **queue = (struct ec_tailq*[]){&(ec)->new_listeners, &(ec)->listeners, NULL} ; *queue ; queue++)

struct etcdwatcher {
	char *prefix;
	size_t preflen;

	char *server_url;
	struct https_state *https;
	int flush_period_s;

	uint64_t wait_idx;

	int evfd[2];
	bool running;
	pthread_t thread;

	pthread_mutex_t lock;
	TAILQ_HEAD(ec_tailq, etcdwatcher_listen) listeners, new_listeners;
};

struct etcdwatcher_listen {
	TAILQ_ENTRY(etcdwatcher_listen) link;

	char *path;
	size_t pathlen;

	int *tpvec;
	int tpvlen;

	struct sd_queue queue;
	uint64_t sending;
	int send_state;
};

struct etcdwatcher *
etcdwatcher_create(const char *prefix, const char *server_url, struct https_state *https) {
	struct etcdwatcher *ec = zmalloc(sizeof(*ec));
	ec->prefix = xstrdup(prefix);
	ec->preflen = strlen(ec->prefix);

	ec->server_url = xstrdup(server_url);
	ec->https = https;
	pthread_mutex_init(&ec->lock, NULL);
	TAILQ_INIT(&ec->listeners);
	TAILQ_INIT(&ec->new_listeners);
	return ec;
}

void
etcdwatcher_set_flush_period(struct etcdwatcher *ec, int seconds) {
	ec->flush_period_s = seconds;
}

void
etcdwatcher_free(struct etcdwatcher *ec) {
	if (ec->running)
		etcdwatcher_stop(ec);

	free(ec->prefix);
	free(ec->server_url);
	FOREACH_QUEUE(ec) {
		while (!TAILQ_EMPTY(*queue)) {
			struct etcdwatcher_listen *l = TAILQ_FIRST(&ec->listeners);
			TAILQ_REMOVE(*queue, l, link);
			sd_queue_destroy(&l->queue);
			free(l);
		}
	}
	free(ec);
}

static size_t
curl_bswrite(char *ptr, size_t size, size_t nmemb, void *userdata) {
	return bswrite(userdata, ptr, size * nmemb);
}

static size_t
etcd_headers(void *ptr, size_t size, size_t nmemb, void *userdata) {
	uint64_t *wait_idx = userdata;

	size *= nmemb;

	if (!wait_idx || *wait_idx != 0)
		return size;
	if (size < sizeof("X-Etcd-Index: "))
		return size;
	if (memcmp(ptr, "X-Etcd-Index: ", sizeof("X-Etcd-Index: ") - 1) != 0)
		return size;
	/* While data might not be nul-terminated. We do know there's a newline in there, which will terminate strtoull. */
	const char *str = ptr;
	uint64_t wi = strtoull(str + sizeof("X-Etcd-Index: ") - 1, NULL, 0);
	*wait_idx = wi + 1;
	return size;
}

static void
etcd_enqueue_for_listener(struct etcdwatcher *ec, struct etcdwatcher_listen *l, const char *key, const char *extrakey, const char *value) {
	const char *keyv[16];
	ssize_t klenv[16];
	int keyc;

	if (l->sending != ec->wait_idx) {
		l->sending = ec->wait_idx;
		sd_queue_begin(&l->queue, &l->send_state);
	}

	for (keyc = 0 ; keyc < 16 ; keyc++) {
		while (*key == '/')
			key++;
		if (*key == '\0')
			break;
		int kidx = keyc;
		if (l->tpvec && kidx < l->tpvlen && l->tpvec[kidx] < 16)
			kidx = l->tpvec[kidx];
		keyv[kidx] = key;
		klenv[kidx] = strcspn(key, "/");
		key += klenv[kidx];
	}

	if (extrakey && keyc < 16) {
		int kidx = keyc;
		/* Don't transpose a message with only extrakey ("flush") */
		if (kidx > 0 && l->tpvec && kidx < l->tpvlen && l->tpvec[kidx] < 16)
			kidx = l->tpvec[kidx];
		keyv[kidx] = extrakey;
		klenv[kidx] = -1;
		keyc++;
	}

	if (!keyc)
		return;

	struct sd_value *v = sd_create_value(ec->wait_idx, keyc, keyv, klenv, value, -1);

	sd_queue_insert(&l->queue, v);
}

static void
etcd_enqueue_value(struct etcdwatcher *ec, const char *key, const char *extrakey, const char *value) {
	struct etcdwatcher_listen *l;
	TAILQ_FOREACH(l, &ec->listeners, link) {
		if (strncmp(key, l->path, l->pathlen) == 0) {
			if (l->pathlen == 0 || key[l->pathlen] == '/' || key[l->pathlen] == '\0') {
				etcd_enqueue_for_listener(ec, l, key + l->pathlen, extrakey, value);
			}
		}
	}
}

static void
etcd_update_index(struct etcdwatcher *ec, struct bconf_node *src) {
	uint64_t mi = strtoull(bconf_get_string(src, "modifiedIndex") ?: "0", NULL, 0);
	if (mi >= ec->wait_idx)
		ec->wait_idx = mi + 1;
}

static void
etcd_parse_node(struct etcdwatcher *ec, const char *prefix, size_t preflen, struct bconf_node *src) {
	const char *key = bconf_get_string(src, "key");
	bool inside = key && strncmp(key, prefix, preflen) == 0;

	etcd_update_index(ec, src);

	const char *dir = bconf_get_string(src, "dir");
	if (!dir || strcmp(dir, "true") != 0) {
		if (!inside)
			return;

		key += preflen;
		const char *value = bconf_get_string(src, "value");

		etcd_enqueue_value(ec, key, NULL, value);
	}

	if (inside || !key || strncmp(key, prefix, strlen(key)) == 0) {
		struct bconf_node *nodes = bconf_get(src, "nodes");
		for (int i = 0 ; i < bconf_count(nodes) ; i++)
			etcd_parse_node(ec, prefix, preflen, bconf_byindex(nodes, i));
	}
}

static void
etcd_parse_delete(struct etcdwatcher *ec, const char *prefix, size_t preflen, struct bconf_node *src) {
	const char *key = bconf_get_string(src, "key");
	bool inside = key && strncmp(key, prefix, preflen) == 0;

	etcd_update_index(ec, src);

	if (!inside)
		return;

	key += preflen;

	etcd_enqueue_value(ec, key, "delete", "");
}

static int
etcd_parse_response(struct etcdwatcher *ec, struct buf_string *body, bool flush) {
	struct bconf_node *result = NULL;
	int r = 0;

	if (json_bconf(&result, NULL, body->buf, body->pos, false)) {
		r = -1;
		goto out;
	}

	etcd_update_index(ec, result);
	struct etcdwatcher_listen *l;
	pthread_mutex_lock(&ec->lock);
	TAILQ_FOREACH(l, &ec->listeners, link) {
		l->sending = 0;
	}
	if (flush) {
		TAILQ_FOREACH(l, &ec->listeners, link) {
			etcd_enqueue_for_listener(ec, l, "", "flush", "");
		}
	}
	const char *action = bconf_get_string(result, "action");
	if (action && (strcmp(action, "expire") == 0 || strcmp(action, "delete") == 0)) {
		etcd_parse_delete(ec, ec->prefix, ec->preflen, bconf_get(result, "node"));
	} else {
		etcd_parse_node(ec, ec->prefix, ec->preflen, bconf_get(result, "node"));
	}
	TAILQ_FOREACH(l, &ec->listeners, link) {
		if (l->sending == ec->wait_idx) {
			sd_queue_commit(&l->queue, &l->send_state);
		}
	}
	pthread_mutex_unlock(&ec->lock);

out:
	bconf_free(&result);
	return r;
}

static bool
new_listener_event(struct etcdwatcher *ec, char *url, size_t urlsz) {
	bool ret = false;
	pthread_mutex_lock(&ec->lock);
	struct etcdwatcher_listen *l = TAILQ_FIRST(&ec->new_listeners);
	if (!l)
		goto out;

	TAILQ_REMOVE(&ec->new_listeners, l, link);
	TAILQ_INSERT_TAIL(&ec->listeners, l, link);
	snprintf(url, urlsz, "%s/v2/keys%s%s?recursive=true", ec->server_url, ec->prefix, l->path);
	ret = true;

out:
	pthread_mutex_unlock(&ec->lock);
	return ret;
}

static bool
check_for_event(struct etcdwatcher *ec, char *url, size_t urlsz) {
	/* Zero timeout poll for events. Could possibly use a flag instead. */
	struct pollfd pfd = { ec->evfd[0], POLLIN };
	if (poll(&pfd, 1, 0) != 1)
		return false;

	enum ec_ev ev;
	int r = read(ec->evfd[0], &ev, EC_EVSZ);
	if (r != EC_EVSZ)
		return false;

	switch (ev) {
	case ec_ev_new_listener:
		return new_listener_event(ec, url, urlsz);
	}
	return false;
}

static void *
etcd_thread(void *v) {
	struct etcdwatcher *ec = v;

	char url[1024];
	int waitoffs = snprintf(url, sizeof(url), "%s/v2/keys%s?recursive=true", ec->server_url, ec->prefix);
	log_printf(LOG_DEBUG, "etcdwatcher: initial url %s", url);

	CURLM *m = curl_multi_init();
	CURL *c = curl_easy_init();

	struct buf_string body = {0};
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_bswrite);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, etcd_headers);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, &ec->wait_idx);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1);
	http_set_curl_https(c, ec->https);
	CURLMcode mcode UNUSED = curl_multi_add_handle(m, c);
	useconds_t backoff = INITIAL_BACKOFF_US;

	bool flush = false;
	struct timespec next_flush = {0,0};

	if (ec->flush_period_s) {
		clock_gettime(CLOCK_MONOTONIC, &next_flush);
		next_flush.tv_sec += ec->flush_period_s;
	}

	/* Initial fetch is an event. */
	bool processing_event = true;
	uint64_t preproc_wait_idx = 0;

	while (1) {
		while (ec->running) {

			if (!processing_event && !flush && next_flush.tv_sec > 0) {
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				if (now.tv_sec > next_flush.tv_sec) {
					/* Reset url to do a full fetch (same as the initial request). */
					waitoffs = snprintf(url, sizeof(url), "%s/v2/keys%s?recursive=true", ec->server_url, ec->prefix);
					curl_easy_setopt(c, CURLOPT_URL, url);
					log_printf(LOG_DEBUG, "etcdwatcher: flush, url %s", url);

					/* We might interrupt a transfer in progress here, reset body. */
					free(body.buf);
					memset(&body, 0, sizeof(body));

					curl_multi_remove_handle(m, c);
					curl_multi_add_handle(m, c);

					flush = true;
					next_flush.tv_sec = now.tv_sec + ec->flush_period_s;

				}
			}

			if (!processing_event && !flush) {
				processing_event = check_for_event(ec, url, sizeof(url));
				if (processing_event) {
					curl_easy_setopt(c, CURLOPT_URL, url);

					/* We might interrupt a transfer in progress here, reset body. */
					free(body.buf);
					memset(&body, 0, sizeof(body));
					curl_multi_remove_handle(m, c);
					curl_multi_add_handle(m, c);

					preproc_wait_idx = ec->wait_idx;
					log_printf(LOG_DEBUG, "etcdwatcher: processing event, url %s", url);
				}
			}

			int running;
			do {
				mcode = curl_multi_perform(m, &running);
			} while (mcode == CURLM_CALL_MULTI_PERFORM);
			if (mcode != CURLM_OK) {
				log_printf(LOG_CRIT, "curl_multi_wait failed: %s", curl_multi_strerror(mcode));
				break;
			}
			if (!running)
				break;
#if LIBCURL_VERSION_MAJOR > 7 || (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 28)
			struct curl_waitfd wfd = { .fd = ec->evfd[0], .events = CURL_WAIT_POLLIN };
			curl_multi_wait(m, &wfd, 1, MULTIWAIT_MS, NULL);
#else
			long tms = -1;
			mcode = curl_multi_timeout(m, &tms);
			if (tms < 0 || tms > MULTIWAIT_MS)
				tms = MULTIWAIT_MS;
			if (tms > 0) {
				fd_set fdread;
				fd_set fdwrite;
				fd_set fdexcep;
				FD_ZERO(&fdread);
				FD_ZERO(&fdwrite);
				FD_ZERO(&fdexcep);
				FD_SET(ec->evfd[0], &fdread);
				int maxfd = -1;
				mcode = curl_multi_fdset(m, &fdread, &fdwrite, &fdexcep, &maxfd);
				if (mcode != CURLM_OK) {
					log_printf(LOG_CRIT, "curl_multi_fdset failed: %s", curl_multi_strerror(mcode));
					break;
				}

				struct timeval timeout;
				timeout.tv_sec = tms / 1000;
				timeout.tv_usec = (tms % 1000) * 1000;
				int rc = select(maxfd+1, &fdread, &fdwrite, &fdexcep, &timeout);
				if (rc < 0) {
					log_printf(LOG_CRIT, "select failed: %s", xstrerror(errno));
					break;
				}
			}
#endif
		}
		if (!ec->running)
			break;

		int msgs;
		CURLMsg *msg;
		while ((msg = curl_multi_info_read(m, &msgs))) {
			assert(msg->easy_handle == c);

			int r;
			if (msg->data.result == CURLE_OK) {
				r = etcd_parse_response(ec, &body, flush);
				if (r == 0) {
					if (processing_event) {
						processing_event = false;
						if (preproc_wait_idx)
							ec->wait_idx = preproc_wait_idx;
						waitoffs = snprintf(url, sizeof(url), "%s/v2/keys%s?recursive=true", ec->server_url, ec->prefix);
					}
					snprintf(url + waitoffs, sizeof(url) - waitoffs, "&wait=true&waitIndex=%" PRIu64, ec->wait_idx);
					//log_printf(LOG_DEBUG, "etcdwatcher: using url %s", url);
					curl_easy_setopt(c, CURLOPT_URL, url);
					flush = false;
				} else {
					long code;
					curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
					log_printf(LOG_WARNING, "etcdwatcher: Not a json reply. Status code: %ld", code);
				}
			} else {
				log_printf(LOG_ERR, "etcdwatcher: Failed to fetch %s: %s", url, curl_easy_strerror(msg->data.result));
				r = -1;
			}

			if (r != 0) {
				log_printf(LOG_WARNING, "Bad reply from etcd, sleeping for %.1f seconds", backoff / 1000000.);
				usleep(backoff);
				backoff *= 2;
				if (backoff > MAX_BACKOFF_US)
					backoff = MAX_BACKOFF_US;
			} else {
				backoff = INITIAL_BACKOFF_US;
			}

			curl_multi_remove_handle(m, c);
			curl_multi_add_handle(m, c);

			free(body.buf);
			memset(&body, 0, sizeof(body));
		}

	}

	close(ec->evfd[0]);
	free(body.buf);

	curl_easy_cleanup(c);
	curl_multi_cleanup(m);
	return NULL;
}

int
etcdwatcher_start(struct etcdwatcher *ec) {
	if (ec->running)
		return 0;

	ec->running = true;
	if (pipe(ec->evfd)) {
		ec->running = false;
		return -1;
	}
	if (pthread_create(&ec->thread, NULL, etcd_thread, ec)) {
		ec->running = false;
		return -1;
	}

	return 0;
}

int
etcdwatcher_stop(struct etcdwatcher *ec) {
	if (!ec->running)
		return 0;

	ec->running = false;
	close(ec->evfd[1]);
	ec->evfd[1] = -1;
	pthread_join(ec->thread, NULL);
	return 0;
}

struct sd_queue *
etcdwatcher_add_listen(struct etcdwatcher *ec, const char *path, const int *tpvec, int tpvlen) {
	struct etcdwatcher_listen *l = zmalloc(sizeof(*l) + tpvlen * sizeof(*tpvec) + strlen(path) + 1);
	l->tpvlen = tpvlen;
	if (tpvlen) {
		l->tpvec = (int *)(l + 1);
		memcpy(l->tpvec, tpvec, tpvlen * sizeof(*tpvec));
		l->path = (char*)(l->tpvec + tpvlen);
	} else {
		l->path = (char*)(l + 1);
	}
	l->pathlen = strlen(path);
	memcpy(l->path, path, l->pathlen + 1);
	sd_queue_init(&l->queue);

	if (ec->running) {
		pthread_mutex_lock(&ec->lock);
		TAILQ_INSERT_TAIL(&ec->new_listeners, l, link);
		pthread_mutex_unlock(&ec->lock);
		int n;
		do {
			n = write(ec->evfd[1], EC_EV_NEW_LISTENER, EC_EVSZ);
		} while (n == -1 && errno == EINTR);
		if (n < (int)EC_EVSZ) {
			/* Not sure we can recover well, at least crit log should tell someone. */
			log_printf(LOG_CRIT, "Failed to send event to etcdwatcher thread: %m");
		}
	} else {
		TAILQ_INSERT_TAIL(&ec->listeners, l, link);
	}

	return &l->queue;
}

void
etcdwatcher_remove_listen(struct etcdwatcher *ec, struct sd_queue *sdq) {
	pthread_mutex_lock(&ec->lock);
	struct etcdwatcher_listen *l;
	FOREACH_QUEUE(ec) {
		TAILQ_FOREACH(l, *queue, link) {
			if (sdq == &l->queue) {
				TAILQ_REMOVE(*queue, l, link);
				sd_queue_destroy(&l->queue);
				free(l);
				goto out;
			}
		}
	}
out:
	pthread_mutex_unlock(&ec->lock);
}
