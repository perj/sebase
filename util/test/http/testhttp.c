// Copyright 2018 Schibsted

#include <stdio.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sbp/logging.h"
#include "sbp/bconf.h"
#include "sbp/http.h"
#include "sbp/memalloc_functions.h"
#include "sbp/controller.h"

struct https_state https = {0};

static void
test_get_discard(const char *url) {
	long res;

	printf("Fetch and discard data... ");
	res = http_get(url, NULL, NULL, &https);
	assert(res == 200);
	printf("OK\n");
}

static void
test_get_keep(const char *url) {
	long res;
	struct buf_string bs;

	printf("Fetch and keep data... ");
	res = http_get(url, &bs, NULL, &https);
	assert(res == 200);
	assert(bs.len != 0);
	printf("OK\n");
	printf("Response:\n%s\n", bs.buf);
	free(bs.buf);
}

static void
test_post(const char *url) {
	long res;
	struct buf_string bs;

	printf("POST three parameters... ");
	res = http_post(url, &bs, "alpha=foo&beta=bar&gamma=baz", NULL, &https);
	assert(res == 200);
	printf("OK\n");
	printf("Response:\n%s\n", bs.buf);
	free(bs.buf);
}

static void
test_post_silly_headers(const char *url) {
	long res;
	struct buf_string bs;

	/* Abuse the fact that mod_templates only parses post vars if content
	   type is application/x-www-form-urlencoded */
	printf("POST three parameters and custom silly headers... ");
	res = http_post(url, &bs, "alpha=foo&beta=bar&gamma=baz", (const char *[]){"Content-Type: potatissallad", "X-Foo: Bar", NULL}, &https);
	assert(res == 200);
	printf("OK\n");
	printf("Response:\n%s\n", bs.buf);
	free(bs.buf);
}

static void
test_post_custom_headers(const char *url) {
	long res;
	struct buf_string bs;

	printf("POST three parameters and custom headers... ");
	res = http_post(url, &bs, "alpha=foo&beta=bar&gamma=baz", (const char *[]){"Content-Type: application/x-www-form-urlencoded", NULL}, &https);
	assert(res == 200);
	printf("OK\n");
	printf("Response:\n%s\n", bs.buf);
	free(bs.buf);
}

static void
test_delete(const char *url) {
	long res;

	printf("DELETE... ");
	res = http_delete(url, NULL, &https);
	assert(res == 204);
	printf("OK\n");
}

static void
test_put_str(const char *url) {
	long res;
	struct buf_string bs;

	printf("PUT string foobar... ");
	res = http_put_str(url, "foobar", NULL, &https);
	assert(res == 201);
	printf("OK\n");

	printf("Reading back file\n");
	http_get(url, &bs, NULL, &https);
	printf("Response:\n%s\n", bs.buf);
	free(bs.buf);
}

static void
test_put_blob(const char *url) {
	long res;
	char blob[1024];
	FILE *f;

	printf("PUT binary blob...");

	if ((f = fopen("binary_blob", "r")) == NULL) {
		printf("Failed to open binary_blob\n");
		exit(EXIT_FAILURE);
	}
	
	if (fread(blob, sizeof(blob), 1, f) == 0) {
		printf("Could not read all data from file\n");
		exit(EXIT_FAILURE);
	}

	res = http_put_bin(url, blob, 1024, NULL, &https);
	assert(res == 201);
	printf("OK\n");
}

static void
test_move(const char *url) {
	long res;
	char *dest;

	xasprintf(&dest, "%s_moved", url);

	printf("MOVE %s to %s... ", url, dest);

	res = http_put_str(url, "testing move", NULL, &https);
	assert(res == 201);
	
	res = http_move(url, dest, NULL, &https);
	assert(res == 201);

	res = http_delete(dest, NULL, &https);
	assert(res == 204);

	printf("OK\n");

	free(dest);
}

static void
test_copy(const char *url) {
	long res;
	char *dest;

	xasprintf(&dest, "%s_moved", url);

	printf("COPY %s to %s... ", url, dest);

	res = http_put_str(url, "testing copy", NULL, &https);
	assert(res == 201);
	
	res = http_copy(url, dest, NULL, &https);
	assert(res == 201);

	res = http_delete(url, NULL, &https);
	assert(res == 204);
	res = http_delete(dest, NULL, &https);
	assert(res == 204);

	printf("OK\n");

	free(dest);
}

static void
test_read_headers_discard_body(const char *url) {
	long rc;
	struct http *h = http_create(&https);
	struct buf_string bs;

	printf("Read headers and discard body from %s... ", url);

	memset(&bs, 0, sizeof bs);
	h->response_header = &bs;

	h->method = "GET";
	h->url = url;
	
	rc = http_perform(h);
	assert(rc == 200);

	printf("OK\n");
	printf("Response:\n%s\n", bs.buf);

	http_free(h);
	free(bs.buf);
}

static struct test_case{
	const char *name;
	void (*func)(const char *);
} tests[] = {
	{ "get_discard",		test_get_discard },
	{ "get_keep",			test_get_keep },
	{ "post",			test_post },
	{ "post_silly_headers",		test_post_silly_headers },
	{ "post_custom_headers",	test_post_custom_headers },
	{ "delete",			test_delete },
	{ "put_string",			test_put_str },
	{ "put_binary_blob",		test_put_blob },
	{ "move",			test_move },
	{ "copy",			test_copy },
	{ "get_headers_discard_body",	test_read_headers_discard_body },
	{ NULL }
};

int
main(int argc, const char *argv[]) {
	const char *url;
	const char *test; 
	struct test_case *tc;
	struct ctrl_handler handlers[] = {
		CONTROLLER_DEFAULT_HANDLERS,
	};

	log_setup_perror("testhttp", "debug");

	if (argc != 3) {
		printf("Usage: %s test_case url\n", argv[0]);
		printf("List of available test cases:\n");
		for (tc = tests; tc && tc->name; tc++)
			printf("\t%s\n", tc->name);
		exit(EXIT_FAILURE);
	}

	test = argv[1];
	url = argv[2];

	if (strncmp(url, "https:", strlen("https:")) == 0) {
		http_setup_https(&https, "./gencert.sh", NULL, NULL, NULL);
		strcpy(https.certfile, https.cafile);
		https.state |= 0x20; /* HAVE_CERT in http.c */

		/* Do the https ourselves so we can use the same cert. */
		struct bconf_node *ctrlconf = NULL;
		bconf_add_data(&ctrlconf, "port", getenv("REGRESS_HTTPS_PORT"));
		ctrl_setup(ctrlconf, handlers, NHANDLERS(handlers), -1, &https);
	}

	curl_global_init(CURL_GLOBAL_ALL);

	for (tc = tests; tc && tc->name; tc++) {
		if (strcmp(tc->name, test) == 0) {
			tc->func(url);
			break;
		}
	}

	http_cleanup_https(&https);
	
	curl_global_cleanup();

	if (!tc) {
		printf("No such testcase: %s\n", test);
		exit(EXIT_FAILURE);	
	}

	return 0;
}
