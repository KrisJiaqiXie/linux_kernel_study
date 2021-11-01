/*
 * Copyright (C) 2006-2012 B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich, Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "hash.h"

/* clears the hash */
static void hash_init(struct hashtable_t *hash)
{
	uint32_t i;

	for (i = 0 ; i < hash->size; i++) {
		INIT_HLIST_HEAD(&hash->table[i]);
		spin_lock_init(&hash->list_locks[i]);
	}
}

/* free only the hashtable and the hash itself. */
void hash_destroy(struct hashtable_t *hash)
{
	kfree(hash->list_locks);
	kfree(hash->table);
	kfree(hash);
}

/* allocates and clears the hash */
struct hashtable_t *hash_new(uint32_t size)
{
	struct hashtable_t *hash;

	hash = kmalloc(sizeof(*hash), GFP_ATOMIC);
	if (!hash)
		return NULL;

	hash->table = kmalloc(sizeof(*hash->table) * size, GFP_ATOMIC);
	if (!hash->table)
		goto free_hash;

	hash->list_locks = kmalloc(sizeof(*hash->list_locks) * size,
				   GFP_ATOMIC);
	if (!hash->list_locks)
		goto free_table;

	hash->size = size;
	hash_init(hash);
	return hash;

free_table:
	kfree(hash->table);
free_hash:
	kfree(hash);
	return NULL;
}
