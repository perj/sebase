/*
 * Copyright (c) 2009 Ted Unangst <ted.unangst@gmail.com>
 * Copyright (c) 2010 Artur Grabowski <art@blocket.se>
 * Copyright 2018 Schibsted
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __AVL_H__
#define __AVL_H__

#include "macros.h"

struct avl_node {
	struct avl_node *link[2];
	int height;
};

typedef int(*avl_comp_fn)(const struct avl_node *, const struct avl_node *);
typedef int(*avl_walk_fn)(struct avl_node *, void *);

typedef void(*avl_fixup_fn)(struct avl_node *);

#define AVL_MAXDEPTH 64

struct avl_it {
	struct avl_it_int {
		struct avl_node *n;
		int d;
	} is[AVL_MAXDEPTH], *isp;
	avl_comp_fn cmp;
	struct avl_node *s, *e;
	int ince;
};

#ifndef offsetof
#define offsetof(s, e) ((size_t)&((s *)0)->e)
#endif

/* the bit at the end is to prevent mistakes where n is not an avl_node */
#define avl_data(n, type, field) ((type *)(void*)((char *)n - offsetof(type, field) - (n - (struct avl_node *)n)))

#ifdef __cplusplus
extern "C" {
#endif

void avl_insert(struct avl_node *, struct avl_node **, avl_comp_fn);
void avl_delete(struct avl_node *, struct avl_node **, avl_comp_fn);
struct avl_node *avl_lookup(const struct avl_node *, struct avl_node **, avl_comp_fn);
struct avl_node *avl_search(const struct avl_node *, struct avl_node **, avl_comp_fn);
void avl_check(struct avl_node *, avl_comp_fn) NONNULL(2);

void avl_it_init(struct avl_it *, struct avl_node *, struct avl_node *,
    struct avl_node *, avl_comp_fn);
void avl_it_init2(struct avl_it *, struct avl_node *, struct avl_node *,
    struct avl_node *, int, avl_comp_fn);
struct avl_node *avl_it_next(struct avl_it *);

// Augmented avl insert.
void aavl_insert(struct avl_node *, struct avl_node **, avl_comp_fn, avl_fixup_fn);


#ifdef __cplusplus
}
#endif

#endif
