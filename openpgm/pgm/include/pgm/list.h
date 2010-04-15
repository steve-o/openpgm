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

#ifndef __PGM_LIST_H__
#define __PGM_LIST_H__

#include <glib.h>


struct pgm_list_t
{
	void*	 		data;
	struct pgm_list_t*	next;
	struct pgm_list_t*	prev;
};

typedef struct pgm_list_t pgm_list_t;


G_BEGIN_DECLS

pgm_list_t* pgm_list_append (pgm_list_t* restrict, void* restrict) G_GNUC_WARN_UNUSED_RESULT;
pgm_list_t* pgm_list_prepend_link (pgm_list_t* restrict, pgm_list_t* restrict) G_GNUC_WARN_UNUSED_RESULT;
pgm_list_t* pgm_list_remove_link (pgm_list_t*, pgm_list_t*) G_GNUC_WARN_UNUSED_RESULT;
pgm_list_t* pgm_list_delete_link (pgm_list_t*, pgm_list_t*) G_GNUC_WARN_UNUSED_RESULT;
pgm_list_t* pgm_list_last (pgm_list_t*);
size_t pgm_list_length (pgm_list_t*);


G_END_DECLS

#endif /* __PGM_LIST_H__ */
