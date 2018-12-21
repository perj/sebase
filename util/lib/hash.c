// Copyright 2018 Schibsted

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "buf_string.h"
#include "hash.h"
#include "memalloc_functions.h"

#define BUCKET_SIZE 50

struct hash_entry {
	int klen;	
	void *key;
	void *data;
	struct hash_entry *next;
};

struct hash_bucket {
	struct hash_entry *entries;
#ifdef HASH_STAT
	int no_entries;
#endif
};

struct hash_table {
	struct hash_bucket *buckets;
	void (*free_func)(void*);
	int free_keys;
	int no_buckets;
#ifdef HASH_STAT
	int max_len;
#endif
	int alloced;
	struct hash_entry *entry_pool;
};

#if 0
static inline unsigned long
hash_table_key(const void *key, uint nKeyLength) {
	const char *arKey = key;
	unsigned long hash = 5381;

	while (nKeyLength--)
		hash = ((hash << 5) + hash) + *arKey++;

	return hash;
}
#else
static inline unsigned long 
hash_table_key(const void *key, uint nKeyLength) {
	const char *arKey = key;
	unsigned long hash = 5381;

	/* variant with the hash unrolled eight times */
	for (; nKeyLength >= 8; nKeyLength -= 8) {
		hash = ((hash << 5) + hash) + *arKey++;
		hash = ((hash << 5) + hash) + *arKey++;
		hash = ((hash << 5) + hash) + *arKey++;
		hash = ((hash << 5) + hash) + *arKey++; 
		hash = ((hash << 5) + hash) + *arKey++;
		hash = ((hash << 5) + hash) + *arKey++;
		hash = ((hash << 5) + hash) + *arKey++;
		hash = ((hash << 5) + hash) + *arKey++;
	}
	switch (nKeyLength) {
		case 7: hash = ((hash << 5) + hash) + *arKey++; /* fallthrough... */
		case 6: hash = ((hash << 5) + hash) + *arKey++; /* fallthrough... */
		case 5: hash = ((hash << 5) + hash) + *arKey++; /* fallthrough... */
		case 4: hash = ((hash << 5) + hash) + *arKey++; /* fallthrough... */
		case 3: hash = ((hash << 5) + hash) + *arKey++; /* fallthrough... */
		case 2: hash = ((hash << 5) + hash) + *arKey++; /* fallthrough... */
		case 1: hash = ((hash << 5) + hash) + *arKey++; break;
		case 0: break;
	}

	return hash;
}
#endif

int
hash_table_get_size(int size) {
	int new_size = 3;

	while (new_size < size) {
		new_size <<= 1;
		new_size++;
	}

	return new_size;
}

size_t
hash_table_alloc_size(int size) {
	return sizeof (struct hash_table) + (size + 1) * (sizeof (struct hash_bucket) + sizeof (struct hash_entry));
}

struct hash_table*
hash_table_initialize(void *data, int size, void (*free_func)(void*)) {
	struct hash_table *tbl = data;
	struct hash_entry *entry;

	tbl->buckets = (struct hash_bucket*)(tbl + 1);
	tbl->free_func = free_func;
	tbl->no_buckets = size;
	tbl->alloced = 0;
	entry = tbl->entry_pool = (struct hash_entry*)(tbl->buckets + size + 1);
	while (size--)
		entry = entry->next = entry + 1;
	entry->next = NULL;

	return tbl;
}

struct hash_table* 
hash_table_create(int size, void (*free_func)(void*)) {
	struct hash_table *tbl;

	size = hash_table_get_size(size);

	tbl = (struct hash_table*)zmalloc(hash_table_alloc_size(size));

	hash_table_initialize(tbl, size, free_func);
	tbl->alloced = 1;
	return tbl;
}

static struct hash_entry*
hash_table_get_entry(struct hash_table *tbl) {
	struct hash_entry *entry = tbl->entry_pool;

	if (entry) {
		tbl->entry_pool = entry->next;
		return entry;
	}

	return xmalloc(sizeof (*entry));
}

static void
hash_table_put_entry(struct hash_table *tbl, struct hash_entry *entry) {
	entry->next = tbl->entry_pool;
	tbl->entry_pool = entry;
}

void
hash_table_free_keys(struct hash_table *tbl, int flag) {
	tbl->free_keys = flag;
}

#define MAIN_ALLOCED(tbl, entry) ((entry) >= (struct hash_entry*)tbl && (char*)(entry) < (char*)(tbl) + sizeof(struct hash_table) + (tbl->no_buckets + 1) * (sizeof (struct hash_bucket) + sizeof (struct hash_entry)))

void 
hash_table_free(struct hash_table *tbl) {
	int cnt;
	struct hash_bucket *bucket;
	struct hash_entry *entry;
	struct hash_entry *next_entry;

	if (!tbl)
		return;

	for (cnt = 0 ; cnt <= tbl->no_buckets ; cnt++) {
		bucket = tbl->buckets + cnt;
		/* fprintf(stderr, "%u\n", bucket->no_entries); */

		for (entry = bucket->entries; entry; entry = next_entry) {
			next_entry = entry->next;
			if (tbl->free_func)
				tbl->free_func(entry->data);
			if (tbl->free_keys)
				free(entry->key);
			if (!MAIN_ALLOCED(tbl, entry))
				free(entry);
		}
	}

	/* Free entries allocated after main allocation. */
	for (entry = tbl->entry_pool; entry ; entry = next_entry) {
		next_entry = entry->next;

		if (!MAIN_ALLOCED(tbl, entry))
			free(entry);
	}
	if (tbl->alloced)
		free(tbl);
}

void
hash_table_empty(struct hash_table *tbl) {
	int cnt;
	struct hash_bucket *bucket;
	struct hash_entry *entry;
	struct hash_entry *next_entry;

	for (cnt = 0 ; cnt <= tbl->no_buckets ; cnt++) {
		bucket = tbl->buckets + cnt;

		for (entry = bucket->entries; entry; entry = next_entry) {
			next_entry = entry->next;
			hash_table_delete(tbl, entry->key, entry->klen);
		}
	}
}
void
hash_table_print(struct hash_table *tbl) {
	int cnt;
	struct hash_bucket *bucket;
	struct hash_entry *entry;
	struct hash_entry *next_entry;

	for (cnt = 0 ; cnt <= tbl->no_buckets ; cnt++) {
		bucket = tbl->buckets + cnt;

		for (entry = bucket->entries; entry; entry = next_entry) {
			next_entry = entry->next;
			xwarn("HASH [%s] => %s ", (char *)entry->key,(char *)entry->data);
		}
	}
}

void
hash_table_delete_by_key_prefix(struct hash_table *tbl, const char *key_prefix) {
	int cnt;
	struct hash_bucket *bucket;
	struct hash_entry *entry;
	struct hash_entry *next_entry;

	for (cnt = 0 ; cnt <= tbl->no_buckets ; cnt++) {
		bucket = tbl->buckets + cnt;

		for (entry = bucket->entries; entry; entry = next_entry) {
			next_entry = entry->next;
			if (strncmp(key_prefix, entry->key, strlen(key_prefix)) == 0 )
			hash_table_delete(tbl, entry->key, entry->klen);
		}
	}
}

void*
hash_table_search(struct hash_table *tbl, const void *key, int klen, const void **out_key) {
	struct hash_bucket *bucket;
	unsigned int b_idx;
	struct hash_entry *entry;

	if (klen == -1) klen = strlen(key);

	b_idx = hash_table_key(key, klen) & (unsigned int)tbl->no_buckets;

	bucket = tbl->buckets + b_idx;

	for (entry = bucket->entries; entry; entry = entry->next) {
		if ((klen == entry->klen) && memcmp(key, entry->key, klen) == 0) {
			if (out_key)
				*out_key = entry->key;
			return entry->data;
		}
	}

	if (out_key)
		*out_key = NULL;
	return NULL;
}

void 
hash_table_insert(struct hash_table *tbl, const void *key, int klen, const void *data) {
	struct hash_bucket *bucket;
	unsigned int b_idx;
	struct hash_entry *entry;

	if (klen == -1) klen = strlen(key);

	b_idx = hash_table_key(key, klen) & (unsigned int)tbl->no_buckets;
	
	bucket = tbl->buckets + b_idx;

	for (entry = bucket->entries; entry; entry = entry->next) {
		if ((klen == entry->klen) && memcmp(key, entry->key, klen) == 0) {
			if (tbl->free_func)
				tbl->free_func(entry->data);
			if (tbl->free_keys)
				free(entry->key);
			entry->key = (char*)key;
			entry->data = (void*)data;
			return;
		}
	}

	entry = hash_table_get_entry(tbl);
	entry->next = bucket->entries;
	entry->key = (char*)key;
	entry->klen = klen;
	entry->data = (void*)data;
	bucket->entries = entry;

#ifdef HASH_STAT
	bucket->no_entries++;
	if (bucket->no_entries > tbl->max_len)
		tbl->max_len = bucket->no_entries;
#endif
}

int 
hash_table_replace(struct hash_table *tbl, const void *key, int klen, const void *data) {
	struct hash_bucket *bucket;
	unsigned int b_idx;
	struct hash_entry *entry;

	if (klen == -1) klen = strlen(key);

	b_idx = hash_table_key(key, klen) & (unsigned int)tbl->no_buckets;
	
	bucket = tbl->buckets + b_idx;

	for (entry = bucket->entries; entry; entry = entry->next) {
		if ((klen == entry->klen) && memcmp(key, entry->key, klen) == 0) {
			if (tbl->free_func)
				tbl->free_func(entry->data);
			entry->data = (void*)data;
			return 1;
		}
	}
	return 0;
}

void
hash_table_delete(struct hash_table *tbl, const void *key, int klen) {
	unsigned int b_idx;
	struct hash_bucket *bucket;
	struct hash_entry *entry;
	struct hash_entry *pentry = NULL;

	if (klen == -1) klen = strlen(key);

	b_idx = hash_table_key(key, klen) & (unsigned int)tbl->no_buckets;

	bucket = tbl->buckets + b_idx;
	
	for (entry = bucket->entries; entry; entry = entry->next) {
		if ((klen == entry->klen) && memcmp(key, entry->key, klen) == 0) {
			if (tbl->free_func)
				tbl->free_func(entry->data);
			if (tbl->free_keys)
				free(entry->key);
			if (pentry)
				pentry->next = entry->next;
			else
				bucket->entries = entry->next;
			hash_table_put_entry(tbl, entry);
			return;
		}
		pentry = entry;
	}
}

void*
hash_table_update(struct hash_table *tbl, const void *key, int klen, void* (*update_func)(const void*, int, void**, void *), void *v) {
	struct hash_bucket *bucket;
	unsigned int b_idx;
	struct hash_entry *entry;
	void *d;
	void *k;

	if (klen == -1) klen = strlen(key);

	b_idx = hash_table_key(key, klen) & (unsigned int)tbl->no_buckets;

	bucket = tbl->buckets + b_idx;

	for (entry = bucket->entries; entry; entry = entry->next)
		if ((klen == entry->klen) && memcmp(key, entry->key, klen) == 0)
			return entry->data;

	k = update_func(key, klen, &d, v);
	if (!k || !d)
		return NULL;

	entry = hash_table_get_entry(tbl);
	entry->next = bucket->entries;
	entry->data = d;
	entry->key = k;
	entry->klen = klen;
	bucket->entries = entry;
#ifdef HASH_STAT
	bucket->no_entries++;
	if (bucket->no_entries > tbl->max_len)
		tbl->max_len = bucket->no_entries;
#endif
	return entry->data;
}

void
hash_table_do(struct hash_table *tbl, void (*f)(const void*, int, void*, void*), void *cb_data) {
	struct hash_bucket *bucket;
	int cnt;
	struct hash_entry *entry;

	for (cnt = 0 ; cnt <= tbl->no_buckets ; cnt++) {
		bucket = tbl->buckets + cnt;
		for (entry = bucket->entries; entry; entry = entry->next)
			f(entry->key, entry->klen, entry->data, cb_data);
	}
}

void *
hash_table_next(struct hash_table *tbl, void **state, void **key, int *klen) {
	struct {
		int cnt;
		struct hash_entry *entry;
	} *st = *state;
	struct hash_entry *entry;

	if (!st) {
		*state = st = xmalloc(sizeof (*st));
		st->cnt = 0;
		st->entry = tbl->buckets[0].entries;
	}

	if (st->entry) {
		entry = st->entry;
		st->entry = entry->next;
		if (key)
			*key = entry->key;
		if (klen)
			*klen = entry->klen;
		return entry->data;
	}

	for (st->cnt++; st->cnt <= tbl->no_buckets; st->cnt++) {
		if (tbl->buckets[st->cnt].entries) {
			entry = tbl->buckets[st->cnt].entries;
			st->entry = entry->next;
			if (key)
				*key = entry->key;
			if (klen)
				*klen = entry->klen;
			return entry->data;
		}
	}
	if (st)
		free(st);
	*state = NULL;
	return NULL;
}

#ifdef HASH_STAT
void
hash_table_info(struct hash_table *tbl) {
	int min = tbl->max_len;
	int max = 0;
	int cnt;
	int tot = 0;

	for (cnt = 0 ; cnt <= tbl->no_buckets ; cnt++) {
		if (max < (tbl->buckets + cnt)->no_entries)
			max = (tbl->buckets + cnt)->no_entries;

		if (min > (tbl->buckets + cnt)->no_entries)
			min = (tbl->buckets + cnt)->no_entries;

		tot += (tbl->buckets + cnt)->no_entries;
	}

	fprintf(stderr, "HASH Max : %u\n", max);
	fprintf(stderr, "HASH Min : %u\n", min);
	fprintf(stderr, "AVG      : %0.2f\n", (float)tot / tbl->no_buckets);
}
#endif

struct perfect_hash_table *
perfect_hash_create(int nkeys, int max_buckets, const void **keys, int *klens, int copy_keys) {
	int i;
	unsigned long vals[nkeys];
	char *bitmap;
	int nbuckets;
	struct perfect_hash_table *res;
	struct perfect_hash_entry *buckets;

	if (klens) {
		for (i = 0; i < nkeys; i++)
			vals[i] = hash_table_key(keys[i], klens[i]);
	} else {
		for (i = 0; i < nkeys; i++)
			vals[i] = hash_table_key(keys[i], strlen(keys[i]));
	}

	for (nbuckets = nkeys; nbuckets <= max_buckets; nbuckets++) {
		bitmap = zmalloc(nbuckets * sizeof(*bitmap));
		for (i = 0; i < nkeys; i++) {
			int b = vals[i] % nbuckets;

			if (bitmap[b])
				break;
			bitmap[b] = 1;
		}
		free(bitmap);
		if (i == nkeys)
			break;
	}

	if (nbuckets > max_buckets)
		return NULL;

	res = zmalloc(sizeof (*res) + nbuckets * sizeof (struct perfect_hash_entry));
	buckets = (struct perfect_hash_entry*)(res + 1);

	res->num_buckets = nbuckets;
	for (i = 0; i < nkeys; i++) {
		struct perfect_hash_entry *entry = &buckets[vals[i] % nbuckets];

		if (copy_keys) {
			if (klens) {
				entry->klen = klens[i];
				entry->key = xmalloc(entry->klen);
				memcpy((void*)entry->key, keys[i], entry->klen);
			} else {
				entry->klen = strlen(keys[i]);
				entry->key = xmalloc(entry->klen + 1);
				memcpy((void*)entry->key, keys[i], entry->klen + 1);
			}
		} else {
			entry->key = keys[i];
			if (klens)
				entry->klen = klens[i];
			else
				entry->klen = strlen(keys[i]);
		}
	}
	return res;
}

void
perfect_hash_free(struct perfect_hash_table *tbl, int free_keys, void (*free_data)(void *)) {
	struct perfect_hash_entry *buckets = (struct perfect_hash_entry*)(tbl + 1);
	int i;

	if (free_keys || free_data) {
		for (i = 0; i < tbl->num_buckets; i++) {
			if (free_keys && buckets[i].key)
				free((void*)buckets[i].key);
			if (free_data && buckets[i].data)
				free_data(buckets[i].data);
		}
	}
	free(tbl);
}

void **
perfect_hash_search(struct perfect_hash_table *tbl, const void *key, int klen) {
	int b;
	struct perfect_hash_entry *entry;

	if (klen == -1)
		klen = strlen(key);

	if (tbl->num_buckets == 1)
		b = 0;
	else
		b = hash_table_key(key, klen) % tbl->num_buckets;

	entry = &((struct perfect_hash_entry *)(tbl + 1))[b];
	
	/*log_printf(LOG_DEBUG, "comparing \"%.*s\" to \"%.*s\"", klen, (const char*)key, entry->klen, (const char*)entry->key);*/

	if (klen == entry->klen && memcmp(key, entry->key, klen) == 0)
		return &entry->data;
	return NULL;
}

char *
perfect_hash_serialize(struct perfect_hash_table *tbl, const char *name) {
	struct perfect_hash_entry *buckets = (struct perfect_hash_entry*)(tbl + 1);
	int i;
	struct buf_string res = {NULL};

	bufcat(&res.buf, &res.len, &res.pos, "struct {\n\tstruct perfect_hash_table table;\n\tstruct perfect_hash_entry entries[%d] __attribute__((packed));\n} %s = {\n", tbl->num_buckets, name);
	bufcat(&res.buf, &res.len, &res.pos, "\t{ %d },\n\t{\n", tbl->num_buckets);
	for (i = 0; i < tbl->num_buckets; i++) {
		/* XXX handle keys containing weird chars */
		if (buckets[i].key)
			bufcat(&res.buf, &res.len, &res.pos, "\t\t{ \"%s\", %d, NULL }", (char*)buckets[i].key, buckets[i].klen);
		else
			bufcat(&res.buf, &res.len, &res.pos, "\t\t{ NULL }");
		if (i != tbl->num_buckets - 1)
			bufcat(&res.buf, &res.len, &res.pos, ",\n");
	}
	bufcat(&res.buf, &res.len, &res.pos, "\n\t}\n};");
	return res.buf;
}

void
perfect_hash_map_values(struct perfect_hash_table *tbl, int only_set, void (*map_fun)(void **data_ptr)) {
	struct perfect_hash_entry *buckets = (struct perfect_hash_entry*)(tbl + 1);
	int i;

	for (i = 0; i < tbl->num_buckets; i++) {
		if (!only_set || buckets[i].data)
			map_fun(&buckets[i].data);
	}
}
