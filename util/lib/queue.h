// Copyright 2018 Schibsted

#include "macros.h"

#if __has_include(<bsd/sys/queue.h>)
#include <bsd/sys/queue.h>
#else
#include <sys/queue.h>
#endif

#ifndef STAILQ_HEAD
// It appears these are named differently
#define STAILQ_EMPTY             SIMPLEQ_EMPTY
#define STAILQ_ENTRY             SIMPLEQ_ENTRY
#define STAILQ_FIRST             SIMPLEQ_FIRST
#define STAILQ_FOREACH           SIMPLEQ_FOREACH
#define STAILQ_HEAD              SIMPLEQ_HEAD
#define STAILQ_HEAD_INITIALIZER  SIMPLEQ_HEAD_INITIALIZER
#define STAILQ_INIT              SIMPLEQ_INIT
#define STAILQ_INSERT_TAIL       SIMPLEQ_INSERT_TAIL
#define STAILQ_REMOVE_HEAD       SIMPLEQ_REMOVE_HEAD
#endif
