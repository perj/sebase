// Copyright 2018 Schibsted

#ifndef EVENT_H
#define EVENT_H

#include "sbp/macros.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */
struct vtree_chain;
int log_event(const char *event);
int syslog_ident(const char *ident, const char *fmt, ...) FORMAT_PRINTF(2, 3);
int stat_log(struct vtree_chain *vtree, const char *event, const char *id);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif
