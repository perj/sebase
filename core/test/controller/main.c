// Copyright 2018 Schibsted

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <err.h>
#include "sbp/logging.h"
#include <inttypes.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>

#include "sbp/bconf.h"
#include "sbp/bconfig.h"
#include "sbp/controller.h"
#include "sbp/platform_app.h"
#if __has_include("sbp/sha2.h")
#include "sbp/sha2.h"
#else
#include <sha2.h>
#endif
#include "sbp/semcompat.h"
#include "sbp/sock_util.h"
#include "sbp/stat_counters.h"
#include "sbp/stringmap.h"
#include "sbp/daemon.h"
#include "sbp/http.h"

static const char *portfile;

static void
hej_cb(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	ctrl_output_text(cr, "hej called\n");
}

static void
hej_hopp_cb(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	ctrl_output_text(cr, "hej/hopp called\n");
}

static int
echo_data(struct ctrl_req *cr, struct stringmap *qs, size_t tot_len, const char *data, size_t len, void *v) {
	ctrl_output_text(cr, "echo: (%lu) [%.*s]\n", tot_len, (int)len, data);
	return 0;
}

static void
echo_cb(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	ctrl_output_text(cr, "echo done\n");
}

static int
discard_data(struct ctrl_req *cr, struct stringmap *qs, size_t tot_len, const char *data, size_t len, void *v) {
	/* Ignore everything. */
	return 0;
}

static void
discard_cb(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	ctrl_output_text(cr, "discaring done\n");
}

struct sha256_cbdata {
	SHA2_CTX ctx;
};

STAT_COUNTER_DECLARE(stat_sha256_data, "counts", "sha256", "data");

static void
sha256_start(struct ctrl_req *cr, void *v) {
	struct sha256_cbdata *sd = calloc(1, sizeof(*sd));
	ctrl_set_handler_data(cr, sd);
	SHA256Init(&sd->ctx);
}

static int
sha256_data(struct ctrl_req *cr, struct stringmap *qs, size_t tot_len, const char *data, size_t len, void *v) {
	struct sha256_cbdata *sd = v;

	SHA256Update(&sd->ctx, (const unsigned char *)data, len);

	STATCNT_ADD(&stat_sha256_data, len);	

	return 0;
}

static void
sha256_cb(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	struct sha256_cbdata *sd = v;
	uint8_t digest[SHA256_DIGEST_LENGTH];
	char digest_string[sizeof(digest) * 2 + 1];
	unsigned int i;

	SHA256Final(digest, &sd->ctx);
	for (i = 0; i < sizeof(digest); i++) {
		snprintf(&digest_string[i * 2], 3, "%02x", digest[i]);
	}
	ctrl_output_text(cr, "%s (%llu bytes)\n", digest_string, (unsigned long long)ctrl_get_content_length(cr));
	if (sm_get(qs, "expect_hash", -1, 0)) {
		if (strcmp(sm_get(qs, "expect_hash", -1, 0), digest_string)) {
			ctrl_error(cr, 417, "hash mismatch");
		}
	}
	free(sd);
}

static void
some_json_start(struct ctrl_req *cr, void *v) {
	struct bconf_node **bc;

	bc = ctrl_get_bconfp(cr);
	bconf_add_data(bc, "hej.hopp.tratt", "1");
	bconf_add_data(bc, "hej.hopp.kaka", "2");
	bconf_add_data(bc, "hej.hatt.tratt", "3");
}

static void
some_json_finish(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	ctrl_output_json(cr, "hej");
}

struct state {
	semaphore_t stop_sem;
	struct ctrl *ctrl;
	bool stop;
	bool started;
	const char *port;
	struct https_state https;
};

static struct state state;

static void
stop_cmd(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	struct state *s = (struct state *)v;

	if (ctrl_quit_stage_one(s->ctrl, true)) {
		ctrl_error(cr, 503, "stop in progress");
		return;
	}
	semaphore_post(&s->stop_sem);
}

static void
custom_headers(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	ctrl_set_custom_headers(cr, "X-Custom", "some-value");
	ctrl_output_json(cr, "min elefanter dricker din vatten");
}

static void
acl_finish(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	ctrl_output_text(cr, "hej");
}

uint64_t keepalive_calls = 0;

static void
keepalive_finish(struct ctrl_req *cr, struct stringmap *qs, void *v) {
	uint64_t k = __sync_fetch_and_add(&keepalive_calls, 1);
	ctrl_output_text(cr, "keepalive-calls: %" PRIu64 "\n", k);
}

static struct bconf_node *root;

static void *
keepalive_thread(void *v) {
	struct state *s = (struct state *)v;

	struct http *hc = http_create(&s->https);
	if (!hc)
		errx(1, "http_create");

	char url[128];
	snprintf(url, sizeof(url), "http://localhost:%s/keepalive", s->port);
	hc->url = url;
	hc->method = "GET";
	curl_easy_setopt(hc->ch, CURLOPT_TIMEOUT_MS, 1000l);
#if LIBCURL_VERSION_MAJOR > 7 || (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 25)
	curl_easy_setopt(hc->ch, CURLOPT_TCP_KEEPALIVE, 1l);
#endif

	while (!s->stop) {
		/* 
 		 * At least one has to fail while starting or shutting down
 		 */
		http_perform(hc);
		s->started = true;
		nanosleep(&(const struct timespec){ .tv_nsec = 250 * 1000 * 1000 }, NULL);

	}

	http_free(hc);
	pthread_exit(NULL);
}

static void
dump_qs_cb(struct ctrl_req *cr, struct stringmap *qs, void *data) {
	ctrl_output_text(cr, "Vars:\n");
	for (const char **k = (const char *[]){"bar", "foo", "first", "param", "second", NULL}; *k; k++) {
		const char *v = sm_get(qs, *k, -1, 0);
		if (v)
			ctrl_output_text(cr, "%s -> %s\n", *k, v);
	}
}

static int
run(void) {
	setproctitle("initialize");

	struct ctrl_handler handlers[] = {
		CONTROLLER_DEFAULT_HANDLERS,
		{
			.url = "/hej", 
			.finish = hej_cb,
		},
		{
			.url = "/hej/hopp", 
			.finish = hej_hopp_cb,
		},
		{
			.url = "/hej/tjo/<param>", 
			.finish = dump_qs_cb,
			.cb_data = NULL,
		},
		{
			.url = "/hej/partial<param>",
			.finish = dump_qs_cb,
			.cb_data = NULL,
		},
		{
			.url = "/foo/<first>/<second>",
			.finish = dump_qs_cb,
			.cb_data = NULL,
		},
		{
			.url = "/middle/<param>/test",
			.finish = dump_qs_cb,
			.cb_data = NULL,
		},
		{
			.url = "/echo",
			.finish = echo_cb,
			.consume_post = echo_data
		},
		{
			.url = "/discard",
			.finish = discard_cb,
			.consume_post = discard_data
		},
		{
			.url = "/sha256",
			.start = sha256_start,
			.finish = sha256_cb,
			.consume_post = sha256_data
		},
		{
			.url = "/stop",
			.finish = stop_cmd,
			.cb_data = &state
		},
		{
			.url = "/dump_vars",
			.finish = dump_qs_cb,
			.cb_data = NULL,
		},
		{
			.url = "/some_json",
			.start = some_json_start,
			.finish = some_json_finish,
		},
		{
			.url = "/custom_headers",
			.finish = custom_headers,
		},
		{
			.url = "/keepalive",
			.finish = keepalive_finish,
		},
		{
			.url = "/no_substring_match",
			.finish = acl_finish,
		},
		{
			.url = "/substring_match/",
			.finish = acl_finish,
		},
	};

	if (semaphore_init(&state.stop_sem, false, 0) == -1)
		err(1, "semaphore_init");


	state.stop = false;
	state.started = false;
	state.port = bconf_get_string(root, "httpd_listen.port");

	http_setup_https(&state.https,
			bconf_get_string(root, "cacert.command"),
			bconf_get_string(root, "cacert.path"),
			bconf_get_string(root, "cert.command"),
			bconf_get_string(root, "cert.path"));

	if ((state.ctrl = ctrl_setup(bconf_get(root, "httpd_listen"), handlers, NHANDLERS(handlers), -1, &state.https)) == NULL)
		errx(1, "setup_command_thread");

	char lportbuf[NI_MAXSERV];
	int r = get_local_port(ctrl_get_listen_socket(state.ctrl), lportbuf, sizeof(lportbuf), NI_NUMERICSERV);
	if (r)
		errx(1, "get_local_port: %s", gai_strerror(r));
	state.port = lportbuf;

	FILE *f = fopen(portfile, "w");
	if (!f)
		err(1, "open %s", portfile);
	fprintf(f, "%s\n", lportbuf);
	fclose(f);

	pthread_t kthread;
	if ((r = pthread_create(&kthread, NULL, keepalive_thread, &state)) != 0) {
		errx(1, "Failed to create keepalive thread");
	}


	/* Poor man's syncronization */
	while (!state.started)
		nanosleep(&(const struct timespec){ .tv_nsec = 250 * 1000 * 1000 }, NULL);

	startup_ready("regress_controller");
	setproctitle("running");

	semaphore_wait(&state.stop_sem);
	ctrl_quit_stage_two(state.ctrl);

	state.stop = true;
	pthread_join(kthread, NULL);

	semaphore_destroy(&state.stop_sem);
	bconf_free(&root);
	log_shutdown();

	return 0;
}

struct papp app;

int
main(int argc, char **argv) {
	papp_init(&app, "controller", PAPP_DAEMON | PAPP_SMART_START | PAPP_PS_DISPLAY, argc, argv);
	struct bconf_node *conf = papp_config(&app, NULL);

	signal(SIGPIPE, SIG_IGN);

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <bconf> <portfile>\n", argv[0]);
		return 1;
	}

	portfile = argv[2];
	root = config_init(argv[1]);
	if (root == NULL)
		err(1, "config_init(%s)", argv[1]);

	set_quick_start(1);
	papp_start(&app, conf, false);
	bconf_free(&conf);

	return run();
}
