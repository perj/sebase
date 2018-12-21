// Copyright 2018 Schibsted

#ifndef AES_H
#define AES_H

#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AES-128 encode inbuf with nonce and key.
 *
 * The nonce ("number used once") should point to a string buffer of at least
 * 16 bytes that is unique to each encryption under the given key. Since we're
 * using CFB it could be a counter. Only the first 16 bytes of the nonce are
 * used. If the nonce string is shorter than 16 bytes, it will be \0 padded.
 *
 * If nonce is NULL, a default nonce is generated based on
 * the current timestamp (sub-second precision).
 *
 * An IV is generated internally by applying the forward function once to the nonce.
 *
 * key should be 128 bit data base64 encoded. Example command for generating
 * key:
 *   dd if=/dev/urandom bs=1 count=16 2> /dev/null | openssl base64
 *
 * The result is padded to always be a multiple of pad (before b64 encoding)
 * Set pad to a multiple of 3 to always avoid = at end of result. Nul bytes
 * are used for padding.
 *
 * Returns malloced base64 encoded iv + ciphertext as a string.
 */
char *aes_encode(const void *inbuf, int inlen, int pad, const char *nonce, const char *key, int klen) ALLOCATOR NONNULL(1, 5);

/*
 * str is output from aes_encode, slen can be -1 for default strlen(str).
 * 
 * Result is malloced data with decoded string. A \0 byte is appended for safety.
 * reslen will be filled with length of result (including pad) if not NULL.
 *
 * IMPORTANT: the function will not fail for invalid indata, instead invalid data is
 * returned, so validy of data must be checked after call.
 */
void *aes_decode(const char *str, int slen, const char *key, int klen, int *reslen) ALLOCATOR NONNULL(1, 3);

/*
 * Decode into resbuf instead of mallocing result.
 */
void *aes_decode_buf(const char *str, int slen, const char *key, int klen, void *resbuf, int *reslen) NONNULL(1, 3);

/*
 * See comment on aes_encode() regarding nonce and IV generation.
 *
 * CBC (Cipher-Block-Chaining): Note, if an attacker can control the inbuf and predict the IV, an attack is possible.
 * The IV therefore must not be (only) time based for user generated plain texts. 
 */
char *aes_cbc_encode(const void *inbuf, int inlen, const char *nonce, const char *key, int klen) ALLOCATOR NONNULL(1, 4);
void *aes_cbc_decode(const char *str, int slen, const char *key, int klen, int *reslen) ALLOCATOR NONNULL(1, 3);
void *aes_cbc_decode_buf(const char *str, int slen, const char *key, int klen, void *resbuf, int *reslen) NONNULL(1, 3);

/*
	Authenticated Encryption API
	http://en.wikipedia.org/wiki/Authenticated_encryption
*/

/*
	Low-level interface. See implementation file for details.
*/
int aes_encrypt_gcm(const char *plaintext, int plaintext_len, const char *aad, int aad_len, const unsigned char *key, int key_size, const unsigned char *iv, int iv_len, unsigned char *ciphertext, unsigned char *tag, int tag_len);
int aes_decrypt_gcm(const char *ciphertext, int ciphertext_len, const char *aad, int aad_len, const unsigned char *key, int key_size, const unsigned char *iv, int iv_len, unsigned char *tag, int tag_len, unsigned char *plaintext);

/*
	Higher-level interface. See implementation file for details.
*/
char *aes_gcm_256_encode(const char *plaintext, int plaintext_len, const char *aad, int aad_len, const char *base64_key, int klen);
int aes_gcm_256_decode_buf(char *dst, const char *base64_text, int text_len, const char *aad, int aad_len, const char *base64_key, int klen);

#ifdef __cplusplus
}
#endif

#endif /*AES_H*/
