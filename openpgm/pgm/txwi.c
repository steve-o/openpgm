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

#ifndef TXW_DEBUG
#define G_DISABLE_ASSERT
#endif

#include <glib.h>

#include "txwi.h"
#include "sn.h"

#ifndef TXW_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


#define ABS_IN_TXW(w,x) \
	( \
		!txw_empty ( (w) ) && \
		guint32_gte ( (x), (w)->trail ) && guint32_lte ( (x), (w)->lead ) \
	)

#define IN_TXW(w,x)	( guint32_gte ( (x), (w)->trail ) )

#define TXW_PACKET_OFFSET(w,x)		( (x) % txw_len( (w) ) )
#define TXW_PACKET(w,x) \
	( (struct txw_packet*)g_ptr_array_index((w)->pdata, TXW_PACKET_OFFSET((w), (x))) )
#define TXW_SET_PACKET(w,x,v) \
	do { \
		register int _o = TXW_PACKET_OFFSET((w), (x)); \
		g_ptr_array_index((w)->pdata, _o) = (v); \
	} while (0)

#ifdef TXW_DEBUG
#define ASSERT_TXW_BASE_INVARIANT(w) \
	{ \
		g_assert ( (w) != NULL ); \
\
/* does the array exist */ \
		g_assert ( (w)->pdata != NULL && (w)->pdata->len > 0 ); \
\
/* packet size has been set */ \
		g_assert ( (w)->max_tpdu > 0 ) ; \
\
/* all pointers are within window bounds */ \
		if ( !txw_empty ( (w) ) ) /* empty: trail = lead + 1, hence wrap around */ \
		{ \
			g_assert ( TXW_PACKET_OFFSET( (w), (w)->lead ) < (w)->pdata->len ); \
			g_assert ( TXW_PACKET_OFFSET( (w), (w)->trail ) < (w)->pdata->len ); \
		} \
\
	}

#define ASSERT_TXW_POINTER_INVARIANT(w) \
	{ \
/* are trail & lead points valid */ \
		if ( !txw_empty ( (w) ) ) \
		{ \
			g_assert ( NULL != TXW_PACKET( (w) , (w)->trail ) );    /* trail points to something */ \
			g_assert ( NULL != TXW_PACKET( (w) , (w)->lead ) );     /* lead points to something */ \
		} \
	} 
#else
#define ASSERT_TXW_BASE_INVARIANT(w)	while(0)
#define ASSERT_TXW_POINTER_INVARIANT(w)	while(0)
#endif

/* globals */
#define G_LOG_DOMAIN		"txw"

static void _list_iterator (gpointer, gpointer);

static inline gpointer txw_alloc_packet (struct txw*);
static inline int txw_pkt_free1 (struct txw*, struct txw_packet*);
static inline int txw_pop (struct txw*);


struct txw*
txw_init (
	guint	tpdu_length,
	guint32	preallocate_size,
	guint32	txw_sqns,		/* transmit window size in sequence numbers */
	guint	txw_secs,		/* size in seconds */
	guint	txw_max_rte		/* max bandwidth */
	)
{
	g_trace ("init (tpdu %i pre-alloc %i txw_sqns %i txw_secs %i txw_max_rte %i).\n",
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

	guint memory = sizeof(struct txw) +
/* pointer array */
			sizeof(GPtrArray) + sizeof(guint) +
			*(guint*)( (char*)t->pdata + sizeof(gpointer) + sizeof(guint) ) +
/* pre-allocated data & packets */
			preallocate_size * (t->max_tpdu + sizeof(struct txw_packet));

	g_trace ("memory usage: %ub (%uMb)", memory, memory / (1024 * 1024));

	ASSERT_TXW_BASE_INVARIANT(t);
	ASSERT_TXW_POINTER_INVARIANT(t);
	return t;
}

int
txw_shutdown (
	struct txw*	t
	)
{
	g_trace ("shutdown.");

	ASSERT_TXW_BASE_INVARIANT(t);
	ASSERT_TXW_POINTER_INVARIANT(t);

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

	g_assert ( user_data != NULL);

	txw_pkt_free1 ((struct txw*)user_data, (struct txw_packet*)data);
}

static inline gpointer
txw_alloc_packet (
	struct txw*	t
	)
{
	ASSERT_TXW_BASE_INVARIANT(t);
	
	gpointer p = t->trash_packet ?  g_trash_stack_pop (&t->trash_packet) : g_slice_alloc (sizeof(struct txw_packet));

	ASSERT_TXW_BASE_INVARIANT(t);
	return p;
}

static inline int
txw_pkt_free1 (
	struct txw*	t,
	struct txw_packet*	tp
	)
{
	ASSERT_TXW_BASE_INVARIANT(t);
	g_assert ( tp != NULL );

//	g_slice_free1 (tp->length, tp->data);
	g_trash_stack_push (&t->trash_data, tp->data);
	tp->data = NULL;

//	g_slice_free1 (sizeof(struct txw), tp);
	g_trash_stack_push (&t->trash_packet, tp);

	ASSERT_TXW_BASE_INVARIANT(t);
	return 0;
}

int
txw_push (
	struct txw*	t,
	gpointer	packet,
	guint		length
	)
{
	ASSERT_TXW_BASE_INVARIANT(t);
	ASSERT_TXW_POINTER_INVARIANT(t);

	g_trace ("#%u: push: window ( trail %u lead %u )",
		t->lead+1, t->trail, t->lead);

/* check for full window */
	if ( txw_full (t) )
	{
		g_warning ("full :o");

/* transmit window advancement scheme dependent action here */
		txw_pop (t);
	}

	t->lead++;

/* add to window */
	struct txw_packet* tp = txw_alloc_packet (t);
	tp->data		= packet;
	tp->length		= length;
	tp->sequence_number	= t->lead;

	TXW_SET_PACKET(t, tp->sequence_number, tp);
	g_trace ("#%u: adding packet", tp->sequence_number);

	ASSERT_TXW_BASE_INVARIANT(t);
	ASSERT_TXW_POINTER_INVARIANT(t);
	return 0;
}

/* the packet is not removed from the window
 */

int
txw_peek (
	struct txw*	t,
	guint32		sequence_number,
	gpointer*	packet,
	guint*		length
	)
{
	ASSERT_TXW_BASE_INVARIANT(t);
	ASSERT_TXW_POINTER_INVARIANT(t);

/* check if sequence number is in window */
	if ( !ABS_IN_TXW(t, sequence_number) )
	{
		g_warning ("%u not in window.", sequence_number);
		return -1;
	}

	struct txw_packet* tp = TXW_PACKET(t, sequence_number);
	*packet = tp->data;
	*length	= tp->length;

	ASSERT_TXW_BASE_INVARIANT(t);
	ASSERT_TXW_POINTER_INVARIANT(t);
	return 0;
}

static inline int
txw_pop (
	struct txw*	t
	)
{
	ASSERT_TXW_BASE_INVARIANT(t);
	ASSERT_TXW_POINTER_INVARIANT(t);

	if ( txw_empty (t) )
	{
		g_trace ("window is empty");
		return -1;
	}

	struct txw_packet* tp = TXW_PACKET(t, t->trail);
	txw_pkt_free1 (t, tp);
	TXW_SET_PACKET(t, t->trail, NULL);

	t->trail++;

	ASSERT_TXW_BASE_INVARIANT(t);
	ASSERT_TXW_POINTER_INVARIANT(t);
	return 0;
}

/* eof */
