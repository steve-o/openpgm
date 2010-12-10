/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable hash table.
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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_HASHTABLE_H__
#define __PGM_IMPL_HASHTABLE_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

typedef struct pgm_hashtable_t pgm_hashtable_t;
typedef uint_fast32_t pgm_hash_t;

typedef pgm_hash_t (*pgm_hashfunc_t) (const void*);
typedef bool (*pgm_equalfunc_t) (const void*restrict, const void*restrict);

PGM_GNUC_INTERNAL pgm_hashtable_t* pgm_hashtable_new (pgm_hashfunc_t, pgm_equalfunc_t);
PGM_GNUC_INTERNAL void pgm_hashtable_destroy (pgm_hashtable_t*);
PGM_GNUC_INTERNAL void pgm_hashtable_insert (pgm_hashtable_t*restrict, const void*restrict, void*restrict);
PGM_GNUC_INTERNAL bool pgm_hashtable_remove (pgm_hashtable_t*restrict, const void*restrict);
PGM_GNUC_INTERNAL void pgm_hashtable_remove_all (pgm_hashtable_t*);
PGM_GNUC_INTERNAL void* pgm_hashtable_lookup (const pgm_hashtable_t*restrict, const void*restrict);
PGM_GNUC_INTERNAL void* pgm_hashtable_lookup_extended (const pgm_hashtable_t*restrict, const void*restrict, void*restrict);
PGM_GNUC_INTERNAL void pgm_hashtable_unref (pgm_hashtable_t*);

/* Hash Functions
 */

PGM_GNUC_INTERNAL bool pgm_str_equal (const void*restrict, const void*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_hash_t pgm_str_hash (const void*) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_int_equal (const void*restrict, const void*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_hash_t pgm_int_hash (const void*) PGM_GNUC_WARN_UNUSED_RESULT;

PGM_END_DECLS

#endif /* __PGM_IMPL_HASHTABLE_H__ */
