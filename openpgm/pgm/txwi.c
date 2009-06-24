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

//#define TXW_DEBUG

#ifndef TXW_DEBUG
#define G_DISABLE_ASSERT
#endif

#include <glib.h>

#include "pgm/txwi.h"
#include "pgm/sn.h"

#ifndef TXW_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* testing function: is TSI null
 *
 * returns TRUE if null, returns FALSE if not null.
 */

static inline
gboolean
pgm_txw_tsi_is_null (
	pgm_tsi_t* const	tsi
	)
{
	pgm_tsi_t nulltsi;
	memset (&nulltsi, 0, sizeof(nulltsi));
	return 0 == memcmp (&nulltsi, tsi, sizeof(nulltsi));
}

/* returns the pointer at the given index of the window.
 */

static inline
struct pgm_sk_buff_t*
_pgm_txw_peek (
	const pgm_txw_t* const	window,
	const guint32		sequence
	)
{
	struct pgm_sk_buff_t* skb;

	g_return_val_if_fail (window, NULL);

	if (pgm_txw_is_empty (window))
		return NULL;

	if (pgm_uint32_gte (sequence, window->trail) && pgm_uint32_lte (sequence, window->lead))
	{
		const guint32 index_ = sequence % pgm_txw_max_length (window);
		skb = window->pdata[index_];
		g_assert (skb);
		g_assert (pgm_skb_is_valid (skb));
		g_assert (pgm_txw_tsi_is_null (&skb->tsi));
	}
	else
		skb = NULL;

	return skb;
}

/* testing function: can a request be peeked from the retransmit queue.
 *
 * returns TRUE if request is available, returns FALSE if not available.
 */

static inline
gboolean
pgm_txw_retransmit_can_peek (
	pgm_txw_t* const	window
	)
{
	struct pgm_sk_buff_t* skb;
	guint32 unfolded_checksum;
	gboolean is_parity;
	guint rs_h;

	g_return_val_if_fail (window, FALSE);

	return 0 == pgm_txw_retransmit_try_peek (window, &skb, &unfolded_checksum, &is_parity, &rs_h);
}


/* globals */

static inline int pgm_txw_pop (pgm_txw_t* const);
static int pgm_txw_retransmit_push_parity (pgm_txw_t* const, const guint32, const guint);
static int pgm_txw_retransmit_push_selective (pgm_txw_t* const, const guint32);


/* constructor for transmit window.  zero-length windows are not permitted.
 *
 * returns pointer to transmit window on success, returns NULL on invalid parameters.
 */

pgm_txw_t*
pgm_txw_init (
	const guint16		tpdu_size,
	const guint32		sqns,		/* transmit window size in sequence numbers */
	const guint		secs,		/* size in seconds */
	const guint		max_rte		/* max bandwidth */
	)
{
	pgm_txw_t* window;

/* pre-conditions */
	if (sqns) {
		g_assert_cmpuint (tpdu_size, ==, 0);
		g_assert_cmpuint (sqns, >, 0);
		g_assert_cmpuint (sqns & PGM_UINT32_SIGN_BIT, ==, 0);
		g_assert_cmpuint (secs, ==, 0);
		g_assert_cmpuint (max_rte, ==, 0);
	} else {
		g_assert_cmpuint (tpdu_size, >, 0);
		g_assert_cmpuint (secs, >, 0);
		g_assert_cmpuint (max_rte, >, 0);
	}

	g_trace ("init (max-tpdu:%" G_GUINT16_FORMAT " sqns:%" G_GUINT32_FORMAT  " secs %u max-rte %u).\n",
		tpdu_size, sqns, secs, max_rte);

/* calculate transmit window parameters */
	guint32 alloc_sqns;

	if (sqns)
	{
		alloc_sqns = sqns;
	}
	else if (tpdu_size && secs && max_rte)
	{
		alloc_sqns = (secs * max_rte) / tpdu_size;
	}
	else
	{
		g_assert_not_reached();
	}

	window = g_slice_alloc0 (sizeof(pgm_txw_t) + ( alloc_sqns * sizeof(struct pgm_sk_buff_t*) ));

/* empty state for transmission group boundaries to align.
 *
 * trail = 0, lead = -1	
 */
	window->lead = -1;
	window->trail = window->lead + 1;

/* lock on queue */
	g_static_mutex_init (&window->retransmit_mutex);

/* pointer array */
	window->alloc = alloc_sqns;

/* post-conditions */
	g_assert_cmpuint (pgm_txw_max_length (window), ==, alloc_sqns);
	g_assert_cmpuint (pgm_txw_length (window), ==, 0);
	g_assert_cmpuint (pgm_txw_size (window), ==, 0);
	g_assert (pgm_txw_is_empty (window));
	g_assert (!pgm_txw_is_full (window));
	g_assert (!pgm_txw_retransmit_can_peek (window));

	return window;
}

/* destructor for transmit window.  must not be called more than once for same window.
 *
 * returns 0 if window shutdown correctly, returns -1 on error.
 */

void
pgm_txw_shutdown (
	pgm_txw_t* const	window
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert_cmpuint (window->alloc, >, 0);

	g_trace ("shutdown (window:%p)", (gpointer)window);

/* contents of window */
	while (!pgm_txw_is_empty (window)) {
#ifndef G_DISABLE_ASSERT
		int e = pgm_txw_pop (window);
		g_assert (0 == e);
#else
		pgm_txw_pop (window);
#endif
	}

/* window must now be empty */
	g_assert_cmpuint (pgm_txw_length (window), ==, 0);
	g_assert_cmpuint (pgm_txw_size (window), ==, 0);
	g_assert (pgm_txw_is_empty (window));
	g_assert (!pgm_txw_is_full (window));

/* retransmit queue must be empty */
	g_assert (!pgm_txw_retransmit_can_peek (window));

/* free lock on queue */
	g_static_mutex_free (&window->retransmit_mutex);

/* window */
	g_slice_free1 (sizeof(pgm_txw_t) + ( window->alloc * sizeof(struct pgm_sk_buff_t*) ), window);
}

/* add skb to transmit window, taking ownership.  window does not grow.
 * PGM skbuff data/tail pointers must point to the PGM payload, and hence skb->len
 * is allowed to be zero.
 *
 * side effects:
 *
 * 1) sequence number is set in skb.
 * 2) window is updated with new skb.
 *
 * no return value.  fatal error raised on invalid parameters.  if window is full then
 * an entry is dropped to fulfil the request.
 *
 * it is an error to try to free the skb after adding to the window.
 */

void
pgm_txw_add (
	pgm_txw_t* const		window,
	struct pgm_sk_buff_t* const	skb		/* cannot be NULL */
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert (skb);
	g_assert_cmpuint (pgm_txw_max_length (window), >, 0);
	g_assert (pgm_skb_is_valid (skb));
	g_assert (((const GList*)skb)->next == NULL);
	g_assert (((const GList*)skb)->prev == NULL);
	g_assert (pgm_txw_tsi_is_null (&skb->tsi));
	g_assert (sizeof(struct pgm_header) + sizeof(struct pgm_data) <= (guint8*)skb->data - (guint8*)skb->head);
	g_assert (skb->len == (guint8*)skb->tail - (guint8*)skb->data);

	g_trace ("add (window:%p skb:%p)", (gpointer)window, (gpointer)skb);

	if (pgm_txw_is_full (window))
	{
/* transmit window advancement scheme dependent action here */
		const int e = pgm_txw_pop (window);
		g_assert_cmpint (e, ==, 0);
	}

/* generate new sequence number */
	skb->sequence = ++(window->lead);

/* add skb to window */
	const guint32 index_ = skb->sequence % pgm_txw_max_length (window);
	window->pdata[index_] = skb;

/* statistics */
	window->size += skb->len;

/* post-conditions */
	g_assert_cmpuint (pgm_txw_length (window), >, 0);
	g_assert_cmpuint (pgm_txw_length (window), <=, pgm_txw_max_length (window));
}

/* peek an entry from the window for retransmission.
 *
 * returns pointer to skbuff on success, returns NULL on invalid parameters.
 */

struct pgm_sk_buff_t*
pgm_txw_peek (
	pgm_txw_t* const	window,
	const guint32		sequence
	)
{
	g_trace ("peek (window:%p sequence:%" G_GUINT32_FORMAT ")", (gpointer)window, sequence);
	return _pgm_txw_peek (window, sequence);
}

/* remove an entry from the trailing edge of the transmit window.
 *
 * returns 0 if entry successfully removed, returns -1 on error.
 */

static inline int
pgm_txw_pop (
	pgm_txw_t* const	window
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_txw_state_t* state;

	g_return_val_if_fail (window, -1);

	skb = _pgm_txw_peek (window, window->trail);
	g_assert (skb);
	g_assert (pgm_skb_is_valid (skb));
	g_assert (pgm_txw_tsi_is_null (&skb->tsi));
	g_assert (skb->len == (guint8*)skb->tail - (guint8*)skb->data);

	state = (pgm_txw_state_t*)&skb->cb;

/* must lock before checking whether part of the retransmit queue */
	g_static_mutex_lock (&window->retransmit_mutex);
	if (state->waiting_retransmit)
	{
		g_queue_unlink (&window->retransmit_queue, (GList*)skb);
		state->waiting_retransmit = 0;
	}
	g_static_mutex_unlock (&window->retransmit_mutex);

/* statistics */
	window->size -= skb->len;

/* remove reference to skb */
#ifdef PGM_TXW_CLEAR_UNUSED_ENTRIES
	const guint32 index_ = skb->sequence % pgm_txw_max_length (window);
	window->pdata[index_] = NULL;
#endif
	pgm_free_skb (skb);

/* advance trailing pointer */
	window->trail++;

/* post-conditions */
	g_assert (!pgm_txw_is_full (window));

	return 0;
}

/* Try to add a sequence number to the retransmit queue, ignore if
 * already there or no longer in the transmit window.
 *
 * For parity NAKs, we deal on the transmission group sequence number
 * rather than the packet sequence number.  To simplify managment we
 * use the leading window packet to store the details of the entire
 * transmisison group.  Parity NAKs are ignored if the packet count is
 * less than or equal to the count already queued for retransmission.
 *
 * returns 0 if request was eliminated, returns 1 if request was
 * added to queue.
 */

int
pgm_txw_retransmit_push (
	pgm_txw_t* const	window,
	const guint32		sequence,
	const gboolean		is_parity,	/* parity NAK ⇒ sequence_number = transmission group | packet count */
	const guint		tg_sqn_shift
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert_cmpuint (tg_sqn_shift, <, sizeof(guint32));

	g_trace ("retransmit_push (window:%p sequence:%" G_GUINT32_FORMAT " is_parity:%s tg_sqn_shift:%u)",
		(gpointer)window, sequence, is_parity ? "TRUE" : "FALSE", tg_sqn_shift);

/* early elimination */
	if (pgm_txw_is_empty (window))
		return 0;

	if (is_parity)
	{
		return pgm_txw_retransmit_push_parity (window, sequence, tg_sqn_shift);
	}
	else
	{
		return pgm_txw_retransmit_push_selective (window, sequence);
	}
}

int
pgm_txw_retransmit_push_parity (
	pgm_txw_t* const	window,
	const guint32		sequence,
	const guint		tg_sqn_shift
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_txw_state_t* state;

/* pre-conditions */
	g_assert (window);
	g_assert_cmpuint (tg_sqn_shift, <, sizeof(guint32));

	const guint32 tg_sqn_mask = 0xffffffff << tg_sqn_shift;
	const guint32 nak_tg_sqn  = sequence &  tg_sqn_mask;	/* left unshifted */
	const guint32 nak_pkt_cnt = sequence & ~tg_sqn_mask;
	skb = _pgm_txw_peek (window, nak_tg_sqn);

	if (NULL == skb)
	{
		g_trace ("transmission group lead #%" G_GUINT32_FORMAT " not in window.", nak_tg_sqn);
		return 0;
	}

	g_assert (pgm_skb_is_valid (skb));
	g_assert (pgm_txw_tsi_is_null (&skb->tsi));
	g_assert (skb->len == (guint8*)skb->tail - (guint8*)skb->data);
	state = (pgm_txw_state_t*)&skb->cb;

/* check if request can be eliminated */
	g_static_mutex_lock (&window->retransmit_mutex);
	if (state->waiting_retransmit)
	{
		g_assert (((const GList*)skb)->next);
		g_assert (((const GList*)skb)->prev);
		if (state->pkt_cnt_requested < nak_pkt_cnt)
		{
/* more parity packets requested than currently scheduled, simply bump up the count */
			state->pkt_cnt_requested = nak_pkt_cnt;
		}
		g_static_mutex_unlock (&window->retransmit_mutex);
		return 0;
	}
	else
	{
		g_assert (((const GList*)skb)->next == NULL);
		g_assert (((const GList*)skb)->prev == NULL);
	}

/* new request */
	state->pkt_cnt_requested++;
	g_queue_push_head_link (&window->retransmit_queue, (GList*)skb);
	g_assert (!g_queue_is_empty (&window->retransmit_queue));
	state->waiting_retransmit = 1;
	g_static_mutex_unlock (&window->retransmit_mutex);
	return 1;
}

int
pgm_txw_retransmit_push_selective (
	pgm_txw_t* const	window,
	const guint32		sequence
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_txw_state_t* state;

/* pre-conditions */
	g_assert (window);

	skb = _pgm_txw_peek (window, sequence);
	if (NULL == skb)
	{
		g_trace ("requested packet #%" G_GUINT32_FORMAT " not in window.", sequence);
		return 0;
	}

	g_assert (pgm_skb_is_valid (skb));
	g_assert (pgm_txw_tsi_is_null (&skb->tsi));
	g_assert (skb->len == (guint8*)skb->tail - (guint8*)skb->data);
	state = (pgm_txw_state_t*)&skb->cb;

/* check if request can be eliminated */
	g_static_mutex_lock (&window->retransmit_mutex);
	if (state->waiting_retransmit)
	{
		g_assert (!g_queue_is_empty (&window->retransmit_queue));
		g_static_mutex_unlock (&window->retransmit_mutex);
		return 0;
	}

	g_assert (((const GList*)skb)->next == NULL);
	g_assert (((const GList*)skb)->prev == NULL);

/* new request */
	g_queue_push_head_link (&window->retransmit_queue, (GList*)skb);
	g_assert (!g_queue_is_empty (&window->retransmit_queue));
	state->waiting_retransmit = 1;
	g_static_mutex_unlock (&window->retransmit_mutex);
	return 1;
}

/* try to peek a request from the retransmit queue
 *
 * return 0 if request peeked, return -1 if queue is empty.
 */

int
pgm_txw_retransmit_try_peek (
	pgm_txw_t* const		window,
	struct pgm_sk_buff_t** 		skb,
	guint32* const			unfolded_checksum,
	gboolean* const			is_parity,
	guint* const			rs_h			/* parity packet offset */
	)
{
	GList* tail_link;
	const pgm_txw_state_t* state;

/* pre-conditions */
	g_assert (window);
	g_assert (skb);
	g_assert (unfolded_checksum);
	g_assert (is_parity);
	g_assert (rs_h);

	g_trace ("retransmit_try_peek (window:%p skb:%p unfolded_checksum:%p is_parity:%p rs_h:%p)",
		(gpointer)window, (gpointer)skb, (gpointer)unfolded_checksum, (gpointer)is_parity, (gpointer)rs_h);

/* no lock required to detect presence of a request */
	tail_link = g_queue_peek_tail_link (&window->retransmit_queue);
	if (NULL == tail_link) {
		return -1;
	}

	*skb = (struct pgm_sk_buff_t*)tail_link;
	g_assert (pgm_skb_is_valid (*skb));
	state = (pgm_txw_state_t*)&(*skb)->cb;
	*unfolded_checksum = state->unfolded_checksum;

/* must lock before reading to catch parity updates */
	g_static_mutex_lock (&window->retransmit_mutex);
	if (!state->waiting_retransmit)
	{
		g_assert (((const GList*)*skb)->next == NULL);
		g_assert (((const GList*)*skb)->prev == NULL);
	}
	if (state->pkt_cnt_requested) {
		*is_parity	= TRUE;
		*rs_h		= state->pkt_cnt_sent;
	} else {
		*is_parity	= FALSE;
	}
	g_static_mutex_unlock (&window->retransmit_mutex);
	return 0;
}

/* remove head entry from retransmit queue, will fail on assertion if queue is empty.
 */

void
pgm_txw_retransmit_remove (
	pgm_txw_t* const	window
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_txw_state_t* state;

/* pre-conditions */
	g_assert (window);

	g_trace ("retransmit_remove (window:%p)",
		(gpointer)window);

/* tail link is valid without lock */
	GList* tail_link = g_queue_peek_tail_link (&window->retransmit_queue);

/* link must be valid for pop */
	g_assert (tail_link);

	g_static_mutex_lock (&window->retransmit_mutex);
	skb = (struct pgm_sk_buff_t*)tail_link;
	g_assert (pgm_skb_is_valid (skb));
	g_assert (pgm_txw_tsi_is_null (&skb->tsi));
	g_assert (skb->len == (guint8*)skb->tail - (guint8*)skb->data);
	state = (pgm_txw_state_t*)&skb->cb;
	if (!state->waiting_retransmit)
	{
		g_assert (((const GList*)skb)->next == NULL);
		g_assert (((const GList*)skb)->prev == NULL);
	}
	if (state->pkt_cnt_requested)
	{
		state->pkt_cnt_sent++;

/* remove if all requested parity packets have been sent */
		if (state->pkt_cnt_sent == state->pkt_cnt_requested) {
			g_queue_pop_tail_link (&window->retransmit_queue);
			state->waiting_retransmit = 0;
		}
	}
	else	/* selective request */
	{
		g_queue_pop_tail_link (&window->retransmit_queue);
		state->waiting_retransmit = 0;
	}

	g_static_mutex_unlock (&window->retransmit_mutex);
}

/* eof */
