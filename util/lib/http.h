// Copyright 2018 Schibsted

#ifndef HTTP_H
#define HTTP_H
#include <curl/curl.h>
#include "buf_string.h"

typedef size_t (*http_response_cb)(void *buffer, size_t size, size_t nmemb, void *cb_data);
typedef int (*http_extra_config_cb)(CURL *ch, void *data);

struct http {
	const char *url;
	const char *method;
	const void *body;
	size_t body_length;
	const char **headers;
	http_response_cb write_function;
	http_response_cb header_write_function;

	char error[CURL_ERROR_SIZE];
	CURL *ch;
	CURLcode curl_status;

	int http_status;
	struct buf_string *response_body;
	struct buf_string *response_header;
};

/* Using an https state is optional, even if fetching via HTTPS.
 * It's only needed if you want to verify the peer using a custom CA certificate,
 * or if you want to send a certificate to authenticate with.
 */
struct https_state {
	int state;
	char cafile[PATH_MAX];
	char certfile[PATH_MAX];
};

long http_delete(const char *url, const char *headers[], const struct https_state *https);
long http_get(const char *url, struct buf_string *response, const char *headers[], const struct https_state *https);
long http_post(const char *url, struct buf_string *response, const char *poststr, const char *headers[], const struct https_state *https);
long http_put_str(const char *url, const char *data, const char *headers[], const struct https_state *https);
long http_put_bin(const char *url, void *data, size_t len, const char *headers[], const struct https_state *https);
long http_move(const char *url, const char *dest, const char *headers[], const struct https_state *https);
long http_copy(const char *url, const char *dest, const char *headers[], const struct https_state *https);

struct http *http_create(const struct https_state *https);
long http_perform(struct http *);
void http_free(struct http *);

int http_setup_https(struct https_state *https, const char *cacmd, const char *cafile, const char *certcmd, const char *certfile);
void http_cleanup_https(struct https_state *https);
void http_clear_https_unlink(struct https_state *https);

/* Pass as certfile above to use the certificate from cacmd/cafile as cert. */
#define HTTP_USE_CACERT ((const char*)-1)

/* Used internally, but also exposed in case you're using curl directly.
 * Will set options on ch to use the certs in the https state.
 */
void http_set_curl_https(CURL *ch, const struct https_state *https);

/* Returns "https" if any of CA or client cert is set in the state, otherwise "http". */
const char *http_default_scheme(const struct https_state *https);

#endif
