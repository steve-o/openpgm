/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * window optimised SKB chunk allocator.
 *
 * Copyright (c) 2009 Miru Limited.
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

#include <glib.h>

#ifdef G_OS_WIN32
#endif

#include "pgm/chunk.h"


//#define CHUNK_DEBUG

#ifndef CHUNK_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* allocate a new atom from a chunk
 */

static
pgm_atom
pgm_atom_alloc (
	pgm_allocator*		allocator
	)
{
	g_return_val_if_fail (allocator != NULL, NULL);

	g_trace ("pgm_atom_alloc ()");

	if ((!allocator->current) ||
	    ((allocator->current->index + allocator->atom_size) > allocator->chunk_size))
	{
		if (allocator->current)
			allocator->current = allocator->current->next = g_malloc (sizeof (pgm_chunk) + allocator->chunk_size);
		else
			allocator->chunks = allocator->current = g_malloc (sizeof (pgm_chunk) + allocator->chunk_size);
		allocator->current->next = NULL;
		allocator->current->index = 0;
	}
  
	gpointer atom = &allocator->current->data[allocator->current->index];
	allocator->current->index += allocator->atom_size;
	return atom;
}

/* allocate a new zeroed atom from a chunk
 */

static
pgm_atom
pgm_atom_alloc0 (
	pgm_allocator*		allocator
	)
{
	pgm_atom atom = pgm_atom_alloc (allocator);
	memset (atom, 0, allocator->atom_size);
	return atom;
}

static
gboolean
pgm_chunk_is_last_atom (
	pgm_allocator*		allocator,
	pgm_atom		atom
	)
{
	g_assert (NULL != allocator->chunks);
	const gulong data_index = allocator->atom_size + (gchar*)atom - (gchar*)allocator->chunks->data;
	g_assert (data_index <= allocator->chunk_size);
	return (data_index + allocator->atom_size) > allocator->chunk_size;
}

#define pgm_atom_new (type, chunk)	( \
	(type *) pgm_atom_alloc (chunk) \
)
#define pgm_atom_new0 (type, chunk)	( \
	(type *) pgm_atom_alloc0 (chunk) \
)

/* create a new skb from an atom
 */

struct pgm_sk_buff_t*
pgm_chunk_alloc_skb (
	pgm_allocator*		allocator
	)
{
	struct pgm_sk_buff_t* skb = pgm_atom_alloc (allocator);
	memset (skb, 0, sizeof(struct pgm_sk_buff_t));
	skb->truesize = allocator->atom_size;
	g_atomic_int_set (&skb->users, 1);
	skb->head = skb + 1;
	skb->data = skb->tail = skb->head;
	skb->end  = (guint8*)skb->data + (skb->truesize - sizeof(struct pgm_sk_buff_t));
	return skb;
}

gboolean
pgm_chunk_is_last_skb (
	pgm_allocator*		allocator,
	struct pgm_sk_buff_t*	skb
	)
{
	return pgm_chunk_is_last_atom (allocator, (pgm_atom)skb);
}

/* frees the last chunk in the allocator */
void
pgm_chunk_free (
	pgm_allocator*		allocator
	)
{
	g_assert (NULL != allocator->chunks);
	g_trace ("pgm_chunk_free ()");

	pgm_chunk* next_chunk = allocator->chunks->next;
	g_free (allocator->chunks);
	allocator->chunks = next_chunk;
}

/* create a new allocator object which manages it's own buffer.
 *
 * on success returns allocator object, on invalid parameters returns NULL.
 */

gboolean
pgm_allocator_create (
	pgm_allocator*	allocator,
	guint		atom_size,
	gulong		chunk_size
	)
{
	g_return_val_if_fail (allocator != NULL, FALSE);
	g_return_val_if_fail (atom_size > 0, FALSE);
	g_return_val_if_fail (chunk_size >= atom_size, FALSE);

	g_trace ("pgm_allocator_create ()");

	allocator->current = NULL;
	allocator->chunks = NULL;
	allocator->atom_size = (atom_size + (G_MEM_ALIGN - 1)) & ~(G_MEM_ALIGN - 1);
	allocator->chunk_size = chunk_size;
	return TRUE;
}

/* destroys an allocator and all allocated chunks.
 */

void
pgm_allocator_destroy (
	pgm_allocator*	allocator
	)
{
	g_return_if_fail (allocator != NULL);

	g_trace("pgm_allocator_destroy ()");

	pgm_chunk* chunks = allocator->chunks;
	while (chunks) {
		pgm_chunk* temp_chunk = chunks;
		chunks = chunks->next;
		g_free (temp_chunk);
	}
}

/* eof */
