/* Copyright 2018 Schibsted
 * Originally written by Per Johansson outside Schibsted, but included here
 * with full permissions.
 */

#ifndef COMMON_TLS_H
#define COMMON_TLS_H

#include <openssl/ssl.h>
#include <stdbool.h>
#include <sys/uio.h>
#include "sbp/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/* In case we ever want to switch to some other engine. */
typedef X509 *tlscert_t;
typedef EVP_PKEY *tlskey_t;
typedef SSL_SESSION *tlssess_t;

struct tls
{
	SSL *ssl;
	BIO *rbio, *wbio;
};

/*
 * You are not supposed to free the context, keep it in a global variable.
 */
struct tls_context {
	/* In params */
	const char *ssl_certs_path; /* Optional directory with hashed ca certs to accept. */
	tlscert_t *cacerts; /* Optional list of cacerts to accept. */
	int ncacerts;
	SSL_METHOD *method; /* defaults to SSLv23_method() */

	/* Private params, 0 initialized */
	SSL_CTX *ssl_ctx;
	int context_id_set;
	unsigned char context_id[32];
};

enum
{
	tlsVerifyPeer     = 1 << 0,
	tlsVerifyOptional = 1 << 1,
};

const char *tls_get_cert_dir (void);

/* Only clear the context when you're shutting down. */
void tls_clear_context(struct tls_context *ctx);

/* Add CA certificates to build chains from.
 * Preferably this should be done on the tls pointer instead, but that feature
 * is not available until openssl 1.1.
 *
 * Note that if the array is from tls_read_cert_array_buf you should typically
 * use (ctx, ncerts - 1, cert_arr + 1) when calling this.
 *
 * Also make sure to have filled all wanted in parameters in ctx (e.g. cacert)
 * before calling these, as the context will be initalized.
 */
int tls_add_ca_chain(struct tls_context *ctx, int ncerts, tlscert_t *cert_arr);

/* Initialize TLS for communicating on an fd. The fd is not considered TLS enabled until you call tls_start, however. */
struct tls *tls_open (struct tls_context *ctx, int fd, int options, tlscert_t cert, tlskey_t key, bool nonblock) ALLOCATOR;
/* Free the TLS structure. Be sure to call tls_stop first if you plan to continue using the fd. */
void tls_free (struct tls *tls);

/* Inject some initial data read by other means. Can only be called once, before tls_start.
 * The data is copied and can be discarded after this call.
 */
int tls_inject_read(struct tls *tls, void *buf, size_t len);

/* Indicate that communication will now be done through TLS. */
void tls_start (struct tls *tls);

/*
 * Shutdown TLS on the given fd. You can communicate in clear text after the call is done.
 * Return values:
 *       0: call done.
 *       1: wait for data available for read on fd and call again.
 *       2: wait for fd available for write and call again.
 *   other: error.
 */
int tls_stop (struct tls *tls);

/* Negotiate as a TLS server. Called after tls_start. Same return values as tls_close. */
int tls_accept (struct tls *tls);
/* Negotiate as a TLS client. Called after tls_start. Same return values as tls_close. */
int tls_connect (struct tls *tls);

/* Read and write, same symantics as the syscalls. */
ssize_t tls_read (struct tls *tls, void *buf, size_t maxlen);
ssize_t tls_write (struct tls *tls, const void *buf, size_t len);
ssize_t tls_write_vecs (struct tls *tls, const struct iovec *vecs, int num);

/* Certificate handling. Always free the certs returned. */
tlscert_t tls_read_cert (const char *file) ALLOCATOR;
tlscert_t tls_read_cert_buf(const char *buf, ssize_t len) ALLOCATOR;
tlscert_t tls_get_peer_cert (const struct tls *tls) ALLOCATOR;
tlscert_t tls_generate_selfsigned_cert(tlskey_t key, const char *cn) ALLOCATOR;
void tls_free_cert (tlscert_t cert);

/* Reads multiple certificates from buf and returns a malloced array in *out_certs.
 * Returns -1 if parsing the certificate failed, otherwise returns number of certificates
 * found.
 */
int tls_read_cert_array_buf(const char *buf, ssize_t len, tlscert_t **out_certs);
void tls_free_cert_array(int ncerts, tlscert_t *arr);

/* Certificate utility funcs. */
int tls_get_cn (tlscert_t cert, char *buf, size_t buflen);
int tls_get_issuer_cn(tlscert_t cert, char *buf, size_t buflen);
int tls_compare_certs (const tlscert_t c1, const tlscert_t c2);

/* Key handling. */
tlskey_t tls_read_key (const char *file) ALLOCATOR;
tlskey_t tls_read_key_buf(const char *buf, ssize_t len) ALLOCATOR;
tlskey_t tls_generate_key(int bits) ALLOCATOR;
void tls_free_key (tlskey_t key);

const char *tls_error (const struct tls *tls, int res);

tlssess_t tls_get_session(struct tls *tls);
void tls_set_session(struct tls *tls, tlssess_t sess);
void tls_free_session(tlssess_t);

#ifdef __cplusplus
}
#endif

#endif /*COMMON_TLS_H*/
