// Copyright 2018 Schibsted

#include <sys/types.h>

#include "macros.h"

char *sbo_encode(char to[9], int32_t from, char salt);
int32_t sbo_decode(const char *from, char salt) FUNCTION_PURE;

