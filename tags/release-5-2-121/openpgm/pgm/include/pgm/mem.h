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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_MEM_H__
#define __PGM_MEM_H__

#ifdef HAVE_ALLOCA_H
#	include <alloca.h>
#elif defined(_WIN32)
#	include <malloc.h>
#else
#	include <stdlib.h>
#endif
#include <pgm/types.h>

PGM_BEGIN_DECLS

extern bool pgm_mem_gc_friendly;

void* pgm_malloc (const size_t) PGM_GNUC_MALLOC PGM_GNUC_ALLOC_SIZE(1);
void* pgm_malloc_n (const size_t, const size_t) PGM_GNUC_MALLOC PGM_GNUC_ALLOC_SIZE2(1, 2);
void* pgm_malloc0 (const size_t) PGM_GNUC_MALLOC PGM_GNUC_ALLOC_SIZE(1);
void* pgm_malloc0_n (const size_t, const size_t) PGM_GNUC_MALLOC PGM_GNUC_ALLOC_SIZE2(1, 2);
void* pgm_memdup (const void*, const size_t) PGM_GNUC_MALLOC;
void* pgm_realloc (void*, const size_t) PGM_GNUC_WARN_UNUSED_RESULT;
void pgm_free (void*);

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

PGM_END_DECLS

#endif /* __PGM_MEM_H__ */
