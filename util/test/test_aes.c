// Copyright 2018 Schibsted

#include <fcntl.h>
#include <openssl/aes.h>
#include <string.h>
#include <unistd.h>

#include "sbp/aes.h"
#include "sbp/base64.h"
#include "sbp/error_functions.h"

const char *test_string = "The quick brown fox";

static char *
_encrypt(char *b64key, int *klen) {
	int fd;
	char binkey[AES_BLOCK_SIZE];

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		xerr(1, "open");

	if (read(fd, binkey, AES_BLOCK_SIZE) < AES_BLOCK_SIZE)
		xerr(1, "read");

	close(fd);

	*klen = base64_encode(b64key, binkey, AES_BLOCK_SIZE);

	return aes_encode(test_string, strlen(test_string) + 1, 3, NULL, b64key, *klen); /* Include \0 byte. */
}

int
main(int argc, char *argv[]) {
	int klen;
	char b64key[BASE64_NEEDED(AES_BLOCK_SIZE)];

	char *enc = _encrypt(b64key, &klen);
	if (!enc)
		xerrx(1, "NULL from aes_encode");

	char *dec = aes_decode(enc, -1, b64key, klen, NULL);
	if (!dec)
		xerrx(1, "NULL from aes_decode");

	if (strcmp(dec, test_string) != 0)
		xerrx(1, "%s != %s", dec, test_string);
	return 0;
}
