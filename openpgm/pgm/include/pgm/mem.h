/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable fail fast memory allocation.
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

#ifndef __PGM_MEM_H__
#define __PGM_MEM_H__

#include <alloca.h>
#include <stdbool.h>

#include <glib.h>


G_BEGIN_DECLS

extern bool pgm_mem_gc_friendly;

void* pgm_malloc (size_t) G_GNUC_MALLOC;
void* pgm_malloc_n (size_t, size_t) G_GNUC_MALLOC;
void* pgm_malloc0 (size_t) G_GNUC_MALLOC;
void* pgm_malloc0_n (size_t, size_t) G_GNUC_MALLOC;
void* pgm_memdup (const void*, size_t) G_GNUC_MALLOC;
void pgm_free (gpointer);

/* Convenience memory allocators that wont work well above 32-bit sizes
 */
#define pgm_new(struct_type, n_structs) \
	((struct_type*)pgm_malloc_n ((size_t)sizeof(struct_type), (size_t)(n_structs)))
#define pgm_new0(struct_type, n_structs) \
	((struct_type*)pgm_malloc0_n ((size_t)sizeof(struct_type), (size_t)(n_structs)))

#define pgm_alloca(size) \
	alloca (size)
#define pgm_newa(struct_type, n_structs) \
	((struct_type*) pgm_alloca (sizeof(struct_type) * (size_t)(n_structs)))

void pgm_mem_init (void);
void pgm_mem_shutdown (void);

G_END_DECLS

#endif /* __PGM_MEM_H__ */
