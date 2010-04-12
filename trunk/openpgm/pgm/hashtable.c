/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable hashtable.
 *
 * Copyright (c) 2010 Miru Limited.
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

#include <string.h>

#include <glib.h>

#include "pgm/messages.h"
#include "pgm/atomic.h"
#include "pgm/mem.h"
#include "pgm/hashtable.h"
#include "pgm/math.h"


//#define HASHTABLE_DEBUG


#define HASH_TABLE_MIN_SIZE 11
#define HASH_TABLE_MAX_SIZE 13845163

struct pgm_hashnode_t
{
	gconstpointer		key;
	gpointer		value;
	struct pgm_hashnode_t*	next;
	guint			key_hash;
};

typedef struct pgm_hashnode_t pgm_hashnode_t;

struct pgm_hashtable_t
{
	gint			size;
	gint			nnodes;
	pgm_hashnode_t**	nodes;
	PGMHashFunc		hash_func;
	PGMEqualFunc		key_equal_func;
	volatile gint32		ref_count;
};

#define PGM_HASH_TABLE_RESIZE(hash_table)			\
   G_STMT_START {						\
     if ((hash_table->size >= 3 * hash_table->nnodes &&	        \
	  hash_table->size > HASH_TABLE_MIN_SIZE) ||		\
	 (3 * hash_table->size <= hash_table->nnodes &&	        \
	  hash_table->size < HASH_TABLE_MAX_SIZE))		\
	   pgm_hash_table_resize (hash_table);			\
   } G_STMT_END

static void pgm_hash_table_resize (pgm_hashtable_t	*);
static pgm_hashnode_t** pgm_hash_table_lookup_node (pgm_hashtable_t*, gconstpointer, guint*);
static pgm_hashnode_t* pgm_hash_node_new (gconstpointer, gpointer, guint);
static void pgm_hash_node_destroy (pgm_hashnode_t*);
static void pgm_hash_nodes_destroy (pgm_hashnode_t*);


pgm_hashtable_t*
pgm_hash_table_new (
	PGMHashFunc	hash_func,
	PGMEqualFunc	key_equal_func
	)
{
	pgm_return_val_if_fail (NULL != hash_func, NULL);
	pgm_return_val_if_fail (NULL != key_equal_func, NULL);

	pgm_hashtable_t *hash_table;
  
	hash_table = pgm_new (pgm_hashtable_t, 1);
	hash_table->size               = HASH_TABLE_MIN_SIZE;
	hash_table->nnodes             = 0;
	hash_table->hash_func          = hash_func;
	hash_table->key_equal_func     = key_equal_func;
	hash_table->ref_count          = 1;
	hash_table->nodes              = pgm_new0 (pgm_hashnode_t*, hash_table->size);
  
	return hash_table;
}

void
pgm_hash_table_unref (
	pgm_hashtable_t*	hash_table
	)
{
	pgm_return_if_fail (hash_table != NULL);
	pgm_return_if_fail (hash_table->ref_count > 0);

	if (pgm_atomic_int32_exchange_and_add (&hash_table->ref_count, -1) - 1 == 0)
	{
		for (int i = 0; i < hash_table->size; i++)
			pgm_hash_nodes_destroy (hash_table->nodes[i]);
		pgm_free (hash_table->nodes);
		pgm_free (hash_table);
	}
}

void
pgm_hash_table_destroy (
	pgm_hashtable_t*	hash_table
	)
{
	pgm_return_if_fail (hash_table != NULL);
	pgm_return_if_fail (hash_table->ref_count > 0);
  
	pgm_hash_table_remove_all (hash_table);
	pgm_hash_table_unref (hash_table);
}

static inline
pgm_hashnode_t**
pgm_hash_table_lookup_node (
	pgm_hashtable_t*	hash_table,
	gconstpointer		key,
	guint*			hash_return
	)
{
	pgm_hashnode_t **node;
	guint hash_value;

	hash_value = (* hash_table->hash_func) (key);
	node = &hash_table->nodes[hash_value % hash_table->size];
  
	if (hash_return)
		*hash_return = hash_value;
  
	while (*node && (((*node)->key_hash != hash_value) ||
                     !(*hash_table->key_equal_func) ((*node)->key, key)))
	{
		node = &(*node)->next;
	}

	return node;
}

gpointer
pgm_hash_table_lookup (
	pgm_hashtable_t*	hash_table,
	gconstpointer		key
	)
{
	pgm_hashnode_t *node;
  
	pgm_return_val_if_fail (hash_table != NULL, NULL);
  
	node = *pgm_hash_table_lookup_node (hash_table, key, NULL);
	return node ? node->value : NULL;
}

void
pgm_hash_table_insert (
	pgm_hashtable_t*	hash_table,
	gconstpointer		key,
	gpointer		value
	)
{
	pgm_hashnode_t **node;
	guint key_hash;
  
	pgm_return_if_fail (hash_table != NULL);
	pgm_return_if_fail (hash_table->ref_count > 0);
  
	node = pgm_hash_table_lookup_node (hash_table, key, &key_hash);
	pgm_return_if_fail (NULL == *node); 

	*node = pgm_hash_node_new (key, value, key_hash);
	hash_table->nnodes++;
	PGM_HASH_TABLE_RESIZE (hash_table);
}

gboolean
pgm_hash_table_remove (
	pgm_hashtable_t*	hash_table,
	gconstpointer		key
	)
{
	pgm_hashnode_t **node, *dest;
  
	pgm_return_val_if_fail (hash_table != NULL, FALSE);
  
	node = pgm_hash_table_lookup_node (hash_table, key, NULL);
	if (*node)
	{
		dest = *node;
		(*node) = dest->next;
		pgm_hash_node_destroy (dest);
		hash_table->nnodes--;
		PGM_HASH_TABLE_RESIZE (hash_table);
		return TRUE;
	}
	return FALSE;
}

void
pgm_hash_table_remove_all (
	pgm_hashtable_t*	hash_table
	)
{
	pgm_return_if_fail (hash_table != NULL);

	for (int i = 0; i < hash_table->size; i++)
	{
		pgm_hash_nodes_destroy (hash_table->nodes[i]);
		hash_table->nodes[i] = NULL;
	}
	hash_table->nnodes = 0;
	PGM_HASH_TABLE_RESIZE (hash_table);
}

static void
pgm_hash_table_resize (
	pgm_hashtable_t*	hash_table
	)
{
	gint new_size = pgm_spaced_primes_closest (hash_table->nnodes);
	new_size = CLAMP (new_size, HASH_TABLE_MIN_SIZE, HASH_TABLE_MAX_SIZE);
 
	pgm_hashnode_t** new_nodes = pgm_new0 (pgm_hashnode_t*, new_size);
  
	for (int i = 0; i < hash_table->size; i++)
		for (pgm_hashnode_t *node = hash_table->nodes[i], *next; node; node = next)
		{
			next = node->next;
			const guint hash_val = node->key_hash % new_size;
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
	gconstpointer	key,
	gpointer	value,
	guint		key_hash
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
	while (hash_node)
	{
		pgm_hashnode_t *next = hash_node->next;
		pgm_free (hash_node);
		hash_node = next;
	}
}

gboolean
pgm_str_equal (
	gconstpointer	v1,
	gconstpointer	v2
	)
{
	const gchar* string1 = v1;
	const gchar* string2 = v2;
	return strcmp (string1, string2) == 0;
}

/* 31 bit hash function */

guint
pgm_str_hash (
	gconstpointer	v
	)
{
	const signed char *p = v;
	guint32 h = *p;

	if (G_LIKELY (h))
		for (p += 1; *p != '\0'; p++)
			h = (h << 5) - h + *p;
	return h;
}

gboolean
pgm_int_equal (
	gconstpointer	v1,
	gconstpointer	v2
	)
{
	return *((const gint*) v1) == *((const gint*) v2);
}

guint
pgm_int_hash (
	gconstpointer	v
	)
{
	return *(const gint*) v;
}


/* eof */
