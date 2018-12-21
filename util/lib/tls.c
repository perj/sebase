/* Copyright 2018 Schibsted
 * Originally written by Per Johansson outside Schibsted, but included here
 * with full permissions.
 */

#include "memalloc_functions.h"
#include "string_functions.h"
#include "tls.h"

#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <pthread.h>
#include <syslog.h>

#if __has_include(<valgrind/valgrind.h>)
#include <valgrind/memcheck.h>
#include <valgrind/valgrind.h>
#endif

struct CRYPTO_dynlock_value
{
	pthread_rwlock_t rwlock;
};

static pthread_mutex_t *tls_locks;

static void UNUSED
tls_locking_function(int mode, int n, const char *file, int line) {
	if (mode & CRYPTO_LOCK)
		pthread_mutex_lock(&tls_locks[n]);
	else
		pthread_mutex_unlock(&tls_locks[n]);
}

static unsigned long UNUSED
tls_thread_id_function(void) {
	return (unsigned long)pthread_self();
}

static struct CRYPTO_dynlock_value UNUSED *
tls_dynlock_create(const char *file, int line) {
	struct CRYPTO_dynlock_value *res = zmalloc(sizeof (*res));

	pthread_rwlock_init(&res->rwlock, NULL);
	return res;
}

static void UNUSED
tls_dynlock_lock(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line) {
	switch (mode) {
	case CRYPTO_LOCK | CRYPTO_WRITE:
		pthread_rwlock_wrlock(&l->rwlock);
		break;
	case CRYPTO_LOCK | CRYPTO_READ:
		pthread_rwlock_rdlock(&l->rwlock);
		break;
	case CRYPTO_UNLOCK | CRYPTO_WRITE:
	case CRYPTO_UNLOCK | CRYPTO_READ:
		pthread_rwlock_unlock(&l->rwlock);
		break;
	default:
		xerrx(1, "Unknown mode to %s: 0x%x, called at %s:%d", __func__, mode, file, line);
	}
}

static void UNUSED
tls_dynlock_destroy(struct CRYPTO_dynlock_value *l, const char *file, int line) {
	pthread_rwlock_destroy(&l->rwlock);
	free(l);
}

const char *
tls_get_cert_dir (void)
{
	return X509_get_default_cert_dir ();
}

static int
tls_init (struct tls_context *ctx) {
	static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
	static int inited = 0;

	pthread_mutex_lock (&init_mutex);
	if (!inited)
	{
		int nlocks;
		int i;

		SSL_load_error_strings ();
		SSL_library_init ();

		nlocks = CRYPTO_num_locks();
		tls_locks = xmalloc(nlocks * sizeof (*tls_locks));
		for (i = 0 ; i < nlocks ; i++)
			pthread_mutex_init(&tls_locks[i], NULL);
		CRYPTO_set_locking_callback(tls_locking_function);
		CRYPTO_set_id_callback(tls_thread_id_function);
		CRYPTO_set_dynlock_create_callback(tls_dynlock_create);
		CRYPTO_set_dynlock_lock_callback(tls_dynlock_lock);
		CRYPTO_set_dynlock_destroy_callback(tls_dynlock_destroy);

		inited = 1;
	}

	if (!ctx->ssl_ctx)
	{
		ctx->ssl_ctx = SSL_CTX_new (ctx->method ?: SSLv23_method());
		if (!ctx->ssl_ctx) {
			pthread_mutex_unlock (&init_mutex);
			return 1;
		}
		SSL_CTX_set_options(ctx->ssl_ctx, SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);
		SSL_CTX_set_cipher_list(ctx->ssl_ctx, "DEFAULT:!RC4:!MD5");
		if (ctx->ssl_certs_path)
			SSL_CTX_load_verify_locations (ctx->ssl_ctx, NULL, ctx->ssl_certs_path);
		if (ctx->ncacerts) {
			X509_STORE *store = SSL_CTX_get_cert_store(ctx->ssl_ctx);
			for (int i = 0 ; i < ctx->ncacerts ; i++)
				X509_STORE_add_cert(store, ctx->cacerts[i]);
		}
		SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_NONE, NULL);
		SSL_CTX_set_mode(ctx->ssl_ctx, SSL_MODE_AUTO_RETRY);
	}

	if (!ctx->context_id_set)
	{
		ctx->context_id_set = 1;
		RAND_bytes (ctx->context_id, sizeof (ctx->context_id));
	}

	pthread_mutex_unlock (&init_mutex);
	return 0;
}

void
tls_clear_context(struct tls_context *ctx) {
	SSL_CTX_free(ctx->ssl_ctx);
	ctx->ssl_ctx = NULL;
	if (ctx->ncacerts) {
		for (int i = 0 ; i < ctx->ncacerts ; i++)
			tls_free_cert(ctx->cacerts[i]);
		free(ctx->cacerts);
		ctx->cacerts = NULL;
	}
}

struct tls *
tls_open (struct tls_context *ctx, int fd, int options, tlscert_t cert, tlskey_t key, bool nonblock)
{
	struct tls *res;
	int sslopts;

	if (fd < 0)
		return NULL;

	if (tls_init(ctx))
		return NULL;

	res = zmalloc (sizeof (*res));
	if (!res) {
		return NULL;
	}
	res->ssl = SSL_new (ctx->ssl_ctx);
	if (!res->ssl) {
		free(res);
		return NULL;
	}

	sslopts = 0;
	SSL_set_options (res->ssl, sslopts);
	if (cert && key) {
		if (!SSL_use_certificate (res->ssl, cert))
		{
			syslog (LOG_ERR, "Failed to load certificate file: %s",
					ERR_reason_error_string (ERR_get_error ()));
			SSL_free (res->ssl);
			free(res);
			return NULL;
		}
		if (!SSL_use_PrivateKey (res->ssl, key))
		{
			syslog (LOG_ERR, "Failed to load private key: %s",
					ERR_reason_error_string (ERR_get_error ()));
			SSL_free (res->ssl);
			free(res);
			return NULL;
		}
	}

	SSL_set_session_id_context (res->ssl, ctx->context_id, sizeof (ctx->context_id));
	if (options & tlsVerifyPeer)
	{
		int vopts = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
		if (options & tlsVerifyOptional)
			vopts &= ~SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
		SSL_set_verify (res->ssl, vopts, NULL);

		if (ctx->ssl_certs_path) {
			STACK_OF(X509_NAME) *castack = sk_X509_NAME_new_null ();

			SSL_add_dir_cert_subjects_to_stack (castack, ctx->ssl_certs_path);
			SSL_set_client_CA_list (res->ssl, castack);
		}
		if (ctx->ncacerts) {
			for (int i = 0 ; i < ctx->ncacerts ; i++)
				SSL_add_client_CA(res->ssl, ctx->cacerts[i]);
		}
	}
	res->rbio = BIO_new_socket (fd, BIO_NOCLOSE);
	if (!res->rbio)
	{
		SSL_free (res->ssl);
		free(res);
		return NULL;
	}
	BIO_set_nbio (res->rbio, nonblock);
	res->wbio = res->rbio;
	return res;
}

int
tls_add_ca_chain(struct tls_context *ctx, int ncerts, tlscert_t *cert_arr) {
	if (tls_init(ctx))
		return -1;

	for (int i = 0 ; i < ncerts ; i++) {
		SSL_CTX_add_extra_chain_cert(ctx->ssl_ctx, cert_arr[i]);
#ifdef CRYPTO_add
		// pre-1.1
		CRYPTO_add(&cert_arr[i]->references, 1, CRYPTO_LOCK_X509);
#else
		// post-1.1
		X509_up_ref(cert_arr[i]);
#endif
	}
	return 0;
}

int
tls_inject_read(struct tls *tls, void *buf, size_t len) {
	BIO *f = BIO_new(BIO_f_buffer());
	if (!f)
		return -1;
	BIO_set_buffer_read_data(f, buf, len);
	BIO_push(f, tls->rbio);
	tls->rbio = f;
	return 0;
}

void
tls_start (struct tls *tls)
{
	SSL_set_bio (tls->ssl, tls->rbio, tls->wbio);
}

int
tls_stop (struct tls *tls)
{
	int l = SSL_shutdown (tls->ssl);

	if (l == 1)
		return 0;
	if (l)
	{
		if (SSL_get_error (tls->ssl, l) == SSL_ERROR_WANT_READ)
			return 1;
		if (SSL_get_error (tls->ssl, l) == SSL_ERROR_WANT_WRITE)
			return 2;
		return l;
	}
	return -1;
}

void
tls_free (struct tls *tls)
{
	if (tls->rbio != tls->wbio) {
		/* Disconnect rbio from wbio, they'll then be freed separately by SSL_free. */
		BIO_pop(tls->rbio);
	}
	SSL_free (tls->ssl);
	free(tls);
}

int
tls_accept (struct tls *tls)
{
	int l = SSL_accept (tls->ssl);

	if (l == 1)
		return 0;
	if (l)
	{
		if (SSL_get_error (tls->ssl, l) == SSL_ERROR_WANT_READ)
			return 1;
		if (SSL_get_error (tls->ssl, l) == SSL_ERROR_WANT_WRITE)
			return 2;
		return l;
	}
	return -1;
}

int
tls_connect (struct tls *tls)
{
	int l = SSL_connect (tls->ssl);

	if (l == 1)
		return 0;
	if (l)
	{
		if (SSL_get_error (tls->ssl, l) == SSL_ERROR_WANT_READ)
			return 1;
		if (SSL_get_error (tls->ssl, l) == SSL_ERROR_WANT_WRITE)
			return 2;
		return l;
	}
	return -1;
}

ssize_t
tls_read (struct tls *tls, void *buf, size_t maxlen)
{
	ssize_t r = SSL_read (tls->ssl, buf, maxlen);
#if defined(VALGRIND_MAKE_MEM_DEFINED) && !defined(NDEBUG)
	if (r > 0) {
		VALGRIND_MAKE_MEM_DEFINED(buf, r);
	}
#endif
	return r;
}

ssize_t
tls_write (struct tls *tls, const void *buf, size_t len)
{
	return SSL_write (tls->ssl, buf, len);
}

ssize_t
tls_write_vecs (struct tls *tls, const struct iovec *vecs, int num)
{
	int i, l = 0;
	int res = 0;

	for (i = 0; i < num; i++)
	{
		l = tls_write (tls, vecs[i].iov_base, vecs[i].iov_len);
		if (l < 0)
			break;
		res += l;
	}
	if (res)
		return res;
	return l;
}

tlscert_t
tls_read_cert (const char *file)
{
	FILE *fp = fopen (file, "rb");
	tlscert_t res;

	if (!fp)
		return NULL;

	res = PEM_read_X509 (fp, NULL, NULL, NULL);
	fclose (fp);
	return res;
}

tlscert_t
tls_read_cert_buf(const char *buf, ssize_t len) {
	if (!buf)
		return NULL;

	BIO *bio = BIO_new_mem_buf((void*)buf, len);
	tlscert_t res = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	BIO_free(bio);

	return res;
}

int
tls_read_cert_array_buf(const char *buf, ssize_t len, tlscert_t **out_certs) {
	if (!buf)
		return 0;

	int n = 0;
	*out_certs = NULL;

	BIO *bio = BIO_new_mem_buf((void*)buf, len);
	unsigned char *data;
	long l;
	while (PEM_bytes_read_bio(&data, &l, NULL, PEM_STRING_X509, bio, NULL, NULL)) {
		unsigned char *orig_data = data;
		X509 *cert = d2i_X509(NULL, (const unsigned char**)&data, l);

		if (!cert) {
			tls_free_cert_array(n, *out_certs);
			*out_certs = NULL;
			return -1;
		}
		OPENSSL_free(orig_data);

		*out_certs = xrealloc(*out_certs, (n+1) * sizeof(**out_certs));
		(*out_certs)[n++] = cert;
	}
	BIO_free(bio);

	return n;
}

void
tls_free_cert_array(int ncerts, tlscert_t *arr) {
	while (ncerts--) {
		tls_free_cert(arr[ncerts]);
	}
	free(arr);
}

tlscert_t
tls_get_peer_cert (const struct tls *tls)
{
	return SSL_get_peer_certificate (tls->ssl);
}

void
tls_free_cert (tlscert_t cert)
{
	X509_free (cert);
}

int
tls_get_cn (tlscert_t cert, char *buf, size_t buflen)
{
	X509_NAME *subject = X509_get_subject_name (cert);

	return X509_NAME_get_text_by_NID (subject, NID_commonName, buf, buflen - 1);
}

int
tls_get_issuer_cn(tlscert_t cert, char *buf, size_t buflen)
{
	X509_NAME *issuer = X509_get_issuer_name(cert);

	return X509_NAME_get_text_by_NID(issuer, NID_commonName, buf, buflen - 1);
}

int
tls_compare_certs (const tlscert_t c1, const tlscert_t c2)
{
	return X509_cmp (c1, c2);
}

tlskey_t
tls_read_key (const char *file)
{
	FILE *fp = fopen (file, "rb");
	tlskey_t res;

	if (!fp)
		return NULL;

	res = PEM_read_PrivateKey (fp, NULL, NULL, NULL);
	fclose (fp);
	return res;
}

tlskey_t
tls_read_key_buf(const char *buf, ssize_t len) {
	if (!buf)
		return NULL;

	BIO *bio = BIO_new_mem_buf((void*)buf, len);
	tlskey_t res = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
	BIO_free(bio);

	return res;
}

void
tls_free_key (tlskey_t key)
{
	if (key)
		EVP_PKEY_free (key);
}

const char *
tls_error (const struct tls *tls, int res)
{
	int ec = SSL_get_error (tls->ssl, res);
	const char *err;

	if (ec == SSL_ERROR_SYSCALL) {
		err = ERR_reason_error_string (ERR_get_error ());
		if (!err)
			err = xstrerror (errno);
	} else if (ec != SSL_ERROR_SSL) {
		err = ERR_reason_error_string (ec);
	} else
		err = ERR_reason_error_string (ERR_get_error ());

	return err;
}

tlskey_t
tls_generate_key(int bits) {
	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
	if (!ctx)
		return NULL;

	if (EVP_PKEY_keygen_init(ctx) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		return NULL;
	}
	if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0) {
		EVP_PKEY_CTX_free(ctx);
		return NULL;
	}

	EVP_PKEY *pkey = NULL;
	EVP_PKEY_keygen(ctx, &pkey);

	EVP_PKEY_CTX_free(ctx);
	return pkey;
}

tlscert_t
tls_generate_selfsigned_cert(tlskey_t key, const char *cn) {
	X509 *cert = X509_new();
	X509_set_version(cert, 2);
	X509_set_pubkey(cert, key);

	ASN1_INTEGER *serial = s2i_ASN1_INTEGER(NULL, (char*)"1");
	X509_set_serialNumber(cert, serial);
	ASN1_INTEGER_free(serial);

	X509_NAME *name = X509_NAME_new();
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8, (const unsigned char*)cn, -1, -1, 0);
	X509_set_subject_name(cert, name);
	X509_set_issuer_name(cert, name);
	X509_NAME_free(name);

	ASN1_TIME *at = ASN1_TIME_set(NULL, time(NULL));
	X509_set_notBefore(cert, at);
	at = ASN1_TIME_set(at, time(NULL) + 24*3600*365);
	X509_set_notAfter(cert, at);
	ASN1_STRING_free(at);

#if 0 /* libnss doesn't want the CA flag set when cert is used as SSL server. */
	BASIC_CONSTRAINTS bc = { .ca = 1 };
	X509_add1_ext_i2d(cert, NID_basic_constraints, &bc, true, 0);
#endif

#define DIGITAL_SIGNATURE_BIT 0
#define KEY_ENCIPHERMENT_BIT 2
#define DATA_ENCIPHERMENT_BIT 3
#define KEY_AGREEMENT_BIT 4
#define CERTIFICATE_SIGN_BIT 5
#define CRL_SIGN_BIT 6
	ASN1_BIT_STRING *ku = ASN1_BIT_STRING_new();
	ASN1_BIT_STRING_set_bit(ku, DIGITAL_SIGNATURE_BIT, 1);
	ASN1_BIT_STRING_set_bit(ku, KEY_ENCIPHERMENT_BIT, 1);
	ASN1_BIT_STRING_set_bit(ku, DATA_ENCIPHERMENT_BIT, 1);
	ASN1_BIT_STRING_set_bit(ku, KEY_AGREEMENT_BIT, 1);
	ASN1_BIT_STRING_set_bit(ku, CERTIFICATE_SIGN_BIT, 1);
	ASN1_BIT_STRING_set_bit(ku, CRL_SIGN_BIT, 1);
	X509_add1_ext_i2d(cert, NID_key_usage, ku, true, 0);
	ASN1_BIT_STRING_free(ku);

	EXTENDED_KEY_USAGE *eku = EXTENDED_KEY_USAGE_new();
	sk_ASN1_OBJECT_push(eku, OBJ_txt2obj("serverAuth", false));
	sk_ASN1_OBJECT_push(eku, OBJ_txt2obj("clientAuth", false));
	X509_add1_ext_i2d(cert, NID_ext_key_usage, eku, true, 0);
	EXTENDED_KEY_USAGE_free(eku);

	X509_sign(cert, key, EVP_sha256());
	return cert;
}

tlssess_t
tls_get_session(struct tls *tls) {
	return SSL_get1_session(tls->ssl);
}

void
tls_set_session(struct tls *tls, tlssess_t sess) {
	SSL_set_session(tls->ssl, sess);
}

void
tls_free_session(tlssess_t sess) {
	SSL_SESSION_free(sess);
}
