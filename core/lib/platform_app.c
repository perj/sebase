// Copyright 2018 Schibsted

#include "sbp/bconf.h"
#include "sbp/bconfig.h"
#include "sbp/daemon.h"
#include "sbp/error_functions.h"
#include "fd_pool.h"
#include "sbp/http.h"
#include "sbp/logging.h"
#include "sbp/memalloc_functions.h"
#include "platform_app.h"
#include "sbp/plog.h"
#include "sbp/goinit.h"
#include "sd_registry.h"
#include "sbp/vtree.h"

#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

static int
pappopt_cmp(void *a, void *b) {
	struct pappopt *pa = a;
	struct pappopt *pb = b;
	return strcmp(pa->keypath, pb->keypath);
}

void
papp_init(struct papp *app, const char *appl, int flags, int argc, char **argv) {
	app->appl = xstrdup(appl);
	app->flags = flags;
	app->orig_argc = argc;
	app->orig_argv = argv;

#ifdef LIBBSD_UNISTD_H
	if (flags & PAPP_PS_DISPLAY) {
		extern char **environ;
		setproctitle_init(argc, argv, environ);
	}
#endif

	app->pp = popt_init(argc, argv);

	popt_set_aux_cmp(app->pp, pappopt_cmp);

	struct pappopt *pa;
	if (flags & PAPP_DAEMON) {
		papp_add_option(app, "foreground", POPT_OBOOL, NULL, "Don't daemonize.");

		pa = papp_add_option(app, "quick-start", POPT_OBOOL, NULL, "Do not wait 5 seconds for an early child exit.");
		pa->type = PAPPOPT_SPECIAL;

		pa = papp_add_option(app, "uid", POPT_OSTR, NULL, "Set uid to run as.");
		pa->type = PAPPOPT_SPECIAL;

		pa = papp_add_option(app, "coresize", POPT_ONUM, NULL, "Set maximum core size (rlimit).");
		pa->type = PAPPOPT_SPECIAL;

		if (flags & PAPP_SMART_START) {
			papp_add_option(app, "no-smart-start", POPT_OBOOL, NULL, "Do not notify that the child is ready.");
			pa = papp_add_option(app, "smart-start-timeout", POPT_OMSEC, NULL, "Set the smart-start notification timeout.");
			pa->type = PAPPOPT_SPECIAL;
		}
	}
	if (!(flags & PAPP_NOBOS))
		papp_add_option(app, "nobos", POPT_OBOOL, NULL, "Disable BOS.");
	if (flags & PAPP_PIDFILE) {
		pa = papp_add_option(app, "pidfile", POPT_OSTR, NULL, "Set pidfile to use.");
		pa->type = PAPPOPT_SPECIAL;
	}
}

void
papp_set_appl(struct papp *app, const char *appl)
{
	free(app->appl);
	app->appl = xstrdup(appl);
}

void
papp_clean(struct papp *app) {
	log_shutdown();
	popt_free(app->pp);
	fd_pool_free(app->fd_pool);
	sd_registry_free(app->sdr);
	http_cleanup_https(&app->https);
	free(app->appl);
}

struct pappopt *
papp_add_option(struct papp *app, const char *name, enum popt_opttype otype, const char *dval, const char *desc) {
	assert(app->nextraopts < sizeof(app->extraopts) / sizeof(*app->extraopts));
	struct pappopt *pappopt = &app->extraopts[app->nextraopts++];
	pappopt->type = PAPPOPT_ONE;
	pappopt->keypath = name;
	popt_add_option(app->pp, name, otype | POPT_DAUX, dval, (intptr_t)pappopt, desc);
	return pappopt;
}


static void
papp_set_special(struct papp *app, const char *key, struct popt *p) {
	switch (*key) {
	case 'c':
		set_coresize(popt_parse_number(app->pp, p, false));
		break;
	case 'p':
		set_pidfile(p->value);
		break;
	case 'q':
		set_quick_start(popt_parse_bool(app->pp, p));
		break;
	case 's':
		set_startup_wait_timeout_ms(popt_parse_number(app->pp, p, false));
		break;
	case 'u':
		set_switchuid(p->value);
		break;
	}
}

static void
papp_set_pappopt(struct papp *app, struct bconf_node **root, struct popt *p) {
	struct pappopt *pa = p->dst.aux;
	char keypath[1024];

	switch (pa->type) {
	case PAPPOPT_ONE:
		bconf_add_data(root, pa->keypath, p->value);
		break;
	case PAPPOPT_MULTI:
		snprintf(keypath, sizeof(keypath), "%s.%d", pa->keypath, bconf_count(bconf_get(*root, pa->keypath)));
		bconf_add_data(root, keypath, p->value);
		break;
	case PAPPOPT_KEYVAL:
		{
			const char *col = strchr(p->value, '=');
			if (!col) {
				fprintf(stderr, "Option %s requires format key=value.\n", p->name);
				papp_usage(app, false);
			}
			snprintf(keypath, sizeof(keypath), "%s.%.*s", pa->keypath, (int)(col - p->value), p->value);
			if (keypath[0] == '.')
				bconf_add_data(root, keypath + 1, col + 1);
			else
				bconf_add_data(root, keypath, col + 1);
		}
		break;
	case PAPPOPT_SPECIAL:
		papp_set_special(app, pa->keypath, p);
		break;
	}
}

static void
papp_set_popt(struct papp *app, struct bconf_node **opts, struct popt *p) {
	switch (POPT_DSTTYPE(p->type)) {
	case POPT_DAUX:
		papp_set_pappopt(app, opts, p);
		break;
	case POPT_DINT:
		if (app->intopt_cb)
			app->intopt_cb(opts, p);
		break;
	default:
		popt_set_dptr(app->pp, p);
		break;
	}
}

struct bconf_node *
papp_parse_command_line(struct papp *app) {
	struct bconf_node *opts = NULL;

	struct popt *p;
	while ((p = popt_next_option(app->pp))) {
		papp_set_popt(app, &opts, p);
	}
	return opts;
}

static const char *
papp_find_config_file(struct papp *app, struct bconf_node *opts, struct bconf_node *defaults, char *cfbuf, size_t cfbuflen) {
	const char *config_file = bconf_get_string(opts, "config_file");
	if (config_file)
		return config_file;
	config_file = bconf_get_string(defaults, "config_file");
	if (config_file)
		return config_file;

	const char *bdir = getenv("BDIR");
	if (!bdir)
		bdir = "/opt/blocket";
	struct stat st;
	if (app->flags & (PAPP_WANT_BCONF|PAPP_NEED_BCONF)) {
		snprintf(cfbuf, cfbuflen, "%s/conf/bconf.conf", bdir);
		if (stat(cfbuf, &st) == 0)
			return cfbuf;
		if (app->flags & PAPP_NEED_BCONF) {
			fprintf(stderr, "Default config %s not found.\n", cfbuf);
			exit(1);
		}
	}
	snprintf(cfbuf, cfbuflen, "%s/conf/tls.conf", bdir);
	if (stat(cfbuf, &st) == 0)
		return cfbuf;
	return NULL;
}

struct bconf_node *
papp_config(struct papp *app, struct bconf_node *opts) {
	bool freeopts = (opts == NULL);
	if (!opts)
		opts = papp_parse_command_line(app);

	struct bconf_node *defaults = NULL;
	struct popt *p;
	while ((p = popt_next_default(app->pp))) {
		papp_set_popt(app, &defaults, p);
	}

	char cfbuf[1024];
	const char *config_file = papp_find_config_file(app, opts, defaults, cfbuf, sizeof(cfbuf));

	struct bconf_node *conf = NULL;
	struct bconf_node *bconf = defaults;

	if (config_file) {
		conf = config_init(config_file);
		if (!conf)
			xerr(1, "Failed to read config file %s", config_file);
	}
	if (app->flags & (PAPP_WANT_BCONF|PAPP_NEED_BCONF)) {
		if (bconf_get_string(opts, "bconf_file")) {
			if (load_bconf_file(app->appl, &bconf, bconf_get_string(opts, "bconf_file")) == -1) {
				xerrx(1, "Failed to load bconf from file (%s)", bconf_get_string(opts, "bconf_file"));
			}
		} else if (app->flags & PAPP_NEED_BCONF) {
			xerrx(1, "Bconf required but no bconf_file provided");
		}
	}

	/* Merge in order:
	 * Bconf overrides defaults (already done above).
	 * Config file overrides bconf.
	 * Command line overrides config file.
	 */
	bconf_merge(&bconf, conf);
	bconf_free(&conf);
	bconf_merge(&bconf, opts);
	if (freeopts)
		bconf_free(&opts);

	int r = http_setup_https(&app->https,
			bconf_get_string(bconf, "cacert.command"),
			bconf_get_string(bconf, "cacert.path"),
			bconf_get_string(bconf, "cert.command"),
			bconf_get_string(bconf, "cert.path"));
	if (r == -1)
		xerr(1, "Failed to setup https certificates: %m");

	const char *service = bconf_get_string(bconf, "sd.service");
	if (!service && app->sd.service_prefix) {
		char tmpservice[256];
		snprintf(tmpservice, sizeof(tmpservice), "%s/%s", app->sd.service_prefix, app->appl);
		bconf_add_data(&bconf, "sd.service", tmpservice);
	} else if (!service) {
		bconf_add_data(&bconf, "sd.service", app->appl);
	}
	if (app->sd.healthcheck_port_key) {
		const char *hcport = bconf_get_string(bconf, app->sd.healthcheck_port_key) ?: "8080";
		if (!bconf_get(bconf, "sd.value") && !bconf_get(bconf, "sd.dynval"))
			bconf_add_data(&bconf, "sd.value.*.*.port", hcport);
		if (!bconf_get(bconf, "sd.healthcheck.url") && app->sd.healthcheck_path) {
			char hcurl[256];
			snprintf(hcurl, sizeof(hcurl), "http://localhost:%s%s", hcport,
					app->sd.healthcheck_path);
			bconf_add_data(&bconf, "sd.healthcheck.url", hcurl);
		}
	}

	return bconf;
}

void
papp_usage(struct papp *app, bool verbose) {
	popt_usage(app->pp, verbose);
}

static void
papp_init_sdr(struct papp *app, struct bconf_node *conf) {
	app->sdr = sd_registry_create(bconf_get_string(conf, "blocket_id"), app->appl, &app->https);
	struct vtree_chain vtree = {0};
	sd_registry_add_sources(app->sdr, bconf_vtree(&vtree, conf));
	vtree_free(&vtree);
	app->fd_pool = fd_pool_new(app->sdr);
}

void
papp_start(struct papp *app, struct bconf_node *conf, bool will_fork) {
	bool nobos = (app->flags & PAPP_NOBOS) || bconf_get_int(conf, "nobos");
	bool foreground = !(app->flags & PAPP_DAEMON) || bconf_get_int(conf, "foreground");

	if (!nobos && (app->flags & PAPP_PS_DISPLAY))
		setproctitle("BOS");

	char ltbuf[256];
	const char *logtag = bconf_get_string_default(conf, app->logtag_key ?: "log_tag", app->appl);
	const char *loglevel = bconf_get_string_default(conf, app->loglevel_key ?: "log_level", "info");
	if (logtag == app->appl && app->logtag_prefix) {
		snprintf(ltbuf, sizeof(ltbuf), "%s+%s", app->logtag_prefix, logtag);
		logtag = ltbuf;
	}
	if (foreground) {
		x_err_init_err(logtag);
		log_setup_perror(logtag, loglevel);
	} else {
		plog_init_x_err(logtag);
		log_setup(logtag, loglevel);
	}

	if (!(app->flags & PAPP_NOBOS) && !(app->flags & PAPP_NO_SD_SETUP))
		sd_registry_setup_bos_client(conf, &app->https);

	bool exiting = false;
	int rc;
	if (foreground) {
		if (app->flags & PAPP_DAEMON) {
			write_pidfile();
			do_switchuid();
		}
		if (!nobos) {
			curl_global_cleanup();
			exiting = bos_here_until(&rc);
		}
	} else {
		if ((app->flags & PAPP_SMART_START) && !bconf_get_int(conf, "no-smart-start"))
			set_startup_wait();

		curl_global_cleanup();
		exiting = daemonify_here_until(nobos, &rc);
	}
	if (exiting) {
		papp_clean(app);
		exit(rc);
	}
	if (!nobos)
		http_clear_https_unlink(&app->https);

	curl_global_init(CURL_GLOBAL_DEFAULT);

	if (!will_fork) {
		init_go_runtime(app->orig_argc, app->orig_argv);
		papp_init_sdr(app, conf);
	}
}

pid_t
papp_fork(struct papp *app, struct bconf_node *conf) {
	/* Make sure shared plog connection is open. */
	logging_plog_ctx();
	curl_global_cleanup();

	pid_t p = fork();

	curl_global_init(CURL_GLOBAL_DEFAULT);

	if (p == 0) {
		init_go_runtime(app->orig_argc, app->orig_argv);
		papp_init_sdr(app, conf);
		http_clear_https_unlink(&app->https);
	}

	return p;
}
