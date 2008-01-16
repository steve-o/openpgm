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

#define RXW_PACKET_OFFSET(w,x)		( (x) % pgm_rxw_len ((w)) ) 
#define RXW_PACKET(w,x) \
	( (pgm_rxw_packet_t*)g_ptr_array_index((w)->pdata, RXW_PACKET_OFFSET((w), (x))) )
#define RXW_SET_PACKET(w,x,v) \
	do { \
		register int _o = RXW_PACKET_OFFSET((w), (x)); \
		g_ptr_array_index((w)->pdata, _o) = (v); \
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
		g_assert ( (w)->pdata != NULL && (w)->pdata->len > 0 ); \
\
/* the state queues exist */ \
		g_assert ( (w)->backoff_queue ); \
		g_assert ( (w)->wait_ncf_queue ); \
		g_assert ( (w)->wait_data_queue ); \
\
/* packet size has been set */ \
		g_assert ( (w)->max_tpdu > 0 ) ; \
\
/* all pointers are within window bounds */ \
		if ( !pgm_rxw_empty( (w) ) ) /* empty: trail = lead + 1, hence wrap around */ \
		{ \
			g_assert ( RXW_PACKET_OFFSET( (w), (w)->lead ) < (w)->pdata->len ); \
			g_assert ( RXW_PACKET_OFFSET( (w), (w)->trail ) < (w)->pdata->len ); \
		} \
\
/* upstream pointer is valid */ \
		g_assert ( (w)->on_data != NULL ); \
\
	}

#define ASSERT_RXW_POINTER_INVARIANT(w) \
	{ \
/* are trail & lead points valid */ \
		if ( !pgm_rxw_empty( (w) ) ) \
		{ \
			g_assert ( NULL != RXW_PACKET( (w) , (w)->trail ) );	/* trail points to something */ \
			g_assert ( NULL != RXW_PACKET( (w) , (w)->lead ) );	/* lead points to something */ \
\
/* queue's contain at least one packet */ \
			g_assert ( ( (w)->backoff_queue->length + \
				     (w)->wait_ncf_queue->length + \
				     (w)->wait_data_queue->length + \
				     (w)->lost_count + \
				     (w)->fragment_count) > 0 ); \
		} \
		else \
		{ \
			g_assert ( (w)->backoff_queue->length == 0 ); \
			g_assert ( (w)->wait_ncf_queue->length == 0 ); \
			g_assert ( (w)->wait_data_queue->length == 0 ); \
			g_assert ( (w)->lost_count == 0 ); \
			g_assert ( (w)->fragment_count == 0 ); \
		} \
	}
#else
#define ASSERT_RXW_BASE_INVARIANT(w)    while(0)
#define ASSERT_RXW_POINTER_INVARIANT(w) while(0)
#endif


/* globals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN	"pgmrxw"

static void _list_iterator (gpointer, gpointer);
static inline int pgm_rxw_flush (pgm_rxw_t*);
static inline int pgm_rxw_flush1 (pgm_rxw_t*);
static inline int pgm_rxw_pop_lead (pgm_rxw_t*);
static inline int pgm_rxw_pop_trail (pgm_rxw_t*);
static inline int pgm_rxw_pkt_remove1 (pgm_rxw_t*, pgm_rxw_packet_t*);
static inline int pgm_rxw_pkt_free1 (pgm_rxw_t*, pgm_rxw_packet_t*);
static inline gpointer pgm_rxw_alloc_packet (pgm_rxw_t*);
static inline gpointer pgm_rxw_alloc0_packet (pgm_rxw_t*);


pgm_rxw_t*
pgm_rxw_init (
	guint		tpdu_length,
	guint32		preallocate_size,
	guint32		rxw_sqns,		/* transmit window size in sequence numbers */
	guint		rxw_secs,		/* size in seconds */
	guint		rxw_max_rte,		/* max bandwidth */
	pgm_rxw_commitfn_t on_data,	/* upstream callback */
	gpointer	param		/* upstream parameter */
	)
{
	g_trace ("init (tpdu %i pre-alloc %i rxw_sqns %i rxw_secs %i rxw_max_rte %i).",
		tpdu_length, preallocate_size, rxw_sqns, rxw_secs, rxw_max_rte);

	pgm_rxw_t* r = g_slice_alloc0 (sizeof(pgm_rxw_t));
	r->pdata = g_ptr_array_new ();
	r->max_tpdu = tpdu_length;

	for (guint32 i = 0; i < preallocate_size; i++)
	{
		gpointer data   = g_slice_alloc (r->max_tpdu);
		gpointer packet = g_slice_alloc (sizeof(pgm_rxw_packet_t));
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
 * trail = 1, lead = 0
 * rxw_trail = rxw_trail_init = 0
 */
	r->trail = r->lead + 1;

/* limit retransmit requests on late session joining */
	r->rxw_constrained = TRUE;

	r->window_defined = FALSE;

/* empty queue's for nak & ncfs */
	r->backoff_queue = g_queue_new ();
	r->wait_ncf_queue = g_queue_new ();
	r->wait_data_queue = g_queue_new ();

/* statistics */
	r->min_fill_time = G_MAXINT;
	r->max_fill_time = G_MININT;
	r->min_nak_transmit_count = G_MAXINT;
	r->max_nak_transmit_count = G_MININT;

/* contiguous packet callback */
	r->on_data = on_data;
	r->param = param;

	guint memory = sizeof(pgm_rxw_t) +
/* pointer array */
			sizeof(GPtrArray) + sizeof(guint) +
			*(guint*)( (char*)r->pdata + sizeof(gpointer) + sizeof(guint) ) +
/* pre-allocated data & packets */
			preallocate_size * (r->max_tpdu + sizeof(pgm_rxw_packet_t)) +
/* state queues */
			3 * sizeof(GQueue) +
/* guess at timer */
			4 * sizeof(int);
			
	g_trace ("memory usage: %ub (%uMb)", memory, memory / (1024 * 1024));

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
			g_slice_free1 (sizeof(pgm_rxw_packet_t), p);
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

	return PGM_RXW_OK;
}

static void
_list_iterator (
	gpointer	data,
	gpointer	user_data
	)
{
	if (data == NULL) return;

	g_assert ( user_data != NULL);

	pgm_rxw_pkt_free1 ((pgm_rxw_t*)user_data, (pgm_rxw_packet_t*)data);
}

static inline gpointer
pgm_rxw_alloc_packet (
	pgm_rxw_t*	r
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	return r->trash_packet ?  g_trash_stack_pop (&r->trash_packet) : g_slice_alloc (sizeof(pgm_rxw_packet_t));
}

static inline gpointer
pgm_rxw_alloc0_packet (
	pgm_rxw_t*	r
	)
{
	gpointer p;

	ASSERT_RXW_BASE_INVARIANT(r);

	if (r->trash_packet)
	{
		p = g_trash_stack_pop (&r->trash_packet);
		memset (p, 0, sizeof(pgm_rxw_packet_t));
	}
	else
	{
		g_trace ("packet trash stack exceeded.");
		p = g_slice_alloc0 (sizeof(pgm_rxw_packet_t));
	}

	ASSERT_RXW_BASE_INVARIANT(r);
	return p;
}

/* the sequence number is inside the packet as opposed to from internal
 * counters, this means one push on the receive window can actually translate
 * as many: the extra's acting as place holders and NAK containers
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
pgm_rxw_push_fragment (
	pgm_rxw_t*	r,
	gpointer	packet,
	guint		length,
	guint32		sequence_number,
	guint32		trail,
	guint32		apdu_first_sqn,
	guint32		fragment_offset,	/* in bytes from beginning of apdu */
	guint32		apdu_len,		/* in bytes */
	pgm_time_t	nak_rb_expiry
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);

	guint dropped = 0;
	int retval = PGM_RXW_UNKNOWN;

	g_trace ("#%u: data trail #%u: push: window ( rxw_trail %u rxw_trail_init %u trail %u lead %u )",
		sequence_number, trail, 
		r->rxw_trail, r->rxw_trail_init, r->trail, r->lead);

/* trail is the next packet to commit upstream, lead is the leading edge
 * of the receive window with possible gaps inside, rxw_trail is the transmit
 * window trail for retransmit requests.
 */

	if ( !r->window_defined )
	{
/* if this packet is a fragment of an apdu, and not the first, we continue on as per spec but careful to
 * advance the trailing edge to discard the remaining fragments.
 */
		g_trace ("#%u: using packet to temporarily define window", sequence_number);

		r->lead = sequence_number - 1;
		r->rxw_trail = r->rxw_trail_init = r->trail = r->lead + 1;

		r->rxw_constrained = TRUE;
		r->window_defined = TRUE;
	}
	else
	{
/* check if packet should be discarded or processed further */

		if ( !IN_TXW(r, sequence_number) )
		{
			g_warning ("#%u: not in transmit window, discarding.", sequence_number);

			retval = PGM_RXW_NOT_IN_TXW;
			goto out;
		}

		pgm_rxw_window_update (r, trail, r->lead, nak_rb_expiry);
	}

	g_trace ("#%u: window ( rxw_trail %u rxw_trail_init %u trail %u lead %u )",
		sequence_number, r->rxw_trail, r->rxw_trail_init, r->trail, r->lead);
	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);

/* already committed */
	if ( pgm_uint32_lt (sequence_number, r->trail) )
	{
		g_trace ("#%u: already committed, discarding.", sequence_number);

		retval = PGM_RXW_DUPLICATE;
		goto out;
	}

/* check for duplicate */
	if ( pgm_uint32_lte (sequence_number, r->lead) )
	{
		g_trace ("#%u: in rx window, checking for duplicate.", sequence_number);

		pgm_rxw_packet_t* rp = RXW_PACKET(r, sequence_number);

		if (rp)
		{
			if (rp->length)
			{
				g_trace ("#%u: already received, discarding.", sequence_number);
				retval = PGM_RXW_DUPLICATE;
				goto out;
			}

/* for fragments check that apdu is valid */
			if ( apdu_len && 
				apdu_first_sqn != sequence_number &&
				( pgm_rxw_empty (r) || !ABS_IN_RXW(r, apdu_first_sqn) )
					)
			{
				g_trace ("#%u: first fragment #%u not in receive window, apdu is lost.", sequence_number, apdu_first_sqn);
				pgm_rxw_mark_lost (r, sequence_number);
				retval = PGM_RXW_APDU_LOST;
				goto out_flush;
			}

			if ( apdu_len && pgm_uint32_gt (apdu_first_sqn, sequence_number) )
			{
				g_trace ("#%u: first apdu fragment sequence number: #%u not lowest, dropping apdu.",
					sequence_number, apdu_first_sqn);
				pgm_rxw_mark_lost (r, apdu_first_sqn);
				retval = PGM_RXW_MALFORMED_APDU;
				goto out_flush;
			}

			g_trace ("#%u: filling in a gap.", sequence_number);

			if (apdu_len)	/* a fragment */
			{
				r->fragment_count++;

				if (sequence_number == apdu_first_sqn)
				{
/* TODO: add sanity check on apdu size, max tsdu * min(rxw_sqns, txw_sqns) is one ceiling */
					rp->data	= g_malloc (apdu_len);
					rp->length	= length;
					memcpy (rp->data, packet, length);
				}
				else
				{	/* not first fragment in apdu */
/* no change:
 *					rp->data	= NULL;
 *					rp->length	= 0;
 */
					pgm_rxw_packet_t* head_rp = RXW_PACKET(r, apdu_first_sqn);
					memcpy ((char*)head_rp->data + fragment_offset, packet, length);
				}

				rp->apdu_first_sqn = apdu_first_sqn;
				rp->apdu_len	= apdu_len;
			}
			else
			{
				rp->data	= packet;
				rp->length	= length;
			}

			pgm_rxw_pkt_state_unlink (r, rp);
			rp->state	= PGM_PKT_HAVE_DATA_STATE;
			retval		= PGM_RXW_FILLED_PLACEHOLDER;

			gint fill_time = pgm_time_now - rp->t0;
			if (fill_time > r->max_fill_time)	r->max_fill_time = fill_time;
			else if (fill_time < r->min_fill_time)	r->min_fill_time = fill_time;

			if (rp->nak_transmit_count > r->max_nak_transmit_count)		r->max_nak_transmit_count = rp->nak_transmit_count;
			else if (rp->nak_transmit_count < r->min_nak_transmit_count)	r->min_nak_transmit_count = rp->nak_transmit_count;
		}
		else
		{
			g_debug ("sequence_number %u points to (null) in window (trail %u lead %u).",
				sequence_number, r->trail, r->lead);
			ASSERT_RXW_BASE_INVARIANT(r);
			ASSERT_RXW_POINTER_INVARIANT(r);
			g_assert_not_reached();
		}
	}
	else	/* sequence_number > lead */
	{
/* extends receive window */
		g_trace ("#%u: lead extended.", sequence_number);
		g_assert ( pgm_uint32_gt (sequence_number, r->lead) );

		if ( pgm_rxw_full(r) )
		{
			dropped++;
//			g_warning ("#%u: dropping #%u due to full window.", sequence_number, r->trail);

			pgm_rxw_pop_trail (r);
			pgm_rxw_flush (r);
		}

		r->lead++;

/* if packet is non-contiguous to current leading edge add place holders */
		if (r->lead != sequence_number)
		{
			while (r->lead != sequence_number)
			{
				pgm_rxw_packet_t* ph = pgm_rxw_alloc0_packet(r);
				ph->link_.data		= ph;
				ph->sequence_number     = r->lead;
				ph->nak_rb_expiry	= nak_rb_expiry;
				ph->state		= PGM_PKT_BACK_OFF_STATE;
				ph->t0			= pgm_time_now;

				RXW_SET_PACKET(r, ph->sequence_number, ph);

/* send nak by sending to end of expiry list */
				g_queue_push_head_link (r->backoff_queue, &ph->link_);
				g_trace ("#%" G_GUINT32_FORMAT ": adding place holder for missing packet, backoff_queue now %" G_GUINT32_FORMAT " long, rxw_sqns %u",
					sequence_number, r->backoff_queue->length, pgm_rxw_sqns(r));

				if ( pgm_rxw_full(r) )
				{
					dropped++;
//					g_warning ("dropping #%u due to full window.", r->trail);

					pgm_rxw_pop_trail (r);
					pgm_rxw_flush (r);
				}

				r->lead++;
			}
			retval = PGM_RXW_CREATED_PLACEHOLDER;
		}
		else
		{
			retval = PGM_RXW_ADVANCED_WINDOW;
		}

		g_assert ( r->lead == sequence_number );

/* for fragments check that apdu is valid: dupe code to above */
		if ( apdu_len && 
			apdu_first_sqn != sequence_number &&
			( pgm_rxw_empty (r) || !ABS_IN_RXW(r, apdu_first_sqn) )
				)
		{
			g_trace ("#%u: first fragment #%u not in receive window, apdu is lost.", sequence_number, apdu_first_sqn);
			pgm_rxw_mark_lost (r, sequence_number);
			retval = PGM_RXW_APDU_LOST;
			goto out_flush;
		}

		if ( apdu_len && pgm_uint32_gt (apdu_first_sqn, sequence_number) )
		{
			g_trace ("#%u: first apdu fragment sequence number: #%u not lowest, dropping apdu.",
				sequence_number, apdu_first_sqn);
			pgm_rxw_mark_lost (r, apdu_first_sqn);
			retval = PGM_RXW_MALFORMED_APDU;
			goto out_flush;
		}

		pgm_rxw_packet_t* rp = pgm_rxw_alloc0_packet(r);
		rp->link_.data = rp;

		if (apdu_len)	/* fragment */
		{
			r->fragment_count++;

			if (sequence_number == apdu_first_sqn)
			{
				rp->data	= g_malloc (apdu_len);

				memcpy (rp->data, packet, length);
			}
			else
			{	/* not first fragment */
				pgm_rxw_packet_t* head_rp = RXW_PACKET(r, apdu_first_sqn);
				memcpy ((char*)head_rp->data + fragment_offset, packet, length);

/* leave check for complete apdu at flush */
			}

			rp->apdu_first_sqn = apdu_first_sqn;
			rp->apdu_len	= apdu_len;
		}
		else
		{	/* regular tpdu */
			rp->data	= packet;
		}
		rp->length		= length;
		rp->sequence_number     = r->lead;
		rp->state		= PGM_PKT_HAVE_DATA_STATE;

		RXW_SET_PACKET(r, rp->sequence_number, rp);
		g_trace ("#%" G_GUINT32_FORMAT ": added packet #%" G_GUINT32_FORMAT ", rxw_sqns %" G_GUINT32_FORMAT,
			sequence_number, rp->sequence_number, pgm_rxw_sqns(r));
	}

out_flush:
	pgm_rxw_flush (r);

	g_trace ("#%u: push complete: window ( rxw_trail %u rxw_trail_init %u trail %u lead %u )",
		sequence_number,
		r->rxw_trail, r->rxw_trail_init, r->trail, r->lead);

	if (dropped) {
		g_warning ("dropped %u messages due to full window.", dropped);
	}

out:
	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);
	return retval;
}

static inline int
pgm_rxw_flush (
	pgm_rxw_t*	r
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);

/* check for contiguous packets to pass upstream */
	g_trace ("flush window for contiguous data.");

	while ( !pgm_rxw_empty( r ) )
	{
		if ( pgm_rxw_flush1 (r) == 0 )
			break;
	}

	g_trace ("flush window complete.");
	ASSERT_RXW_BASE_INVARIANT(r);
	return PGM_RXW_OK;
}

/* flush one apdu from contiguous data from the trailing edge of the window
 *
 * returns >0  if a packet was flushed and possibly more exist.
 */

static inline int
pgm_rxw_flush1 (
	pgm_rxw_t*	r
	)
{
	int flushed = 0;
	ASSERT_RXW_BASE_INVARIANT(r);

	pgm_rxw_packet_t* cp = RXW_PACKET(r, r->trail);
	g_assert ( cp != NULL );

	if (cp->state != PGM_PKT_HAVE_DATA_STATE && cp->state != PGM_PKT_LOST_DATA_STATE) {
		g_trace ("!(have|lost)_data_state, sqn %" G_GUINT32_FORMAT " packet state %s(%i) cp->length %u", r->trail, pgm_rxw_state_string(cp->state), cp->state, cp->length);
		goto out;
	}

	if (cp->state == PGM_PKT_LOST_DATA_STATE)
	{
/* check for lost apdu */
		if ( cp->apdu_len )
		{
			guint32 dropped = 0;
			guint32 apdu_first_sqn = cp->apdu_first_sqn;

/* drop the first fragment, then others follow through as its no longer in the window */
			if ( r->trail == apdu_first_sqn )
			{
				dropped++;
				r->lost_count--;
				RXW_SET_PACKET(r, r->trail, NULL);
				r->trail++;
				pgm_rxw_pkt_remove1 (r, cp);
			}

/* flush others, make sure to check each packet is an apdu packet and not simply a zero match */
			while (!pgm_rxw_empty(r))
			{
				cp = RXW_PACKET(r, r->trail);
				if (cp->apdu_len && cp->apdu_first_sqn == apdu_first_sqn)
				{
					dropped++;
					if ( cp->state == PGM_PKT_LOST_DATA_STATE ) {
						r->lost_count--;
					} else {
						g_assert (cp->state == PGM_PKT_HAVE_DATA_STATE);
						r->fragment_count--;
					}
					RXW_SET_PACKET(r, r->trail, NULL);
					r->trail++;
					pgm_rxw_pkt_remove1 (r, cp);
				}
				else
				{	/* another apdu or tpdu exists */
					flushed++;
					break;
				}
			}
		}
		else
		{	/* plain tpdu */
			g_trace ("skipping lost packet @ #%" G_GUINT32_FORMAT, cp->sequence_number);

			r->lost_count--;

			RXW_SET_PACKET(r, r->trail, NULL);
			r->trail++;

/* cleanup */
			pgm_rxw_pkt_remove1 (r, cp);

/* one tpdu lost */
			flushed++;
		}
	}
	else
	{	/* not lost */
		g_assert ( cp->data != NULL && cp->length > 0 );

/* check for contiguous apdu */
		if ( cp->apdu_len )
		{
			if ( cp->apdu_first_sqn != cp->sequence_number )
			{
				g_trace ("partial apdu at trailing edge");
				goto out;
			}

			guint32 frag = cp->apdu_first_sqn;
			guint32 apdu_len = 0;
			pgm_rxw_packet_t* ap = NULL;
			while ( ABS_IN_RXW(r, frag) && apdu_len < cp->apdu_len )
			{
g_trace ("check for contiguous tpdu #%u in apdu #%u", frag, cp->apdu_first_sqn);
				ap = RXW_PACKET(r, frag);
				g_assert ( ap != NULL );
				if (ap->state != PGM_PKT_HAVE_DATA_STATE)
				{
					break;
				}
				apdu_len += ap->length;
				frag++;
			}

			if (apdu_len == cp->apdu_len)
			{
				g_trace ("contiguous apdu found @ #%" G_GUINT32_FORMAT " - #%" G_GUINT32_FORMAT 
						", passing upstream.",
					cp->apdu_first_sqn, ap->sequence_number);

/* remove from window */
				for (guint32 i = cp->apdu_first_sqn; i < frag; i++)
				{
					r->fragment_count--;
					r->trail++;
				}

/* pass upstream */
				r->on_data (cp->data, cp->length, r->param);

/* cleanup */
				for (guint32 i = cp->apdu_first_sqn; i < frag; i++)
				{
					ap = RXW_PACKET(r, i);
					pgm_rxw_pkt_remove1 (r, ap);
					RXW_SET_PACKET(r, i, NULL);
				}
			}
			else
			{	/* incomplete apdu */
				g_trace ("partial apdu found %u of %u bytes.",
					apdu_len, cp->apdu_len);
				goto out;
			}
		}
		else
		{	/* plain tpdu */
			g_trace ("contiguous packet found @ #%" G_GUINT32_FORMAT ", passing upstream.",
				cp->sequence_number);

			RXW_SET_PACKET(r, r->trail, NULL);
			r->trail++;

/* pass upstream, including data memory ownership */
			r->on_data (cp->data, cp->length, r->param);

/* cleanup */
			pgm_rxw_pkt_remove1 (r, cp);
		}

/* one apdu or tpdu processed */
		flushed++;
	}

out:
	ASSERT_RXW_BASE_INVARIANT(r);
	return flushed;
}

int
pgm_rxw_pkt_state_unlink (
	pgm_rxw_t*		r,
	pgm_rxw_packet_t*	rp
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	g_assert ( rp != NULL );

/* remove from state queues */
	GQueue* queue = NULL;

	switch (rp->state) {
	case PGM_PKT_BACK_OFF_STATE:  queue = r->backoff_queue; break;
	case PGM_PKT_WAIT_NCF_STATE:  queue = r->wait_ncf_queue; break;
	case PGM_PKT_WAIT_DATA_STATE: queue = r->wait_data_queue; break;
	case PGM_PKT_HAVE_DATA_STATE: break; 

	default:
		g_assert_not_reached();
		break;
	}

	if (queue)
	{
#ifdef RXW_DEBUG
		guint original_length = queue->length;
#endif
		g_queue_unlink (queue, &rp->link_);
		rp->link_.prev = rp->link_.next = NULL;
#ifdef RXW_DEBUG
		g_assert (queue->length == original_length - 1);
#endif
	}

	rp->state = PGM_PKT_ERROR_STATE;

	ASSERT_RXW_BASE_INVARIANT(r);
	return PGM_RXW_OK;
}

/* similar to rxw_pkt_free1 but ignore the data payload, used to transfer
 * ownership upstream.
 */

static inline int
pgm_rxw_pkt_remove1 (
	pgm_rxw_t*		r,
	pgm_rxw_packet_t*	rp
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	g_assert ( rp != NULL );

	if (rp->data)
	{
		rp->data = NULL;
	}

//	g_slice_free1 (sizeof(pgm_rxw_t), rp);
	g_trash_stack_push (&r->trash_packet, rp);

	ASSERT_RXW_BASE_INVARIANT(r);
	return PGM_RXW_OK;
}

static inline int
pgm_rxw_pkt_free1 (
	pgm_rxw_t*		r,
	pgm_rxw_packet_t*	rp
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	g_assert ( rp != NULL );

	if (rp->data)
	{
		if (rp->apdu_len)
		{
			g_free (rp->data);
		}
		else
		{
//			g_slice_free1 (rp->length, rp->data);
			g_trash_stack_push (&r->trash_data, rp->data);
		}
		rp->data = NULL;
	}

//	g_slice_free1 (sizeof(pgm_rxw_t), rp);
	g_trash_stack_push (&r->trash_packet, rp);

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

	pgm_rxw_packet_t* rp = RXW_PACKET(r, r->lead);

	pgm_rxw_pkt_state_unlink (r, rp);
	pgm_rxw_pkt_free1 (r, rp);
	RXW_SET_PACKET(r, r->lead, NULL);

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

	pgm_rxw_packet_t* rp = RXW_PACKET(r, r->trail);

	pgm_rxw_pkt_state_unlink (r, rp);
	pgm_rxw_pkt_free1 (r, rp);
	RXW_SET_PACKET(r, r->trail, NULL);

	r->trail++;

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
	pgm_time_t	nak_rb_expiry
	)
{
	ASSERT_RXW_BASE_INVARIANT(r);
	ASSERT_RXW_POINTER_INVARIANT(r);

	guint naks = 0;
	guint dropped = 0;

/* SPM is first message seen, define new window parameters */
	if (!r->window_defined)
	{
		g_trace ("SPM defining receive window");

		r->lead = txw_lead;
		r->rxw_trail = r->rxw_trail_init = r->trail = r->lead + 1;

		r->rxw_constrained = TRUE;
		r->window_defined = TRUE;

		return 0;
	}

	if ( pgm_uint32_gt (txw_lead, r->lead) )
	{
		g_trace ("advancing lead to %u", txw_lead);

		if ( r->lead != txw_lead)
		{
/* generate new naks, should rarely if ever occur? */
	
			while ( r->lead != txw_lead )
			{
				if ( pgm_rxw_full(r) )
				{
					dropped++;
//					g_warning ("dropping #%u due to full window.", r->trail);

					pgm_rxw_pop_trail (r);
					pgm_rxw_flush (r);
				}

				r->lead++;

				pgm_rxw_packet_t* ph = pgm_rxw_alloc0_packet(r);
				ph->link_.data		= ph;
				ph->sequence_number     = r->lead;
				ph->nak_rb_expiry	= nak_rb_expiry;
				ph->state		= PGM_PKT_BACK_OFF_STATE;
				ph->t0			= pgm_time_now;

				RXW_SET_PACKET(r, ph->sequence_number, ph);
				g_trace ("adding placeholder #%u", ph->sequence_number);

/* send nak by sending to end of expiry list */
				g_queue_push_head_link (r->backoff_queue, &ph->link_);
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

	if ( r->rxw_constrained && SLIDINGWINDOW_GT(r, txw_trail, r->rxw_trail_init) )
	{
		g_trace ("constraint removed on trail.");
		r->rxw_constrained = FALSE;
	}

	if ( !r->rxw_constrained && SLIDINGWINDOW_GT(r, txw_trail, r->rxw_trail) )
	{
		g_trace ("advancing rxw_trail to %u", txw_trail);
		r->rxw_trail = txw_trail;

/* expire outstanding naks ... */
		while ( pgm_uint32_gt(r->rxw_trail, r->trail) )
		{
/* jump remaining sequence numbers if window is empty */
			if ( pgm_rxw_empty(r) )
			{
				guint32 distance = ( (gint32)(r->rxw_trail) - (gint32)(r->trail) );

				dropped  += distance;
				r->trail += distance;
				r->lead  += distance;
				break;
			}
			else
			{
				dropped++;
//				g_warning ("dropping #%u due to advancing transmit window.", r->trail);
				pgm_rxw_pop_trail (r);
				pgm_rxw_flush (r);
			}
		}
	}
	else
	{
		g_trace ("rxw_trail not advanced.");

		if (!r->rxw_constrained)
		{
			if (txw_trail != r->rxw_trail)
			{
				g_warning ("rxw_trail stepped backwards, ignoring.");
			}
		}
	}

	if (dropped)
	{
/* check trailing packet to see if fragment of an apdu that has just been dropped */
		if (!pgm_rxw_empty (r))
		{
			pgm_rxw_packet_t* rp = RXW_PACKET(r, r->trail);
			while (!pgm_rxw_empty (r) && rp->apdu_len && !ABS_IN_RXW(r, rp->apdu_first_sqn))
			{
				pgm_rxw_pop_trail (r);
				dropped++;
			}
		}

		g_warning ("dropped %u messages due to full window.", dropped);
	}

	g_trace ("window ( rxw_trail %u rxw_trail_init %u trail %u lead %u rxw_sqns %u )",
		r->rxw_trail, r->rxw_trail_init, r->trail, r->lead, pgm_rxw_sqns(r));

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

	pgm_rxw_packet_t* rp = RXW_PACKET(r, sequence_number);

/* remove current state */
	pgm_rxw_pkt_state_unlink (r, rp);
	rp->state = PGM_PKT_LOST_DATA_STATE;
	r->lost_count++;

	pgm_rxw_flush (r);

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

	if (!r->window_defined) {
		retval = PGM_RXW_WINDOW_UNDEFINED;
		goto out;
	}

/* already committed */
	if ( pgm_uint32_lt (sequence_number, r->trail) )
	{
		g_trace ("ncf #%u: already committed, discarding.", sequence_number);
		retval = PGM_RXW_DUPLICATE;
		goto out;
	}

	pgm_rxw_packet_t* rp = RXW_PACKET(r, sequence_number);

	if (rp)
	{
		switch (rp->state) {
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
			rp->nak_rdata_expiry = nak_rdata_expiry;
			g_trace ("nak_rdata_expiry in %f seconds.", pgm_to_secsf( rp->nak_rdata_expiry - pgm_time_now ));
			break;

		default:
			g_assert_not_reached();
		}

		pgm_rxw_pkt_state_unlink (r, rp);
		rp->state = PGM_PKT_WAIT_DATA_STATE;
		g_queue_push_head_link (r->wait_data_queue, &rp->link_);

		retval = PGM_RXW_CREATED_PLACEHOLDER;
		goto out;
	}

/* not an expected ncf, extend receive window to pre-empt loss detection */
	if ( !IN_TXW(r, sequence_number) )
	{
		g_warning ("ncf #%u not in tx window, discarding.", sequence_number);
		retval = PGM_RXW_NOT_IN_TXW;
		goto out;
	}

	g_trace ("ncf extends leads to #%u", sequence_number);

/* mark all sequence numbers to ncf # in BACK-OFF_STATE */

	guint dropped = 0;

	while (r->lead != sequence_number)
	{
		if ( pgm_rxw_full(r) )
		{
			dropped++;
//			g_warning ("dropping #%u due to full window.", r->trail);

			pgm_rxw_pop_trail (r);
			pgm_rxw_flush (r);
		}

		pgm_rxw_packet_t* ph = pgm_rxw_alloc0_packet(r);
		ph->link_.data		= ph;
		ph->sequence_number     = r->lead;
		ph->nak_rb_expiry	= nak_rb_expiry;
		ph->state		= PGM_PKT_BACK_OFF_STATE;
		ph->t0			= pgm_time_now;

		RXW_SET_PACKET(r, ph->sequence_number, ph);
		g_trace ("ncf: adding placeholder #%u", ph->sequence_number);

/* send nak by sending to end of expiry list */
		g_queue_push_head_link (r->backoff_queue, &ph->link_);

		r->lead++;
	}

/* create WAIT_DATA state placeholder for ncf # */

	g_assert ( r->lead == sequence_number );

	if ( pgm_rxw_full(r) )
	{
		dropped++;
//		g_warning ("dropping #%u due to full window.", r->trail);

		pgm_rxw_pop_trail (r);
		pgm_rxw_flush (r);
	}

	pgm_rxw_packet_t* ph = pgm_rxw_alloc0_packet(r);
	ph->link_.data		= ph;
	ph->sequence_number     = r->lead;
	ph->nak_rdata_expiry	= nak_rdata_expiry;
	ph->state		= PGM_PKT_WAIT_DATA_STATE;
	ph->t0			= pgm_time_now;
		
	RXW_SET_PACKET(r, ph->sequence_number, ph);
	g_trace ("ncf: adding placeholder #%u", ph->sequence_number);

/* do not send nak, simply add to ncf list */
	g_queue_push_head_link (r->wait_data_queue, &ph->link_);

	pgm_rxw_flush (r);

	if (dropped) {
		g_warning ("dropped %u messages due to full window.", dropped);
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
