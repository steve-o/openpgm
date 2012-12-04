/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable double-ended queue.
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


//#define QUEUE_DEBUG

PGM_GNUC_INTERNAL
bool
pgm_queue_is_empty (
	const pgm_queue_t*const queue
	)
{
	pgm_return_val_if_fail (queue != NULL, TRUE);

	return queue->head == NULL;
}

PGM_GNUC_INTERNAL
void
pgm_queue_push_head_link (
	pgm_queue_t* restrict queue,
	pgm_list_t*  restrict head_link
	)
{
	pgm_return_if_fail (queue != NULL);
	pgm_return_if_fail (head_link != NULL);
	pgm_return_if_fail (head_link->prev == NULL);
	pgm_return_if_fail (head_link->next == NULL);

	head_link->next = queue->head;
	if (queue->head)
		queue->head->prev = head_link;
	else
		queue->tail = head_link;
	queue->head = head_link;
	queue->length++;
}

PGM_GNUC_INTERNAL
pgm_list_t*
pgm_queue_pop_tail_link (
	pgm_queue_t*	queue
	)
{
	pgm_return_val_if_fail (queue != NULL, NULL);

	if (queue->tail)
	{
		pgm_list_t *node = queue->tail;

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

PGM_GNUC_INTERNAL
pgm_list_t*
pgm_queue_peek_tail_link (
	pgm_queue_t*	queue
	)
{
	pgm_return_val_if_fail (queue != NULL, NULL);

	return queue->tail;
}

PGM_GNUC_INTERNAL
void
pgm_queue_unlink (
	pgm_queue_t* restrict queue,
	pgm_list_t*  restrict target_link
	)
{
	pgm_return_if_fail (queue != NULL);
	pgm_return_if_fail (target_link != NULL);

	if (target_link == queue->tail)
		queue->tail = queue->tail->prev;
  
	queue->head = pgm_list_remove_link (queue->head, target_link);
	queue->length--;
}

/* eof */
