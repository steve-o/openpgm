/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable singly-linked list.
 *
 * Copyright (c) 2010-2011 Miru Limited.
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

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <impl/framework.h>


//#define SLIST_DEBUG

PGM_GNUC_INTERNAL
pgm_slist_t*
pgm_slist_append (
	pgm_slist_t* restrict list,
	void*	     restrict data
	)
{
	pgm_slist_t* new_list;
	pgm_slist_t* last;

	new_list = pgm_new (pgm_slist_t, 1);
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

PGM_GNUC_INTERNAL
pgm_slist_t*
pgm_slist_prepend (
	pgm_slist_t* restrict list,
	void*	     restrict data
	)
{
	pgm_slist_t *new_list;

	new_list = pgm_new (pgm_slist_t, 1);
	new_list->data = data;
	new_list->next = list;

	return new_list;
}

PGM_GNUC_INTERNAL
pgm_slist_t*
pgm_slist_prepend_link (
	pgm_slist_t* restrict list,
	pgm_slist_t* restrict link_
	)
{
	pgm_slist_t *new_list;

	new_list = link_;
	new_list->next = list;

	return new_list;
}

PGM_GNUC_INTERNAL
pgm_slist_t*
pgm_slist_remove (
	pgm_slist_t* restrict list,
	const void*  restrict data
	)
{
	pgm_slist_t *tmp = list, *prev = NULL;

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

PGM_GNUC_INTERNAL
pgm_slist_t* 
pgm_slist_remove_first (
	pgm_slist_t*	list
	)
{
	pgm_slist_t *tmp;

	if (PGM_LIKELY (NULL != list))
	{
		tmp = list->next;
		list->data = NULL;
		list->next = NULL;
		return tmp;
	}
	else
		return NULL;
}

PGM_GNUC_INTERNAL
void
pgm_slist_free (
	pgm_slist_t*	list
	)
{
	while (list)
	{
		pgm_slist_t* current = list;
		list = list->next;
		pgm_free (current);
	}
}

PGM_GNUC_INTERNAL
pgm_slist_t*
pgm_slist_last (
	pgm_slist_t*	list
	)
{
	if (PGM_LIKELY (NULL != list))
	{
		while (list->next)
			list = list->next;
	}

	return list;
}

PGM_GNUC_INTERNAL
unsigned
pgm_slist_length (
	pgm_slist_t*	list
	)
{
	unsigned length = 0;

	while (list)
	{
		length++;
		list = list->next;
	}

	return length;
}

/* eof */
