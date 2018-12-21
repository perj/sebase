// Copyright 2018 Schibsted

#ifndef LEVENSHTEIN_INCLUDED
#define LEVENSHTEIN_INCLUDED
#include <stdlib.h>
#include <string.h>
#include "unicode/utf.h"
#include "unicode/utf8.h"

#define MIN3(a, b, c) (a < b ? (a < c ? a : c) : (b < c ? b : c))

int u8_strlen(const char* s);
int levenshtein(const char* s1, const char* s2, unsigned int n, unsigned int m);
double similarity_by_distance(const char* s1, const char* s2);
#endif
