// Copyright 2018 Schibsted

#include "sbp/atomic.h"

static __inline int
spinlock_add_int(volatile int *ptr, int delta)
{
	int oldv = *ptr, newv;

	while ((newv = atomic_cas_int(ptr, oldv, oldv + delta)) != oldv)
		oldv = newv;
	return oldv + delta;
}

