// Copyright 2018 Schibsted

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openssl/evp.h> // For BIO_dump_fp

#include "sbp/memalloc_functions.h"
#include "sbp/base64.h"
#include "sbp/aes.h"

int
main(int argc, char *argv[]) {
/*
	Test vector extracted from SP 800-38D (https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/mac/gcmtestvectors.zip):
	https://www.nist.gov/disclaimer states "With the exception of material marked as copyrighted, information presented on these pages is considered public information and may be distributed or copied. Use of appropriate byline/photo/image credits is requested."
	[Keylen = 256]
	[IVlen = 96]
	[PTlen = 128]
	[AADlen = 128]
	[Taglen = 128]
	Count = 0
*/
	int iv_len = 12;
	int tag_len = 16;
	int key_size = 256;

	const char ptext[] = { 0x2d, 0x71, 0xbc, 0xfa, 0x91, 0x4e, 0x4a, 0xc0, 0x45, 0xb2, 0xaa, 0x60, 0x95, 0x5f, 0xad, 0x24 };
	const char AAD[] = { 0x1e, 0x08, 0x89, 0x01, 0x6f, 0x67, 0x60, 0x1c, 0x8e, 0xbe, 0xa4, 0x94, 0x3b, 0xc2, 0x3a, 0xd6 };
	const unsigned char iv[] = { 0xac, 0x93, 0xa1, 0xa6, 0x14, 0x52, 0x99, 0xbd, 0xe9, 0x02, 0xf2, 0x1a, 0 }; // + 1 dummy byte for testing
	unsigned char expected_ct[] = { 0x89, 0x95, 0xae, 0x2e, 0x6d, 0xf3, 0xdb, 0xf9, 0x6f, 0xac, 0x7b, 0x71, 0x37, 0xba, 0xe6, 0x7f };
	unsigned char expected_tag[] = { 0xec, 0xa5, 0xaa, 0x77, 0xd5, 0x1d, 0x4a, 0x0a, 0x14, 0xd9, 0xc5, 0x1e, 0x1d, 0xa4, 0x74, 0xab };
	unsigned char secret_key[32] = { 0x92, 0xe1, 0x1d, 0xcd, 0xaa, 0x86, 0x6f, 0x5c, 0xe7, 0x90, 0xfd, 0x24, 0x50, 0x1f, 0x92, 0x50, 0x9a, 0xac, 0xf4, 0xcb, 0x8b, 0x13, 0x39, 0xd5, 0x0c, 0x9c, 0x12, 0x40, 0x93, 0x5d, 0xd0, 0x8b };
	unsigned char base64_key[BASE64_NEEDED(sizeof(secret_key))];
	unsigned char dtext[128];
	unsigned char ctext[128];
	unsigned char tag[tag_len];

	printf("key_size=%d, iv_len=%d, tag_len=%d\n", key_size, iv_len, tag_len);

	base64_encode((char*)base64_key, secret_key, sizeof(secret_key));

	printf("Key:\n");
	BIO_dump_fp(stdout, (const char *)secret_key, sizeof(secret_key));
	printf("Key (base64): %s\n", base64_key);

	/* Test standard encryption */
	int res = aes_encrypt_gcm(ptext, sizeof(ptext),
		AAD, sizeof(AAD),
		secret_key, key_size,
		iv, iv_len,
		ctext, tag, tag_len
	);

	int ctext_len = res;

	int tag_ok = memcmp(tag, expected_tag, tag_len) == 0;
	int ct_ok = memcmp(ctext, expected_ct, res) == 0;  // should fail spectacularly if res is < 0

	if (res >= 0 && tag_ok && ct_ok) {
		printf("Ciphertext (OK):\n");
		BIO_dump_fp (stdout, (const char *)ctext, res);
		printf("Tag (OK):\n");
		BIO_dump_fp(stdout, (const char *)tag, tag_len);
	} else {
		printf("Ciphertext (%s):\n", ct_ok ? "OK" : "invalid");
		BIO_dump_fp (stdout, (const char *)ctext, sizeof(expected_ct));
		printf("Ciphertext (expected):\n");
		BIO_dump_fp (stdout, (const char *)expected_ct, sizeof(expected_ct));

		printf("Tag (%s):\n", tag_ok ? "OK": "invalid");
		BIO_dump_fp(stdout, (const char *)tag, tag_len);
		printf("Tag (expected):\n");
		BIO_dump_fp(stdout, (const char *)expected_tag, tag_len);

		printf("ERROR: encryption failed: %d\n", res);
		exit(1);
	}

	/* Test standard decryption */
	res = aes_decrypt_gcm((const char*)ctext, ctext_len,
		AAD, sizeof(AAD),
		secret_key, key_size,
		iv, iv_len, tag, tag_len,
		dtext
	);

	int pt_ok = memcmp(dtext, ptext, res) == 0;  // should fail spectacularly if res is < 0

	if (res >= 0 && pt_ok) {
		printf("Plaintext (OK):\n");
		BIO_dump_fp(stdout, (const char *)dtext, res);
		printf("Tag (OK):\n");
		BIO_dump_fp(stdout, (const char *)tag, tag_len);
	} else {
		printf("Plaintext (invalid):\n");
		BIO_dump_fp (stdout, (const char *)dtext, sizeof(ptext));
		printf("Plaintext (expected):\n");
		BIO_dump_fp (stdout, (const char *)ptext, sizeof(ptext));

		fprintf(stderr, "ERROR: decryption failed: %d\n", res);
		exit(1);
	}

	/* Test decryption with a modified IV -- expected to fail */
	res = aes_decrypt_gcm((const char*)ctext, ctext_len,
		NULL, 0,
		(unsigned char*)secret_key, key_size,
		(unsigned char*)iv + 1, iv_len, tag, tag_len,
		dtext
	);
	if (res >= 0) {
		fprintf(stderr, "ERROR: the modified IV was accepted: %d\n", res);
		exit(1);
	}

	/* Test decryption with invalid tag -- expected to fail */
	tag[1] ^= 128;
	res = aes_decrypt_gcm((const char*)ctext, ctext_len,
		NULL, 0,
		(unsigned char*)secret_key, key_size,
		(unsigned char*)iv, iv_len, tag, tag_len,
		dtext
	);
	if (res >= 0) {
		fprintf(stderr, "ERROR: the invalid tag was accepted: %d\n", res);
		exit(1);
	}
	tag[1] ^= 128;

	/* Test decryption with modified ciphertext -- expected to fail */
	ctext[1] ^= 1;
	res = aes_decrypt_gcm((const char*)ctext, ctext_len,
		NULL, 0,
		(unsigned char*)secret_key, key_size,
		(unsigned char*)iv, iv_len, tag, tag_len,
		dtext
	);
	if (res >= 0) {
		fprintf(stderr, "ERROR: decryption on modified ciphertext was accepted: %d\n", res);
		// BIO_dump_fp(stdout, (const char *)dtext, res);
		exit(1);
	}

	/*
		Basic tests of higher-level interfaces.
	*/
	char *enc = aes_gcm_256_encode(ptext, -1, AAD, -1, (const char*)base64_key, -1);
	if (!enc) {
		fprintf(stderr, "ERROR: aes_gcm_256_encode() failed.\n");
		exit(1);
	}

	memset(dtext, 0, sizeof(dtext));
	res = aes_gcm_256_decode_buf((char*)dtext, enc, -1, AAD, -1, (const char*)base64_key, -1);

	if (res < 0) {
		fprintf(stderr, "ERROR: aes_gcm_256_decode() failed.\n");
		exit(1);

	}

	// Check wrong AAD results in failure.
	res = aes_gcm_256_decode_buf((char*)dtext, enc, -1, NULL, 0, (const char*)base64_key, -1);
	if (res >= 0) {
		fprintf(stderr, "ERROR: aes_gcm_256_decode() with modified AAD accepted.\n");
		exit(1);

	}

	free(enc);

	return EXIT_SUCCESS;
}
