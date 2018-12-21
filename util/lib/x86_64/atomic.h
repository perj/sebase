// Copyright 2018 Schibsted

#ifndef _ATOMIC_H
#define _ATOMIC_H

#include <sys/types.h>

static __inline int
x86_atomic_cas_int(volatile int *ptr, int expect, int set)
{
	int res;
	__asm volatile("lock cmpxchgl %2, %1" : "=a" (res), "=m" (*ptr)
			: "r" (set), "a" (expect), "m" (*ptr) : "memory");
	return (res);
}

static __inline u_long
x86_atomic_cas_ul(volatile u_long *ptr, u_long expect, u_long set)
{
	u_long res;
	__asm volatile("lock cmpxchgq %2, %1" : "=a" (res), "=m" (*ptr)
			: "r" (set), "a" (expect), "m" (*ptr) : "memory");
	return (res);
}

static __inline void *
x86_atomic_cas_ptr(volatile void *ptr, void *expect, void *set)
{
	return (void*)x86_atomic_cas_ul((u_long *)ptr, (u_long)expect, (u_long)set);
}

#if 0
static __inline int
atomic_xadd_int(volatile int *ptr, int add)
{
	__asm volatile("lock xaddl %2, %0" : "=m" (*ptr), "=r" (add) : "r" (add), "m" (*ptr) : "memory");
	return add;
}
#else
#define atomic_xadd_int(ptr, add) __sync_fetch_and_add(ptr, add)
#endif

#define atomic_xchg_ptr(ptr, newval) __sync_lock_test_and_set(ptr, newval)

#define atomic_cas_int x86_atomic_cas_int
#define atomic_cas_ptr x86_atomic_cas_ptr

#endif /*_ATOMIC_H*/
