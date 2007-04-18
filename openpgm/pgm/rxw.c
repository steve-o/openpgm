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

#undef g_debug
#define g_debug(x...)		while (0)


struct rxw_packet {
	gpointer	data;

	guint		length;
	guint32		sequence_number;

	gdouble		bo_start;
	gdouble		nak_sent;
	gdouble		ncf_received;
	GList		link_;
	pgm_pkt_state	state;
	guint		ncf_retry_count;
	guint		data_retry_count;
};

struct rxw {
	GPtrArray*	pdata;
	GTrashStack*	trash_packet;		/* sizeof(rxw_packet) */
	GTrashStack*	trash_data;		/* max_tpdu */

	GQueue*		backoff_queue;
	GQueue*		wait_ncf_queue;
	GQueue*		wait_data_queue;

	guint		max_tpdu;		/* maximum packet size */

	guint32		lead, trail;
	guint32		rxw_trail, rxw_trail_init;
	gboolean	rxw_constrained;
	gboolean	window_defined;
	guint		offset;

	rxw_callback	on_data;
	gpointer	param;

	GTimer*		zero;
};

#define RXW_LENGTH(w)	( (w)->pdata->len )
#define RXW_SQNS(w)	( (w)->lead - (w)->trail )

#define IN_TXW(w,x) \
	( (x) >= (w)->rxw_trail && (x) <= ((w)->rxw_trail + ((UINT32_MAX/2) - 1)) )
#define IN_RXW(w,x) \
	( \
		RXW_SQNS(w) && \
		( (x) >= (w)->rxw_trail && (x) <= (w)->lead ) \
	)

#define RXW_PACKET_OFFSET(w,x) \
	( \
		(x) >= (w)->offset ? \
			( (x) - (w)->offset ) : \
			( (x) - ( (w)->offset - RXW_LENGTH(w) ) ) \
	)
#define RXW_PACKET(w,x) \
	( (struct rxw_packet*)g_ptr_array_index((w)->pdata, RXW_PACKET_OFFSET((w), (x))) )
#define RXW_SET_PACKET(w,x,v) \
	do { \
		int _o = RXW_PACKET_OFFSET((w), (x)); \
		g_ptr_array_index((w)->pdata, _o) = (v); \
	} while (0)

#define RXW_CHECK_WRAPAROUND(w,x) \
	do { \
		if ( (x) == ( RXW_LENGTH( (w) ) + (w)->offset ) ) \
		{ \
			(w)->offset += RXW_LENGTH( (w) ); \
		} \
	} while (0)

/* is (a) greater than (b) wrt. leading edge of receive window (w) */
#define SLIDINGWINDOW_GT(a,b,l) \
	( \
		( (gint32)(a) - (gint32)(l) ) > ( (gint32)(b) - (gint32)(l) ) \
	)


/* globals */
#define G_LOG_DOMAIN	"rxw"

static void _list_iterator (gpointer, gpointer);
static int rxw_pop (struct rxw*);
static int rxw_pkt_state_unlink (struct rxw*, struct rxw_packet*);
static int rxw_pkt_free1 (struct rxw*, struct rxw_packet*);
static gpointer rxw_alloc_packet (struct rxw*);
static gpointer rxw_alloc0_packet (struct rxw*);


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
	g_debug ("init (tpdu %i pre-alloc %i rxw_sqns %i rxw_secs %i rxw_max_rte %i).",
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

	g_ptr_array_set_size (r->pdata, rxw_sqns);

/* empty state:
 *
 * trail = lead = 0
 * rxw_trail = rxw_trail_init = 0
 */

/* limit retransmit requests on late session joining */
	r->rxw_constrained = TRUE;

	r->window_defined = FALSE;

/* empty queue's for nak & ncfs */
	r->backoff_queue = g_queue_new ();
	r->wait_ncf_queue = g_queue_new ();
	r->wait_data_queue = g_queue_new ();

/* contiguous packet callback */
	r->on_data = on_data;
	r->param = param;

/* timing */
	r->zero = g_timer_new();

	guint memory = sizeof(struct rxw) +
			sizeof(GPtrArray) + sizeof(guint) +
			preallocate_size * (r->max_tpdu + sizeof(struct rxw_packet)) +
			*(guint*)( (char*)r->pdata + sizeof(gpointer) + sizeof(guint) ) +
			3 * sizeof(GQueue) +
			4 * sizeof(int);
			
	g_debug ("memory usage: %ub (%uMb)", memory, memory / (1024 * 1024));

	return (gpointer)r;
}

int
rxw_shutdown (
	gpointer	ptr
	)
{
	g_debug ("rxw: shutdown.");

	g_return_val_if_fail (ptr != NULL, -1);
	struct rxw* r = (struct rxw*)ptr;

	if (r->pdata)
	{
		g_ptr_array_foreach (r->pdata, _list_iterator, r);
		g_ptr_array_free (r->pdata, TRUE);
		r->pdata = NULL;
	}

	if (r->trash_data)
	{
		gpointer *p = NULL;

/* gcc recommends parentheses around assignment used as truth value */
		while ( (p = g_trash_stack_pop (&r->trash_data)) )
		{
			g_slice_free1 (r->max_tpdu, p);
		}

		g_assert (r->trash_data == NULL);
	}

	if (r->trash_packet)
	{
		gpointer *p = NULL;
		while ( (p = g_trash_stack_pop (&r->trash_packet)) )
		{
			g_slice_free1 (sizeof(struct rxw_packet), p);
		}

		g_assert (r->trash_packet == NULL);
	}

/* nak/ncf time lists,
 * important: link items are static to each packet struct
 */
	if (r->backoff_queue)
	{
		g_slice_free (GQueue, r->backoff_queue);
		r->backoff_queue = NULL;
	}
	if (r->wait_ncf_queue)
	{
		g_slice_free (GQueue, r->wait_ncf_queue);
		r->wait_ncf_queue = NULL;
	}
	if (r->wait_data_queue)
	{
		g_slice_free (GQueue, r->wait_data_queue);
		r->wait_data_queue = NULL;
	}

/* timer reference */
	if (r->zero)
	{
		g_timer_destroy (r->zero);
		r->zero = NULL;
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

	struct rxw* r = (struct rxw*)user_data;
	struct rxw_packet *rp = (struct rxw_packet*)data;

	rxw_pkt_free1 (r, rp);
}

/* alloc for the payload per packet */
gpointer
rxw_alloc (
	gpointer	ptr
	)
{
	g_return_val_if_fail (ptr != NULL, NULL);

	struct rxw* r = (struct rxw*)ptr;

	if (G_LIKELY(r->trash_data)) {
		return g_trash_stack_pop (&r->trash_data);
	}

	g_debug ("data trash stack exceeded");

	return g_slice_alloc (r->max_tpdu);
}

gpointer
rxw_alloc_packet (
	struct rxw*	r
	)
{
	g_return_val_if_fail (r != NULL, NULL);

	return r->trash_packet ?  g_trash_stack_pop (&r->trash_packet) : g_slice_alloc (sizeof(struct rxw_packet));
}

gpointer
rxw_alloc0_packet (
	struct rxw*	r
	)
{
	g_return_val_if_fail (r != NULL, NULL);

	if (G_LIKELY(r->trash_packet)) {
		gpointer p = g_trash_stack_pop (&r->trash_packet);
		memset (p, 0, sizeof(struct rxw_packet));

		return p;
	}

	g_debug ("packet trash stack exceeded.");

	return g_slice_alloc0 (sizeof(struct rxw_packet));
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
	g_return_val_if_fail (ptr != NULL, -1);
	struct rxw* r = (struct rxw*)ptr;

	g_debug ("#%u: trail#%u rxw_trail %u rxw_trail_init %u trail %u lead %u",
		sequence_number, trail, 
		r->rxw_trail, r->rxw_trail_init, r->trail, r->lead);

/* trail is the next packet to commit upstream, lead is the leading edge
 * of the receive window with possible gaps inside, rxw_trail is the transmit
 * window trail for retransmit requests.
 */

	if ( !r->window_defined )
	{
		g_debug ("#%u: using packet to temporarily define window",
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
			g_warning ("#%u: not in tx window, discarding.",
				sequence_number);
			return -1;
		}

		if ( SLIDINGWINDOW_GT(trail, r->rxw_trail, r->lead) )
		{
			g_debug ("#%u: using new rxw_trail value.",
				sequence_number);

			r->rxw_trail = trail;
		}

		if ( r->rxw_constrained && SLIDINGWINDOW_GT(trail, r->rxw_trail_init, r->lead) )
		{
			g_debug ("#%u: constraint removed on trail.",
				sequence_number);

			r->rxw_constrained = FALSE;
		}
	}

	if ( IN_RXW(r, sequence_number) )
	{
/* possible duplicate */
		g_debug ("#%u: in rx window, checking for duplicate.",
			sequence_number);

		struct rxw_packet* rp = RXW_PACKET(r, sequence_number);

		if (rp)
		{
			if (rp->length)
			{
				g_debug ("#%u: already received, discarding.",
				sequence_number);
			}

			g_debug ("#%u: filling in a gap.",
				sequence_number);

			rp->data	= packet;
			rp->length	= length;

			rxw_pkt_state_unlink (r, rp);

			rp->state	= PGM_PKT_HAVE_DATA_STATE;
		}
		else
		{
			g_assert_not_reached();
			return -1;
		}
	}
	else
	{
/* extends receive window */
		g_debug ("#%u: lead extended.",
			sequence_number);

		if (r->lead != sequence_number)
		{
			gdouble now = g_timer_elapsed (r->zero, NULL);

			while (r->lead != sequence_number) {
				if (RXW_LENGTH(r) == RXW_SQNS(r)) {
					g_warning ("full, dropping packet ;(");
					return -1;
				}

				struct rxw_packet* ph = rxw_alloc0_packet(r);
				ph->link_.data		= ph;
				ph->sequence_number     = r->lead++;
				ph->bo_start		= now;

				RXW_CHECK_WRAPAROUND(r, ph->sequence_number);
				RXW_SET_PACKET(r, ph->sequence_number, ph);
				g_debug ("#%u: adding placeholder #%u",
					sequence_number, ph->sequence_number);

/* send nak by sending to end of expiry list */
				g_queue_push_head_link (r->backoff_queue, &ph->link_);
				g_debug ("#%" G_GUINT32_FORMAT ": backoff_queue now %u long",
					sequence_number, r->backoff_queue->length);
			}
		}

/* r->lead = sequence_number; */
		
		if (RXW_LENGTH(r) == RXW_SQNS(r)) {
			g_warning ("full, dropping packet ;(");
			return -1;
		}

		struct rxw_packet* rp = rxw_alloc0_packet(r);
		rp->data                = packet;
		rp->length              = length;
		rp->sequence_number     = r->lead++;
		rp->state		= PGM_PKT_HAVE_DATA_STATE;

		RXW_CHECK_WRAPAROUND(r, rp->sequence_number);
		RXW_SET_PACKET(r, rp->sequence_number, rp);
		g_debug ("#%" G_GUINT32_FORMAT ": adding packet #%" G_GUINT32_FORMAT,
			sequence_number, rp->sequence_number);
	}

/* check for contiguous packets to pass upstream */
	g_debug ("#%" G_GUINT32_FORMAT ": flush window for contiguous data.",
		sequence_number);

	for (guint32 peak = r->trail, peak_end = r->lead; peak <= peak_end; peak++)
	{
		struct rxw_packet* pp = RXW_PACKET(r, peak);

		if (!pp || pp->state != PGM_PKT_HAVE_DATA_STATE)
		{
			if (!pp) {
				g_debug ("#%" G_GUINT32_FORMAT ": pp = NULL", sequence_number);
			}
			if (pp && pp->state != PGM_PKT_HAVE_DATA_STATE) {
				g_debug ("#%" G_GUINT32_FORMAT ": !have_data_state pp->length = %u",sequence_number, pp->length);
			}
			break;
		}

		g_debug ("#%" G_GUINT32_FORMAT ": contiguous packet found @ #%" G_GUINT32_FORMAT ", passing upstream.",
			sequence_number, pp->sequence_number);

		if (r->trail == r->lead)
			r->lead++;
		r->trail++;

/* pass upstream */
		if (r->on_data) r->on_data (pp->data, pp->length, r->param);

/* cleanup */
		rxw_pkt_free1 (r, pp);
		RXW_SET_PACKET(r, peak, NULL);
	}

	g_debug ("#%u: push() complete.",
		sequence_number);

	return 0;
}

int
rxw_pkt_state_unlink (
	struct rxw*	r,
	struct rxw_packet*	rp
	)
{
	g_return_val_if_fail (r != NULL && rp != NULL, -1);

/* remove from state queues */
	switch (rp->state) {
	case PGM_PKT_BACK_OFF_STATE:
		g_queue_unlink (r->backoff_queue, &rp->link_);
		break;

	case PGM_PKT_WAIT_NCF_STATE:
		g_queue_unlink (r->wait_ncf_queue, &rp->link_);
		break;

	case PGM_PKT_WAIT_DATA_STATE:
		g_queue_unlink (r->wait_data_queue, &rp->link_);
		break;

	case PGM_PKT_HAVE_DATA_STATE:
		break;

	default:
		g_assert_not_reached();
		break;
	}

	rp->link_.prev = NULL;
	rp->link_.next = NULL;

	return 0;
}

int
rxw_pkt_free1 (
	struct rxw*	r,
	struct rxw_packet*	rp
	)
{
	g_return_val_if_fail (r != NULL && rp != NULL, -1);

	if (rp->data)
	{
//		g_slice_free1 (rp->length, rp->data);
		g_trash_stack_push (&r->trash_data, rp->data);
		rp->data = NULL;
	}

//	g_slice_free1 (sizeof(struct rxw), rp);
	g_trash_stack_push (&r->trash_packet, rp);

	return 0;
}

/* remove from leading edge of ahead side of receive window */
int
rxw_pop (
	struct rxw*	r
	)
{
/* check if window is not empty */
	g_return_val_if_fail (RXW_SQNS(r), -1);

	struct rxw_packet* rp = RXW_PACKET(r, r->lead);

	rxw_pkt_state_unlink (r, rp);
	rxw_pkt_free1 (r, rp);

	r->lead--;

	return 0;
}

/* update receiving window with new trailing and leading edge parameters of transmit window
 * can generate data loss by excluding outstanding NAK requests.
 */
int
rxw_window_update (
	gpointer	ptr,
	guint32		txw_trail,
	guint32		txw_lead
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct rxw* r = (struct rxw*)ptr;

	if (SLIDINGWINDOW_GT(txw_trail, r->rxw_trail, txw_lead))
	{
		g_debug ("advancing rxw_trail to %u",
			txw_trail);
		r->rxw_trail = txw_trail;

/* expire outstanding naks ... */
		while (SLIDINGWINDOW_GT(r->rxw_trail, r->trail, txw_lead))
		{
			struct rxw_packet* dp = RXW_PACKET(r, r->trail);

			g_warning ("lost data #%u",
				dp->sequence_number);

			rxw_pkt_state_unlink (r, dp);
			rxw_pkt_free1 (r, dp);
			RXW_SET_PACKET(r, r->trail, NULL);

			if (r->trail == r->lead)
				r->lead++;
			r->trail++;
		}
	}

	if ( txw_lead > r->lead && txw_lead <= ( r->lead + ((UINT32_MAX/2) - 1)) )
	{
		g_debug ("advancing lead to %u",
			txw_lead);

		if ( r->lead == txw_lead)
			return 0;

/* generate new naks ... */
		gdouble now = g_timer_elapsed (r->zero, NULL);

		while ( r->lead != txw_lead )
		{
			if (RXW_LENGTH(r) == RXW_SQNS(r)) {
				g_warning ("full, dropping packet ;(");
				return -1;
			}

			struct rxw_packet* ph = rxw_alloc0_packet(r);
			ph->link_.data		= ph;
			ph->sequence_number     = r->lead++;
			ph->bo_start		= now;

			RXW_CHECK_WRAPAROUND(r, ph->sequence_number);
			RXW_SET_PACKET(r, ph->sequence_number, ph);
			g_debug ("adding placeholder #%u",
				ph->sequence_number);

/* send nak by sending to end of expiry list */
			g_queue_push_head_link (r->backoff_queue, &ph->link_);
		}
	}

	return 0;
}

/* received a uni/multicast ncf, search for a matching nak & tag or extend window if
 * beyond lead
 */

int
rxw_ncf (
	gpointer	ptr,
	guint32		sequence_number
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct rxw* r = (struct rxw*)ptr;

	struct rxw_packet* rp = RXW_PACKET(r, sequence_number);

	if (rp)
	{
		rp->ncf_received = g_timer_elapsed (r->zero, NULL);

/* already received ncf */
		if (rp->state == PGM_PKT_WAIT_DATA_STATE)
		{
			return 0;	/* ignore */
		}

		g_assert (rp->state == PGM_PKT_BACK_OFF_STATE || rp->state == PGM_PKT_WAIT_NCF_STATE);

		rxw_pkt_state_unlink (r, rp);
		g_queue_push_head_link (r->wait_data_queue, &rp->link_);

		return 0;
	}

/* not an expected ncf, extend receive window to pre-empt loss detection */
	if ( !IN_TXW(r, sequence_number) )
	{
		g_warning ("ncf #%u not in tx window, discarding.",
			sequence_number);
		return -1;
	}

	g_debug ("ncf extends leads to #%u",
		sequence_number);

	gdouble now = g_timer_elapsed (r->zero, NULL);

	while (r->lead != sequence_number)
	{
		if (RXW_LENGTH(r) == RXW_SQNS(r)) {
			g_warning ("full, ignoring extension.");
			return -1;
		}

		struct rxw_packet* ph = rxw_alloc0_packet(r);
		ph->link_.data		= ph;
		ph->sequence_number     = r->lead++;
		ph->bo_start		= now;

		RXW_CHECK_WRAPAROUND(r, ph->sequence_number);
		RXW_SET_PACKET(r, ph->sequence_number, ph);
		g_debug ("ncf: adding placeholder #%u",
			ph->sequence_number);

/* send nak by sending to end of expiry list */
		g_queue_push_head_link (r->backoff_queue, &ph->link_);
	}

/* r->lead = ncf sequence_number; */

	if (RXW_LENGTH(r) == RXW_SQNS(r)) {
		g_warning ("full, ignoring extension.");
		return -1;
	}

	struct rxw_packet* ph = rxw_alloc0_packet(r);
	ph->link_.data		= ph;
	ph->sequence_number     = r->lead++;
	ph->state		= PGM_PKT_WAIT_DATA_STATE;
	ph->ncf_received	= now;

	RXW_CHECK_WRAPAROUND(r, ph->sequence_number);
	RXW_SET_PACKET(r, ph->sequence_number, ph);
	g_debug ("ncf: adding placeholder #%u",
		ph->sequence_number);

/* do not send nak, simply add to ncf list */
	g_queue_push_head_link (r->wait_data_queue, &ph->link_);

	return 0;
}

/* iterate tail of queue, with #s to send naks on, then expired naks to re-send.
 */

int
rxw_state_foreach (
	gpointer	ptr,
	pgm_pkt_state	state,
	rxw_state_callback	state_callback,
	gpointer	param
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct rxw* r = (struct rxw*)ptr;

	GList* list = NULL;
	switch (state) {
	case PGM_PKT_BACK_OFF_STATE:	list = r->backoff_queue->tail; break;
	case PGM_PKT_WAIT_NCF_STATE:	list = r->wait_ncf_queue->tail; break;
	case PGM_PKT_WAIT_DATA_STATE:	list = r->wait_data_queue->tail; break;
	default: g_assert_not_reached(); break;
	}

	if (!list) return;

/* minimize timer checks in the loop */
	gdouble now = g_timer_elapsed(r->zero, NULL);

	while (list)
	{
		GList* next_list = list->prev;
		struct rxw_packet* rp = (struct rxw_packet*)list->data;

		gdouble age = 0.0;
		guint retry_count = 0;

		g_assert (rp->state == state);

		rxw_pkt_state_unlink (r, rp);

		switch (state) {
		case PGM_PKT_BACK_OFF_STATE:
			age		= now - rp->bo_start;
			break;

		case PGM_PKT_WAIT_NCF_STATE:
			age		= now - rp->nak_sent;
			retry_count	= rp->ncf_retry_count;
			break;

		case PGM_PKT_WAIT_DATA_STATE:
			age		= now - rp->ncf_received;
			retry_count	= rp->data_retry_count;
			break;

		default:
			g_assert_not_reached();
			break;
		}

		if ( (*state_callback)(rp->data,
					rp->length,
					rp->sequence_number,
					&rp->state,
					age,
					retry_count,
					param) )
		{
			break;
		}


/* callback should return TRUE and cease iteration for no state change */
		g_assert (rp->state != state);

		switch (rp->state) {	/* new state change */
/* send nak later */
		case PGM_PKT_BACK_OFF_STATE:
			rp->bo_start = now;
			g_queue_push_head_link (r->backoff_queue, &rp->link_);
			break;

/* nak sent, await ncf */
		case PGM_PKT_WAIT_NCF_STATE:
			rp->nak_sent = now;
			g_queue_push_head_link (r->wait_ncf_queue, &rp->link_);
			break;

/* cancelled */
		case PGM_PKT_LOST_DATA_STATE:
			for (;;)
			{
				struct rxw_packet* dp = RXW_PACKET(r, r->trail);

				g_warning ("lost data #%u",
					dp->sequence_number);

				rxw_pkt_state_unlink (r, dp);
				rxw_pkt_free1 (r, dp);
				RXW_SET_PACKET(r, r->trail, NULL);

				if (r->trail == r->lead)
					r->lead++;
				r->trail++;
			}
			break;

		default:
			g_assert_not_reached();
		}

		list = next_list;
	}

	return 0;
}

/* eof */
