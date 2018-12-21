// Copyright 2018 Schibsted

#include <stdio.h>
#include <stdlib.h>
#include "sbp/utf8.h"

int main(int argc, char *argv[])
{
	const char *src = argc > 1 ? argv[1] : "\xD6vertorne\xE5"; /* Övertorneå */

	int dstlen = 0;

	char *res = latin1_to_utf8(src, -1, &dstlen);

	printf("%d:%s\n", dstlen, res);

	free(res);

	return 0;
}
