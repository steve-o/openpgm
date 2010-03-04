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

#ifndef __PGM_QUEUE_H__
#define __PGM_QUEUE_H__

#include <glib.h>

#ifndef __PGM_LIST_H__
#	include <pgm/list.h>
#endif


struct PGMQueue
{
	PGMList*	head;
	PGMList*	tail;
	guint		length;
};

typedef struct PGMQueue PGMQueue;


G_BEGIN_DECLS

gboolean pgm_queue_is_empty (PGMQueue*);
void pgm_queue_push_head_link (PGMQueue*, PGMList*);
PGMList* pgm_queue_pop_tail_link (PGMQueue*);
PGMList* pgm_queue_peek_tail_link (PGMQueue*);
void pgm_queue_unlink (PGMQueue*, PGMList*);


G_END_DECLS

#endif /* __PGM_QUEUE_H__ */
