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
#	define G_DISABLE_ASSERT
#	ifdef g_assert_cmpuint
#		undef g_assert_cmpuint
#	endif
#	define g_assert_cmpuint(n1, cmp, n2)	do { (void) 0; } while (0)
#endif

#include <glib.h>

#include "pgm/txwi.h"
#include "pgm/sn.h"
#include "pgm/reed_solomon.h"
#include "pgm/math.h"
#include "pgm/checksum.h"
#include "pgm/tsi.h"


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
pgm_tsi_is_null (
	pgm_tsi_t* const	tsi
	)
{
	pgm_tsi_t nulltsi;

/* pre-conditions */
	g_assert (tsi);

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

/* pre-conditions */
	g_assert (window);

	if (pgm_txw_is_empty (window))
		return NULL;

	if (pgm_uint32_gte (sequence, window->trail) && pgm_uint32_lte (sequence, window->lead))
	{
		const guint32 index_ = sequence % pgm_txw_max_length (window);
		skb = window->pdata[index_];
		g_assert (skb);
		g_assert (pgm_skb_is_valid (skb));
		g_assert (pgm_tsi_is_null (&skb->tsi));
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
	g_return_val_if_fail (window, FALSE);
	return NULL != pgm_txw_retransmit_try_peek (window);
}


/* globals */

static inline void pgm_txw_remove_tail (pgm_txw_t* const);
static int pgm_txw_retransmit_push_parity (pgm_txw_t* const, const guint32, const guint);
static int pgm_txw_retransmit_push_selective (pgm_txw_t* const, const guint32);


/* constructor for transmit window.  zero-length windows are not permitted.
 *
 * returns pointer to window.
 */

pgm_txw_t*
pgm_txw_create (
	const pgm_tsi_t* const	tsi,
	const guint16		tpdu_size,
	const guint32		sqns,		/* transmit window size in sequence numbers */
	const guint		secs,		/* size in seconds */
	const guint		max_rte,	/* max bandwidth */
	const gboolean		use_fec,
	const guint		rs_n,
	const guint		rs_k
	)
{
	pgm_txw_t* window;

/* pre-conditions */
	g_assert (NULL != tsi);
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
	if (use_fec) {
		g_assert_cmpuint (rs_n, >, 0);
		g_assert_cmpuint (rs_k, >, 0);
	}

	g_trace ("create (tsi:%s max-tpdu:%" G_GUINT16_FORMAT " sqns:%" G_GUINT32_FORMAT  " secs %u max-rte %u use-fec:%s rs(n):%u rs(k):%u).\n",
		pgm_tsi_print (tsi),
		tpdu_size, sqns, secs, max_rte,
		use_fec ? "YES" : "NO",
		rs_n, rs_k);

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
	window->tsi = tsi;

/* empty state for transmission group boundaries to align.
 *
 * trail = 0, lead = -1	
 */
	window->lead = -1;
	window->trail = window->lead + 1;

/* reed-solomon forward error correction */
	if (use_fec) {
		window->parity_buffer = pgm_alloc_skb (tpdu_size);
		window->tg_sqn_shift = pgm_power2_log2 (rs_k);
		pgm_rs_create (&window->rs, rs_n, rs_k);
		window->is_fec_enabled = 1;
	}

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
		pgm_txw_remove_tail (window);
	}

/* window must now be empty */
	g_assert_cmpuint (pgm_txw_length (window), ==, 0);
	g_assert_cmpuint (pgm_txw_size (window), ==, 0);
	g_assert (pgm_txw_is_empty (window));
	g_assert (!pgm_txw_is_full (window));

/* retransmit queue must be empty */
	g_assert (!pgm_txw_retransmit_can_peek (window));

/* free reed-solomon state */
	if (window->is_fec_enabled) {
		pgm_free_skb (window->parity_buffer);
		pgm_rs_destroy (&window->rs);
	}

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
	g_assert (pgm_tsi_is_null (&skb->tsi));
	g_assert ((sizeof(struct pgm_header) + sizeof(struct pgm_data)) <= ((guint8*)skb->data - (guint8*)skb->head));

	g_trace ("add (window:%p skb:%p)", (gpointer)window, (gpointer)skb);

	if (pgm_txw_is_full (window))
	{
/* transmit window advancement scheme dependent action here */
		pgm_txw_remove_tail (window);
	}

/* generate new sequence number */
	pgm_atomic_int32_inc (&window->lead);
	skb->sequence = window->lead;

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

static inline
void
pgm_txw_remove_tail (
	pgm_txw_t* const	window
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_txw_state_t* state;

	g_trace ("pgm_txw_remove_tail (window:%p)", (gpointer)window);

/* pre-conditions */
	g_assert (window);
	g_assert (!pgm_txw_is_empty (window));

	skb = _pgm_txw_peek (window, pgm_txw_trail (window));
	g_assert (skb);
	g_assert (pgm_skb_is_valid (skb));
	g_assert (pgm_tsi_is_null (&skb->tsi));

	state = (pgm_txw_state_t*)&skb->cb;
	if (state->waiting_retransmit) {
		g_queue_unlink (&window->retransmit_queue, (GList*)skb);
		state->waiting_retransmit = 0;
	}

/* statistics */
	window->size -= skb->len;

/* remove reference to skb */
#ifdef PGM_TXW_CLEAR_UNUSED_ENTRIES
	const guint32 index_ = skb->sequence % pgm_txw_max_length (window);
	window->pdata[index_] = NULL;
#endif
	pgm_free_skb (skb);

/* advance trailing pointer */
	pgm_atomic_int32_inc (&window->trail);

/* post-conditions */
	g_assert (!pgm_txw_is_full (window));
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

gboolean
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
		return FALSE;

	if (is_parity)
	{
		return pgm_txw_retransmit_push_parity (window, sequence, tg_sqn_shift);
	}
	else
	{
		return pgm_txw_retransmit_push_selective (window, sequence);
	}
}

gboolean
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

	if (NULL == skb) {
		g_trace ("transmission group lead #%" G_GUINT32_FORMAT " not in window.", nak_tg_sqn);
		return FALSE;
	}

	g_assert (pgm_skb_is_valid (skb));
	g_assert (pgm_tsi_is_null (&skb->tsi));
	state = (pgm_txw_state_t*)&skb->cb;

/* check if request can be eliminated */
	if (state->waiting_retransmit)
	{
		g_assert (((const GList*)skb)->next);
		g_assert (((const GList*)skb)->prev);
		if (state->pkt_cnt_requested < nak_pkt_cnt) {
/* more parity packets requested than currently scheduled, simply bump up the count */
			state->pkt_cnt_requested = nak_pkt_cnt;
		}
		return FALSE;
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
	return TRUE;
}

gboolean
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
	if (NULL == skb) {
		g_trace ("requested packet #%" G_GUINT32_FORMAT " not in window.", sequence);
		return FALSE;
	}

	g_assert (pgm_skb_is_valid (skb));
	g_assert (pgm_tsi_is_null (&skb->tsi));
	state = (pgm_txw_state_t*)&skb->cb;

/* check if request can be eliminated */
	if (state->waiting_retransmit) {
		g_assert (!g_queue_is_empty (&window->retransmit_queue));
		return FALSE;
	}

	g_assert (((const GList*)skb)->next == NULL);
	g_assert (((const GList*)skb)->prev == NULL);

/* new request */
	g_queue_push_head_link (&window->retransmit_queue, (GList*)skb);
	g_assert (!g_queue_is_empty (&window->retransmit_queue));
	state->waiting_retransmit = 1;
	return TRUE;
}

/* try to peek a request from the retransmit queue
 *
 * return pointer of first skb in queue, or return NULL if the queue is empty.
 */

struct pgm_sk_buff_t*
pgm_txw_retransmit_try_peek (
	pgm_txw_t* const		window
	)
{
/* pre-conditions */
	g_assert (window);

	g_trace ("retransmit_try_peek (window:%p)", (gpointer)window);

/* no lock required to detect presence of a request */
	GList* tail_link = g_queue_peek_tail_link (&window->retransmit_queue);
	if (NULL == tail_link)
		return NULL;

	struct pgm_sk_buff_t* skb = (struct pgm_sk_buff_t*)tail_link;
	g_assert (pgm_skb_is_valid (skb));
	pgm_txw_state_t* state = (pgm_txw_state_t*)&skb->cb;

	if (!state->waiting_retransmit) {
		g_assert (((const GList*)skb)->next == NULL);
		g_assert (((const GList*)skb)->prev == NULL);
	}
	if (!state->pkt_cnt_requested) {
		return skb;
	}

/* generate parity packet to satisify request */	
	const guint rs_h = state->pkt_cnt_sent % (window->rs.n - window->rs.k);
	const guint32 tg_sqn_mask = 0xffffffff << window->tg_sqn_shift;
	const guint32 tg_sqn = skb->sequence & tg_sqn_mask;
	gboolean is_var_pktlen = FALSE;
	gboolean is_op_encoded = FALSE;
	guint16 parity_length = 0;
	const guint8* src[ window->rs.k ];
	for (unsigned i = 0; i < window->rs.k; i++)
	{
		const struct pgm_sk_buff_t* odata_skb = pgm_txw_peek (window, tg_sqn + i);
		const guint16 odata_tsdu_length = g_ntohs (odata_skb->pgm_header->pgm_tsdu_length);
		if (!parity_length)
		{
			parity_length = odata_tsdu_length;
		}
		else if (odata_tsdu_length != parity_length)
		{
			is_var_pktlen = TRUE;
			if (odata_tsdu_length > parity_length)
				parity_length = odata_tsdu_length;
		}

		src[i] = odata_skb->data;
		if (odata_skb->pgm_header->pgm_options & PGM_OPT_PRESENT) {
			is_op_encoded = TRUE;
		}
	}

/* construct basic PGM header to be completed by send_rdata() */
	skb = window->parity_buffer;
	skb->data = skb->tail = skb->head = skb + 1;

/* space for PGM header */
	pgm_skb_put (skb, sizeof(struct pgm_header));

	skb->pgm_header		= skb->data;
	skb->pgm_data		= (gpointer)( skb->pgm_header + 1 );
	memcpy (skb->pgm_header->pgm_gsi, &window->tsi->gsi, sizeof(pgm_gsi_t));
	skb->pgm_header->pgm_options = PGM_OPT_PARITY;

/* append actual TSDU length if variable length packets, zero pad as necessary.
 */
	if (is_var_pktlen)
	{
		skb->pgm_header->pgm_options |= PGM_OPT_VAR_PKTLEN;

		for (unsigned i = 0; i < window->rs.k; i++)
		{
			struct pgm_sk_buff_t* odata_skb = pgm_txw_peek (window, tg_sqn + i);
			const guint16 odata_tsdu_length = g_ntohs (odata_skb->pgm_header->pgm_tsdu_length);

			g_assert (odata_tsdu_length == odata_skb->len);
			g_assert (parity_length >= odata_tsdu_length);

			if (!odata_skb->zero_padded) {
				memset (odata_skb->tail, 0, parity_length - odata_tsdu_length);
				*(guint16*)((guint8*)odata_skb->data + parity_length) = odata_tsdu_length;
				odata_skb->zero_padded = 1;
			}
		}
		parity_length += 2;
	}

	skb->pgm_header->pgm_tsdu_length = g_htons (parity_length);

/* space for DATA */
	pgm_skb_put (skb, sizeof(struct pgm_data) + parity_length);

	skb->pgm_data->data_sqn	= g_htonl ( tg_sqn | rs_h );

	gpointer data_bytes = skb->pgm_data + 1;

/* encode every option separately, currently only one applies: opt_fragment
 */
	if (is_op_encoded)
	{
		skb->pgm_header->pgm_options |= PGM_OPT_PRESENT;

		struct pgm_opt_fragment null_opt_fragment;
		guint8* opt_src[ window->rs.k ];
		memset (&null_opt_fragment, 0, sizeof(null_opt_fragment));
		*(guint8*)&null_opt_fragment |= PGM_OP_ENCODED_NULL;
		for (unsigned i = 0; i < window->rs.k; i++)
		{
			const struct pgm_sk_buff_t* odata_skb = pgm_txw_peek (window, tg_sqn + i);

			if (odata_skb->pgm_opt_fragment)
			{
				g_assert (odata_skb->pgm_header->pgm_options & PGM_OPT_PRESENT);
/* skip three bytes of header */
				opt_src[i] = (guint8*)odata_skb->pgm_opt_fragment + sizeof (struct pgm_opt_header);
			}
			else
			{
				opt_src[i] = (guint8*)&null_opt_fragment;
			}
		}

/* add options to this rdata packet */
		const guint16 opt_total_length = sizeof(struct pgm_opt_length) +
						 sizeof(struct pgm_opt_header) +
						 sizeof(struct pgm_opt_fragment);

/* add space for PGM options */
		pgm_skb_put (skb, opt_total_length);

		struct pgm_opt_length* opt_len		= data_bytes;
		opt_len->opt_type			= PGM_OPT_LENGTH;
		opt_len->opt_length			= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length		= g_htons ( opt_total_length );
		struct pgm_opt_header* opt_header 	= (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type			= PGM_OPT_FRAGMENT | PGM_OPT_END;
		opt_header->opt_length			= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_fragment);
		opt_header->opt_reserved 		= PGM_OP_ENCODED;
		struct pgm_opt_fragment* opt_fragment	= (struct pgm_opt_fragment*)(opt_header + 1);

/* The cast below is the correct way to handle the problem. 
 * The (void *) cast is to avoid a GCC warning like: 
 *
 *   "warning: dereferencing type-punned pointer will break strict-aliasing rules"
 */
		pgm_rs_encode (&window->rs, (const void**)opt_src, window->rs.k + rs_h, opt_fragment + sizeof(struct pgm_opt_header), sizeof(struct pgm_opt_fragment) - sizeof(struct pgm_opt_header));

		data_bytes = opt_fragment + 1;
	}

/* encode payload */
	pgm_rs_encode (&window->rs, (const void**)src, window->rs.k + rs_h, data_bytes, parity_length);

/* calculate partial checksum */
	const guint tsdu_length = g_ntohs (skb->pgm_header->pgm_tsdu_length);
	state->unfolded_checksum = pgm_csum_partial ((guint8*)skb->tail - tsdu_length, tsdu_length, 0);
	return skb;
}

/* remove head entry from retransmit queue, will fail on assertion if queue is empty.
 */

void
pgm_txw_retransmit_remove_head (
	pgm_txw_t* const	window
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_txw_state_t* state;

/* pre-conditions */
	g_assert (window);

	g_trace ("retransmit_remove_head (window:%p)",
		(gpointer)window);

/* tail link is valid without lock */
	GList* tail_link = g_queue_peek_tail_link (&window->retransmit_queue);

/* link must be valid for pop */
	g_assert (tail_link);

	skb = (struct pgm_sk_buff_t*)tail_link;
	g_assert (pgm_skb_is_valid (skb));
	g_assert (pgm_tsi_is_null (&skb->tsi));
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
}

/* eof */
