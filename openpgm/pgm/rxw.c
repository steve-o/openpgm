/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * A basic receive window: pointer array implementation.
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

#include "rxw.h"


struct rxw_packet {
	gpointer	data;

	guint		length;
	guint32		sequence_number;

	struct timeval	nak_sent;
	struct timeval	ncf_received;
	GList*		nak;
	GList*		ncf;
};

struct rxw {
	GPtrArray*	pdata;
	GTrashStack*	trash_packet;		/* sizeof(rxw_packet) */
	GTrashStack*	trash_data;		/* max_tpdu */

	GList*		nak_list;
	GList*		ncf_list;

	guint		max_tpdu;		/* maximum packet size */

	guint32		txw_trail, txw_lead;
	guint32		rxw_trail, rxw_lead, rxw_next_lead;
	guint32		rxw_ahead;
	guint32		rxw_trail_init;
	gboolean	rxw_constrained;
	guint		offset;

	rxw_callback	on_data;
	gpointer	param;
};

/* between lead and ahead, as processed packets are not important receive side */
#define RXW_LENGTH(w)	( (w)->pdata->len )
#define RXW_SQNS(w)	( (1 + (w)->rxw_ahead) - (w)->rxw_lead )

#define ABS_IN_TXW(w,x) \
	( (x) >= (w)->txw_trail && (x) <= (w)->txw_lead )

/* lead+2 in order to cope with mid-shuffling of window */
#define IN_RXW_AHEAD(w,x) \
	( (x) > (w)->rxw_next_lead && (x) <= (w)->rxw_ahead )

#define IN_TXW(w,x) \
	( (x) >= (w)->txw_trail && (x) <= ((w)->txw_trail + ((UINT32_MAX/2) - 1)) )
#define IN_RXW(w,x) \
	( (x) >= (w)->rxw_trail && (x) <= (w)->rxw_lead )

#define RXW_PACKET_OFFSET(w,x) \
	( \
		(x) >= (w)->offset ? \
			( (x) - (w)->offset ) : \
			( (x) - ( (w)->offset - RXW_LENGTH(w) ) ) \
	)

/* is (a) greater than (b) wrt. leading edge of receive window (w) */
#define SLIDINGWINDOW_GT(a,b,l) \
	( \
		( (gint32)(a) - (gint32)(l) ) > ( (gint32)(b) - (gint32)(l) ) \
	)

#define SLIDINGWINDOW_LT(a,b,l) \
	( \
		( (gint32)(a) - (gint32)(l) ) < ( (gint32)(b) - (gint32)(l) ) \
	)


/* globals */
int rxw_debug = 1;

static void _list_iterator (gpointer, gpointer);
static int rxw_pop (struct rxw*);


gpointer
rxw_init (
	guint	tpdu_length,
	guint32	preallocate_size,
	guint32	rxw_sqns,		/* transmit window size in sequence numbers */
	guint	rxw_secs,		/* size in seconds */
	guint	rxw_max_rte,		/* max bandwidth */
	rxw_callback	on_data,	/* upstream callback */
	gpointer	param		/* upstream parameter */
	)
{
	if (G_UNLIKELY(rxw_debug))
	printf ("rxw: init (tpdu %i pre-alloc %i rxw_sqns %i rxw_secs %i rxw_max_rte %i).\n",
		tpdu_length, preallocate_size, rxw_sqns, rxw_secs, rxw_max_rte);

	struct rxw* r = g_slice_alloc0 (sizeof(struct rxw));
	r->pdata = g_ptr_array_new ();

	r->max_tpdu = tpdu_length;

	for (guint32 i = 0; i < preallocate_size; i++)
	{
		gpointer data   = g_slice_alloc (r->max_tpdu);
		gpointer packet = g_slice_alloc (sizeof(struct rxw_packet));
		g_trash_stack_push (&r->trash_data, data);
		g_trash_stack_push (&r->trash_packet, packet);
	}

/* calculate receive window parameters as per transmit window */
	if (rxw_sqns)
	{
	}
	else if (rxw_secs && rxw_max_rte)
	{
		rxw_sqns = (rxw_secs * rxw_max_rte) / r->max_tpdu;
	}

	if (G_UNLIKELY(rxw_debug))
	printf ("rxw: %i packets.\n", rxw_sqns);
	g_ptr_array_set_size (r->pdata, rxw_sqns);

/* set empty window */
	r->rxw_trail = r->rxw_lead - 1;

/* limit retransmit requests on late session joining */
	r->rxw_constrained = TRUE;

	r->on_data = on_data;
	r->param = param;

	return (gpointer)r;
}

int
rxw_shutdown (
	gpointer	ptr
	)
{
	if (G_UNLIKELY(rxw_debug))
	puts ("rxw: shutdown.");

	if (G_UNLIKELY(!ptr)) {
		puts ("rxw: invalid rxw.");
		return -1;
	}
	struct rxw* r = (struct rxw*)ptr;

	if (G_LIKELY(r->pdata))
	{
		g_ptr_array_foreach (r->pdata, _list_iterator, &r->max_tpdu);
		g_ptr_array_free (r->pdata, TRUE);
		r->pdata = NULL;
	}

	if (r->trash_data)
	{
		gpointer *p = NULL;

/* gcc recommends parentheses around assignment used as truth value */
		while ( (p = g_trash_stack_pop (&r->trash_data)) )
			g_slice_free1 (r->max_tpdu, p);

/* empty trash stack = NULL */
	}

	if (r->trash_packet)
	{
		gpointer *p = NULL;
		while ( (p = g_trash_stack_pop (&r->trash_packet)) )
			g_slice_free1 (sizeof(struct rxw_packet), p);
	}

/* nak/ncf time lists */
	if (r->nak_list)
	{
		g_list_free (r->nak_list);
		r->nak_list = NULL;
	}
	if (r->ncf_list)
	{
		g_list_free (r->ncf_list);
		r->ncf_list = NULL;
	}

	return 0;
}

static void
_list_iterator (
	gpointer	data,
	gpointer	user_data
	)
{
/* iteration on empty array sized on init() */
	if (G_UNLIKELY(!data)) return;

	struct rxw_packet *rp = (struct rxw_packet*)data;
	int length = rp->length;

	int max_tpdu = *(int*)user_data;

//	g_slice_free1 ( length, rp->data );
	g_slice_free1 ( max_tpdu, rp->data );

	g_slice_free1 ( sizeof(struct rxw_packet), data );
}

/* alloc for the payload per packet */
gpointer
rxw_alloc (
	gpointer	ptr
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("rxw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct rxw* r = (struct rxw*)ptr;

	return r->trash_data ? g_trash_stack_pop (&r->trash_data) : g_slice_alloc (r->max_tpdu);
}

/* the sequence number is inside the packet as opposed to from internal
 * counters, this means one push on the receive window can actually translate
 * as many: the extra's acting as place holders and NAK containers
 */

int
rxw_push (
	gpointer	ptr,
	gpointer	packet,
	guint		length,
	guint32		sequence_number,
	guint32		trail
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("rxw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct rxw* r = (struct rxw*)ptr;

printf ("rxw_push (#%lu trail#%lu) rxw: trail %u lead %u next-lead %u ahead %u\n",
		sequence_number, trail, r->rxw_trail, r->rxw_lead, r->rxw_next_lead, r->rxw_ahead);

/* we need an SPM to define the transmit window before we can accept packets */
	if ( !IN_TXW(r, sequence_number) )
	{
		printf ("not in tx window %u, %lu - %lu\n",
			sequence_number,
			r->txw_trail,
			r->txw_lead);
		return -1;
	}

	if (IN_RXW(r, sequence_number))
	{
		printf ("rxw: dupe, #%lu win %lu-%lu\n", sequence_number, r->rxw_trail, r->rxw_lead);
		return -1;
	}

/* if we are joined a session late, set rxw_trail_init to the packets sequence number */
	if (r->rxw_constrained)
	{
		if (r->rxw_lead == r->rxw_next_lead)	/* special case for first packet */
		{
			r->rxw_trail = r->rxw_lead = r->rxw_trail_init = sequence_number;
			if (G_UNLIKELY(rxw_debug > 1))
				printf ("rxw_trail_init=%lu\n", r->rxw_trail_init);

			r->rxw_ahead = r->rxw_next_lead = r->rxw_lead ;
		}
		else
		{

/* constraint is removed when the trailing sequence number passes rxw_trail_init wrt rxw_lead */
			if ( SLIDINGWINDOW_GT(trail, r->rxw_trail_init, r->rxw_lead) )
			{
				r->rxw_constrained = FALSE;
				if (G_UNLIKELY(rxw_debug > 1))
					puts ("rxw constraint removed.");
			}
		}
	}

/* check for dupe or fills in a gap in the ahead buffer */
	if (IN_RXW_AHEAD(r, sequence_number))
	{
		if (G_UNLIKELY(rxw_debug > 1))
			printf ("rxw: in ahead buffer #%lu, %lu-%lu\n", sequence_number, r->rxw_next_lead, r->rxw_ahead);

		guint offset = RXW_PACKET_OFFSET(r, sequence_number);
		struct rxw_packet* rp = g_ptr_array_index (r->pdata, offset);
		if (rp->length)
		{
			puts ("ahead dupe");
			return -1;
		}
		else
		{
/* fill in ahead buffer */
			rp->data	= packet;
			rp->length	= length;

/* clean up nak/ncf lists */
			if (rp->nak)
			{
				r->nak_list = g_list_delete_link (r->nak_list, rp->nak);
				rp->nak = NULL;
			}

			if (rp->ncf)
			{
				r->ncf_list = g_list_delete_link (r->ncf_list, rp->ncf);
				rp->ncf = NULL;
			}
		}
	}
	else
	{
		if (G_UNLIKELY(rxw_debug > 1))
			printf ("rxw: add to ahead buffer #%lu, %lu-%lu\n", sequence_number, r->rxw_next_lead, r->rxw_ahead);

/* we have a packet that defines or extends the ahead buffer */
		while ( (gint32)sequence_number - (gint32)r->rxw_ahead > 0 )
		{
			int offset = r->rxw_ahead - r->offset;
			struct rxw_packet* ph = g_ptr_array_index (r->pdata, offset);

			if (ph)
			{
				r->rxw_ahead++;
				continue;
			}

/* check for full window */
			if (RXW_LENGTH(r) == RXW_SQNS(r)) {
				puts ("rxw: full, dropping packet ;(");
				return -1;
			}

			ph = r->trash_packet ?
				g_trash_stack_pop (&r->trash_packet) :
				g_slice_alloc (sizeof(struct rxw_packet));
			ph->data		= NULL;
			ph->length		= 0;
			ph->sequence_number	= r->rxw_ahead++;

			if (G_UNLIKELY(rxw_debug > 1))
				printf ("rxw: place holder for #%lu\n", ph->sequence_number);

			if (ph->sequence_number == ( RXW_LENGTH(r) + r->offset ))
			{
				if (G_UNLIKELY(rxw_debug > 1))
					puts ("rxw: wrap offset.");
				r->offset += RXW_LENGTH(r);
			}

			offset = ph->sequence_number - r->offset;
			g_ptr_array_index (r->pdata, offset) = ph;

/* send nak by prepending to list */
			r->nak_list = g_list_prepend (r->nak_list, ph);
		}

/* check for full window */
		if (RXW_LENGTH(r) == RXW_SQNS(r)) {
			puts ("rxw: full, expunging packet from ahead buffer ;(");

/* data loss notification here */
			rxw_pop (ptr);
		}

/* add packet to receive window */
		struct rxw_packet* rp = r->trash_packet ?
						g_trash_stack_pop (&r->trash_packet) :
						g_slice_alloc (sizeof(struct rxw_packet));
		rp->data		= packet;
		rp->length		= length;
		rp->sequence_number	= sequence_number;

		if (G_UNLIKELY(rxw_debug > 1))
			printf ("rxw: packet added to window #%lu\n", rp->sequence_number);

		if (rp->sequence_number == ( RXW_LENGTH(r) + r->offset ))
		{
			if (G_UNLIKELY(rxw_debug > 1))
				puts ("rxw: wrap offset.");
			r->offset += RXW_LENGTH(r);
		}

		int offset = rp->sequence_number - r->offset;
		g_ptr_array_index (r->pdata, offset) = rp;
	}

/* check for contigious packets to pass upstream */
	if (G_UNLIKELY(rxw_debug > 1))
		printf ("rxw: flush rxw_lead %lu rxw_ahead %lu\n", r->rxw_lead, r->rxw_ahead);

	guint32 peak = r->rxw_next_lead;
	guint offset = RXW_PACKET_OFFSET(r, peak);
	struct rxw_packet* pp = g_ptr_array_index (r->pdata, offset);

	while (pp && pp->length)
	{
		if (G_UNLIKELY(rxw_debug > 1))
			printf ("rxw: #%lu ", pp->sequence_number);

		r->rxw_lead = r->rxw_next_lead;
		if (r->rxw_ahead == r->rxw_next_lead)
			r->rxw_ahead = r->rxw_next_lead = r->rxw_lead + 1;
		else
			r->rxw_next_lead = r->rxw_lead + 1;

/* pass upstream */
		if (r->on_data) r->on_data (pp->data, pp->length, r->param);

/* cleanup */
//		g_slice_free1 (pp->length, pp->data);
		g_trash_stack_push (&r->trash_data, pp->data);

//		g_slice_free1 (sizeof(struct rxw), pp);
		g_trash_stack_push (&r->trash_packet, pp);

		g_ptr_array_index (r->pdata, offset) = NULL;

		peak++;
		offset = RXW_PACKET_OFFSET(r, peak);
		pp = g_ptr_array_index (r->pdata, offset);
	}

	return 0;
}

/* remove from leading edge of ahead side of receive window */
int
rxw_pop (
	struct rxw*	r
	)
{
/* check if window is not empty */
	if (!RXW_SQNS(r))
	{
		puts ("rxw: empty.");
		return -1;
	}

	int offset = RXW_PACKET_OFFSET(r, r->rxw_ahead);
	struct rxw_packet* rp = g_ptr_array_index (r->pdata, offset);

	if (rp->nak)
	{
		r->nak_list = g_list_delete_link (r->nak_list, rp->nak);
		rp->nak = NULL;
	}

	if (rp->ncf)
	{
		r->ncf_list = g_list_delete_link (r->ncf_list, rp->ncf);
		rp->ncf = NULL;
	}

	if (rp->data)
	{
//		g_slice_free1 (rp->length, rp->data);
		g_trash_stack_push (&r->trash_data, rp->data);
	}

//	g_slice_free1 (sizeof(struct rxw), rp);
	g_trash_stack_push (&r->trash_packet, rp);

	r->rxw_ahead--;

	return 0;
}

/* update receiving window with new trailing and leading edge parameters of transmit window
 * can generate data loss by excluding outstanding NAK requests.
 */
int
rxw_update (
	gpointer	ptr,
	guint32		txw_trail,
	guint32		txw_lead
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("rxw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct rxw* r = (struct rxw*)ptr;

/* does the SPM advance the trail of the window? */
	if (SLIDINGWINDOW_GT(txw_trail, r->txw_trail, txw_lead))
	{
		r->txw_trail = txw_trail;
		r->txw_lead = txw_lead;
		return 0;
	}

	return -1;
}

/* eof */
