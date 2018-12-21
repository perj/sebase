// Copyright 2018 Schibsted

#ifndef COMMON_JSON_VTREE_H
#define COMMON_JSON_VTREE_H

#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vtree_chain;
struct bconf_node;

int json_vtree(struct vtree_chain *dst, const char *root_name, const char *json_str, ssize_t jsonlen, int validate_utf8);
int json_bconf(struct bconf_node **dst, const char *root_name, const char *json_str, ssize_t jsonlen, int validate_utf8);
void vtree_json(struct vtree_chain *n, int use_arrays, int depth, int (*pf)(void *, int, int, const char *, ...), void *cbdata);

/* Escape a single chartacter, up to 7 bytes are written to dst, which must have room. */
int json_encode_char_unsafe(char *dst, char ch, bool escape_solus);

/* Callback vtree_json for writing to a struct buf_string* passed in cbdata. */
int vtree_json_bscat(void *, int, int, const char*, ...);

#ifdef __cplusplus
}
#endif

#endif /*COMMON_JSON_VTREE_H*/
