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

	struct timeval	loss_detected;
	struct timeval	nak_sent;
	struct timeval	ncf_received;
	GList*		nak;			/* pointer to GList element contained this in rxw.nak_list */
	GList*		ncf;			/*  ... rxw.ncf_list */

	pgm_pkt_state	state;
	guint		ncf_retry_count;
	guint		data_retry_count;
};

struct rxw {
	GPtrArray*	pdata;
	GTrashStack*	trash_packet;		/* sizeof(rxw_packet) */
	GTrashStack*	trash_data;		/* max_tpdu */

	GQueue*		nak_list;
	GQueue*		ncf_list;

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
	( (struct rxw_packet*)( g_ptr_array_index ((w)->pdata, RXW_PACKET_OFFSET((w), (x))) ) )

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
#define G_LOG_DOMAIN	"rxw"

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

	g_debug ("%i packets.", rxw_sqns);
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
	r->nak_list = g_queue_new ();
	r->ncf_list = g_queue_new ();

	r->on_data = on_data;
	r->param = param;

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
		g_queue_free (r->nak_list);
		r->nak_list = NULL;
	}
	if (r->ncf_list)
	{
		g_queue_free (r->ncf_list);
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
	if (data == NULL) return;

	struct rxw_packet *rp = (struct rxw_packet*)data;
//	int length = rp->length;

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
	g_return_val_if_fail (ptr != NULL, NULL);
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
			rp->state	= PGM_PKT_HAVE_DATA_STATE;

/* clean up lists */
			if (rp->nak) {
				g_queue_delete_link (r->nak_list, rp->nak);
				rp->nak = NULL;
			}
			if (rp->ncf) {
				g_queue_delete_link (r->ncf_list, rp->ncf);
				rp->ncf = NULL;
			}
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

		while (r->lead != sequence_number) {
			if (RXW_LENGTH(r) == RXW_SQNS(r)) {
				g_warning ("full, dropping packet ;(");
				return -1;
			}

			struct rxw_packet* ph = r->trash_packet ?
							g_trash_stack_pop (&r->trash_packet) :
							g_slice_alloc (sizeof(struct rxw_packet));
			memset (ph, 0, sizeof(struct rxw_packet));
			ph->sequence_number     = r->lead++;

			if (ph->sequence_number == ( RXW_LENGTH(r) + r->offset ))
			{
				r->offset += RXW_LENGTH(r);
			}

			guint offset = ph->sequence_number - r->offset;
			g_ptr_array_index (r->pdata, offset) = ph;
			g_debug ("#%u: adding placeholder #%u",
				sequence_number, ph->sequence_number);

/* send nak by sending to end of expiry list */
			g_queue_push_head (r->nak_list, ph);
			ph->nak = r->nak_list->head;
		}

/* r->lead = sequence_number; */
		
		if (RXW_LENGTH(r) == RXW_SQNS(r)) {
			g_warning ("full, dropping packet ;(");
			return -1;
		}

		struct rxw_packet* rp = r->trash_packet ?
						g_trash_stack_pop (&r->trash_packet) :
						g_slice_alloc (sizeof(struct rxw_packet));
		rp->data                = packet;
		rp->length              = length;
		rp->sequence_number     = r->lead;
		rp->state		= PGM_PKT_HAVE_DATA_STATE;

		if (rp->sequence_number == ( RXW_LENGTH(r) + r->offset ))
		{
			r->offset += RXW_LENGTH(r);
		}

		guint offset = rp->sequence_number - r->offset;
		g_ptr_array_index (r->pdata, offset) = rp;
		g_debug ("#%u: adding packet #%u",
			sequence_number, rp->sequence_number);
	}

/* check for contigious packets to pass upstream */
	g_debug ("#%u: flush window for contigious data.",
		sequence_number);

	for (guint32 peak = r->trail, peak_end = r->lead; peak <= peak_end; peak++)
	{
		struct rxw_packet* pp = RXW_PACKET(r, peak);

		if (!pp || pp->state != PGM_PKT_HAVE_DATA_STATE)
		{
			if (!pp) {
				g_debug ("#%u: pp = NULL", sequence_number);
			}
			if (pp->state != PGM_PKT_HAVE_DATA_STATE) {
				g_debug ("#%u: !have_data_state pp->length = %u", sequence_number, pp->length);
			}
			break;
		}

		g_debug ("#%u: contigious packet found @ #%u, passing upstream.",
			sequence_number, pp->sequence_number);

		if (r->trail == r->lead)
			r->lead++;
		r->trail++;

/* pass upstream */
		if (r->on_data) r->on_data (pp->data, pp->length, r->param);

/* cleanup */
//		g_slice_free1 (pp->length, pp->data);
		g_trash_stack_push (&r->trash_data, pp->data);

//		g_slice_free1 (sizeof(struct rxw), pp);
		g_trash_stack_push (&r->trash_packet, pp);

		g_ptr_array_index (r->pdata, RXW_PACKET_OFFSET(r, peak)) = NULL;
	}

	g_debug ("#%u: push() complete.",
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
	g_return_val_if_fail (RXW_SQNS(r), -1);

	struct rxw_packet* rp = RXW_PACKET(r, r->lead);

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
	}

	if ( txw_lead > r->lead && txw_lead <= ( r->lead + ((UINT32_MAX/2) - 1)) )
	{
		g_debug ("advancing lead* to %u",
			txw_lead);

/* generate new naks ... */
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
		gettimeofday (&rp->ncf_received, NULL);

/* already received ncf */
		if (rp->state == PGM_PKT_WAIT_DATA_STATE)
		{
			return 0;	/* ignore */
		}

		g_assert (rp->state == PGM_PKT_BACK_OFF_STATE || rp->state == PGM_PKT_WAIT_NCF_STATE);

/* push onto head of ncf expiry list */
		g_queue_push_head (r->ncf_list, rp);
		rp->ncf = r->ncf_list->head;

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

	while (r->lead != sequence_number)
	{
		if (RXW_LENGTH(r) == RXW_SQNS(r)) {
			g_warning ("full, ignoring extension.");
			return -1;
		}

		struct rxw_packet* ph = r->trash_packet ?
						g_trash_stack_pop (&r->trash_packet) :
						g_slice_alloc (sizeof(struct rxw_packet));
		memset (ph, 0, sizeof(struct rxw_packet));
		ph->sequence_number     = r->lead++;

		if (ph->sequence_number == ( RXW_LENGTH(r) + r->offset ))
		{
			r->offset += RXW_LENGTH(r);
		}

		guint offset = ph->sequence_number - r->offset;
		g_ptr_array_index (r->pdata, offset) = ph;
		g_debug ("ncf: adding placeholder #%u",
			ph->sequence_number);

/* send nak by sending to end of expiry list */
		g_queue_push_tail (r->nak_list, ph);
		ph->nak = r->nak_list->tail;
	}

/* r->lead = ncf sequence_number; */

	if (RXW_LENGTH(r) == RXW_SQNS(r)) {
		g_warning ("full, ignoring extension.");
		return -1;
	}

	struct rxw_packet* ph = r->trash_packet ?
					g_trash_stack_pop (&r->trash_packet) :
					g_slice_alloc (sizeof(struct rxw_packet));
	memset (ph, 0, sizeof(struct rxw_packet));
	ph->sequence_number     = r->lead;
	ph->state		= PGM_PKT_WAIT_DATA_STATE;

	if (ph->sequence_number == ( RXW_LENGTH(r) + r->offset ))
	{
		r->offset += RXW_LENGTH(r);
	}

	guint offset = ph->sequence_number - r->offset;
	g_ptr_array_index (r->pdata, offset) = ph;
	g_debug ("ncf: adding placeholder #%u",
		ph->sequence_number);

/* do not send nak, simply add to ncf list */
	g_queue_push_head (r->ncf_list, ph);
	ph->ncf = r->ncf_list->head;

	return 0;
}

/* iterate tail of queue, with #s to send naks on, then expired naks to re-send.
 */

int
rxw_nak_list_foreach (
	gpointer	ptr,
	rxw_nak_callback	nak_callback,
	gpointer	param
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct rxw* r = (struct rxw*)ptr;

	GList* list = r->nak_list->tail;
	while (list)
	{
		struct rxw_packet* rp = (struct rxw_packet*)list->data;

		if ( (*nak_callback)(rp->data,
					rp->length,
					rp->sequence_number,
					&rp->state,
					param) )
		{
			break;
		}

		GList* next_list = list->prev;

		switch (rp->state) {
		case PGM_PKT_BACK_OFF_STATE:
/* send nak later */
			break;

		case PGM_PKT_WAIT_NCF_STATE:
/* nak sent */
			gettimeofday (&rp->nak_sent, NULL);

/* transfer to ncf list */
			g_queue_unlink (r->nak_list, list);
			rp->nak = NULL;
			g_queue_push_head_link (r->ncf_list, list);
			rp->ncf = r->ncf_list->head;
			break;

		case PGM_PKT_LOST_DATA_STATE:
/* cancelled */
			g_queue_delete_link (r->nak_list, list);
			rp->nak = NULL;

			for (;;)
			{
				struct rxw_packet* dp = RXW_PACKET(r, r->trail);

				g_warning ("lost data #%u",
					dp->sequence_number);

				if (dp->nak) {
					r->nak_list = g_list_delete_link (r->nak_list, dp->nak);
					dp->nak = NULL;
				}
				if (dp->ncf) {
					r->ncf_list = g_list_delete_link (r->ncf_list, dp->ncf);
					dp->ncf = NULL;
				}
				if (dp->data) {
//					g_slice_free1 (rp->length, dp->data);
					g_trash_stack_push (&r->trash_data, dp->data);
				}
//				g_slice_free1 (sizeof(struct rxw), dp);
				g_trash_stack_push (&r->trash_packet, dp);

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

int
rxw_ncf_list_foreach (
	gpointer	ptr,
	rxw_nak_callback	ncf_callback,
	gpointer	param
	)
{
	g_return_val_if_fail (ptr != NULL, -1);
	struct rxw* r = (struct rxw*)ptr;

	GList* list = r->ncf_list->tail;
	while (list)
	{
		struct rxw_packet* rp = (struct rxw_packet*)list->data;

		if ( (*ncf_callback)(rp->data,
					rp->length,
					rp->sequence_number,
					&rp->state,
					param) )
		{
			break;
		}

		GList* next_list = list->prev;

		switch (rp->state) {
		case PGM_PKT_BACK_OFF_STATE:
/* send nak later */
			g_queue_unlink (r->ncf_list, list);
			rp->ncf = NULL;
			g_queue_push_head_link (r->nak_list, list);
			rp->nak = r->nak_list->head;
			break;

		case PGM_PKT_WAIT_NCF_STATE:
/* nak sent */
			gettimeofday (&rp->nak_sent, NULL);
			break;

		case PGM_PKT_LOST_DATA_STATE:
/* cancelled */
			g_queue_delete_link (r->ncf_list, list);
			rp->ncf = NULL;

			for (;;)
			{
				struct rxw_packet* dp = RXW_PACKET(r, r->trail);

				g_warning ("lost data #%u",
					dp->sequence_number);

				if (dp->nak) {
					r->nak_list = g_list_delete_link (r->nak_list, dp->nak);
					dp->nak = NULL;
				}
				if (dp->ncf) {
					r->ncf_list = g_list_delete_link (r->ncf_list, dp->ncf);
					dp->ncf = NULL;
				}
				if (dp->data) {
//					g_slice_free1 (rp->length, dp->data);
					g_trash_stack_push (&r->trash_data, dp->data);
				}
//				g_slice_free1 (sizeof(struct rxw), dp);
				g_trash_stack_push (&r->trash_packet, dp);

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
