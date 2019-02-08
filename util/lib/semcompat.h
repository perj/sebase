#pragma once

// OS X doesn't have unnamed POSIX semaphores. This header exists to allow
// for uniform named semaphores.

// Syntax is based on POSIX semaphores.

#include "macros.h"

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>

typedef dispatch_semaphore_t semaphore_t;

static inline int ARTIFICIAL NONNULL(1)
semaphore_init(semaphore_t *sem, bool shared, uint64_t start_value) {
	if (start_value > LONG_MAX) {
		*sem = NULL;
		errno = EINVAL;
		return -1;
	}
	*sem = dispatch_semaphore_create(start_value);
	return *sem ? 0 : -1;
}

static inline int ARTIFICIAL NONNULL(1)
semaphore_wait(semaphore_t *sem) {
	return dispatch_semaphore_wait(*sem, DISPATCH_TIME_FOREVER);
}

static inline int ARTIFICIAL NONNULL(1)
semaphore_post(semaphore_t *sem) {
	return dispatch_semaphore_signal(*sem) ? 0 : -1;
}

static inline void ARTIFICIAL NONNULL(1)
semaphore_destroy(semaphore_t *sem) {
	dispatch_release(*sem);
}
#else
#include <semaphore.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>

typedef sem_t semaphore_t;

static inline int ARTIFICIAL NONNULL(1)
semaphore_init(semaphore_t *sem, bool shared, uint64_t start_value) {
	if (start_value > UINT_MAX) {
		errno = EINVAL;
		return -1;
	}
	return sem_init(sem, shared, start_value);
}

static inline int ARTIFICIAL NONNULL(1)
semaphore_wait(semaphore_t *sem) {
	return sem_wait(sem);
}

static inline int ARTIFICIAL NONNULL(1)
semaphore_post(semaphore_t *sem) {
	return sem_post(sem);
}

static inline void ARTIFICIAL NONNULL(1)
semaphore_destroy(semaphore_t *sem) {
	sem_destroy(sem);
}
#endif
