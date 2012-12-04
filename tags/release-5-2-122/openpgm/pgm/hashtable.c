/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable hashtable.
 *
 * Copyright (c) 2010-2011 Miru Limited.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <impl/framework.h>


//#define HASHTABLE_DEBUG

#define HASHTABLE_MIN_SIZE	11
#define HASHTABLE_MAX_SIZE	13845163

struct pgm_hashnode_t
{
	const void*		key;
	void*			value;
	struct pgm_hashnode_t*	next;
	uint_fast32_t		key_hash;
};

typedef struct pgm_hashnode_t pgm_hashnode_t;

struct pgm_hashtable_t
{
	unsigned		size;
	unsigned		nnodes;
	pgm_hashnode_t**	nodes;
	pgm_hashfunc_t		hash_func;
	pgm_equalfunc_t		key_equal_func;
};

#define PGM_HASHTABLE_RESIZE(hash_table) \
	do { \
		if ( (hash_table->size >= 3 * hash_table->nnodes && hash_table->size > HASHTABLE_MIN_SIZE) || \
		     (3 * hash_table->size <= hash_table->nnodes && hash_table->size < HASHTABLE_MAX_SIZE) ) \
		{ \
			pgm_hashtable_resize (hash_table); \
		} \
	} while (0)

static void pgm_hashtable_resize (pgm_hashtable_t*);
static pgm_hashnode_t** pgm_hashtable_lookup_node (const pgm_hashtable_t*restrict, const void*restrict, pgm_hash_t*restrict) PGM_GNUC_PURE;
static pgm_hashnode_t* pgm_hash_node_new (const void*restrict, void*restrict, const pgm_hash_t);
static void pgm_hash_node_destroy (pgm_hashnode_t*);
static void pgm_hash_nodes_destroy (pgm_hashnode_t*);

PGM_GNUC_INTERNAL
pgm_hashtable_t*
pgm_hashtable_new (
	pgm_hashfunc_t	hash_func,
	pgm_equalfunc_t	key_equal_func
	)
{
	pgm_return_val_if_fail (NULL != hash_func, NULL);
	pgm_return_val_if_fail (NULL != key_equal_func, NULL);

	pgm_hashtable_t *hash_table;
  
	hash_table = pgm_new (pgm_hashtable_t, 1);
	hash_table->size               = HASHTABLE_MIN_SIZE;
	hash_table->nnodes             = 0;
	hash_table->hash_func          = hash_func;
	hash_table->key_equal_func     = key_equal_func;
	hash_table->nodes              = pgm_new0 (pgm_hashnode_t*, hash_table->size);
  
	return hash_table;
}

PGM_GNUC_INTERNAL
void
pgm_hashtable_unref (
	pgm_hashtable_t*	hash_table
	)
{
	pgm_return_if_fail (hash_table != NULL);

	for (unsigned i = 0; i < hash_table->size; i++)
		pgm_hash_nodes_destroy (hash_table->nodes[i]);
	pgm_free (hash_table->nodes);
	pgm_free (hash_table);
}

PGM_GNUC_INTERNAL
void
pgm_hashtable_destroy (
	pgm_hashtable_t*	hash_table
	)
{
	pgm_return_if_fail (hash_table != NULL);
  
	pgm_hashtable_remove_all (hash_table);
	pgm_hashtable_unref (hash_table);
}

static inline
pgm_hashnode_t**
pgm_hashtable_lookup_node (
	const pgm_hashtable_t* restrict hash_table,
	const void*	       restrict key,
	pgm_hash_t*	       restrict hash_return	/* non-NULL to return hash value */
	)
{
	const pgm_hash_t hash_value = (*hash_table->hash_func) (key);
	pgm_hashnode_t** node = &hash_table->nodes[hash_value % hash_table->size];
  
	if (hash_return)
		*hash_return = hash_value;
  
	while (*node && (((*node)->key_hash != hash_value) ||
                     !(*hash_table->key_equal_func) ((*node)->key, key)))
	{
		node = &(*node)->next;
	}

	return node;
}

PGM_GNUC_INTERNAL
void*
pgm_hashtable_lookup (
	const pgm_hashtable_t* restrict hash_table,
	const void*	       restrict key
	)
{
	pgm_return_val_if_fail (hash_table != NULL, NULL);
  
	const pgm_hashnode_t* node = *pgm_hashtable_lookup_node (hash_table, key, NULL);
	return node ? node->value : NULL;
}

PGM_GNUC_INTERNAL
void*
pgm_hashtable_lookup_extended (
	const pgm_hashtable_t* restrict hash_table,
	const void*	       restrict key,
	void*		       restrict hash_return
	)
{
	pgm_return_val_if_fail (hash_table != NULL, NULL);
  
	const pgm_hashnode_t* node = *pgm_hashtable_lookup_node (hash_table, key, hash_return);
	return node ? node->value : NULL;
}

PGM_GNUC_INTERNAL
void
pgm_hashtable_insert (
	pgm_hashtable_t* restrict hash_table,
	const void*	 restrict key,
	void*		 restrict value
	)
{
	pgm_hashnode_t **node;
	pgm_hash_t key_hash;
  
	pgm_return_if_fail (hash_table != NULL);
  
	node = pgm_hashtable_lookup_node (hash_table, key, &key_hash);
	pgm_return_if_fail (NULL == *node); 

	*node = pgm_hash_node_new (key, value, key_hash);
	hash_table->nnodes++;
	PGM_HASHTABLE_RESIZE (hash_table);
}

PGM_GNUC_INTERNAL
bool
pgm_hashtable_remove (
	pgm_hashtable_t* restrict hash_table,
	const void*	 restrict key
	)
{
	pgm_hashnode_t **node, *dest;
  
	pgm_return_val_if_fail (hash_table != NULL, FALSE);
  
	node = pgm_hashtable_lookup_node (hash_table, key, NULL);
	if (*node)
	{
		dest = *node;
		(*node) = dest->next;
		pgm_hash_node_destroy (dest);
		hash_table->nnodes--;
		PGM_HASHTABLE_RESIZE (hash_table);
		return TRUE;
	}
	return FALSE;
}

PGM_GNUC_INTERNAL
void
pgm_hashtable_remove_all (
	pgm_hashtable_t*	hash_table
	)
{
	pgm_return_if_fail (hash_table != NULL);

	for (unsigned i = 0; i < hash_table->size; i++)
	{
		pgm_hash_nodes_destroy (hash_table->nodes[i]);
		hash_table->nodes[i] = NULL;
	}
	hash_table->nnodes = 0;
	PGM_HASHTABLE_RESIZE (hash_table);
}

static
void
pgm_hashtable_resize (
	pgm_hashtable_t*	hash_table
	)
{
	const unsigned new_size = CLAMP (pgm_spaced_primes_closest (hash_table->nnodes),
					 HASHTABLE_MIN_SIZE, HASHTABLE_MAX_SIZE);
	pgm_hashnode_t** new_nodes = pgm_new0 (pgm_hashnode_t*, new_size);
  
	for (unsigned i = 0; i < hash_table->size; i++)
		for (pgm_hashnode_t *node = hash_table->nodes[i], *next; node; node = next)
		{
			next = node->next;
			const pgm_hash_t hash_val = node->key_hash % new_size;
			node->next = new_nodes[hash_val];
			new_nodes[hash_val] = node;
		}
  
	pgm_free (hash_table->nodes);
	hash_table->nodes = new_nodes;
	hash_table->size = new_size;
}

static
pgm_hashnode_t*
pgm_hash_node_new (
	const void* restrict key,
	void* 	    restrict value,
	const pgm_hash_t     key_hash
	)
{
	pgm_hashnode_t *hash_node = pgm_new (pgm_hashnode_t, 1);
	hash_node->key = key;
	hash_node->value = value;
	hash_node->key_hash = key_hash;
	hash_node->next = NULL;
	return hash_node;
}

static
void
pgm_hash_node_destroy (
	pgm_hashnode_t*	hash_node
	)
{
	pgm_free (hash_node);
}

static
void
pgm_hash_nodes_destroy (
	pgm_hashnode_t*	hash_node
	)
{
	while (hash_node) {
		pgm_hashnode_t *next = hash_node->next;
		pgm_free (hash_node);
		hash_node = next;
	}
}

/* common hash value compare and hash key generation functions */

PGM_GNUC_INTERNAL
bool
pgm_str_equal (
	const void* restrict p1,
	const void* restrict p2
	)
{
	const char *restrict s1 = p1, *restrict s2 = p2;
	return (strcmp (s1, s2) == 0);
}

/* 31 bit hash function */

PGM_GNUC_INTERNAL
pgm_hash_t
pgm_str_hash (
	const void*	p
	)
{
	const char* s = p;
	pgm_hash_t hash_val = *s;

	if (PGM_LIKELY (hash_val))
		for (s++; *s; s++)
			hash_val = (hash_val << 5) - hash_val + *s;
	return hash_val;
}

PGM_GNUC_INTERNAL
bool
pgm_int_equal (
	const void* restrict p1,
	const void* restrict p2
	)
{
	const int i1 = *(const int*restrict)p1, i2 = *(const int*restrict)p2;
	return (i1 == i2);
}

PGM_GNUC_INTERNAL
pgm_hash_t
pgm_int_hash (
	const void*	p
	)
{
	const int i = *(const int*)p;
	return (pgm_hash_t)i;
}


/* eof */
