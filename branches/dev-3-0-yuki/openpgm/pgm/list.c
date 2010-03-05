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

#include <glib.h>

#include "pgm/mem.h"
#include "pgm/list.h"


//#define LIST_DEBUG

#ifndef LIST_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


pgm_list_t*
pgm_list_append (
	pgm_list_t*	list,
	gpointer	data
	)
{
	pgm_list_t* new_list;
	pgm_list_t* last;

	new_list = pgm_new (pgm_list_t, 1);
	new_list->data = data;
	new_list->next = NULL;

	if (list)
	{
		last = pgm_list_last (list);
		last->next = new_list;
		new_list->prev = last;
		return list;
	}
	else
	{
		new_list->prev = NULL;
		return new_list;
	}
}

pgm_list_t*
pgm_list_prepend_link (
	pgm_list_t*	list,
	pgm_list_t*	link_
	)
{
	g_return_val_if_fail (NULL != link_, list);

	pgm_list_t* new_list = link_;

	new_list->next = list;
	new_list->prev = NULL;

	if (list)
		list->prev = new_list;

	return new_list;
}

static inline
pgm_list_t* 
_pgm_list_remove_link (
	pgm_list_t*	list,
	pgm_list_t*	link_
	)
{
	if (G_LIKELY (link_))
	{
		if (link_->prev)
			link_->prev->next = link_->next;
		if (link_->next)
			link_->next->prev = link_->prev;

		if (link_ == list)
			list = list->next;

		link_->next = link_->prev = NULL;
	}
	return list;
}

pgm_list_t*
pgm_list_remove_link (
	pgm_list_t*	list,
	pgm_list_t*	link_
	)
{
	return _pgm_list_remove_link (list, link_);
}

pgm_list_t*
pgm_list_delete_link (
	pgm_list_t*	list,
	pgm_list_t*	link_
	)
{
	pgm_list_t* new_list = _pgm_list_remove_link (list, link_);
	pgm_free (link_);

	return new_list;
}

pgm_list_t*
pgm_list_last (
	pgm_list_t*	list
	)
{
	if (G_LIKELY (list)) {
		while (list->next)
			list = list->next;
	}
	return list;
}

guint
pgm_list_length (
	pgm_list_t*	list
	)
{
	guint length = 0;

	while (list)
	{
		length++;
		list = list->next;
	}

	return length;
}

/* eof */
