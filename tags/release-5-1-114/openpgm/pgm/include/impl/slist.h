/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable singly-linked list.
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
#       error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_SLIST_H__
#define __PGM_IMPL_SLIST_H__

typedef struct pgm_slist_t pgm_slist_t;

#include <pgm/types.h>

PGM_BEGIN_DECLS

struct pgm_slist_t
{
	void*		    restrict data;
	struct pgm_slist_t* restrict next;
};

PGM_GNUC_INTERNAL pgm_slist_t* pgm_slist_append (pgm_slist_t*restrict, void*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_slist_t* pgm_slist_prepend (pgm_slist_t*restrict, void*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_slist_t* pgm_slist_prepend_link (pgm_slist_t*restrict, pgm_slist_t*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_slist_t* pgm_slist_remove (pgm_slist_t*restrict, const void*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_slist_t* pgm_slist_remove_first (pgm_slist_t*) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_slist_free (pgm_slist_t*);
PGM_GNUC_INTERNAL pgm_slist_t* pgm_slist_last (pgm_slist_t*) PGM_GNUC_PURE PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL unsigned pgm_slist_length (pgm_slist_t*) PGM_GNUC_PURE PGM_GNUC_WARN_UNUSED_RESULT;

PGM_END_DECLS

#endif /* __PGM_IMPL_SLIST_H__ */
