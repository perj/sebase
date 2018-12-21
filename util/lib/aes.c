// Copyright 2018 Schibsted

#include <errno.h>
#include <fcntl.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "base64.h"
#include "memalloc_functions.h"

#include "aes.h"

static void
generate_iv(unsigned char *dst, const char *src) {
	memset(dst, 0, AES_BLOCK_SIZE);
	/* Fetch a default iv which is unlikely to be reused for this key. */
	int use_default_iv = (src == NULL) || (src[0] == '\0');
	if (use_default_iv) {
		struct timeval tv;

		gettimeofday(&tv, NULL);
		snprintf((char*)dst, AES_BLOCK_SIZE, "%lx%lx", (long)tv.tv_sec, (long)tv.tv_usec);
	} else {
		/* This is supposed to be strncpy */
		strncpy((char*) dst, src, AES_BLOCK_SIZE);
	}
}

static bool
setup_key_and_iv(AES_KEY *aes, unsigned char *iv_buf, const char *nonce, const char *key, int klen, bool deckey) {
	/* Base64 decode key. */
	if (klen == -1)
		klen = strlen(key);
	if (klen != BASE64_NEEDED(AES_BLOCK_SIZE) - 1 /* exclude terminator */) {
		errno = EINVAL;
		return false;
	}

	unsigned char binkey[AES_BLOCK_SIZE + 2];
	if (base64_decode((char*)binkey, key, klen) < 0)
		return false;

	if (deckey)
		AES_set_decrypt_key(binkey, AES_BLOCK_SIZE * 8, aes);
	else
		AES_set_encrypt_key(binkey, AES_BLOCK_SIZE * 8, aes);

	if (!iv_buf)
		return true;

	/* NIST SP 800-38A: "The first method is to apply the forward cipher function, under the same key
	   that is used for the encryption of the plaintext, to a nonce. The nonce must be a data block
	   that is unique to each execution of the encryption operation." */

	/* Setup nonce in a buffer for encryption. Input is treated as a string, and padded to one block if needed. */
	unsigned char nonce_buf[AES_BLOCK_SIZE];
	generate_iv(nonce_buf, nonce);

	/* Encrypt nonce using same key as message to use as IV */
	AES_ecb_encrypt(nonce_buf, iv_buf, aes, AES_ENCRYPT);
	return true;
}

static bool
decode_input(unsigned char **enc, int *elen, const char *str, int slen) {
	if (slen == -1)
		slen = strlen(str);
	if (!slen) {
		*enc = zmalloc(1);
		return false;
	}
	if (slen % 4) {
		errno = EINVAL;
		*enc = NULL;
		return false;
	}

	/* Base64 decode str. */
	*elen = slen / 4 * 3;
	*enc = xmalloc(*elen);

	if (base64_decode((char*)*enc, str, slen) < 0)
		return false;

	int i = 0;
	if (slen > 2) {
		if (str[slen-1] == '=')
			++i;
		if (i && str[slen-2] == '=')
			++i;
	}

	*elen -= i;
	return true;
}

static int
adjust_pad(int inlen, int pad) {
	if (pad <= 1)
		return 0;
	return pad - inlen % pad;
}

/* Use multiple of 3 for pad to always avoid = at end of result. */
char *
aes_encode(const void *inbuf, int inlen, int pad, const char *nonce, const char *key, int klen) {
	AES_KEY aes;

	if (inlen == -1)
		inlen = strlen(inbuf) + 1;

	unsigned char iv_buf[AES_BLOCK_SIZE];
	if (!setup_key_and_iv(&aes, iv_buf, nonce, key, klen, false))
		return NULL;

	pad = adjust_pad(inlen + AES_BLOCK_SIZE, pad);
	int elen = inlen + pad;
	unsigned char enc[AES_BLOCK_SIZE + elen];

	/* Store iv and ciphertext in enc. */
	memcpy(enc, iv_buf, AES_BLOCK_SIZE);
	int num = 0;
	AES_cfb128_encrypt(inbuf, enc + AES_BLOCK_SIZE, inlen, &aes, iv_buf, &num, AES_ENCRYPT);
	if (pad > 0) {
		static unsigned char padbuf[AES_BLOCK_SIZE] = {0};
		AES_cfb128_encrypt(padbuf, enc + AES_BLOCK_SIZE + inlen, elen - inlen, &aes, iv_buf, &num, AES_ENCRYPT);
	}

	/* Finally base64 encode the result. */
	return base64_encode_new((const char*)enc, AES_BLOCK_SIZE + elen, NULL);
}

void *
aes_decode(const char *str, int slen, const char *key, int klen, int *reslen) {
	return aes_decode_buf(str, slen, key, klen, NULL, reslen);
}

void *
aes_decode_buf(const char *str, int slen, const char *key, int klen, void *resbuf, int *reslen) {
	unsigned char *enc;
	int elen;
	AES_KEY aes;
	char *res = NULL;

	if (!decode_input(&enc, &elen, str, slen))
		return enc;

	if (!setup_key_and_iv(&aes, NULL, NULL, key, klen, false))
		goto out;

	int r = elen - AES_BLOCK_SIZE;
	if (r <= 0) {
		errno = EINVAL;
		goto out;
	}

	if (resbuf == NULL) {
		res = malloc(r + 1);
		if (!res)
			goto out;
	} else {
		if (*reslen < r + 1)
			goto out;
		res = resbuf;
	}

	/* Uncipher text into res. */
	int num = 0;
	AES_cfb128_encrypt(enc + AES_BLOCK_SIZE, (unsigned char*) res, r, &aes, enc, &num, AES_DECRYPT);
	res[r] = '\0';

	if (reslen)
		*reslen = r;
out:
	free(enc);
	return res;
}


char *
aes_cbc_encode(const void *inbuf, int inlen, const char *nonce, const char *key, int klen) {
	AES_KEY aes;

	if (inlen == -1)
		inlen = strlen(inbuf);

	unsigned char iv_buf[AES_BLOCK_SIZE];
	if (!setup_key_and_iv(&aes, iv_buf, nonce, key, klen, false))
		return NULL;

	int pad = (AES_BLOCK_SIZE - inlen % AES_BLOCK_SIZE) % AES_BLOCK_SIZE;
	int elen = inlen + pad;
	unsigned char enc[AES_BLOCK_SIZE + elen];

	/* Pad the in buf, should be divisble by AES_BLOCK_SIZE */
	unsigned char *block_buf = zmalloc(inlen + pad);
	memcpy(block_buf, inbuf, inlen);

	/* Store iv and ciphertext in enc. */
	memcpy(enc, iv_buf, AES_BLOCK_SIZE);
	AES_cbc_encrypt(block_buf, enc + AES_BLOCK_SIZE, inlen + pad, &aes, iv_buf, AES_ENCRYPT);
	free(block_buf);

	/* Finally base64 encode the result. */
	return base64_encode_new((const char*)enc, AES_BLOCK_SIZE + elen, NULL);
}

void *
aes_cbc_decode(const char *str, int slen, const char *key, int klen, int *reslen) {
	return aes_cbc_decode_buf(str, slen, key, klen, NULL, reslen);
}

void *
aes_cbc_decode_buf(const char *str, int slen, const char *key, int klen, void *resbuf, int *reslen) {
	unsigned char *enc;
	int elen;
	AES_KEY aes;
	char *res = NULL;

	if (!decode_input(&enc, &elen, str, slen))
		return enc;

	if (!setup_key_and_iv(&aes, NULL, NULL, key, klen, true))
		goto out;

	int r = elen - AES_BLOCK_SIZE;
	if (r <= 0) {
		errno = EINVAL;
		goto out;
	}

	if (resbuf == NULL) {
		res = malloc(r + 1);
		if (!res)
			goto out;
	} else {
		if (*reslen < r + 1)
			goto out;
		res = resbuf;
	}

	/* Uncipher text into res. */
	AES_cbc_encrypt(enc + AES_BLOCK_SIZE, (unsigned char*) res, r, &aes, enc, AES_DECRYPT);
	res[r] = '\0';

	if (reslen)
		*reslen = r;
out:
	free(enc);
	return res;
}

static const EVP_CIPHER *
aes_gcm_by_keysize(int key_size) {
	const EVP_CIPHER *cipher = NULL;

	switch (key_size) {
		case 128:
			cipher = EVP_aes_128_gcm();
			break;
		case 192:
			cipher = EVP_aes_192_gcm();
			break;
		case 256:
			cipher = EVP_aes_256_gcm();
			break;
	}
	return cipher;
}

/*
	Low-level AES-GCM authenticated encryption interface.

	AAD stands for 'additonal authenticated data'. It's data that's hashed into the signature tag,
	but is not included in the ciphertext. For instance, imagine that you are encrypting the data
	associated with a HTTP header. By providing the header as AAD, you ensure that the encrypted
	data can not be copied and used on a different header. AAD is optional, pass NULL or zero
	for aad_len if you don't have any.

	The amount of data provided by `key` must match the `key_size` (given in bits), i.e if for
	AES-GCM-256 you need to pass 32 bytes of key and specify 256 for the `key_size`.

	The `iv` and `iv_len` SHOULD be 12 bytes. Longer IVs will introduce extra computation. As
	usual, the `iv` MUST be unique over all encryptions under the same key.

	The `ciphertext` output buffer must be at least the size of `plaintext_len`. There is no padding.

	The `tag` output buffer must have room for `tag_len` bytes. A `tag_len` of 16 (128 bits)
	is the standard full-security length. Decreasing the tag length by truncation is allowed,
	but of course reduces the security of the signature.
*/
int
aes_encrypt_gcm(const char *plaintext, int plaintext_len,
	const char *aad, int aad_len,
	const unsigned char *key, int key_size,
	const unsigned char *iv, int iv_len,
	unsigned char *ciphertext, unsigned char *tag, int tag_len)
{
	EVP_CIPHER_CTX *ctx;
	int res;
	int ciphertext_len;

	const EVP_CIPHER *cipher = aes_gcm_by_keysize(key_size);

	if (!cipher || ((ctx = EVP_CIPHER_CTX_new()) == NULL))
		return -1;

	if (EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL) != 1) {
		res = -2;
		goto leave;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL) != 1) {
		res = -3;
		goto leave;
	}

	/* Initialise key and IV */
	if (EVP_EncryptInit_ex(ctx, NULL, NULL, (unsigned char*)key, (unsigned char*)iv) != 1) {
		res = -4;
		goto leave;
	}

	/* Provide any additional authenticated data (AAD). */
	if (aad && aad_len && (EVP_EncryptUpdate(ctx, NULL, &res, (unsigned char*)aad, aad_len) != 1)) {
		res = -5;
		goto leave;
	}

	if (EVP_EncryptUpdate(ctx, ciphertext, &ciphertext_len, (unsigned char*)plaintext, plaintext_len) != 1) {
		res = -6;
		goto leave;
	}

	if (EVP_EncryptFinal_ex(ctx, ciphertext + ciphertext_len, &res) != 1) {
		res = -7;
		goto leave;
	}

	ciphertext_len += res;

	/* This extracts up to 128-bits (16 bytes) from the authentication tag (signature) */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag_len, tag) != 1) {
		res = -8;
		goto leave;
	}

	res = ciphertext_len;

leave:
	EVP_CIPHER_CTX_free(ctx);
	return res;
}

/*
	Low-level AES-GCM authenticated decryption interface.

	The `plaintext` output buffer must be at least the size of `ciphertext_len`.

*/
int
aes_decrypt_gcm(const char *ciphertext, int ciphertext_len,
	const char *aad, int aad_len,
	const unsigned char *key, int key_size,
	const unsigned char *iv, int iv_len, unsigned char *tag, int tag_len,
	unsigned char *plaintext)
{
	EVP_CIPHER_CTX *ctx;
	int res;
	int plaintext_len;

	const EVP_CIPHER *cipher = aes_gcm_by_keysize(key_size);

	if (!cipher || ((ctx = EVP_CIPHER_CTX_new()) == NULL))
		return -1;

	if (EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL) != 1) {
		res = -2;
		goto leave;
	}

	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL) != 1) {
		res = -3;
		goto leave;
	}

	/* Initialise key and IV */
	if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
		res = -4;
		goto leave;
	}

	/* Set expected tag value. Set before AAD/plaintext for compatibility reasons. */
	if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len, tag) != 1) {
		res = -5;
		goto leave;
	}

	/* Provide any additional authenticated data (AAD). */
	if (aad && aad_len && (EVP_DecryptUpdate(ctx, NULL, &res, (unsigned char*)aad, aad_len) != 1)) {
		res = -6;
		goto leave;
	}

	if (EVP_DecryptUpdate(ctx, plaintext, &plaintext_len, (unsigned char*)ciphertext, ciphertext_len) != 1) {
		res = -7;
		goto leave;
	}

	/* GCM mode doesn't actually generate any additional output here. */
	int ret = EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &res);

	if (ret != 1) {
		/* Verify failed -- make some effort to clear the buffer */
		explicit_bzero(plaintext, plaintext_len);
		res = -100;
		goto leave;
	}

	res += plaintext_len;

leave:
	EVP_CIPHER_CTX_free(ctx);
	return res;
}

/*
	The `base64_key` argument should be the base64 encoding of a 32-byte key, eg:
	$ dd if=/dev/urandom bs=1 count=32 status=none | base64

	A random IV is generated internally.

	The return value is a string of the form base64(tag || IV || ciphertext) that must
	be free'd by the caller.
*/
char *
aes_gcm_256_encode(const char *plaintext, int plaintext_len, const char *aad, int aad_len, const char *base64_key, int klen) {
	static const int TAG_LENGTH = 16;
	static const int IV_LENGTH = 12;

	if (!plaintext)
		return NULL;
	if (klen < 1)
		klen = strlen(base64_key);
	if (klen > BASE64_NEEDED(32))
		return NULL;

	unsigned char key[BASE64DECODE_NEEDED(klen)];
	int key_len = base64_decode((char*)key, base64_key, klen);
	if (key_len != 32)
		return NULL;

	if (plaintext_len < 0)
		plaintext_len = strlen(plaintext);
	if (aad_len < 0)
		aad_len = aad ? strlen(aad) : 0;

	unsigned char iv_buf[IV_LENGTH];
	arc4random_buf(iv_buf, sizeof(iv_buf));

	unsigned char *enc = xmalloc(TAG_LENGTH + IV_LENGTH + plaintext_len);

	int res = aes_encrypt_gcm(plaintext, plaintext_len, aad, aad_len, key, 256, iv_buf, IV_LENGTH, enc + TAG_LENGTH + IV_LENGTH, enc, TAG_LENGTH);
	if (res < 0) {
		// Don't know what's in enc at this point, clear it just to be sure.
		explicit_bzero(enc, TAG_LENGTH + IV_LENGTH + plaintext_len);
		free(enc);
		return NULL;
	}

	// Copy IV into buffer and base64 encode the whole buffer.
	memcpy(enc + TAG_LENGTH, iv_buf, IV_LENGTH);
	char *encoded_string = base64_encode_new((char*)enc, TAG_LENGTH + IV_LENGTH + plaintext_len, NULL);

	// BIO_dump_fp(stdout, (const char *)enc, TAG_LENGTH + IV_LENGTH + plaintext_len); // debug
	free(enc);

	return encoded_string;
}

int
aes_gcm_256_decode_buf(char *dst, const char *base64_text, int text_len, const char *aad, int aad_len, const char *base64_key, int klen) {
	static const int TAG_LENGTH = 16;
	static const int IV_LENGTH = 12;

	if (klen < 1)
		klen = strlen(base64_key);
	if (klen > BASE64_NEEDED(32))
		return -1;

	unsigned char key[BASE64DECODE_NEEDED(klen)];
	int key_len = base64_decode((char*)key, base64_key, klen);
	if (key_len != 32)
		return -1;

	if (aad_len < 0)
		aad_len = aad ? strlen(aad) : 0;

	size_t dec_len;
	char *dec = base64_decode_new(base64_text, text_len, &dec_len);

	int res = aes_decrypt_gcm(dec + TAG_LENGTH + IV_LENGTH, dec_len - TAG_LENGTH - IV_LENGTH,
		aad, aad_len,
		key, 256,
		(unsigned char*)dec + TAG_LENGTH, IV_LENGTH, (unsigned char*)dec, TAG_LENGTH,
		(unsigned char*)dst
	);

	free(dec);

	return res;
}
