// Copyright 2018 Schibsted

#ifndef HEAP_H
#define HEAP_H

#ifndef HEAP_TYPE_PREFIX
#define HEAP_TYPE_PREFIX struct
#endif
#ifndef HEAP_FUNCTION_PREFIX
#define HEAP_FUNCTION_PREFIX
#endif

#define HEAP_HEAD(name, type) 						\
struct name {								\
	HEAP_TYPE_PREFIX type *hh_root;					\
	long hh_num;							\
}

#define HEAP_ENTRY(type)						\
	struct {							\
		HEAP_TYPE_PREFIX type *he_left;				\
		HEAP_TYPE_PREFIX type *he_right;			\
	}

#define HEAP_ENTRY_INITIALIZER						\
	{ NULL, NULL }

#define HEAP_INIT(head)	do {						\
	(head)->hh_root = NULL;						\
	(head)->hh_num = 0;						\
} while (0)

#define HEAP_FIRST(head) ((head)->hh_root)
#define HEAP_LEFT(elm, field) ((elm)->field.he_left)
#define HEAP_RIGHT(elm, field) ((elm)->field.he_right)

#define HEAP_EMPTY(head) ((head)->hh_root == NULL)

#define HEAP_INSERT(name, head, item) name##_HEAP_INSERT(head, item)
#define HEAP_REMOVE_HEAD(name, head) name##_HEAP_REMOVE_HEAD(head)
#define HEAP_UPDATE_HEAD(name, head) name##_HEAP_UPDATE_HEAD(head)

#define HEAP_PROTOTYPE(name, type, field, cmp)				\
void    name##_HEAP_linkin(HEAP_TYPE_PREFIX type **pp, long n, HEAP_TYPE_PREFIX type *e);\
HEAP_TYPE_PREFIX type ** name##_HEAP_link(HEAP_TYPE_PREFIX type **pp, int n);\
void	name##_HEAP_INSERT(struct name *, HEAP_TYPE_PREFIX type *);	\
void	name##_HEAP_REMOVE_HEAD(struct name *);				\
void	name##_HEAP_UPDATE_HEAD(struct name *);

#define HEAP_GENERATE(name, type, field, cmp)				\
									\
HEAP_FUNCTION_PREFIX void						\
name##_HEAP_linkin(HEAP_TYPE_PREFIX type **pp, long n, HEAP_TYPE_PREFIX type *e)\
{									\
	if (n == 1) {							\
		*pp = e;						\
		return;							\
	}								\
									\
	if (n & 1) {							\
		name##_HEAP_linkin(&((*pp)->field.he_left), n >> 1, e);\
		if (cmp((*pp), e) > 0) {				\
			HEAP_TYPE_PREFIX type *r = (*pp)->field.he_right;\
			(*pp)->field.he_left = e->field.he_left;	\
			(*pp)->field.he_right = e->field.he_right;	\
			e->field.he_left = *pp;				\
			e->field.he_right = r;				\
			*pp = e;					\
		}							\
	} else {							\
		name##_HEAP_linkin(&((*pp)->field.he_right), n >> 1, e);\
		if (cmp((*pp), e) > 0) {				\
			HEAP_TYPE_PREFIX type *l = (*pp)->field.he_left;\
			(*pp)->field.he_left = e->field.he_left;	\
			(*pp)->field.he_right = e->field.he_right;	\
			e->field.he_right = *pp;			\
			e->field.he_left = l;				\
			*pp = e;					\
		}							\
	}								\
									\
}									\
									\
HEAP_FUNCTION_PREFIX void						\
name##_HEAP_INSERT(struct name *head, HEAP_TYPE_PREFIX type *el)	\
{									\
	el->field.he_left = el->field.he_right = NULL;			\
	name##_HEAP_linkin(&head->hh_root, ++head->hh_num, el);		\
}									\
									\
HEAP_FUNCTION_PREFIX HEAP_TYPE_PREFIX type **				\
name##_HEAP_link(HEAP_TYPE_PREFIX type **pp, int n)			\
{									\
	for (; n != 1; n >>= 1)						\
		pp = (n & 1) ? &(*pp)->field.he_left : 			\
		    &(*pp)->field.he_right;				\
	return (pp);							\
}									\
									\
HEAP_FUNCTION_PREFIX void						\
name##_HEAP_REMOVE_HEAD(struct name *head)				\
{									\
	HEAP_TYPE_PREFIX type **pp, *el, *r;				\
									\
	pp = name##_HEAP_link(&head->hh_root, head->hh_num);		\
	el = *pp;							\
	r = head->hh_root;						\
	head->hh_root = el;						\
	*pp = NULL;							\
	el->field.he_left = r->field.he_left;				\
	el->field.he_right = r->field.he_right;				\
	head->hh_num--;							\
	name##_HEAP_UPDATE_HEAD(head);					\
}									\
									\
HEAP_FUNCTION_PREFIX void						\
name##_HEAP_UPDATE_HEAD(struct name *head)				\
{									\
	HEAP_TYPE_PREFIX type **pp;					\
									\
	if (head->hh_root == NULL)					\
		return;							\
									\
	for (pp = &head->hh_root; (*pp)->field.he_right != NULL;) {	\
		HEAP_TYPE_PREFIX type *r = (*pp)->field.he_right;	\
		HEAP_TYPE_PREFIX type *l = (*pp)->field.he_left;	\
									\
		if (l != NULL && cmp(l, r) <= 0) {			\
			if (cmp((*pp), l) <= 0)				\
				return;					\
			(*pp)->field = l->field;			\
			l->field.he_left = *pp;				\
			l->field.he_right = r;				\
			*pp = l;					\
			pp = &(*pp)->field.he_left;			\
		} else {						\
			if (cmp((*pp), r) <= 0)				\
				return;					\
			(*pp)->field = r->field;			\
			r->field.he_left = l;				\
			r->field.he_right = *pp;			\
			*pp = r;					\
			pp = &(*pp)->field.he_right;			\
		}							\
	}								\
}

#endif /*HEAP_H*/

