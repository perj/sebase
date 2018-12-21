// Copyright 2018 Schibsted

#include "sbp/bconf.h"
#include "etcdclient.h"
#include "sbp/http.h"
#include "sbp/json_vtree.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "sd_registry.h"
#include "sbp/vtree.h"
#include "sbp/url.h"

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct sd_etcd_bos
{
	char *service;
	char *base_url;
	int ttl_s;
	struct bconf_node *sdconf;
	struct sd_registry_hostkey *hk;
	struct http *hc;
	struct https_state *https;
	struct https_state https_store;

	char *conf_value;
	int conf_value_len;
	enum sd_etcd_health { HEALTH_UNKNOWN, HEALTH_DOWN, HEALTH_UP } health;
};

static void *
sd_etcd_bos_setup(const char *service, struct bconf_node *conf, struct bconf_node **sdconf, struct sd_registry_hostkey **hk, struct https_state *https) {
	const char *base_url = bconf_get_string(conf, "sd.etcd_url");
	if (!base_url || !*base_url)
		return NULL;

	struct sd_etcd_bos *sd = zmalloc(sizeof(*sd));

	sd->service = xstrdup(service);
	sd->base_url = xstrdup(base_url);
	sd->ttl_s = bconf_get_int_default(conf, "sd.ttl_s", 30);
	sd->sdconf = *sdconf;
	*sdconf = NULL;
	sd->hk = *hk;
	*hk = NULL;
	if (https)
		sd->https = https;
	else
		sd->https = &sd->https_store;
	http_setup_https(sd->https,
			bconf_get_string(conf, "cacert.command"),
			bconf_get_string(conf, "cacert.path"),
			bconf_get_string(conf, "cert.command"),
			bconf_get_string(conf, "cert.path"));

	return sd;
}

static void
sd_etcd_flush_handle(struct sd_etcd_bos *sd) {
	http_free(sd->hc);
	sd->hc = NULL;
}

static void
sd_etcd_register(struct sd_etcd_bos *sd, int http_code) {
	const char *hostkey = sd_registry_hostkey(sd->hk);
	if (!hostkey) {
		log_printf(LOG_NOTICE, "sd_etcd: hostkey not yet available");
		return;
	}

	if (!sd->hc) {
		sd->hc = http_create(sd->https);
		curl_easy_setopt(sd->hc->ch, CURLOPT_TIMEOUT_MS, 1000l);
	}
	sd->hc->method = "PUT";

	char url[1024];
	int offs = snprintf(url, sizeof(url), "%s/v2/keys/service/%s/%s", sd->base_url, sd->service, hostkey);
	sd->hc->url = url;

	char body[1024];
	sd->hc->body_length = snprintf(body, sizeof(body), "dir=true&ttl=%d&prevExist=true&refresh=true", sd->ttl_s);
	sd->hc->body = body;

	bool newdir = false;
	int r = http_perform(sd->hc);
	log_printf(LOG_DEBUG, "sd_etcd: Refreshing %s = %d", url, r);
	if (r == 404) {
		/* Not found, so create it. */
		sd->hc->body_length = snprintf(body, sizeof(body), "dir=true&ttl=%d", sd->ttl_s);
		r = http_perform(sd->hc);
		log_printf(LOG_DEBUG, "sd_etcd: Creating %s = %d", url, r);
		newdir = true;
	}

	if (r != 200 && r != 201) {
		if (r == -1)
			log_printf(LOG_ERR, "sd_etcd: Failed to create %s: %s", url, sd->hc->error);
		else
			log_printf(LOG_ERR, "sd_etcd: Failed to create %s: http code %d", url, r);
		return;
	}

	if (!sd->conf_value) {
		bool lhost = false;
		newdir = true; /* Extra safety since it's our first run. */
#if LIBCURL_VERSION_MAJOR > 7 || (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 21)
		if (!bconf_get(sd->sdconf, "*.*.name")) {
			char *local_ip = NULL;
			CURLcode cc = curl_easy_getinfo(sd->hc->ch, CURLINFO_LOCAL_IP, &local_ip);
			if (local_ip && *local_ip) {
				if (strcmp(local_ip, "::1") != 0 && strcmp(local_ip, "127.0.0.1") != 0)
					bconf_add_data(&sd->sdconf, "*.*.name", local_ip);
				else
					lhost = true;
			} else {
				log_printf(LOG_ERR, "sd_etcd: failed to determine local ip: %s", curl_easy_strerror(cc));
			}
		}
#endif
		if (!bconf_get(sd->sdconf, "*.*.name")) {
			char hostname[256];
			if (gethostname(hostname, sizeof(hostname)) == 0) {
				/* Try to get an ip address. This hostname might only work locally. */
				struct addrinfo *res, *curr;
				if (getaddrinfo(hostname, NULL, NULL, &res) == 0) {
					for (curr = res ; curr ; curr = curr->ai_next) {
						char host[NI_MAXHOST];
						if (getnameinfo(curr->ai_addr, curr->ai_addrlen,
								host, sizeof(host), NULL, 0,
								NI_NUMERICHOST) == 0) {
							bconf_add_data(&sd->sdconf, "*.*.name", host);
							break;
						}
					}
					freeaddrinfo(res);
				}

				if (!bconf_get(sd->sdconf, "*.*.name"))
					bconf_add_data(&sd->sdconf, "*.*.name", hostname);
			}
		}
		if (!bconf_get(sd->sdconf, "*.*.name") && lhost) {
			/* In worst case if we had localhost above, use it. */
			bconf_add_data(&sd->sdconf, "*.*.name", "localhost");
		}
		struct vtree_chain vtree;
		struct buf_string buf = {0};
		vtree_json(bconf_vtree(&vtree, sd->sdconf), 0, 0, vtree_json_bscat, &buf);
		vtree_free(&vtree);
		sd->conf_value = buf.buf;
		sd->conf_value_len = buf.pos;
	}

	snprintf(url + offs, sizeof(url) - offs, "/config");
	struct buf_string value = {0};
	bscat(&value, "value=");
	url_encode_postdata(&value, sd->conf_value, sd->conf_value_len);
	if (!newdir) {
		/* If not a new directory, we assume the existing config is ok, if it exists.
		 * This prevents a PUT event from being sent for each request setting an equal value.
		 */
		bscat(&value, "&prevExist=false");
	}

	sd->hc->body = value.buf;
	sd->hc->body_length = value.pos;

	r = http_perform(sd->hc);
	log_printf(LOG_DEBUG, "sd_etcd: Creating %s = %d", url, r);
	if (r != 200 && r != 201 && r != 412) {
		/* r == 412 if prevExist check failed. */
		if (r == -1)
			log_printf(LOG_ERR, "sd_etcd: Failed to put %s: %s", url, sd->hc->error);
		else
			log_printf(LOG_ERR, "sd_etcd: Failed to put %s: http code %d", url, r);
	}

	enum sd_etcd_health health = http_code >= 200 && http_code <= 299 ? HEALTH_UP : HEALTH_DOWN;

	snprintf(url + offs, sizeof(url) - offs, "/health");

	bool newval = newdir || health != sd->health;
	if (!newval) {
		/* For health, the value changes. Theres no way we can'n be sure there hasn't been a rollback,
		 * except by fetching the current value. So do that and only write if there was a change.
		 * XXX Fix this by using v3 API transactions, when they become generally available.
		 */
		enum sd_etcd_health etcdhealth = HEALTH_UNKNOWN;

		sd->hc->method = "GET";
		sd->hc->body = NULL;
		struct buf_string buf = {0};
		sd->hc->response_body = &buf;
		r = http_perform(sd->hc);
		if (r == 200) {
			struct bconf_node *root = NULL;
			if (!json_bconf(&root, NULL, buf.buf, buf.pos, false)) {
				const char *v = bconf_get_string(root, "node.value");
				if (v && strcmp(v, "up") == 0)
					etcdhealth = HEALTH_UP;
				else if (v && strcmp(v, "down") == 0)
					etcdhealth = HEALTH_DOWN;
			}
			bconf_free(&root);
		}
		free(buf.buf);
		sd->hc->response_body = NULL;
		sd->hc->method = "PUT";

		newval = health != etcdhealth;
	}

	if (newval) {
		value.pos = 0;
		bscat(&value, "value=%s", health == HEALTH_UP ? "up" : "down");

		sd->hc->body = value.buf;
		sd->hc->body_length = value.pos;

		r = http_perform(sd->hc);
		log_printf(LOG_DEBUG, "sd_etcd: Creating %s = %d", url, r);
		if (r != 200 && r != 201) {
			if (r == -1)
				log_printf(LOG_ERR, "sd_etcd: Failed to put %s: %s", url, sd->hc->error);
			else
				log_printf(LOG_ERR, "sd_etcd: Failed to put %s: http code %d", url, r);
		} else {
			sd->health = health;
		}
	}
	free(value.buf);
}

static void
sd_etcd_free(struct sd_etcd_bos *sd) {
	free(sd->service);
	free(sd->base_url);
	bconf_free(&sd->sdconf);
	sd_registry_hostkey_free(sd->hk);
	http_free(sd->hc);
	http_cleanup_https(&sd->https_store);
	free(sd->conf_value);
	free(sd);
}

static void
sd_etcd_deregister(struct sd_etcd_bos *sd) {
	const char *hostkey = sd_registry_hostkey(sd->hk);
	if (!hostkey) {
		log_printf(LOG_DEBUG, "sd_etcd: hostkey not yet available");
		return;
	}

	if (!sd->hc) {
		sd->hc = http_create(sd->https);
		curl_easy_setopt(sd->hc->ch, CURLOPT_TIMEOUT_MS, 1000l);
	}
	sd->hc->method = "DELETE";

	char url[1024];
	snprintf(url, sizeof(url), "%s/v2/keys/service/%s/%s?recursive=true", sd->base_url, sd->service, hostkey);
	sd->hc->url = url;
	sd->hc->body = NULL;

	int r = http_perform(sd->hc);
	if (r != 200 && r != 201) {
		if (r == -1)
			log_printf(LOG_ERR, "sd_etcd: Failed to delete %s: %s", url, sd->hc->error);
		else
			log_printf(LOG_ERR, "sd_etcd: Failed to delete %s: http code %d", url, r);
	}

	sd_etcd_free(sd);
}

static void
sd_etcd_bos_event(enum bos_event bev, int arg, void *v) {
	switch (bev) {
	case bev_prefork:
		sd_etcd_flush_handle(v);
		break;
	case bev_postfork_child:
		sd_etcd_free(v);
		break;
	case bev_start:
	case bev_exit_bad:
	case bev_crash:
		sd_etcd_register(v, -1);
		break;
	case bev_healthcheck:
		sd_etcd_register(v, arg);
		break;
	case bev_exit_ok:
	case bev_quick_exit:
		sd_etcd_deregister(v);
		break;
	}
}

SD_REGISTRY_ADD_BOS_CLIENT(etcd, "etcd_url", sd_etcd_bos_setup, sd_etcd_bos_event);

static int
sd_etcd_setup(void **srcdata, struct vtree_chain *node, struct https_state *https) {
	const char *url = vtree_get(node, "sd", "etcd_url", NULL);
	struct etcdwatcher *ec = etcdwatcher_create("/service/", url, https);
	if (!ec) {
		log_printf(LOG_CRIT, "sd_etcd(%s): failed to create watcher: %m", url);
		return -1;
	}

	int flush_s = vtree_getint(node, "sd", "etcd", "reload_s", NULL);
	if (!flush_s && !vtree_haskey(node, "sd", "etcd", "reload_s", NULL))
		flush_s = 600;
	if (flush_s)
		etcdwatcher_set_flush_period(ec, flush_s);

	if (etcdwatcher_start(ec) == -1) {
		log_printf(LOG_CRIT, "sd_etcd(%s): Failed to start etcdwatcher", url);
		etcdwatcher_free(ec);
		return -1;
	}

	*srcdata = ec;
	return 0;
}

static void
sd_etcd_cleanup(void *srcdata) {
	etcdwatcher_free(srcdata);
}

static void *
sd_etcd_connect(void *srcdata, const char *service, struct vtree_chain *node, struct sd_queue **queue) {
	struct etcdwatcher *ec = srcdata;

	*queue = etcdwatcher_add_listen(ec, service, (const int[]){1, 0}, 2);
	if (!*queue) {
		log_printf(LOG_CRIT, "sd_etcd(%s): Failed to add listener", service);
		return NULL;
	}

	return *queue;
}

static void
sd_etcd_disconnect(void *srcdata, void *v) {
	etcdwatcher_remove_listen(srcdata, v);
}

SD_REGISTRY_ADD_SOURCE(etcd, "etcd_url", sd_etcd_setup, sd_etcd_cleanup, sd_etcd_connect, sd_etcd_disconnect);
