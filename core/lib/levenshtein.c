/* Copyright 2018 Schibsted
 * Originally author Love Jädergård at Schibsted
 */

#include "levenshtein.h"

int
u8_strlen(const char* s) {
	int l = 0;
	int p = 0;
	UChar32 ch;
	
	while (*(s + p) != '\0') {
		++l;
		U8_NEXT_UNSAFE(s, p, ch);
	}
	return l;
}

int
levenshtein(const char* s1, const char* s2, unsigned int s1_len, unsigned int s2_len) {
	int* D = malloc(2*(s2_len + 1)*sizeof (int));
	unsigned int i;
	unsigned int j;
	int p1 = 0;
	int p2 = 0;
	UChar32 c1;
	UChar32 c2;
	int* d;
	int* d_1;
	int* tmp;
	int dist;
	
	d = D;
	d_1 = D + s2_len + 1;
   	for (j = 0; j <= s2_len; ++j) {
		d_1[j] = j;
	}
	for (i = 1; i <= s1_len; ++i) {
		U8_NEXT_UNSAFE(s1, p1, c1);
		for (j = 1, p2 = 0, d[0] = i; j <= s2_len; ++j) {
			U8_NEXT_UNSAFE(s2, p2, c2);
			if (c1 == c2) {
				d[j] = d_1[j - 1];
			} else {
				d[j] = 1 + MIN3(d_1[j], d[j - 1], d_1[j-1]);
			}
		}
		tmp = d;
		d = d_1;
		d_1 = tmp;
	}
	dist = d_1[s2_len];
	free(D);
	return dist;
}

double
similarity_by_distance(const char* s1, const char* s2) {
	unsigned int l1 = u8_strlen(s1);
	unsigned int l2 = u8_strlen(s2);

	return 100.0*(1.0 - (double)levenshtein(s1, s2, l1, l2)/(double)(l1 > l2 ? l1 : l2));
}
