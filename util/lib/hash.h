// Copyright 2018 Schibsted

#ifndef HASH_H
#define HASH_H

#include <sys/types.h>

#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

struct hash_table;

struct perfect_hash_entry {
	const void *key;
	int klen;
	void *data;
};

struct perfect_hash_table {
	int num_buckets;
} TYPE_ALIGN(__alignof__(struct perfect_hash_entry));


int hash_table_get_size(int size);
size_t hash_table_alloc_size(int size);
struct hash_table* hash_table_initialize(void *data, int size, void (*free_func)(void*));
struct hash_table* hash_table_create(int size, void (*free_func)(void*)) ALLOCATOR;
void hash_table_free(struct hash_table* tbl);
void hash_table_print(struct hash_table* tbl);
void hash_table_empty(struct hash_table *cache);
void hash_table_delete_by_key_prefix(struct hash_table *tbl, const char *);
void hash_table_free_keys(struct hash_table* tbl, int flag);

void* hash_table_search(struct hash_table *tbl, const void *key, int, const void **);
void hash_table_insert(struct hash_table *tbl, const void *key, int, const void *data);
int hash_table_replace(struct hash_table *tbl, const void *key, int klen, const void *data);
void* hash_table_update(struct hash_table *tbl, const void *key, int, void* (*update_func)(const void*, int, void**, void *), void *);
void hash_table_info();
void hash_table_delete(struct hash_table *tbl, const void *key, int);
void hash_table_do(struct hash_table *tbl, void (*f)(const void*, int, void*, void*), void*);
void *hash_table_next(struct hash_table *tbl, void **state, void **key, int *klen);

struct perfect_hash_table *perfect_hash_create(int nkeys, int max_buckets, const void **keys, int *klens, int copy_keys);
void perfect_hash_free(struct perfect_hash_table *tbl, int free_keys, void (*free_data)(void *data));
void **perfect_hash_search(struct perfect_hash_table *tbl, const void *key, int klen);
char *perfect_hash_serialize(struct perfect_hash_table *tbl, const char *name);
void perfect_hash_map_values(struct perfect_hash_table *tbl, int only_set, void (*map_fun)(void **data_ptr));

#ifdef __cplusplus
}
#endif

#endif
