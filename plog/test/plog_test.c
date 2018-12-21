// Copyright 2018 Schibsted

#include "../lib/plog.c"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TEST(cond, ...) test(cond, __func__, __FILE__, __LINE__, #cond, __VA_ARGS__)
static void
test(bool cond, const char *func, const char *file, int line, const char *condstr, const char *fmt, ...) {
	if (cond)
		return;

	va_list ap;

	printf("\x1b[31;1mTest failed\x1b[0m: %s\n"
		"\x1b[1mFunction\x1b[0m: %s\n"
		"\x1b[1mLine\x1b[0m: %s:%d\n"
		"\x1b[1mDetails\x1b[0m: ", condstr, func, file, line);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	abort();
}

static void
test_json_encode_buf_malloc(void) {
	char inp[10000];
	memset(inp, 'A', sizeof(inp));
	char buf[2000]; // Trigger xrealloc
	char *dst;
	ssize_t dlen = sizeof(inp);

	char *fv = json_encode_buf(buf, sizeof(buf), 1, inp, &dst, &dlen);
	TEST(dlen == sizeof(inp) + 2, "Unexpected dlen: %zd", dlen);
	TEST(dst[0] == '"' && dst[sizeof(inp) + 1] == '"', "Missing quotes");
	TEST(memcmp(dst + 1, inp, sizeof(inp)) == 0, "Input/output mismatch");
	TEST(dst == fv, "Expected dst to trigger malloc");
	free(fv);

	inp[1000] = '\0';
	inp[500] = '"';
	dlen = 1000;
	char *dst2;
	ssize_t dlen2 = -1;
	inp[9999] = '\0';
	fv = json_encode_buf(buf, sizeof(buf), 2, inp, &dst, &dlen, inp + 1001, &dst2, &dlen2);
	TEST(dlen == 1003, "Unexpected dlen: %zd", dlen);
	TEST(dst[0] == '"' && dst[1002] == '"', "Missing quotes");
	TEST(memcmp(dst + 1, inp, 500) == 0, "Input/output mismatch");
	TEST(memcmp(dst + 501, "\\\"", 2) == 0, "Input/output mismatch");
	TEST(memcmp(dst + 503, inp + 501, 1000 - 501) == 0, "Input/output mismatch");
	TEST(dlen2 == 9000, "Unexpected dlen2: %zd", dlen);
	TEST(dst2[0] == '"' && dst2[8999] == '"', "Missing quotes");
	TEST(memcmp(dst2 + 1, inp + 1001, 8998) == 0, "Input/output mismatch");
	TEST(dst2 == fv, "Expected dst2 to trigger malloc");
	free(fv);

	char buf2[8192];
	char inp2[20000 * 8]; /* Doing my best to trigger realloc moving the pointer (works on my current setup) */
	memset(inp2, 'B', sizeof(inp2));
	inp2[sizeof(inp2) - 1] = '\0';

	dlen2 = -1;
	fv = json_encode_buf(buf2, sizeof(buf2), 2, NULL, &dst, &dlen, inp2, &dst2, &dlen2);
	TEST(dlen == 0, "Unexpected dlen: %zd", dlen);
	TEST(dst == NULL, "Unexpected dst: %p", dst);
	TEST(fv == dst2, "Expected fv to point to dst2");
	TEST(dlen2 == sizeof(inp2) + 1, "Expected dlen2 to match inp2 + 1");
	TEST(dst2[0] == '"' && dst2[dlen2 - 1] == '"', "Missing quotes");
	TEST(memcmp(dst2 + 1, inp2, dlen2 - 2) == 0, "Input/output mismatch");
	free(fv);
}

static void
test_encode_latin2(void) {
	plog_set_global_charset(PLOG_LATIN2);
	char buf[1024];
	char inp[] = "test\x80\xA0\xA1";
	char *dst;
	ssize_t dlen = -1;
	char *fv = json_encode_buf(buf, sizeof(buf), 1, inp, &dst, &dlen);
	TEST(fv == NULL, "Didn't expect a malloc");
	TEST(strcmp(dst, "\"test?\xC2\xA0\xC4\x84\"") == 0, "Invalid output, got %s", dst);
	plog_set_global_charset(PLOG_UTF8);
}

int
main(int argc, const char *argv[]) {
	test_json_encode_buf_malloc();
	test_encode_latin2();
	return 0;
}
