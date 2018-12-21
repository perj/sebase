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
#include <sys/param.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "avl.h"

#undef max
#define max(a, b) (a > b ? a : b)

/* these macros make performance quite a bit better */
#define avl_height(n) (n ? n->height : 0)
#define avl_reheight(n) do { (n)->height = max(avl_height((n)->link[0]), avl_height((n)->link[1])) + 1; } while (0)
#define avl_balance(n) (n ? avl_height(n->link[0]) - avl_height(n->link[1]) : 0)

struct avl_node *avl_search_internal(const struct avl_node *, struct avl_node *, avl_comp_fn);

static void
avl_rotate(struct avl_node **p, int d)
{
	struct avl_node *pivot;
	struct avl_node *n = *p;

	pivot = n->link[!d];
	n->link[!d] = pivot->link[d];
	pivot->link[d] = n;
	avl_reheight(n);
	avl_reheight(pivot);
	*p = pivot;
}

static void
avl_rebalance(struct avl_node **p)
{
	struct avl_node *n = *p;
	int bal;
	int d;

	avl_reheight(n);
	bal = avl_balance(n);

	if (bal >= -1 && bal <= 1)
		return;

	d = bal < -1;

	if ((d ? 1 : -1) * (avl_balance(n->link[d])) > 0)
		avl_rotate(&n->link[d], d);

	avl_rotate(p, !d);
}

void
avl_insert(struct avl_node *x, struct avl_node **p, avl_comp_fn cmp)
{
	struct avl_node *n;

	if ((n = *p) == NULL) {
		x->link[0] = x->link[1] = NULL;
		x->height = 1;
		*p = x;
		return;
	}
	avl_insert(x, &n->link[cmp(x, n) > 0], cmp);
	avl_rebalance(p);
}

void
avl_delete(struct avl_node *x, struct avl_node **p, avl_comp_fn cmp)
{
	struct avl_node *n = *p;

	if (n == NULL)
		return;

	if (x == n) {
		if (n->link[0] == NULL) {
			*p = n->link[1];
		} else if (n->link[1] == NULL) {
			*p = n->link[0];
		} else {
			/*
			 * One would think that choosing the taller or shorter side would
			 * make a difference. It doesn't, so just arbitrarily choose the
			 * left side.
			 */
			struct avl_node *r = n->link[0];

			while (r->link[1] != NULL)
				r = r->link[1];
			avl_delete(r, &n->link[0], cmp);
			r->link[0] = n->link[0];
			r->link[1] = n->link[1];
			avl_reheight(r);
			*p = r;
		}
	} else {
		avl_delete(x, &n->link[cmp(x, n) > 0], cmp);
	}
	if (*p != NULL)
		avl_rebalance(p);
}

struct avl_node *
avl_lookup(const struct avl_node *x, struct avl_node **root, avl_comp_fn cmp)
{
	struct avl_node *n = *root;

	while (n != NULL) {
		int c = cmp(x, n);
		if (c == 0)                        
			break;
		n = n->link[c > 0];
	}
	return n;
}

struct avl_node *
avl_search_internal(const struct avl_node *x, struct avl_node *p, avl_comp_fn cmp)
{
	int diff;

	if (p == NULL)
		return NULL;

	diff = cmp(x, p);
	if (diff == 0)
		return p;

	if (diff < 0) {
		struct avl_node *left = NULL;

		left = avl_search_internal(x, p->link[0], cmp);

		return left && cmp(left, p) < 0 ? left : p;
	}

	return avl_search_internal(x, p->link[1], cmp);
}


/*
 * Return the best greater or equal match.
 */
struct avl_node *
avl_search(const struct avl_node *x, struct avl_node **root, avl_comp_fn cmp)
{
	return avl_search_internal(x, *root, cmp);
}

void
avl_check(struct avl_node *n, avl_comp_fn cmp)
{
	assert(abs(avl_balance(n)) < 2);

	if (n->link[0] == NULL && n->link[1] == NULL)
		assert(avl_height(n) == 1);
	assert(avl_height(n) == max(avl_height(n->link[0]), avl_height(n->link[1])) + 1);

	if (n->link[0]) {
		assert(cmp(n, n->link[0]) >= 0);
		avl_check(n->link[0], cmp);
	}
	if (n->link[1]) {
		assert(cmp(n, n->link[1]) <= 0);
		avl_check(n->link[1], cmp);
	}
}

void
avl_it_init(struct avl_it *it, struct avl_node *root, struct avl_node *s,
    struct avl_node *e, avl_comp_fn cmp)
{
	return avl_it_init2(it, root, s, e, 0, cmp);
}

void
avl_it_init2(struct avl_it *it, struct avl_node *root, struct avl_node *s,
    struct avl_node *e, int ince, avl_comp_fn cmp)
{
	memset(it, 0, sizeof(*it));
	it->is[0].n = root;
	it->cmp = cmp;
	it->s = s;
	it->e = e;
	it->ince = ince;
	it->isp = &it->is[0];
	if (root == NULL)
		it->isp--;
}

struct avl_node *
avl_it_next(struct avl_it *it)
{
	while (it->isp >= &it->is[0]) {
		struct avl_it_int *is = it->isp;
		struct avl_node *n = is->n;

		switch (is->d) {
		case 0:
			if (it->e && (it->ince ? (it->cmp(n, it->e) > 0) : (it->cmp(n, it->e) >= 0)))
				is->d = 3;
			else
				is->d = 2;
			if (it->s == NULL || it->cmp(n, it->s) >= 0) {
				if (is->d == 2)
					is->d = 1;
				if (n->link[0]) {
					it->isp++;
					it->isp->n = n->link[0];
					it->isp->d = 0;
					continue;
				}
			}
			continue;
		case 1:
			is->d = 2;
			return n;
		case 2:
			if (n->link[1]) {
				is->n = n->link[1];
				is->d = 0;
				continue;
			}
			is->d = 3;
			continue;
		case 3: 
			it->isp--;
			continue;
		}
	}
	return NULL;
}

static void
aavl_rotate(struct avl_node **p, int d, avl_fixup_fn fix)
{
	struct avl_node *pivot;
	struct avl_node *n = *p;

	pivot = n->link[!d];
	n->link[!d] = pivot->link[d];
	pivot->link[d] = n;
	avl_reheight(n);
	fix(n);
	avl_reheight(pivot);
	fix(pivot);
	*p = pivot;
}

static void
aavl_rebalance(struct avl_node **p, avl_fixup_fn fix)
{
	struct avl_node *n = *p;
	int bal;
	int d;

	avl_reheight(n);
	fix(n);
	bal = avl_balance(n);

	if (bal >= -1 && bal <= 1)
		return;

	d = bal < -1;

	if ((d ? 1 : -1) * (avl_balance(n->link[d])) > 0)
		aavl_rotate(&n->link[d], d, fix);

	aavl_rotate(p, !d, fix);
}

void
aavl_insert(struct avl_node *x, struct avl_node **p, avl_comp_fn cmp, avl_fixup_fn fix)
{
	struct avl_node *n;

	if ((n = *p) == NULL) {
		x->link[0] = x->link[1] = NULL;
		x->height = 1;
		*p = x;
		fix(*p);
		return;
	}
	aavl_insert(x, &n->link[cmp(x, n) > 0], cmp, fix);
	aavl_rebalance(p, fix);
}
