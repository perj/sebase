// Copyright 2018 Schibsted

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include "sbp/levenshtein.h"

static int
chk_levdist(int expected, const char* s1, const char* s2, int len1, int len2) {

	int d = levenshtein(s1, s2, len1, len2);
	if (d != expected) {
		fprintf(stderr, "Error: Got d=%d for ('%s', '%s', %d, %d), expected %d\n",
			d, s1, s2, len1, len2, expected
		);
		return 1;
	}
	return 0;
}

static int
chk_sim(double expected, const char* s1, const char* s2) {
	static const double e = 0.5f;
	double d = similarity_by_distance(s1, s2);
	if (d < expected-e || d > expected+e) {
		fprintf(stderr, "Error: Got d=%f for ('%s', '%s'), expected %f\n",
			d, s1, s2, expected
		);
		return 1;
	}
	return 0;
}

int
main(int argc, char **argv) {

	int fail = 0;

	fail += chk_levdist(0, "", "", 0, 0);
	fail += chk_levdist(4, "test", "", 4, 0);
	fail += chk_levdist(0, "test", "test", 4, 4);
	fail += chk_levdist(2, "test", "test", 4, 2);

	/* insertion */
	fail += chk_levdist(1, "te1st", "test", 5, 4);
	/* deletion */
	fail += chk_levdist(1, "tet", "test", 3, 4);
	/* substitution */
	fail += chk_levdist(1, "tezt", "test", 4, 4);
	/* swap */
	fail += chk_levdist(2, "tset", "test", 4, 4);

	fail += chk_sim(100, "", "");
	fail += chk_sim(0, "test", "");
	fail += chk_sim(0, "", "test");
	fail += chk_sim(100, "test", "test");
	fail += chk_sim(50, "tset", "test");

	fail += chk_sim(66.6, "åra", "ara");
	fail += chk_sim(86.6, "Regnet öser ner, våren är här.", "Regnet oser ner, varen ar har.");

	return fail;
}

