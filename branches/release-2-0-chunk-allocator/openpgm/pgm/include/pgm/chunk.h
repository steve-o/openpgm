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

#ifndef __PGM_CHUNK_H__
#define __PGM_CHUNK_H__

#include <glib.h>

typedef gpointer			pgm_atom;
typedef struct pgm_chunk		pgm_chunk;
typedef struct pgm_allocator		pgm_allocator;

#ifndef __PGM_SKBUFF_H__
#	include <pgm/skbuff.h>
#endif


struct pgm_chunk
{
	pgm_chunk*	next;
	gulong		index;
	gchar		data[];
};

struct pgm_allocator
{
	gulong		atom_size;
	gulong		chunk_size;
	pgm_chunk*	current;
	pgm_chunk*	chunks;
};

G_BEGIN_DECLS

struct pgm_sk_buff_t* pgm_chunk_alloc_skb (pgm_allocator*, guint16);
gboolean pgm_chunk_is_last_skb (pgm_allocator*, pgm_chunk*, struct pgm_sk_buff_t*);

static inline pgm_chunk* pgm_chunk_get_current_chunk (pgm_allocator* allocator) {
	return allocator->current;
}
static inline void pgm_chunk_free (pgm_chunk* chunk) {
	g_free (chunk);
}

gboolean pgm_allocator_create (pgm_allocator*, guint, gulong);
void pgm_allocator_destroy (pgm_allocator*);

G_END_DECLS

#endif /* __PGM_CHUNK_H__ */

/* eof */
