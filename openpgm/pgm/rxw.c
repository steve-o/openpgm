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

	guint32		lead, trail;
	guint32		rxw_trail, rxw_trail_init;
	gboolean	rxw_constrained;
	gboolean	window_defined;
	guint		offset;

	rxw_callback	on_data;
	gpointer	param;
};

#define RXW_LENGTH(w)	( (w)->pdata->len )
#define RXW_SQNS(w)	( (w)->lead - (w)->trail )

#define IN_TXW(w,x) \
	( (x) >= (w)->rxw_trail && (x) <= ((w)->rxw_trail + ((UINT32_MAX/2) - 1)) )
#define IN_RXW(w,x) \
	( (x) > (w)->rxw_trail && (x) <= (w)->lead )

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

#define SLIDINGWINDOW_GTE(a,b,l) \
	( \
		( (gint32)(a) - (gint32)(l) ) >= ( (gint32)(b) - (gint32)(l) ) \
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

/* empty state:
 *
 * trail = lead = 0
 * rxw_trail = rxw_trail_init = 0
 */

/* limit retransmit requests on late session joining */
	r->rxw_constrained = TRUE;

	r->window_defined = FALSE;

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

printf ("rxw: #%u: trail#%u rxw_trail %u rxw_trail_init %u trail %u lead %u\n",
		sequence_number, trail, 
		r->rxw_trail, r->rxw_trail_init, r->trail, r->lead);

/* trail is the next packet to commit upstream, lead is the leading edge
 * of the receive window with possible gaps inside, rxw_trail is the transmit
 * window trail for retransmit requests.
 */

	if ( !r->window_defined )
	{
		printf ("rxw: #%u: using packet to temporarily define window\n",
			sequence_number);

		r->lead = r->trail = r->rxw_trail = r->rxw_trail_init = sequence_number;
		r->rxw_constrained = TRUE;
		r->window_defined = TRUE;
	}
	else
	{
/* check if packet should be discarded or processed further */

		if ( !IN_TXW(r, sequence_number) )
		{
			printf ("rxw: #%u: not in tx window, discarding.\n",
				sequence_number);
			return -1;
		}

		if ( SLIDINGWINDOW_GT(trail, r->rxw_trail, r->lead) )
		{
			printf ("rxw: #%u: using new rxw_trail value.\n",
				sequence_number);

			r->rxw_trail = trail;
		}

		if ( r->rxw_constrained && SLIDINGWINDOW_GT(trail, r->rxw_trail_init, r->lead) )
		{
			printf ("rxw: #%u: constraint removed on trail.\n",
				sequence_number);

			r->rxw_constrained = FALSE;
		}
	}

	if ( IN_RXW(r, sequence_number) )
	{
/* possible duplicate */
		printf ("rxw: #%u: in rx window, checking for duplicate.\n",
			sequence_number);

		guint offset = RXW_PACKET_OFFSET(r, sequence_number);
		struct rxw_packet* rp = g_ptr_array_index (r->pdata, offset);

		if (rp)
		{
			if (rp->length)
			{
				printf ("rxw: #%u: already received, discarding.\n",
				sequence_number);
			}

			printf ("rxw: #%u: filling in a gap.\n",
				sequence_number);

			rp->data	= packet;
			rp->length	= length;

/* clean up lists */
			if (rp->nak) {
				r->nak_list = g_list_delete_link (r->nak_list, rp->nak);
				rp->nak = NULL;
			}
			if (rp->ncf) {
				r->ncf_list = g_list_delete_link (r->ncf_list, rp->ncf);
				rp->ncf = NULL;
			}
		}
		else
		{
			puts ("rxw: internal error :(");
			return -1;
		}
	}
	else
	{
/* extends receive window */
		printf ("rxw: #%u: lead extended.\n",
			sequence_number);

		while (r->lead != sequence_number) {
			if (RXW_LENGTH(r) == RXW_SQNS(r)) {
				puts ("rxw: full, dropping packet ;(");
				return -1;
			}

			struct rxw_packet* ph = r->trash_packet ?
							g_trash_stack_pop (&r->trash_packet) :
							g_slice_alloc (sizeof(struct rxw_packet));
			ph->data                = NULL;
			ph->length              = 0;
			ph->sequence_number     = r->lead++;

			if (ph->sequence_number == ( RXW_LENGTH(r) + r->offset ))
			{
				r->offset += RXW_LENGTH(r);
			}

			guint offset = ph->sequence_number - r->offset;
			g_ptr_array_index (r->pdata, offset) = ph;
			printf ("rxw: #%u: adding placeholder #%u\n",
				sequence_number, ph->sequence_number);

/* send nak by prepending to list */
			r->nak_list = g_list_prepend (r->nak_list, ph);

		}

/* r->lead = sequence_number; */
		
		if (RXW_LENGTH(r) == RXW_SQNS(r)) {
			puts ("rxw: full, dropping packet ;(");
			return -1;
		}

		struct rxw_packet* rp = r->trash_packet ?
						g_trash_stack_pop (&r->trash_packet) :
						g_slice_alloc (sizeof(struct rxw_packet));
		rp->data                = packet;
		rp->length              = length;
		rp->sequence_number     = r->lead;

		if (rp->sequence_number == ( RXW_LENGTH(r) + r->offset ))
		{
			r->offset += RXW_LENGTH(r);
		}

		guint offset = rp->sequence_number - r->offset;
		g_ptr_array_index (r->pdata, offset) = rp;
		printf ("rxw: #%u: adding packet #%u\n",
			sequence_number, rp->sequence_number);
	}

/* check for contigious packets to pass upstream */
	printf ("rxw: #%u: flush window for contigious data.\n",
		sequence_number);

	for (guint32 peak = r->trail; peak <= r->lead; peak++)
	{
		guint offset = RXW_PACKET_OFFSET(r, peak);
		struct rxw_packet* pp = g_ptr_array_index (r->pdata, offset);

		if (!pp || !pp->length)
		{
			if (!pp)
				printf ("rxw: #%u: pp = NULL\n", sequence_number);
			if (!pp->length)
				printf ("rxw: #%u: pp->length = 0\n", sequence_number);
			break;
		}

		printf ("rxw: #%u: contigious packet found @ #%u, passing upstream.\n",
			sequence_number, pp->sequence_number);

		r->trail++;

/* pass upstream */
		if (r->on_data) r->on_data (pp->data, pp->length, r->param);

/* cleanup */
//		g_slice_free1 (pp->length, pp->data);
		g_trash_stack_push (&r->trash_data, pp->data);

//		g_slice_free1 (sizeof(struct rxw), pp);
		g_trash_stack_push (&r->trash_packet, pp);

		g_ptr_array_index (r->pdata, offset) = NULL;
	}

	printf ("rxw: #%u: push() complete.\n",
		sequence_number);

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

	int offset = RXW_PACKET_OFFSET(r, r->lead);
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

	r->lead--;

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
	if (SLIDINGWINDOW_GT(txw_trail, r->rxw_trail, txw_lead))
	{
		printf ("rxw: advancing rxw_trail to %u\n",
			txw_trail);
		r->rxw_trail = txw_trail;
	}

	if ( txw_lead > r->lead && txw_lead <= ( r->lead + ((UINT32_MAX/2) - 1)) )
	{
		printf ("rxw: advancing lead* to %u\n",
			txw_lead);
	}

	return -1;
}

/* eof */
