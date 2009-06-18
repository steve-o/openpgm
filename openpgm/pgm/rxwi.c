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
#include <sys/uio.h>

//#define RXW_DEBUG

#ifndef RXW_DEBUG
#define G_DISABLE_ASSERT
#endif

#include <glib.h>

#include "pgm/rxwi.h"
#include "pgm/sn.h"
#include "pgm/timer.h"

#ifndef RXW_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* globals */

/* local globals */

#define IN_TXW(w,x)	( pgm_uint32_gte ( (x), (w)->rxw_trail ) )
#define IN_RXW(w,x) \
	( \
		pgm_uint32_gte ( (x), (w)->rxw_trail ) && pgm_uint32_lte ( (x), (w)->lead ) \
	)

#define ABS_IN_RXW(w,x) \
	( \
		!pgm_rxw_empty( (w) ) && \
		pgm_uint32_gte ( (x), (w)->trail ) && pgm_uint32_lte ( (x), (w)->lead ) \
	)

#define RXW_SKB_OFFSET(w,x)		( (x) % pgm_rxw_len ((w)) ) 
#define RXW_SKB(w,x) \
	( (struct pgm_sk_buff_t*)g_ptr_array_index(&(w)->pdata, RXW_SKB_OFFSET((w), (x))) )
#define RXW_PACKET(w,x) \
	( (struct pgm_rxw_packet_t*)&(RXW_SKB(w,x))->cb )
#define RXW_SET_SKB(w,x,v) \
	do { \
		register int _o = RXW_SKB_OFFSET((w), (x)); \
		g_ptr_array_index(&(w)->pdata, _o) = (v); \
	} while (0)

/* is (a) greater than (b) wrt. leading edge of receive window (w) */
#define SLIDINGWINDOW_GT(w,a,b) \
	( \
		pgm_rxw_empty( (w) ) ? \
		( \
			( (gint32)(a) - (gint32)( (w)->trail ) ) > ( (gint32)(b) - (gint32)( (w)->trail ) ) \
		) \
			: \
		( \
			( (gint32)(a) - (gint32)( (w)->lead ) ) > ( (gint32)(b) - (gint32)( (w)->lead ) ) \
		) \
	)

#ifdef RXW_DEBUG
#define ASSERT_RXW_BASE_INVARIANT(w) \
	{ \
		g_assert ( (w) != NULL ); \
\
/* does the array exist */ \
		g_assert ( (w)->pdata.len > 0 ); \
\
/* packet size has been set */ \
		g_assert ( (w)->max_tpdu > 0 ) ; \
\
/* all pointers are within window bounds */ \
		if ( !pgm_rxw_empty( (w) ) ) /* empty: trail = lead + 1, hence wrap around */ \
		{ \
			g_assert ( RXW_SKB_OFFSET( (w), (w)->lead ) < (w)->pdata.len ); \
			g_assert ( RXW_SKB_OFFSET( (w), (w)->trail ) < (w)->pdata.len ); \
		} \
\
	}

#define ASSERT_RXW_POINTER_INVARIANT(w) \
	{ \
/* are trail & lead points valid */ \
		if ( !pgm_rxw_empty( (w) ) ) \
		{ \
			g_assert ( NULL != RXW_SKB( (w) , (w)->trail ) );	/* trail points to something */ \
			g_assert ( NULL != RXW_SKB( (w) , (w)->lead ) );	/* lead points to something */ \
\
/* queue's contain at least one packet */ \
			if ( !(w)->is_waiting ) \
			{ \
				g_assert ( ( (w)->backoff_queue.length + \
					     (w)->wait_ncf_queue.length + \
					     (w)->wait_data_queue.length + \
					     (w)->lost_count + \
					     (w)->fragment_count + \
					     (w)->parity_count + \
					     (w)->committed_count + \
					     (w)->parity_data_count ) > 0 ); \
			} \
		} \
		else \
		{ \
			g_assert ( (w)->backoff_queue.length == 0 ); \
			g_assert ( (w)->wait_ncf_queue.length == 0 ); \
			g_assert ( (w)->wait_data_queue.length == 0 ); \
			g_assert ( (w)->lost_count == 0 ); \
			g_assert ( (w)->fragment_count == 0 ); \
			g_assert ( (w)->parity_count == 0 ); \
			g_assert ( (w)->committed_count == 0 ); \
			g_assert ( (w)->parity_data_count == 0 ); \
		} \
	}
#else
#define ASSERT_RXW_BASE_INVARIANT(w)    while(0)
#define ASSERT_RXW_POINTER_INVARIANT(w) while(0)
#endif


static inline int pgm_rxw_pop_lead (pgm_rxw_t*);
static inline int pgm_rxw_pop_trail (pgm_rxw_t*);


/* sub-windows of the receive window
 *                                      r->lead
 *  | Parity-data | Commit |   Incoming   |
 *  |<----------->|<------>|<------------>|
 *  |             |        |              |
 * r->trail    r->commit_trail
 *                       r->commit_lead
 */

static inline guint32
pgm_rxw_incoming_empty (pgm_rxw_t* r)
{
	return r->commit_lead == r->lead + 1;
}

static inline guint32
pgm_rxw_commit_empty (pgm_rxw_t* r)
{
	return r->commit_trail == r->commit_lead;
}

static inline guint32
pgm_rxw_parity_data_empty (pgm_rxw_t* r)
{
	return r->trail == r->commit_trail;
}


pgm_rxw_t*
pgm_rxw_init (
	const void*	identifier,		/* TSI */
	guint16		tpdu_length,
	guint32		rxw_sqns,		/* transmit window size in sequence numbers */
	guint		rxw_secs,		/* size in seconds */
	guint		rxw_max_rte		/* max bandwidth */
	)
{
	g_trace ("init (tpdu %i rxw_sqns %i rxw_secs %i rxw_max_rte %i).",
		tpdu_length, rxw_sqns, rxw_secs, rxw_max_rte);

	pgm_rxw_t* r = g_slice_alloc0 (sizeof(pgm_rxw_t));
	r->identifier = identifier;
	r->max_tpdu = tpdu_length;

/* calculate receive window parameters as per transmit window */
	if (rxw_sqns)
	{
	}
	else if (rxw_secs && rxw_max_rte)
	{
		rxw_sqns = (rxw_secs * rxw_max_rte) / r->max_tpdu;
	}

/* pointer array */
	r->pdata.alloc = r->pdata.len = rxw_sqns;
	r->pdata.pdata = g_malloc0 (sizeof(gpointer) * r->pdata.alloc);

/* empty state:
 *
 * trail = 0, lead = -1
 * commit_trail = commit_lead = rxw_trail = rxw_trail_init = 0
 */
	r->lead = -1;
	r->trail = r->lead + 1;

/* limit retransmit requests on late session joining */
	r->is_rxw_constrained = TRUE;
	r->is_window_defined = FALSE;

/* statistics */
#if 0
	r->min_fill_time = G_MAXINT;
	r->max_fill_time = G_MININT;
	r->min_nak_transmit_count = G_MAXINT;
	r->max_nak_transmit_count = G_MININT;
#endif

#ifdef RXW_DEBUG
	guint memory = sizeof(pgm_rxw_t) +
/* pointer array */
			( sizeof(gpointer) * r->pdata.alloc );
			
	g_trace ("memory usage: %ub (%uMb)", memory, memory / (1024 * 1024));
#endif

	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);
	return r;
}

int
pgm_rxw_shutdown (
	pgm_rxw_t*	r
	)
{
	g_trace ("rxw: shutdown.");

	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);

/* pointer array */
	for (guint i = 0; i < r->pdata.len; i++)
	{
		if (r->pdata.pdata[i]) {
			pgm_free_skb (r->pdata.pdata[i]);
			r->pdata.pdata[i] = NULL;
		}
	}
	g_free (r->pdata.pdata);
	r->pdata.pdata = NULL;

/* window */
	g_slice_free1 (sizeof(pgm_rxw_t), r);

	return PGM_RXW_OK;
}

/* the sequence number is inside the packet as opposed to from internal
 * counters, this means one push on the receive window can actually translate
 * as many: the extra's acting as place holders and NAK containers.
 *
 * if ownership of packet is taken return flag PGM_RXW_CONSUMED_SKB is set.
 * if packet advances a new apdu then the flag PGM_RXW_NEW_APDU is set.
 *
 * returns:
 *	PGM_RXW_CREATED_PLACEHOLDER
 *	PGM_RXW_FILLED_PLACEHOLDER
 *	PGM_RXW_ADVANCED_WINDOW
 *	PGM_RXW_NOT_IN_TXW if sequence number is very wibbly wobbly
 *	PGM_RXW_DUPLICATE
 *	PGM_RXW_APDU_LOST
 *	PGM_RXW_MALFORMED_APDU
 */

int
pgm_rxw_push (
	pgm_rxw_t*		r,
	struct pgm_sk_buff_t*	skb,
	pgm_time_t		nak_rb_expiry
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);

	guint         dropped        	= 0;
	int           retval          	= PGM_RXW_UNKNOWN;
	const guint32 data_sqn 		= g_ntohl (skb->pgm_data->data_sqn);
	const guint32 data_trail 	= g_ntohl (skb->pgm_data->data_trail);

/* convert to more apparent names */
	const guint32 apdu_first_sqn	= skb->pgm_opt_fragment ? g_ntohl (skb->pgm_opt_fragment->opt_sqn) : 0;
	const guint32 apdu_len		= skb->pgm_opt_fragment ? g_ntohl (skb->pgm_opt_fragment->opt_frag_len) : 0;

	g_trace ("#%u: data trail #%u: push: window ( rxw_trail %u rxw_trail_init %u trail %u lead %u )",
		data_sqn, data_trail, 
		r->rxw_trail, r->rxw_trail_init, r->trail, r->lead);

/* trail is the next packet to commit upstream, lead is the leading edge
 * of the receive window with possible gaps inside, rxw_trail is the transmit
 * window trail for retransmit requests.
 */

	if ( !r->is_window_defined )
	{
/* if this packet is a fragment of an apdu, and not the first, we continue on as per spec but careful to
 * advance the trailing edge to discard the remaining fragments.
 */
		g_trace ("#%u: using odata to temporarily define window", skb->sequence);

		r->lead = data_sqn - 1;
		r->commit_trail = r->commit_lead = r->rxw_trail = r->rxw_trail_init = r->trail = r->lead + 1;

		r->is_rxw_constrained = TRUE;
		r->is_window_defined = TRUE;
	}
	else
	{
/* check if packet should be discarded or processed further */

		if ( !IN_TXW(r, data_sqn) )
		{
			g_trace ("#%u: not in transmit window, discarding.", data_sqn);
			retval = PGM_RXW_NOT_IN_TXW;
			goto out;
		}

		pgm_rxw_window_update (r, data_trail, r->lead, r->tg_size, r->tg_sqn_shift, nak_rb_expiry);
	}

	g_trace ("#%u: window ( rxw_trail %u rxw_trail_init %u trail %u commit_trail %u commit_lead %u lead %u )",
		data_sqn, r->rxw_trail, r->rxw_trail_init, r->trail, r->commit_trail, r->commit_lead, r->lead);
	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);

/* already committed */
	if ( pgm_uint32_lt (data_sqn, r->commit_lead) )
	{
		g_trace ("#%u: already committed, discarding.", data_sqn);
		retval = PGM_RXW_DUPLICATE;
		goto out;
	}

/* check for duplicate */
	if ( pgm_uint32_lte (data_sqn, r->lead) )
	{
		g_trace ("#%u: in rx window, checking for duplicate.", data_sqn);

		struct pgm_sk_buff_t* rx_skb	= RXW_SKB(r, data_sqn);
		pgm_rxw_packet_t* rx_pkt	= (pgm_rxw_packet_t*)&rx_skb->cb;

		if (rx_skb)
		{
			if ( rx_skb->len && rx_pkt->state == PGM_PKT_HAVE_DATA_STATE )
			{
				g_trace ("#%u: already received, discarding.", data_sqn);
				retval = PGM_RXW_DUPLICATE;
				goto out;
			}

/* for fragments check that apdu is valid */
			if (	apdu_len && 
				apdu_first_sqn != data_sqn &&
				(
					pgm_rxw_empty (r) ||
				       !ABS_IN_RXW(r, apdu_first_sqn) ||
					RXW_PACKET(r, apdu_first_sqn)->state == PGM_PKT_LOST_DATA_STATE
				)
			   )
			{
				g_trace ("#%u: first fragment #%u not in receive window, apdu is lost.", data_sqn, apdu_first_sqn);
				pgm_rxw_mark_lost (r, data_sqn);
				retval = PGM_RXW_APDU_LOST;
				goto out_flush;
			}

			if ( apdu_len && pgm_uint32_gt (apdu_first_sqn, data_sqn) )
			{
				g_trace ("#%u: first apdu fragment sequence number: #%u not lowest, ignoring packet.",
					data_sqn, apdu_first_sqn);
				retval = PGM_RXW_MALFORMED_APDU;
				goto out;
			}

/* destination should not contain a data packet, although it may contain parity */
			g_assert( rx_pkt->state == PGM_PKT_BACK_OFF_STATE ||
				  rx_pkt->state == PGM_PKT_WAIT_NCF_STATE ||
				  rx_pkt->state == PGM_PKT_WAIT_DATA_STATE ||
				  rx_pkt->state == PGM_PKT_HAVE_PARITY_STATE ||
				  rx_pkt->state == PGM_PKT_LOST_DATA_STATE );
			g_trace ("#%u: filling in a gap.", data_sqn);

			if ( rx_pkt->state == PGM_PKT_HAVE_PARITY_STATE )
			{
				g_trace ("#%u: destination contains parity, shuffling to next available entry.", data_sqn);
/* find if any other packets are lost in this transmission group */

				const guint32 tg_sqn_mask = 0xffffffff << r->tg_sqn_shift;
				const guint32 next_tg_sqn = (data_sqn & tg_sqn_mask) + 1;

				if (data_sqn != next_tg_sqn)
				for (guint32 i = data_sqn + 1; i != next_tg_sqn; i++)
				{
					struct pgm_sk_buff_t* parity_skb	= RXW_SKB(r, i);
					pgm_rxw_packet_t* parity_pkt		= (pgm_rxw_packet_t*)&parity_skb->cb;

					if ( parity_pkt->state == PGM_PKT_BACK_OFF_STATE ||
					     parity_pkt->state == PGM_PKT_WAIT_NCF_STATE ||
					     parity_pkt->state == PGM_PKT_WAIT_DATA_STATE ||
					     parity_pkt->state == PGM_PKT_LOST_DATA_STATE )
					{
						g_assert (parity_skb->len == 0);

/* move parity to this new sequence number, to reduce copying we simply
 * swap skb pointers and update the references.
 */
						memcpy (parity_skb->head, rx_skb->head, (guint8*)rx_skb->tail - (guint8*)rx_skb->head);
						parity_skb->data		= (guint8*)parity_skb->head + ((guint8*)rx_skb->data - (guint8*)rx_skb->head);
						parity_skb->tail		= (guint8*)parity_skb->data + ((guint8*)rx_skb->tail - (guint8*)rx_skb->data);
						parity_skb->len			= rx_skb->len;
						parity_skb->pgm_header    	= (gpointer)( (guint8*)parity_skb->head + ((guint8*)rx_skb->pgm_header - (guint8*)rx_skb->head) );
						parity_skb->pgm_opt_fragment	= (gpointer)( (guint8*)parity_skb->head + ((guint8*)rx_skb->pgm_opt_fragment - (guint8*)rx_skb->head) );
						parity_skb->pgm_data       	= (gpointer)( (guint8*)parity_skb->head + ((guint8*)rx_skb->pgm_data - (guint8*)rx_skb->head) );

						pgm_rxw_pkt_state_unlink (r, parity_skb);
						parity_pkt->state		= rx_pkt->state;
						rx_pkt->state			= PGM_PKT_WAIT_DATA_STATE;
						rx_skb->data			= rx_skb->tail = rx_skb->head;
						rx_skb->len			= 0;
						break;
					}
				}

/* no incomplete packet found, therefore parity is no longer required */
				if (rx_pkt->state != PGM_PKT_WAIT_DATA_STATE)
				{
					rx_pkt->state = PGM_PKT_WAIT_DATA_STATE;
				}
				g_queue_push_head_link (&r->wait_data_queue, &rx_skb->link_);
			}
			else if ( rx_pkt->state == PGM_PKT_LOST_DATA_STATE )	/* lucky packet */
			{
				r->lost_count--;
			}

/* a non-committed packet */
			r->fragment_count++;

			g_assert (rx_skb->len == 0);

/* swap place holder skb with incoming skb
 */
			memcpy (rx_skb->head, skb->head, (guint8*)skb->tail - (guint8*)skb->head);
			rx_skb->tstamp			= skb->tstamp;
			memcpy (&rx_skb->tsi, &skb->tsi, sizeof(pgm_tsi_t));
			rx_skb->data			= (guint8*)rx_skb->head + ((guint8*)skb->data - (guint8*)skb->head);
			rx_skb->tail			= (guint8*)rx_skb->data + ((guint8*)skb->tail - (guint8*)skb->data);
			rx_skb->len			= skb->len;
			rx_skb->pgm_header		= (gpointer)( (guint8*)rx_skb->head + ((guint8*)skb->pgm_header - (guint8*)skb->head) );
			rx_skb->pgm_opt_fragment	= skb->pgm_opt_fragment ?
								(gpointer)( (guint8*)rx_skb->head + ((guint8*)skb->pgm_opt_fragment - (guint8*)skb->head) ) :
								skb->pgm_opt_fragment;
			rx_skb->pgm_data		= (gpointer)( (guint8*)rx_skb->head + ((guint8*)skb->pgm_data - (guint8*)skb->head) );

			pgm_rxw_pkt_state_unlink (r, rx_skb);
			rx_pkt->state	= PGM_PKT_HAVE_DATA_STATE;
			retval		= PGM_RXW_FILLED_PLACEHOLDER;

			const guint32 fill_time = pgm_time_now - rx_pkt->t0;
			if (!r->max_fill_time) {
				r->max_fill_time = r->min_fill_time = fill_time;
			}
			else
			{
				if (fill_time > r->max_fill_time)
					r->max_fill_time = fill_time;
				else if (fill_time < r->min_fill_time)
					r->min_fill_time = fill_time;

				if (!r->max_nak_transmit_count) {
					r->max_nak_transmit_count = r->min_nak_transmit_count = rx_pkt->nak_transmit_count;
				}
				else
				{
					if (rx_pkt->nak_transmit_count > r->max_nak_transmit_count)
						r->max_nak_transmit_count = rx_pkt->nak_transmit_count;
					else if (rx_pkt->nak_transmit_count < r->min_nak_transmit_count)
						r->min_nak_transmit_count = rx_pkt->nak_transmit_count;
				}
			}
		}
		else
		{
			g_debug ("sequence_number %u points to (null) in window (trail %u commit_trail %u commit_lead %u lead %u).",
				data_sqn, r->trail, r->commit_trail, r->commit_lead, r->lead);
			ASSERT_RXW_BASE_INVARIANT(r);
			ASSERT_RXW_POINTER_INVARIANT(r);
			g_assert_not_reached();
		}
	}
	else	/* data_sqn > lead */
	{
/* extends receive window */

/* check bounds of commit window */
		guint32 new_commit_sqns = ( 1 + data_sqn ) - r->commit_trail;
                if ( !pgm_rxw_commit_empty (r) &&
		     (new_commit_sqns >= pgm_rxw_len (r)) )
                {
			pgm_rxw_window_update (r, r->rxw_trail, data_sqn, r->tg_size, r->tg_sqn_shift, nak_rb_expiry);
			goto out;
                }


		g_trace ("#%u: lead extended.", data_sqn);
		g_assert ( pgm_uint32_gt (data_sqn, r->lead) );

		if ( pgm_rxw_full(r) )
		{
			dropped++;
//			g_trace ("#%u: dropping #%u due to odata filling window.", sequence_number, r->trail);

			pgm_rxw_pop_trail (r);
//			pgm_rxw_flush (r);
		}

		r->lead++;

/* if packet is non-contiguous to current leading edge add place holders */
		if (r->lead != data_sqn)
		{
/* TODO: can be rather inefficient on packet loss looping through dropped sequence numbers
 */
			while (r->lead != data_sqn)
			{
				struct pgm_sk_buff_t* pad_skb	= pgm_alloc_skb (r->max_tpdu);
				pgm_rxw_packet_t* pad_pkt	= (pgm_rxw_packet_t*)&pad_skb->cb;
				pad_skb->sequence		= r->lead;
				pad_pkt->nak_rb_expiry		= nak_rb_expiry;
				pad_pkt->state			= PGM_PKT_BACK_OFF_STATE;
				pad_pkt->t0			= pgm_time_now;

				RXW_SET_SKB(r, pad_skb->sequence, pad_skb);

/* send nak by sending to end of expiry list */
				g_queue_push_head_link (&r->backoff_queue, &pad_skb->link_);
				g_trace ("#%" G_GUINT32_FORMAT ": place holder, backoff_queue %" G_GUINT32_FORMAT "/%u lead %" G_GUINT32_FORMAT,
					data_sqn, r->backoff_queue.length, pgm_rxw_sqns(r), r->lead);

				if ( pgm_rxw_full(r) )
				{
					dropped++;
//					g_trace ("dropping #%u due to odata filling window.", r->trail);

					pgm_rxw_pop_trail (r);
//					pgm_rxw_flush (r);
				}

				r->lead++;
			}
			retval = PGM_RXW_CREATED_PLACEHOLDER;
		}
		else
		{
			retval = PGM_RXW_ADVANCED_WINDOW;
		}

		g_assert ( r->lead == data_sqn );

/* sanity check on sequence number distance */
		if ( apdu_len && pgm_uint32_gt (apdu_first_sqn, data_sqn) )
		{
			g_trace ("#%u: first apdu fragment sequence number: #%u not lowest, ignoring packet.",
				data_sqn, apdu_first_sqn);
			retval = PGM_RXW_MALFORMED_APDU;
			goto out;
		}

		struct pgm_sk_buff_t* rx_skb	= pgm_alloc_skb (r->max_tpdu);
		pgm_rxw_packet_t* rx_pkt	= (pgm_rxw_packet_t*)&rx_skb->cb;
		rx_skb->sequence		= r->lead;

/* for fragments check that apdu is valid: dupe code to above */
		if (    apdu_len && 
			apdu_first_sqn != data_sqn &&
			(	
				pgm_rxw_empty (r) ||
			       !ABS_IN_RXW(r, apdu_first_sqn) ||
				RXW_PACKET(r, apdu_first_sqn)->state == PGM_PKT_LOST_DATA_STATE
			)
		   )
		{
			g_trace ("#%u: first fragment #%u not in receive window, apdu is lost.", data_sqn, apdu_first_sqn);
			rx_pkt->state = PGM_PKT_LOST_DATA_STATE;
			r->lost_count++;
			RXW_SET_SKB(r, rx_skb->sequence, rx_skb);
			retval = PGM_RXW_APDU_LOST;
			r->is_waiting = TRUE;
			goto out_flush;
		}

/* a non-committed packet */
		r->fragment_count++;

		memcpy (rx_skb->head, skb->head, (guint8*)skb->tail - (guint8*)skb->head);
		rx_skb->tstamp			= skb->tstamp;
		memcpy (&rx_skb->tsi, &skb->tsi, sizeof(pgm_tsi_t));
		rx_skb->data			= (guint8*)rx_skb->head + ((guint8*)skb->data - (guint8*)skb->head);
		rx_skb->tail			= (guint8*)rx_skb->data + ((guint8*)skb->tail - (guint8*)skb->data);
		rx_skb->len			= skb->len;
		rx_skb->pgm_header		= (gpointer)( (guint8*)rx_skb->head + ((guint8*)skb->pgm_header - (guint8*)skb->head) );
		rx_skb->pgm_opt_fragment	= skb->pgm_opt_fragment ?
							(gpointer)( (guint8*)rx_skb->head + ((guint8*)skb->pgm_opt_fragment - (guint8*)skb->head) ) :
							skb->pgm_opt_fragment;
		rx_skb->pgm_data		= (gpointer)( (guint8*)rx_skb->head + ((guint8*)skb->pgm_data - (guint8*)skb->head) );
		rx_pkt->state			= PGM_PKT_HAVE_DATA_STATE;

		RXW_SET_SKB(r, rx_skb->sequence, rx_skb);
		g_trace ("#%" G_GUINT32_FORMAT ": added packet #%" G_GUINT32_FORMAT ", rxw_sqns %" G_GUINT32_FORMAT,
			data_sqn, rx_skb->sequence, pgm_rxw_sqns(r));
	}

	r->is_waiting = TRUE;

out_flush:
	g_trace ("#%u: push complete: window ( rxw_trail %u rxw_trail_init %u trail %u commit_trail %u commit_lead %u lead %u )",
		data_sqn,
		r->rxw_trail, r->rxw_trail_init, r->trail, r->commit_trail, r->commit_lead, r->lead);

out:
	if (dropped) {
		g_trace ("dropped %u messages due to odata filling window.", dropped);
		r->cumulative_losses += dropped;
	}

	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);
	return retval;
}

/* flush packets but instead of calling on_data append the contiguous data packets
 * to the provided scatter/gather vector.
 *
 * when transmission groups are enabled, packets remain in the windows tagged committed
 * until the transmission group has been completely committed.  this allows the packet
 * data to be used in parity calculations to recover the missing packets.
 *
 * returns -1 on nothing read, returns 0 on zero bytes read.
 */
gssize
pgm_rxw_readv (
	pgm_rxw_t*		r,
	pgm_msgv_t**		pmsg,		/* message array, updated as messages appended */
	guint			msg_len,	/* number of items in pmsg */
	struct pgm_iovec**	piov,		/* underlying iov storage */
	guint			iov_len,	/* number of items in piov */
	gboolean		is_final	/* transfer ownership to application */
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);

	g_trace ("pgm_rxw_readv");

	guint dropped = 0;
	gssize bytes_read = 0;
	guint msgs_read = 0;
	const pgm_msgv_t* msg_end = *pmsg + msg_len;
	const struct pgm_iovec* iov_end = *piov + iov_len;

	while ( !pgm_rxw_incoming_empty (r) )
	{
		struct pgm_sk_buff_t* commit_skb	= RXW_SKB(r, r->commit_lead);
		g_assert ( commit_skb != NULL );
		pgm_rxw_packet_t* commit_pkt		= (pgm_rxw_packet_t*)&commit_skb->cb;
		g_assert ( commit_pkt != NULL );

		switch (commit_pkt->state) {
		case PGM_PKT_LOST_DATA_STATE:
/* if packets are being held drop them all as group is now unrecoverable */
			while (r->commit_lead != r->trail) {
				dropped++;
				pgm_rxw_pop_trail (r);
			}

/* from now on r->commit_lead ≡ r->trail */
			g_assert (r->commit_lead == r->trail);

/* check for lost apdu */
			if ( commit_skb->pgm_opt_fragment )
			{
				const guint32 apdu_first_sqn = g_ntohl (commit_skb->of_apdu_first_sqn);

/* drop the first fragment, then others follow through as its no longer in the window */
				if ( r->trail == apdu_first_sqn )
				{
					dropped++;
					pgm_rxw_pop_trail (r);
				}

/* flush others, make sure to check each packet is an apdu packet and not simply a zero match */
				while (!pgm_rxw_empty(r))
				{
					commit_skb = RXW_SKB(r, r->trail);
					commit_pkt = (pgm_rxw_packet_t*)&commit_skb->cb;
					if (g_ntohl (commit_skb->of_apdu_len) && g_ntohl (commit_skb->of_apdu_first_sqn) == apdu_first_sqn)
					{
						dropped++;
						pgm_rxw_pop_trail (r);
					}
					else
					{	/* another apdu or tpdu exists */
						break;
					}
				}
			}
			else
			{	/* plain tpdu */
				g_trace ("skipping lost packet @ #%" G_GUINT32_FORMAT, commit_skb->sequence);

				dropped++;
				pgm_rxw_pop_trail (r);
/* one tpdu lost */
			}

			g_assert (r->commit_lead == r->trail);
			goto out;
			continue;
		
		case PGM_PKT_HAVE_DATA_STATE:
			/* not lost */
			g_assert ( commit_skb->len > 0 );

/* check for contiguous apdu */
			if ( commit_skb->pgm_opt_fragment )
			{
				if ( g_ntohl (commit_skb->of_apdu_first_sqn) != commit_skb->sequence )
				{
					g_trace ("partial apdu at trailing edge, marking lost.");
					pgm_rxw_mark_lost (r, commit_skb->sequence);
					break;
				}

				guint32 frag			= g_ntohl (commit_skb->of_apdu_first_sqn);
				guint32 apdu_len		= 0;
				struct pgm_sk_buff_t* apdu_skb	= NULL;
				pgm_rxw_packet_t* apdu_pkt	= NULL;
				while ( ABS_IN_RXW(r, frag) && apdu_len < g_ntohl (commit_skb->of_apdu_len) )
				{
					apdu_skb = RXW_SKB(r, frag);
					g_assert ( apdu_skb != NULL );
					apdu_pkt = (pgm_rxw_packet_t*)&apdu_skb->cb;
					g_assert ( apdu_pkt != NULL );
					if (apdu_pkt->state != PGM_PKT_HAVE_DATA_STATE)
					{
						break;
					}
					apdu_len += apdu_skb->len;
					frag++;
				}

				if (apdu_len == g_ntohl (commit_skb->of_apdu_len))
				{
/* check if sufficient room for apdu */
					const guint32 apdu_len_in_frags = frag - g_ntohl (commit_skb->of_apdu_first_sqn) + 1;
					if (*piov + apdu_len_in_frags > iov_end) {
						break;
					}

					g_trace ("contiguous apdu found @ #%" G_GUINT32_FORMAT " - #%" G_GUINT32_FORMAT 
							", passing upstream.",
						g_ntohl (commit_skb->of_apdu_first_sqn), apdu_skb->sequence);

/* pass upstream & cleanup */
					(*pmsg)->msgv_identifier = r->identifier;
					(*pmsg)->msgv_iovlen     = 0;
					(*pmsg)->msgv_iov        = *piov;
					for (guint32 i = g_ntohl (commit_skb->of_apdu_first_sqn); i < frag; i++)
					{
						apdu_skb = RXW_SKB(r, i);
						apdu_pkt = (pgm_rxw_packet_t*)&apdu_skb->cb;

						(*piov)->iov_base   = apdu_skb;
						(*piov)->iov_offset = (guint8*)apdu_skb->data - (guint8*)apdu_skb;	/* from skb, not head */
						(*piov)->iov_len    = apdu_skb->len;
						(*pmsg)->msgv_iovlen++;

						if (is_final) {
							pgm_skb_get (apdu_skb);
						}
						++(*piov);

						bytes_read += apdu_skb->len;	/* stats */

						apdu_pkt->state = PGM_PKT_COMMIT_DATA_STATE;
						r->fragment_count--;		/* accounting */
						r->commit_lead++;
						r->committed_count++;
					}

/* end of commit buffer */
					++(*pmsg);
					msgs_read++;

					if (*pmsg == msg_end) {
						goto out;
					}

					if (*piov == iov_end) {
						goto out;
					}
				}
				else
				{	/* incomplete apdu */
					g_trace ("partial apdu found %u of %u bytes.",
						apdu_len, g_ntohl (commit_skb->of_apdu_len));
					goto out;
				}
			}
			else
			{	/* plain tpdu */
				g_trace ("one packet found @ #%" G_GUINT32_FORMAT ", passing upstream.",
					commit_skb->sequence);

/* pass upstream */
				(*pmsg)->msgv_identifier = r->identifier;
				(*pmsg)->msgv_iovlen     = 1;
				(*pmsg)->msgv_iov        = *piov;

				(*piov)->iov_base   = commit_skb;
				(*piov)->iov_offset = (guint8*)commit_skb->data - (guint8*)commit_skb;
				(*piov)->iov_len    = commit_skb->len;

				bytes_read += commit_skb->len;
				msgs_read++;

/* move to commit window */
				if (is_final) {
					pgm_skb_get (commit_skb);
				}

				commit_pkt->state = PGM_PKT_COMMIT_DATA_STATE;
				r->fragment_count--;
				r->commit_lead++;
				r->committed_count++;

/* end of commit buffer */
				++(*pmsg);
				++(*piov);

				if (*pmsg == msg_end) {
					goto out;
				}

				if (*piov == iov_end) {
					goto out;
				}
			}

/* one apdu or tpdu processed */
			break;

		default:
			g_trace ("!(have|lost)_data_state, sqn %" G_GUINT32_FORMAT " packet state %s(%i) commit_skb->len %u", r->commit_lead, pgm_rxw_state_string(commit_pkt->state), commit_pkt->state, commit_skb->len);
			goto out;
		}
	}

out:
	r->cumulative_losses += dropped;
	r->bytes_delivered   += bytes_read;
	r->msgs_delivered    += msgs_read;

	r->pgm_sock_err.lost_count = dropped;

	ASSERT_RXW_BASE_INVARIANT(r);

	return msgs_read ? bytes_read : -1;
}

/* used to indicate application layer has released interest in packets in committed-data state,
 * move to parity-data state until transmission group has completed.
 */
int
pgm_rxw_release_committed (
	pgm_rxw_t*		r
	)
{
	if (r->committed_count == 0)		/* first call to read */
	{
		g_trace ("no commit packets to release");
		return PGM_RXW_OK;
	}

	g_assert( !pgm_rxw_empty(r) );

	struct pgm_sk_buff_t* skb	= RXW_SKB(r, r->commit_trail);
	g_assert (skb);
	pgm_rxw_packet_t* pkt		= (pgm_rxw_packet_t*)&skb->cb;
	while ( r->committed_count && pkt->state == PGM_PKT_COMMIT_DATA_STATE )
	{
		g_assert (skb);
		g_trace ("releasing commit sqn %u", skb->sequence);
		pkt->state = PGM_PKT_PARITY_DATA_STATE;
		r->committed_count--;
		r->parity_data_count++;
		r->commit_trail++;
		skb = RXW_SKB(r, r->commit_trail);
		pkt = (pgm_rxw_packet_t*)&skb->cb;
	}

	g_assert( r->committed_count == 0 );

	return PGM_RXW_OK;
}

/* used to flush completed transmission groups of any parity-data state packets.
 */
int
pgm_rxw_free_committed (
	pgm_rxw_t*		r
	)
{
	if ( r->parity_data_count == 0 ) {
		g_trace ("no parity-data packets free'd");
		return PGM_RXW_OK;
	}

	g_assert( r->commit_trail != r->trail );

/* calculate transmission group at commit trailing edge */
	const guint32 tg_sqn_mask = 0xffffffff << r->tg_sqn_shift;
	const guint32 tg_sqn = r->commit_trail & tg_sqn_mask;
	const guint32 pkt_sqn = r->commit_trail & ~tg_sqn_mask;

	guint32 new_rx_trail = tg_sqn;
	if (pkt_sqn == r->tg_size - 1)	/* end of group */
		new_rx_trail++;

	struct pgm_sk_buff_t* skb	= RXW_SKB(r, r->trail);
	g_assert (skb);
	pgm_rxw_packet_t* pkt		= (pgm_rxw_packet_t*)&skb->cb;
	while ( new_rx_trail != r->trail )
	{
		g_assert (skb);
		g_trace ("free committed sqn %u", skb->sequence);
		g_assert( pkt->state == PGM_PKT_PARITY_DATA_STATE );
		pgm_rxw_pop_trail (r);
		skb = RXW_SKB(r, r->trail);
		pkt = (pgm_rxw_packet_t*)&skb->cb;
	}

	return PGM_RXW_OK;
}

int
pgm_rxw_pkt_state_unlink (
	pgm_rxw_t*		r,
	struct pgm_sk_buff_t*	skb
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	g_assert ( skb != NULL );

/* remove from state queues */
	GQueue* queue = NULL;
	pgm_rxw_packet_t* pkt = (pgm_rxw_packet_t*)&skb->cb;

	switch (pkt->state) {
	case PGM_PKT_BACK_OFF_STATE:  queue = &r->backoff_queue; break;
	case PGM_PKT_WAIT_NCF_STATE:  queue = &r->wait_ncf_queue; break;
	case PGM_PKT_WAIT_DATA_STATE: queue = &r->wait_data_queue; break;
	case PGM_PKT_HAVE_DATA_STATE:
	case PGM_PKT_HAVE_PARITY_STATE:
	case PGM_PKT_COMMIT_DATA_STATE:
	case PGM_PKT_PARITY_DATA_STATE:
	case PGM_PKT_LOST_DATA_STATE:
		break;

	default:
		g_critical ("pkt->state = %i", pkt->state);
		g_assert_not_reached();
		break;
	}

	if (queue)
	{
#ifdef RXW_DEBUG
		guint original_length = queue->length;
#endif
		g_queue_unlink (queue, &skb->link_);
		skb->link_.prev = skb->link_.next = NULL;
#ifdef RXW_DEBUG
		g_assert (queue->length == original_length - 1);
#endif
	}

	pkt->state = PGM_PKT_ERROR_STATE;

	ASSERT_RXW_BASE_INVARIANT(r);
	return PGM_RXW_OK;
}

/* peek contents of of window entry, allow writing of data and parity members
 * to store temporary repair data.
 */
int
pgm_rxw_peek (
	pgm_rxw_t*	r,
	guint32		sequence_number,
	struct pgm_opt_fragment**	opt_fragment,
	gpointer*	data,
	guint16*	length,			/* matched to underlying type size */
	gboolean*	is_parity
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);

/* already committed */
	if ( pgm_uint32_lt (sequence_number, r->trail) )
	{
		return PGM_RXW_DUPLICATE;
	}

/* not in window */
	if ( !IN_TXW(r, sequence_number) )
	{
		return PGM_RXW_NOT_IN_TXW;
	}

	if ( !ABS_IN_RXW(r, sequence_number) )
	{
		return PGM_RXW_ADVANCED_WINDOW;
	}

/* check if window is not empty */
	g_assert ( !pgm_rxw_empty (r) );

	struct pgm_sk_buff_t* skb	= RXW_SKB(r, sequence_number);
	pgm_rxw_packet_t* pkt		= (pgm_rxw_packet_t*)&skb->cb;

	*opt_fragment	= skb->pgm_opt_fragment;
	*data		= skb->data;
	*length		= skb->len;
	*is_parity	= (pkt->state == PGM_PKT_HAVE_PARITY_STATE);

	ASSERT_RXW_BASE_INVARIANT(r);
	return PGM_RXW_OK;
}

/* overwrite an existing packet entry with parity repair data.
 */
int
pgm_rxw_push_nth_parity (
	pgm_rxw_t*		r,
	struct pgm_sk_buff_t*	skb,
	pgm_time_t		nak_rb_expiry
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);

	const guint32 data_sqn		= g_ntohl (skb->pgm_data->data_sqn);
	const guint32 data_trail	= g_ntohl (skb->pgm_data->data_trail);

/* advances window */
	if ( !ABS_IN_RXW(r, data_sqn) )
	{
		pgm_rxw_window_update (r, data_trail, r->lead, r->tg_size, r->tg_sqn_shift, nak_rb_expiry);
	}

/* check if window is not empty */
	g_assert ( !pgm_rxw_empty (r) );
	g_assert ( ABS_IN_RXW(r, data_sqn) );

	struct pgm_sk_buff_t* parity_skb	= RXW_SKB(r, data_sqn);
	pgm_rxw_packet_t* parity_pkt		= (pgm_rxw_packet_t*)&parity_skb->cb;

/* cannot push parity over original data or existing parity */
	g_assert ( parity_pkt->state == PGM_PKT_BACK_OFF_STATE ||
		   parity_pkt->state == PGM_PKT_WAIT_NCF_STATE ||
		   parity_pkt->state == PGM_PKT_WAIT_DATA_STATE ||
		   parity_pkt->state == PGM_PKT_LOST_DATA_STATE );

	pgm_rxw_pkt_state_unlink (r, parity_skb);

	memcpy (parity_skb->head, skb->head, (guint8*)skb->tail - (guint8*)skb->head);
	parity_skb->data		= (guint8*)parity_skb->head + ((guint8*)skb->data - (guint8*)skb->head);
	parity_skb->tail		= (guint8*)parity_skb->data + ((guint8*)skb->tail - (guint8*)skb->data);
	parity_skb->len			= skb->len;
	parity_skb->pgm_header		= (gpointer)( (guint8*)parity_skb->head + ((guint8*)skb->pgm_header - (guint8*)skb->head) );
	parity_skb->pgm_opt_fragment	= (gpointer)( (guint8*)parity_skb->head + ((guint8*)skb->pgm_opt_fragment - (guint8*)skb->head) );
	parity_skb->pgm_data		= (gpointer)( (guint8*)parity_skb->head + ((guint8*)skb->pgm_data - (guint8*)skb->head) );
	parity_pkt->state		= PGM_PKT_HAVE_PARITY_STATE;

	r->parity_count++;

	ASSERT_RXW_BASE_INVARIANT(r);
	return PGM_RXW_OK;
}

/* overwrite parity-data with a calculated repair payload
 */
int
pgm_rxw_push_nth_repair (
	pgm_rxw_t*		r,
	struct pgm_sk_buff_t*	skb,
	pgm_time_t		nak_rb_expiry
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);

	const guint32 data_sqn		= g_ntohl (skb->pgm_data->data_sqn);
	const guint32 data_trail	= g_ntohl (skb->pgm_data->data_trail);

/* advances window */
	if ( !ABS_IN_RXW(r, data_sqn) )
	{
		pgm_rxw_window_update (r, data_trail, r->lead, r->tg_size, r->tg_sqn_shift, nak_rb_expiry);
	}

/* check if window is not empty */
	g_assert ( !pgm_rxw_empty (r) );
	g_assert ( ABS_IN_RXW(r, data_sqn) );

	struct pgm_sk_buff_t* repair_skb	= RXW_SKB(r, data_sqn);
	pgm_rxw_packet_t* repair_pkt		= (pgm_rxw_packet_t*)&repair_skb->cb;

	g_assert ( repair_pkt->state == PGM_PKT_HAVE_PARITY_STATE );

	r->fragment_count++;

	memcpy (repair_skb->head, skb->head, (guint8*)skb->tail - (guint8*)skb->head);
	repair_skb->data		= (guint8*)repair_skb->head + ((guint8*)skb->data - (guint8*)skb->head);
	repair_skb->tail		= (guint8*)repair_skb->data + ((guint8*)skb->tail - (guint8*)skb->data);
	repair_skb->len			= skb->len;
	repair_skb->pgm_header		= (gpointer)( (guint8*)repair_skb->head + ((guint8*)skb->pgm_header - (guint8*)skb->head) );
	repair_skb->pgm_opt_fragment	= (gpointer)( (guint8*)repair_skb->head + ((guint8*)skb->pgm_opt_fragment - (guint8*)skb->head) );
	repair_skb->pgm_data		= (gpointer)( (guint8*)repair_skb->head + ((guint8*)skb->pgm_data - (guint8*)skb->head) );
	repair_pkt->state		= PGM_PKT_HAVE_DATA_STATE;

	r->parity_count--;

	ASSERT_RXW_BASE_INVARIANT(r);
	return PGM_RXW_OK;
}

/* remove from leading edge of ahead side of receive window */
static int
pgm_rxw_pop_lead (
	pgm_rxw_t*	r
	)
{
/* check if window is not empty */
	ASSERT_RXW_BASE_INVARIANT(r);
	g_assert ( !pgm_rxw_empty (r) );

	struct pgm_sk_buff_t* skb	= RXW_SKB(r, r->lead);
	pgm_rxw_packet_t* pkt		= (pgm_rxw_packet_t*)&skb->cb;

/* cleanup state counters */
	switch (pkt->state) {
	case PGM_PKT_LOST_DATA_STATE:
		r->lost_count--;
		break;

	case PGM_PKT_HAVE_DATA_STATE:
		r->fragment_count--;
		break;

	case PGM_PKT_HAVE_PARITY_STATE:
		r->parity_count--;
		break;

	case PGM_PKT_COMMIT_DATA_STATE:
		r->committed_count--;
		break;

	case PGM_PKT_PARITY_DATA_STATE:
		r->parity_data_count--;
		break;

	default: break;
	}

	pgm_rxw_pkt_state_unlink (r, skb);
	pgm_free_skb (skb);
	RXW_SET_SKB(r, r->lead, NULL);

	r->lead--;

	ASSERT_RXW_BASE_INVARIANT(r);
	return PGM_RXW_OK;
}

/* remove from trailing edge of non-contiguous receive window causing data loss */
static inline int
pgm_rxw_pop_trail (
	pgm_rxw_t*	r
	)
{
/* check if window is not empty */
	ASSERT_RXW_BASE_INVARIANT(r);
	g_assert ( !pgm_rxw_empty (r) );

	struct pgm_sk_buff_t* skb	= RXW_SKB(r, r->trail);
	g_assert (skb);
	pgm_rxw_packet_t* pkt		= (pgm_rxw_packet_t*)&skb->cb;

/* cleanup state counters */
	switch (pkt->state) {
	case PGM_PKT_LOST_DATA_STATE:
		r->lost_count--;
		break;

	case PGM_PKT_HAVE_DATA_STATE:
		r->fragment_count--;
		break;

	case PGM_PKT_HAVE_PARITY_STATE:
		r->parity_count--;
		break;

	case PGM_PKT_COMMIT_DATA_STATE:
		r->committed_count--;
		break;

	case PGM_PKT_PARITY_DATA_STATE:
		r->parity_data_count--;
		break;

	default: break;
	}

	pgm_rxw_pkt_state_unlink (r, skb);
	pgm_free_skb (skb);
	RXW_SET_SKB(r, r->trail, NULL);

/* advance trailing pointers as necessary */
	if (r->trail++ == r->commit_trail)
		if (r->commit_trail++ == r->commit_lead)
			r->commit_lead++;

	ASSERT_RXW_BASE_INVARIANT(r);
	return PGM_RXW_OK;
}

/* update receiving window with new trailing and leading edge parameters of transmit window
 * can generate data loss by excluding outstanding NAK requests.
 *
 * returns number of place holders (NAKs) generated
 */
int
pgm_rxw_window_update (
	pgm_rxw_t*	r,
	guint32		txw_trail,
	guint32		txw_lead,
	guint32		tg_size,		/* transmission group size, 1 = no groups */
	guint		tg_sqn_shift,		/*			    0 = no groups */
	pgm_time_t	nak_rb_expiry
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);

	guint naks = 0;
	guint dropped = 0;

/* SPM is first message seen, define new window parameters */
	if (!r->is_window_defined)
	{
		g_trace ("SPM defining receive window");

		r->lead = txw_lead;
		r->commit_trail = r->commit_lead = r->rxw_trail = r->rxw_trail_init = r->trail = r->lead + 1;

		r->tg_size = tg_size;
		r->tg_sqn_shift = tg_sqn_shift;

		r->is_rxw_constrained = TRUE;
		r->is_window_defined = TRUE;

		return 0;
	}

	if ( pgm_uint32_gt (txw_lead, r->lead) )
	{
/* check bounds of commit window */
		guint32 new_commit_sqns = ( 1 + txw_lead ) - r->commit_trail;
		if ( !pgm_rxw_commit_empty (r) &&
		     (new_commit_sqns > pgm_rxw_len (r)) )
		{
			guint32 constrained_lead = r->commit_trail + pgm_rxw_len (r) - 1;
			g_trace ("constraining advertised lead %u to commit window, new lead %u",
				txw_lead, constrained_lead);
			txw_lead = constrained_lead;
		}

		g_trace ("advancing lead to %u", txw_lead);

		if ( r->lead != txw_lead)
		{
/* generate new naks, should rarely if ever occur? */
	
			while ( r->lead != txw_lead )
			{
				if ( pgm_rxw_full(r) )
				{
					dropped++;
//					g_trace ("dropping #%u due to full window.", r->trail);

					pgm_rxw_pop_trail (r);
					r->is_waiting = TRUE;
				}

				r->lead++;

/* place holder */
				struct pgm_sk_buff_t* ph_skb	= pgm_alloc_skb (r->max_tpdu);
				pgm_rxw_packet_t* ph_pkt	= (pgm_rxw_packet_t*)&ph_skb->cb;
				ph_skb->sequence		= r->lead;
				ph_pkt->nak_rb_expiry		= nak_rb_expiry;
				ph_pkt->state			= PGM_PKT_BACK_OFF_STATE;
				ph_pkt->t0			= pgm_time_now;

				RXW_SET_SKB(r, ph_skb->sequence, ph_skb);
				g_trace ("adding placeholder #%u", ph_skb->sequence);

/* send nak by sending to end of expiry list */
				g_queue_push_head_link (&r->backoff_queue, &ph_skb->link_);
				naks++;
			}
		}
	}
	else
	{
		g_trace ("lead not advanced.");

		if (txw_lead != r->lead)
		{
			g_trace ("lead stepped backwards, ignoring: %u -> %u.", r->lead, txw_lead);
		}
	}

	if ( r->is_rxw_constrained && SLIDINGWINDOW_GT(r, txw_trail, r->rxw_trail_init) )
	{
		g_trace ("constraint removed on trail.");
		r->is_rxw_constrained = FALSE;
	}

	if ( !r->is_rxw_constrained && SLIDINGWINDOW_GT(r, txw_trail, r->rxw_trail) )
	{
		g_trace ("advancing rxw_trail to %u", txw_trail);
		r->rxw_trail = txw_trail;

		if (SLIDINGWINDOW_GT(r, r->rxw_trail, r->trail))
		{
			g_trace ("advancing trail to rxw_trail");

/* jump remaining sequence numbers if window is empty */
			if ( pgm_rxw_empty(r) )
			{
				const guint32 distance = ( (gint32)(r->rxw_trail) - (gint32)(r->trail) );

				dropped  += distance;
				r->commit_trail = r->commit_lead = r->trail += distance;
				r->lead  += distance;
			}
			else
			{
/* mark lost all non-received sequence numbers between commit lead and new rxw_trail */
				for (guint32 sequence_number = r->commit_lead;
				     IN_TXW(r, sequence_number) && SLIDINGWINDOW_GT(r, r->rxw_trail, sequence_number);
				     sequence_number++)
				{
					switch (RXW_PACKET(r, sequence_number)->state) {
					case PGM_PKT_BACK_OFF_STATE:
					case PGM_PKT_WAIT_NCF_STATE:
					case PGM_PKT_WAIT_DATA_STATE:
						dropped++;
						pgm_rxw_mark_lost (r, sequence_number);
						break;

					default: break;
					}
				}
			}
		} /* trail > commit_lead */
	}
	else
	{
		g_trace ("rxw_trail not advanced.");

		if (!r->is_rxw_constrained)
		{
			if (txw_trail != r->rxw_trail)
			{
				g_trace ("rxw_trail stepped backwards, ignoring.");
			}
		}
	}

	if (dropped)
	{
		g_trace ("dropped %u messages due to full window.", dropped);
		r->cumulative_losses += dropped;
	}

	if (r->tg_size != tg_size) {
		g_trace ("window transmission group size updated %i -> %i.", r->tg_size, tg_size);
		r->tg_size = tg_size;
		r->tg_sqn_shift = tg_sqn_shift;
	}

	g_trace ("window ( rxw_trail %u rxw_trail_init %u trail %u commit_trail %u commit_lead %u lead %u rxw_sqns %u )",
		r->rxw_trail, r->rxw_trail_init, r->trail, r->commit_trail, r->commit_lead, r->lead, pgm_rxw_sqns(r));

	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);
	return naks;
}

/* mark a packet lost due to failed recovery, this either advances the trailing edge
 * or creates a hole to later skip.
 */

int
pgm_rxw_mark_lost (
	pgm_rxw_t*	r,
	guint32		sequence_number
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);

	struct pgm_sk_buff_t* skb	= RXW_SKB(r, sequence_number);
	pgm_rxw_packet_t* pkt		= (pgm_rxw_packet_t*)&skb->cb;

/* invalid if we already have data or parity */
	g_assert( pkt->state == PGM_PKT_BACK_OFF_STATE ||
		  pkt->state == PGM_PKT_WAIT_NCF_STATE ||
		  pkt->state == PGM_PKT_WAIT_DATA_STATE );

/* remove current state */
	pgm_rxw_pkt_state_unlink (r, skb);

	pkt->state = PGM_PKT_LOST_DATA_STATE;
	r->lost_count++;
	r->cumulative_losses++;
	r->is_waiting = TRUE;

	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);
	return PGM_RXW_OK;
}

/* received a uni/multicast ncf, search for a matching nak & tag or extend window if
 * beyond lead
 *
 * returns:
 *	PGM_RXW_WINDOW_UNDEFINED	- still waiting for SPM 
 *	PGM_RXW_DUPLICATE
 *	PGM_RXW_CREATED_PLACEHOLDER
 */

int
pgm_rxw_ncf (
	pgm_rxw_t*	r,
	guint32		sequence_number,
	pgm_time_t	nak_rdata_expiry,
	pgm_time_t	nak_rb_expiry
	)
{
	int retval = PGM_RXW_UNKNOWN;

	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);

	g_trace ("pgm_rxw_ncf(#%u)", sequence_number);

	if (!r->is_window_defined) {
		retval = PGM_RXW_WINDOW_UNDEFINED;
		goto out;
	}

/* already committed */
	if ( pgm_uint32_lt (sequence_number, r->commit_lead) )
	{
		g_trace ("ncf #%u: already committed, discarding.", sequence_number);
		retval = PGM_RXW_DUPLICATE;
		goto out;
	}

	struct pgm_sk_buff_t* skb	= RXW_SKB(r, sequence_number);
	pgm_rxw_packet_t* pkt		= (pgm_rxw_packet_t*)&skb->cb;

	if (skb)
	{
		switch (pkt->state) {
/* already received ncf */
		case PGM_PKT_WAIT_DATA_STATE:
		{
			ASSERT_RXW_BASE_INVARIANT(r);
			ASSERT_RXW_POINTER_INVARIANT(r);
			g_trace ("ncf ignored as sequence number already in wait_data_state.");
			retval = PGM_RXW_DUPLICATE;
			goto out;
		}

		case PGM_PKT_BACK_OFF_STATE:
		case PGM_PKT_WAIT_NCF_STATE:
			pkt->nak_rdata_expiry = nak_rdata_expiry;
			g_trace ("nak_rdata_expiry in %f seconds.", pgm_to_secsf( pkt->nak_rdata_expiry - pgm_time_now ));
			break;

/* ignore what we have or have not */
		case PGM_PKT_HAVE_DATA_STATE:
		case PGM_PKT_HAVE_PARITY_STATE:
		case PGM_PKT_COMMIT_DATA_STATE:
		case PGM_PKT_PARITY_DATA_STATE:
		case PGM_PKT_LOST_DATA_STATE:
			g_trace ("ncf ignored as sequence number already closed.");
			retval = PGM_RXW_DUPLICATE;
			goto out;

		default:
			g_assert_not_reached();
		}

		pgm_rxw_pkt_state_unlink (r, skb);
		pkt->state = PGM_PKT_WAIT_DATA_STATE;
		g_queue_push_head_link (&r->wait_data_queue, &skb->link_);

		retval = PGM_RXW_CREATED_PLACEHOLDER;
		goto out;
	}

/* not an expected ncf, extend receive window to pre-empt loss detection */
	if ( !IN_TXW(r, sequence_number) )
	{
		g_trace ("ncf #%u not in tx window, discarding.", sequence_number);
		retval = PGM_RXW_NOT_IN_TXW;
		goto out;
	}

	g_trace ("ncf extends lead #%u to #%u", r->lead, sequence_number);

/* mark all sequence numbers to ncf # in BACK-OFF_STATE */

	guint dropped = 0;
	
/* check bounds of commit window */
	guint32 new_commit_sqns = ( 1 + sequence_number ) - r->commit_trail;
	if ( !pgm_rxw_commit_empty (r) &&
		(new_commit_sqns > pgm_rxw_len (r)) )
	{
		pgm_rxw_window_update (r, r->rxw_trail, sequence_number, r->tg_size, r->tg_sqn_shift, nak_rb_expiry);
		retval = PGM_RXW_CREATED_PLACEHOLDER;
		goto out;
	}

	r->lead++;

	while (r->lead != sequence_number)
	{
		if ( pgm_rxw_full(r) )
		{
			dropped++;
//			g_trace ("dropping #%u due to full window.", r->trail);

			pgm_rxw_pop_trail (r);
			r->is_waiting = TRUE;
		}

/* place holder */
		struct pgm_sk_buff_t* ph_skb	= pgm_alloc_skb(r->max_tpdu);
		pgm_rxw_packet_t* ph_pkt	= (pgm_rxw_packet_t*)&ph_skb->cb;
		ph_skb->sequence		= r->lead;
		ph_pkt->nak_rb_expiry		= nak_rb_expiry;
		ph_pkt->state			= PGM_PKT_BACK_OFF_STATE;
		ph_pkt->t0			= pgm_time_now;

		RXW_SET_SKB(r, ph_skb->sequence, ph_skb);
		g_trace ("ncf: adding placeholder #%u", ph_skb->sequence);

/* send nak by sending to end of expiry list */
		g_queue_push_head_link (&r->backoff_queue, &ph_skb->link_);

		r->lead++;
	}

/* create WAIT_DATA state placeholder for ncf # */

	g_assert ( r->lead == sequence_number );

	if ( pgm_rxw_full(r) )
	{
		dropped++;
//		g_trace ("dropping #%u due to full window.", r->trail);

		pgm_rxw_pop_trail (r);
		r->is_waiting = TRUE;
	}

	struct pgm_sk_buff_t* ph_skb	= pgm_alloc_skb(r->max_tpdu);
	pgm_rxw_packet_t* ph_pkt	= (pgm_rxw_packet_t*)&ph_skb->cb;
	ph_skb->sequence	     	= r->lead;
	ph_pkt->nak_rdata_expiry	= nak_rdata_expiry;
	ph_pkt->state			= PGM_PKT_WAIT_DATA_STATE;
	ph_pkt->t0			= pgm_time_now;
		
	RXW_SET_SKB(r, ph_skb->sequence, ph_skb);
	g_trace ("ncf: adding placeholder #%u", ph_skb->sequence);

/* do not send nak, simply add to ncf list */
	g_queue_push_head_link (&r->wait_data_queue, &ph_skb->link_);

	r->is_waiting = TRUE;

	if (dropped) {
		g_trace ("ncf: dropped %u messages due to full window.", dropped);
		r->cumulative_losses += dropped;
	}

	retval = PGM_RXW_CREATED_PLACEHOLDER;

out:
	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);
	return retval;
}

/* state string helper
 */

const char*
pgm_rxw_state_string (
	pgm_pkt_state_e		state
	)
{
	const char* c;

	switch (state) {
	case PGM_PKT_BACK_OFF_STATE:	c = "PGM_PKT_BACK_OFF_STATE"; break;
	case PGM_PKT_WAIT_NCF_STATE:	c = "PGM_PKT_WAIT_NCF_STATE"; break;
	case PGM_PKT_WAIT_DATA_STATE:	c = "PGM_PKT_WAIT_DATA_STATE"; break;
	case PGM_PKT_HAVE_DATA_STATE:	c = "PGM_PKT_HAVE_DATA_STATE"; break;
	case PGM_PKT_HAVE_PARITY_STATE:	c = "PGM_PKT_HAVE_PARITY_STATE"; break;
	case PGM_PKT_COMMIT_DATA_STATE: c = "PGM_PKT_COMMIT_DATA_STATE"; break;
	case PGM_PKT_PARITY_DATA_STATE:	c = "PGM_PKT_PARITY_DATA_STATE"; break;
	case PGM_PKT_LOST_DATA_STATE:	c = "PGM_PKT_LOST_DATA_STATE"; break;
	case PGM_PKT_ERROR_STATE:	c = "PGM_PKT_ERROR_STATE"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

const char*
pgm_rxw_returns_string (
	pgm_rxw_returns_e	retval
	)
{
	const char* c;

	switch (retval) {
	case PGM_RXW_OK:			c = "PGM_RXW_OK"; break;
	case PGM_RXW_CREATED_PLACEHOLDER:	c = "PGM_RXW_CREATED_PLACEHOLDER"; break;
	case PGM_RXW_FILLED_PLACEHOLDER:	c = "PGM_RXW_FILLED_PLACEHOLDER"; break;
	case PGM_RXW_ADVANCED_WINDOW:		c = "PGM_RXW_ADVANCED_WINDOW"; break;
	case PGM_RXW_NOT_IN_TXW:		c = "PGM_RXW_NOT_IN_TXW"; break;
	case PGM_RXW_WINDOW_UNDEFINED:		c = "PGM_RXW_WINDOW_UNDEFINED"; break;
	case PGM_RXW_DUPLICATE:			c = "PGM_RXW_DUPLICATE"; break;
	case PGM_RXW_APDU_LOST:			c = "PGM_RXW_APDU_LOST"; break;
	case PGM_RXW_MALFORMED_APDU:		c = "PGM_RXW_MALFORMED_APDU"; break;
	case PGM_RXW_UNKNOWN:			c = "PGM_RXW_UNKNOWN"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

/* eof */
