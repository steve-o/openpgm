/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable doubly-linked list.
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
#ifndef __PGM_IMPL_LIST_H__
#define __PGM_IMPL_LIST_H__

#include <pgm/types.h>
#include <pgm/list.h>

PGM_BEGIN_DECLS

PGM_GNUC_INTERNAL pgm_list_t* pgm_list_append (pgm_list_t*restrict, void*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_list_t* pgm_list_prepend_link (pgm_list_t*restrict, pgm_list_t*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_list_t* pgm_list_remove_link (pgm_list_t*, pgm_list_t*) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_list_t* pgm_list_delete_link (pgm_list_t*, pgm_list_t*) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL pgm_list_t* pgm_list_last (pgm_list_t*) PGM_GNUC_PURE PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL unsigned pgm_list_length (pgm_list_t*) PGM_GNUC_PURE PGM_GNUC_WARN_UNUSED_RESULT;

PGM_END_DECLS

#endif /* __PGM_IMPL_LIST_H__ */
