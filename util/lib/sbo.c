// Copyright 2018 Schibsted

#include <ctype.h>
#ifdef SBO_CODER
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "sbo.h"

/*
 * Security by obscurity.
 *
 * Encode a 32-bit integer into a more or less readable string.
 * Other than not being standard, this offers as much security as
 * a Caesar cipher - none whatsoever. It's just supposed to be
 * obscure and weird and use human readable characters.
 */

static const char table[16] = "kqmwjenrhtbyguvi";
#define SBO_MAGIC 666471142

static char
fix_salt(char salt) {
	salt = tolower(salt);
	if (salt < 'a' || salt > 'z')
		salt = 'q';
	return salt - 'a';
}

char *
sbo_encode(char to[9], int32_t from, char salt) {
	int i;

	salt = fix_salt(salt);
	from ^= SBO_MAGIC;
	for (i = 0; i < 8; i++) {
		to[i] = table[(from >> (i * 4)) & 0xf];
		to[i] = 'a' + (to[i] - 'a' + salt) % 26;
	}
	to[i] = '\0';

	return to;
}

static int32_t
sbo_reverse(char a) {
	unsigned int i;

	for (i = 0; i < sizeof(table); i++)
		if (table[i] == a)
			break;

	return i;
}

int32_t
sbo_decode(const char *from, char salt) {
	int32_t res = 0;
	int i;

	salt = fix_salt(salt);
	for (i = 0; i < 8; i++) {
		char ch = from[i] - salt;
		if (ch < 'a')
			ch += 26;
		res |= sbo_reverse(ch) << (i * 4);
	}

	return res ^ SBO_MAGIC;
}

#ifdef SBO_CODER
int main(int argc, char **argv) {
	if (!strcmp(argv[1], "--encode")) {
		struct in_addr ia;
		char to[9];

		inet_pton(AF_INET, argv[2], &ia);
		printf("%s = %s\n", argv[2], sbo_encode(to, ia.s_addr, argv[3][0]));
	} else {
		int32_t res = sbo_decode(argv[1], argv[2][0]);
		printf("%s = 0x%x (%d.%d.%d.%d)\n", argv[1], res, (res >> 0) & 0xff, (res >> 8) & 0xff, (res >> 16) & 0xff, (res >> 24) & 0xff);
	}
	return 0;
}
#endif
