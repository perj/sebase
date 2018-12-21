// Copyright 2018 Schibsted

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "file_util.h"
#include "http.h"
#include "memalloc_functions.h"

static const char **
merge_headers(const char *a[], const char *b[]) {
	int a_len = 0;
	int b_len = 0;
	const char **headers;

	while (a && a[a_len++]);
	while (b && b[b_len++]);

	headers = xmalloc(sizeof(*headers) * (a_len + b_len + 1));

	for (int i = 0; i < a_len; i++)
		headers[i] = a[i];
	for (int i = 0; i < b_len; i++)
		headers[i + a_len] = b[i];

	headers[a_len + b_len] = NULL;

	return headers;
}

static size_t
null_write_data(void *buffer, size_t size, size_t nmemb, void *cb_data) {
	return size * nmemb;
}

static size_t
default_write_data(void *buffer, size_t size, size_t nmemb, void *cb_data) {
	struct buf_string *bs = (struct buf_string *) cb_data;
	size_t rs = size * nmemb;

	if (bs)
		bswrite(bs, buffer, rs);

	return rs;
}

struct http *
http_create(const struct https_state *https) {
	struct http *h;

	h = zmalloc(sizeof(struct http));
	h->write_function = null_write_data;
	h->header_write_function = null_write_data;

	if ((h->ch = curl_easy_init()) == NULL) {
		return NULL;
	}

	curl_easy_setopt(h->ch, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
	curl_easy_setopt(h->ch, CURLOPT_FOLLOWLOCATION, 1l);
	curl_easy_setopt(h->ch, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);

	curl_easy_setopt(h->ch, CURLOPT_ERRORBUFFER, h->error);

	curl_easy_setopt(h->ch, CURLOPT_NOSIGNAL, 1l);

	http_set_curl_https(h->ch, https);

	return h;
}

void
http_free(struct http *h) {
	if (!h)
		return;
	curl_easy_cleanup(h->ch);
	free(h);
}

long
http_perform(struct http *h) {
	long response_code;
	struct curl_slist *header_list = NULL;

	if (h->response_body && (h->write_function == NULL || h->write_function == null_write_data)) 
		h->write_function = default_write_data;

	if (h->response_header && (h->header_write_function == NULL || h->header_write_function == null_write_data)) 
		h->header_write_function = default_write_data;
	
	h->curl_status = CURLE_OK;
	
	if (h->body) {
		curl_easy_setopt(h->ch, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)h->body_length);
		curl_easy_setopt(h->ch, CURLOPT_POSTFIELDS, (const char *) h->body);
	} else {
		/* Used to reset postfields and method. */
		curl_easy_setopt(h->ch, CURLOPT_HTTPGET, 1l);
	}

	curl_easy_setopt(h->ch, CURLOPT_URL, h->url);
	curl_easy_setopt(h->ch, CURLOPT_CUSTOMREQUEST, h->method);
	
	curl_easy_setopt(h->ch, CURLOPT_WRITEFUNCTION, h->write_function);
	curl_easy_setopt(h->ch, CURLOPT_WRITEDATA, h->response_body);

	curl_easy_setopt(h->ch, CURLOPT_HEADERFUNCTION, h->header_write_function);
	curl_easy_setopt(h->ch, CURLOPT_HEADERDATA, h->response_header);

	if (h->headers) {
		while (*(h->headers)) {
			header_list = curl_slist_append(header_list, *h->headers++);
		}
		curl_easy_setopt(h->ch, CURLOPT_HTTPHEADER, header_list);
	}

	if ((h->curl_status = curl_easy_perform(h->ch)) != CURLE_OK) {
		response_code = -1;
		goto out;
	}
	
	curl_easy_getinfo(h->ch, CURLINFO_RESPONSE_CODE, &response_code);
		
out:
	curl_slist_free_all(header_list);
	return response_code;
}

long
http_get(const char *url, struct buf_string *response, const char *headers[], const struct https_state *https) {
	struct http *h = http_create(https);
	long rc;
	
	h->method = "GET";
	h->url = url;
	h->headers = headers;

	if (response) {
		memset(response, 0, sizeof(*response));
		h->response_body = response;
		h->write_function = default_write_data;
	}

	rc = http_perform(h);

	http_free(h);
	return rc;
}

long
http_post(const char *url, struct buf_string *response, const char *poststr, const char *headers[], const struct https_state *https) {
	struct http *h = http_create(https);
	long rc;
	
	h->method = "POST";
	h->url = url;
	h->headers = headers;

	if (response) {
		memset(response, 0, sizeof(*response));
		h->response_body = response;
		h->write_function = default_write_data;
	}

	h->body = poststr;
	h->body_length = strlen(poststr);

	rc = http_perform(h);

	http_free(h);
	return rc;
}

long
http_delete(const char *url, const char *headers[], const struct https_state *https) {
	struct http *h = http_create(https);
	long rc;
	
	h->method = "DELETE";
	h->url = url;
	h->headers = headers;

	rc = http_perform(h);

	http_free(h);
	return rc;
}

long
http_put_str(const char *url, const char *data, const char *headers[], const struct https_state *https) {
	struct http *h = http_create(https);
	long rc;
	
	h->method = "PUT";
	h->url = url;
	h->headers = headers;

	h->body = data;
	h->body_length = strlen(data);

	rc = http_perform(h);

	http_free(h);
	return rc;
}

long
http_put_bin(const char *url, void *data, size_t len, const char *headers[], const struct https_state *https) {
	struct http *h = http_create(https);
	long rc;
	
	h->method = "PUT";
	h->url = url;
	h->headers = headers;

	h->body = data;
	h->body_length = len;

	rc = http_perform(h);

	http_free(h);
	return rc;
}

long
http_move(const char *url, const char *dest, const char *headers[], const struct https_state *https) {
	char *dest_hdr;
	const char **hdrs;
	long rc;
	struct http *h = http_create(https);

	xasprintf(&dest_hdr, "Destination: %s", dest);
	hdrs = merge_headers(headers, (const char *[]){dest_hdr, NULL});

	h->method = "MOVE";
	h->url = url;
	h->headers = hdrs;

	rc = http_perform(h);

	http_free(h);
	free(hdrs);
	free(dest_hdr);
	return rc;
}

long
http_copy(const char *url, const char *dest, const char *headers[], const struct https_state *https) {
	char *dest_hdr;
	const char **hdrs;
	long rc;
	struct http *h = http_create(https);

	xasprintf(&dest_hdr, "Destination: %s", dest);
	hdrs = merge_headers(headers, (const char *[]){dest_hdr, NULL});

	h->method = "COPY";
	h->url = url;
	h->headers = hdrs;

	rc = http_perform(h);

	http_free(h);
	free(hdrs);
	free(dest_hdr);
	return rc;
}

#define HAVE_CA     0x10
#define HAVE_CERT   0x20
#define UNLINK_CA   0x40
#define UNLINK_CERT 0x80

int
http_setup_https(struct https_state *https, const char *cacmd, const char *cafile, const char *certcmd, const char *certfile) {
	bool haveca = false, havecert = false;
	bool unlinkca = false, unlinkcert = false;
	if (https->state > 0)
		return (https->state & 0xf) - 1;
	if (cacmd && *cacmd) {
		FILE *f = popen(cacmd, "r");
		if (!f)
			return -1;
		strlcpy(https->cafile, "/tmp/cacert.XXXXXX", sizeof(https->cafile));
		int r = write_to_tmpfile(https->cafile, f);
		fclose(f);
		if (r < 0)
			return -1;
		haveca = true;
		unlinkca = true;
	} else if (cafile && *cafile) {
		strlcpy(https->cafile, cafile, sizeof(https->cafile));
		haveca = true;
	}

	if (certcmd && *certcmd) {
		FILE *f = popen(certcmd, "r");
		if (!f) {
			if (unlinkca)
				unlink(https->cafile);
			return -1;
		}
		strlcpy(https->certfile, "/tmp/clcert.XXXXXX", sizeof(https->certfile));
		int r = write_to_tmpfile(https->certfile, f);
		fclose(f);
		if (r < 0) {
			if (unlinkca)
				unlink(https->cafile);
			return -1;
		}
		havecert = true;
		unlinkcert = true;
	} else if (certfile == HTTP_USE_CACERT) {
		strlcpy(https->certfile, https->cafile, sizeof(https->certfile));
		havecert = true;
	} else if (certfile && *certfile) {
		strlcpy(https->certfile, certfile, sizeof(https->certfile));
		havecert = true;
	}

	if (!haveca && !havecert) {
		https->state = 1;
		return 0;
	}

	https->state = 2 | (haveca ? HAVE_CA : 0) | (havecert ? HAVE_CERT : 0) |
			(unlinkca ? UNLINK_CA : 0) | (unlinkcert ? UNLINK_CERT : 0);
	return 1;
}

void
http_cleanup_https(struct https_state *https) {
	if (https->state & UNLINK_CA)
		unlink(https->cafile);
	if (https->state & UNLINK_CERT)
		unlink(https->certfile);
	https->state = 0;
}

void
http_clear_https_unlink(struct https_state *https) {
	https->state &= ~(UNLINK_CA|UNLINK_CERT);
}

void
http_set_curl_https(CURL *ch, const struct https_state *https) {
	if (!https)
		return;
	if (https->state & HAVE_CA)
		curl_easy_setopt(ch, CURLOPT_CAINFO, https->cafile);
	if (https->state & HAVE_CERT) {
		curl_easy_setopt(ch, CURLOPT_SSLCERT, https->certfile);
		//curl_easy_setopt(ch, CURLOPT_SSLKEY, https->certfile);
	}
}

const char *
http_default_scheme(const struct https_state *https) {
	if (https->state >= 2)
		return "https";
	return "http";
}
