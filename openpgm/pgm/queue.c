/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable double-ended queue.
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
#include "pgm/list.h"
#include "pgm/queue.h"


//#define QUEUE_DEBUG

#ifndef QUEUE_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


gboolean
pgm_queue_is_empty (
	pgm_queue_t*	queue
	)
{
	g_return_val_if_fail (queue != NULL, TRUE);

	return queue->head == NULL;
}

void
pgm_queue_push_head_link (
	pgm_queue_t*	queue,
	PGMList*	link
	)
{
	g_return_if_fail (queue != NULL);
	g_return_if_fail (link != NULL);
	g_return_if_fail (link->prev == NULL);
	g_return_if_fail (link->next == NULL);

	link->next = queue->head;
	if (queue->head)
		queue->head->prev = link;
	else
		queue->tail = link;
	queue->head = link;
	queue->length++;
}

PGMList*
pgm_queue_pop_tail_link (
	pgm_queue_t*	queue
	)
{
	g_return_val_if_fail (queue != NULL, NULL);

	if (queue->tail)
	{
		PGMList *node = queue->tail;

		queue->tail = node->prev;
		if (queue->tail)
		{
			queue->tail->next = NULL;
			node->prev = NULL;
		}
		else
			queue->head = NULL;
		queue->length--;

		return node;
	}
  
	return NULL;
}

PGMList*
pgm_queue_peek_tail_link (
	pgm_queue_t*	queue
	)
{
	g_return_val_if_fail (queue != NULL, NULL);

	return queue->tail;
}

void
pgm_queue_unlink (
	pgm_queue_t*	queue,
	PGMList*	link_
	)
{
	g_return_if_fail (queue != NULL);
	g_return_if_fail (link_ != NULL);

	if (link_ == queue->tail)
		queue->tail = queue->tail->prev;
  
	queue->head = pgm_list_remove_link (queue->head, link_);
	queue->length--;
}

/* eof */
