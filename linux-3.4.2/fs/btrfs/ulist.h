/*
 * Copyright (C) 2011 STRATO AG
 * written by Arne Jansen <sensille@gmx.net>
 * Distributed under the GNU GPL license version 2.
 *
 */

#ifndef __ULIST__
#define __ULIST__

/*
 * ulist is a generic data structure to hold a collection of unique u64
 * values. The only operations it supports is adding to the list and
 * enumerating it.
 * It is possible to store an auxiliary value along with the key.
 *
 * The implementation is preliminary and can probably be sped up
 * significantly. A first step would be to store the values in an rbtree
 * as soon as ULIST_SIZE is exceeded.
 */

/*
 * number of elements statically allocated inside struct ulist
 */
#define ULIST_SIZE 16

/*
 * element of the list
 */
struct ulist_node {
	u64 val;		/* value to store */
	unsigned long aux;	/* auxiliary value saved along with the val */
};

struct ulist {
	/*
	 * number of elements stored in list
	 */
	unsigned long nnodes;

	/*
	 * number of nodes we already have room for
	 */
	unsigned long nodes_alloced;

	/*
	 * pointer to the array storing the elements. The first ULIST_SIZE
	 * elements are stored inline. In this case the it points to int_nodes.
	 * After exceeding ULIST_SIZE, dynamic memory is allocated.
	 */
	struct ulist_node *nodes;

	/*
	 * inline storage space for the first ULIST_SIZE entries
	 */
	struct ulist_node int_nodes[ULIST_SIZE];
};

void ulist_init(struct ulist *ulist);
void ulist_fini(struct ulist *ulist);
void ulist_reinit(struct ulist *ulist);
struct ulist *ulist_alloc(unsigned long gfp_mask);
void ulist_free(struct ulist *ulist);
int ulist_add(struct ulist *ulist, u64 val, unsigned long aux,
	      unsigned long gfp_mask);
struct ulist_node *ulist_next(struct ulist *ulist, struct ulist_node *prev);

#endif
