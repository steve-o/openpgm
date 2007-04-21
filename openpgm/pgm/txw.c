/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * A basic transmit window: pointer array implementation.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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


#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>


#include <glib.h>

#include "txw.h"

#if 0
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


struct txw_packet {
	gpointer	data;

	guint		length;
	guint32		sequence_number;
	struct timeval	expiry;
	struct timeval	last_retransmit;
};

struct txw {
	GPtrArray*	pdata;
	GTrashStack*	trash_packet;		/* sizeof(txw_packet) */
	GTrashStack*	trash_data;		/* max_tpdu */

	guint		max_tpdu;		/* maximum packet size */

	guint32		lead;
	guint32		trail;
	guint		offset;
	
};

#define TXW_LENGTH(w)	( (w)->pdata->len )

/* trail = lead		=> size = 1
 * trail = lead + 1	=> size = 0
 */

#define TXW_SQNS(w)	( ( 1 + (w)->lead ) - (w)->trail )

#define ABS_IN_TXW(w,x) \
	( (x) >= (w)->trail && (x) <= (w)->lead )

#define IN_TXW(w,x) \
	( (x) >= (w)->trail && (x) <= ((w)->trail + ((UINT32_MAX/2) - 1)) )

#define TXW_PACKET_OFFSET(w,x) \
	( \
		(x) >= (w)->offset ? \
			( (x) - (w)->offset ) : \
			( (x) - ( (w)->offset - TXW_LENGTH(w) ) ) \
	)
#define TXW_PACKET(w,x) \
	( (struct txw_packet*)g_ptr_array_index((w)->pdata, TXW_PACKET_OFFSET((w), (x))) )
#define TXW_SET_PACKET(w,x,v) \
	do { \
		int _o = TXW_PACKET_OFFSET((w), (x)); \
		g_ptr_array_index((w)->pdata, _o) = (v); \
	} while (0)

#define TXW_CHECK_WRAPAROUND(w,x) \
	do { \
		if ( (x) == ( TXW_LENGTH( (w) ) + (w)->offset ) ) \
		{ \
			(w)->offset += TXW_LENGTH( (w) ); \
		} \
	} while (0)


/* globals */
#define G_LOG_DOMAIN		"txw"

static void _list_iterator (gpointer, gpointer);

gpointer txw_alloc_packet (struct txw*);
int txw_pkt_free1 (struct txw*, struct txw_packet*);


gpointer
txw_init (
	guint	tpdu_length,
	guint32	preallocate_size,
	guint32	txw_sqns,		/* transmit window size in sequence numbers */
	guint	txw_secs,		/* size in seconds */
	guint	txw_max_rte		/* max bandwidth */
	)
{
	g_debug ("init (tpdu %i pre-alloc %i txw_sqns %i txw_secs %i txw_max_rte %i).\n",
		tpdu_length, preallocate_size, txw_sqns, txw_secs, txw_max_rte);

	struct txw* t = g_slice_alloc0 (sizeof(struct txw));
	t->pdata = g_ptr_array_new ();

	t->max_tpdu = tpdu_length;

	for (guint32 i = 0; i < preallocate_size; i++)
	{
		gpointer data   = g_slice_alloc (t->max_tpdu);
		gpointer packet = g_slice_alloc (sizeof(struct txw_packet));
		g_trash_stack_push (&t->trash_data, data);
		g_trash_stack_push (&t->trash_packet, packet);
	}

/* calculate transmit window parameters */
	if (txw_sqns)
	{
	}
	else if (txw_secs && txw_max_rte)
	{
		txw_sqns = (txw_secs * txw_max_rte) / t->max_tpdu;
	}

	g_ptr_array_set_size (t->pdata, txw_sqns);

/* empty state:
 *
 * trail = 1, lead = 0
 */
	t->trail = t->lead + 1;

	g_debug ("sqns %u len %u", TXW_SQNS(t), TXW_LENGTH(t) );

	return (gpointer)t;
}

int
txw_shutdown (
	gpointer	ptr
	)
{
	g_debug ("shutdown.");

	g_return_val_if_fail (ptr != NULL, -1);
	struct txw* t = (struct txw*)ptr;

	if (t->pdata)
	{
		g_ptr_array_foreach (t->pdata, _list_iterator, t);
		g_ptr_array_free (t->pdata, TRUE);
		t->pdata = NULL;
	}

	if (t->trash_data)
	{
		gpointer *p = NULL;

/* gcc recommends parentheses around assignment used as truth value */
		while ( (p = g_trash_stack_pop (&t->trash_data)) )
		{
			g_slice_free1 (t->max_tpdu, p);
		}

		g_assert ( t->trash_data == NULL );
	}

	if (t->trash_packet)
	{
		gpointer *p = NULL;
		while ( (p = g_trash_stack_pop (&t->trash_packet)) )
		{
			g_slice_free1 (sizeof(struct txw_packet), p);
		}

		g_assert ( t->trash_packet == NULL );
	}

	return 0;
}

static void
_list_iterator (
	gpointer	data,
	gpointer	user_data
	)
{
	if (data == NULL) return;

	struct txw* t = (struct txw*)user_data;
	struct txw_packet* tp = (struct txw_packet*)data;

	txw_pkt_free1 (t, tp);
}

/* alloc for the payload per packet */
gpointer
txw_alloc (
	gpointer	ptr
	)
{
	g_return_val_if_fail (ptr != NULL, NULL);
	struct txw* t = (struct txw*)ptr;

	if (G_LIKELY(t->trash_data)) {
		return g_trash_stack_pop (&t->trash_data);
	}

	g_debug ("data trash stack exceeded");

	return g_slice_alloc (t->max_tpdu);
}

gpointer
txw_alloc_packet (
	struct txw*	t
	)
{
	g_return_val_if_fail (t != NULL, NULL);

	return t->trash_packet ?  g_trash_stack_pop (&t->trash_packet) : g_slice_alloc (sizeof(struct txw_packet));
}

int
txw_pkt_free1 (
	struct txw*	t,
	struct txw_packet*	tp
	)
{
	g_return_val_if_fail (t != NULL && tp != NULL, -1);

//	g_slice_free1 (tp->length, tp->data);
	g_trash_stack_push (&t->trash_data, tp->data);
	tp->data = NULL;

//	g_slice_free1 (sizeof(struct txw), tp);
	g_trash_stack_push (&t->trash_packet, tp);

	return 0;
}

guint32
txw_next_lead (
	gpointer	ptr
	)
{
	g_return_val_if_fail (ptr != NULL, 0);
	struct txw* t = (struct txw*)ptr;

	return (guint32)(t->lead + 1);
}

guint32
txw_lead (
	gpointer	ptr
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct txw* t = (struct txw*)ptr;

	return t->lead;
}

guint32
txw_trail (
	gpointer	ptr
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct txw* t = (struct txw*)ptr;

	return t->trail;
}

int
txw_in_window (
	gpointer	ptr,
	guint32		sequence_number
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct txw* t = (struct txw*)ptr;

	return ABS_IN_TXW(t, sequence_number);
}

int
txw_push (
	gpointer	ptr,
	gpointer	packet,
	guint		length
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct txw* t = (struct txw*)ptr;

/* check for full window */
	if (TXW_LENGTH(t) == TXW_SQNS(t))
	{
		g_warning ("full :o");

/* transmit window advancement scheme dependent action here */
		txw_pop (ptr);
	}

/* add to window */
	struct txw_packet* tp = txw_alloc_packet (t);
	tp->data		= packet;
	tp->length		= length;
	tp->sequence_number	= ++(t->lead);

	TXW_CHECK_WRAPAROUND(t, tp->sequence_number);
	TXW_SET_PACKET(t, tp->sequence_number, tp);
	g_debug ("#%u: adding packet", tp->sequence_number);

	return 0;
}

int
txw_push_copy (
	gpointer	ptr,
	gpointer	packet,
	guint		length
	)
{
	g_return_val_if_fail (ptr != NULL, -1);

	gpointer copy = txw_alloc (ptr);
	memcpy (copy, packet, length);

	return txw_push (ptr, copy, length);
}

/* more like txw_peek(), the packet is not removed from the window
 */

int
txw_get (
	gpointer	ptr,
	guint32		sequence_number,
	gpointer*	packet,
	guint*		length
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct txw* t = (struct txw*)ptr;

/* check if sequence number is in window */
	if (G_UNLIKELY( !ABS_IN_TXW(t, sequence_number) ))
	{
		g_warning ("%u not in window.", sequence_number);
		return -1;
	}

	struct txw_packet* tp = TXW_PACKET(t, sequence_number);
	*packet = tp->data;
	*length	= tp->length;

	return 0;
}

int
txw_pop (
	gpointer	ptr
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct txw* t = (struct txw*)ptr;

/* check if window is not empty */
	if (G_UNLIKELY( !TXW_SQNS(t) ))
	{
		g_debug ("window is empty");
		return -1;
	}

	struct txw_packet* tp = TXW_PACKET(t, t->trail);
	txw_pkt_free1 (t, tp);
	TXW_SET_PACKET(t, t->trail, NULL);

	t->trail++;

	return 0;
}

/* eof */
