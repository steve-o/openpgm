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

#include <glib.h>

#include "pgm/malloc.h"
#include "pgm/slist.h"


//#define SLIST_DEBUG

#ifndef SLIST_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


PGMSList*
pgm_slist_append (
	PGMSList*	list,
	gpointer	data
	)
{
	PGMSList* new_list;
	PGMSList* last;

	new_list = pgm_new (PGMSList, 1);
	new_list->data = data;
	new_list->next = NULL;

	if (list)
	{
		last = pgm_slist_last (list);
		last->next = new_list;
		return list;
	}
	else
		return new_list;
}

PGMSList*
pgm_slist_prepend_link (
	PGMSList*	list,
	PGMSList*	link_
	)
{
	PGMSList *new_list;

	new_list = link_;
	new_list->next = list;

	return new_list;
}

PGMSList*
pgm_slist_remove (
	PGMSList*	list,
	gconstpointer	data
	)
{
	PGMSList *tmp = list, *prev = NULL;

	while (tmp)
	{
		if (tmp->data == data)
		{
			if (prev)
				prev->next = tmp->next;
			else
				list = tmp->next;
			pgm_free (tmp);
			break;
		}
		prev = tmp;
		tmp = prev->next;
	}

	return list;
}

PGMSList* 
pgm_slist_remove_first (
	PGMSList*	list
	)
{
	PGMSList *tmp;

	if (G_LIKELY (list))
	{
		tmp = list->next;
		list->data = NULL;
		list->next = NULL;
		return tmp;
	}
	else
		return NULL;
}

PGMSList*
pgm_slist_last (
	PGMSList*	list
	)
{
	if (G_LIKELY (list))
	{
		while (list->next)
			list = list->next;
	}

	return list;
}

guint
pgm_slist_length (
	PGMSList*	list
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
