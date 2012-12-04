/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * A basic transmit window: pointer array implementation.
 *
 * Copyright (c) 2006-2011 Miru Limited.
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

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/txw.h>


//#define TXW_DEBUG

#ifndef TXW_DEBUG
#	define PGM_DISABLE_ASSERT
#endif


/* testing function: is TSI null
 *
 * returns TRUE if null, returns FALSE if not null.
 */

static inline
bool
pgm_tsi_is_null (
	const void*const	tsi
	)
{
	const union {
		pgm_tsi_t	tsi;
		uint32_t	l[2];
	} *u = tsi;

/* pre-conditions */
	pgm_assert (NULL != tsi);

	return (0 == u->l[0] && 0 == u->l[1]);
}

/* returns the pointer at the given index of the window.  responsibility
 * is with the caller to verify a single user ownership.
 */

static inline
struct pgm_sk_buff_t*
_pgm_txw_peek (
	const pgm_txw_t*const	window,
	const uint32_t		sequence
	)
{
	struct pgm_sk_buff_t* skb;

/* pre-conditions */
	pgm_assert (NULL != window);

	if (pgm_txw_is_empty (window))
		return NULL;

	if (pgm_uint32_gte (sequence, window->trail) && pgm_uint32_lte (sequence, window->lead))
	{
		const uint_fast32_t index_ = sequence % pgm_txw_max_length (window);
		skb = window->pdata[index_];
		pgm_assert (NULL != skb);
		pgm_assert (pgm_skb_is_valid (skb));
		pgm_assert (pgm_tsi_is_null (&skb->tsi));
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
bool
pgm_txw_retransmit_can_peek (
	pgm_txw_t*const		window
	)
{
	pgm_return_val_if_fail (NULL != window, FALSE);
	return (NULL != pgm_txw_retransmit_try_peek (window));
}

/* sequence state must be smaller than PGM skbuff control buffer */
PGM_STATIC_ASSERT(sizeof(struct pgm_txw_state_t) <= sizeof(((struct pgm_sk_buff_t*)0)->cb));

PGM_GNUC_INTERNAL
uint32_t
pgm_txw_get_unfolded_checksum (
	const struct pgm_sk_buff_t*const skb
	)
{
	const pgm_txw_state_t*const state = (const pgm_txw_state_t*const)&skb->cb;
	return state->unfolded_checksum;
}

PGM_GNUC_INTERNAL
void
pgm_txw_set_unfolded_checksum (
	struct pgm_sk_buff_t*const skb,
	const uint32_t		csum
	)
{
	pgm_txw_state_t* state = (pgm_txw_state_t*)&skb->cb;
	state->unfolded_checksum = csum;
}

PGM_GNUC_INTERNAL
void
pgm_txw_inc_retransmit_count (
	struct pgm_sk_buff_t*const skb
	)
{
	pgm_txw_state_t*const state = (pgm_txw_state_t*const)&skb->cb;
	state->retransmit_count++;
}

PGM_GNUC_INTERNAL
bool
pgm_txw_retransmit_is_empty (
	const pgm_txw_t*const	window
	)
{
	pgm_assert (NULL != window);
	return pgm_queue_is_empty (&window->retransmit_queue);
}


/* globals */

static void pgm_txw_remove_tail (pgm_txw_t*const);
static bool pgm_txw_retransmit_push_parity (pgm_txw_t*const, const uint32_t, const uint8_t);
static bool pgm_txw_retransmit_push_selective (pgm_txw_t*const, const uint32_t);


/* constructor for transmit window.  zero-length windows are not permitted.
 *
 * returns pointer to window.
 */

PGM_GNUC_INTERNAL
pgm_txw_t*
pgm_txw_create (
	const pgm_tsi_t*const	tsi,
	const uint16_t		tpdu_size,
	const uint32_t		sqns,		/* transmit window size in sequence numbers */
	const unsigned		secs,		/* size in seconds */
	const ssize_t		max_rte,	/* max bandwidth */
	const bool		use_fec,
	const uint8_t		rs_n,
	const uint8_t		rs_k
	)
{
	pgm_txw_t* window;

/* pre-conditions */
	pgm_assert (NULL != tsi);
	if (sqns) {
		pgm_assert_cmpuint (tpdu_size, ==, 0);
		pgm_assert_cmpuint (sqns, >, 0);
		pgm_assert_cmpuint (sqns & PGM_UINT32_SIGN_BIT, ==, 0);
		pgm_assert_cmpuint (secs, ==, 0);
		pgm_assert_cmpuint (max_rte, ==, 0);
	} else {
		pgm_assert_cmpuint (tpdu_size, >, 0);
		pgm_assert_cmpuint (secs, >, 0);
		pgm_assert_cmpuint (max_rte, >, 0);
	}
	if (use_fec) {
		pgm_assert_cmpuint (rs_n, >, 0);
		pgm_assert_cmpuint (rs_k, >, 0);
	}

	pgm_debug ("create (tsi:%s max-tpdu:%" PRIu16 " sqns:%" PRIu32  " secs %u max-rte %" PRIzd " use-fec:%s rs(n):%u rs(k):%u)",
		pgm_tsi_print (tsi),
		tpdu_size, sqns, secs, max_rte,
		use_fec ? "YES" : "NO",
		rs_n, rs_k);

/* calculate transmit window parameters */
	pgm_assert (sqns || (tpdu_size && secs && max_rte));
	const unsigned alloc_sqns = sqns ? sqns : (unsigned)( (secs * max_rte) / tpdu_size );
	window = pgm_malloc0 (sizeof(pgm_txw_t) + ( alloc_sqns * sizeof(struct pgm_sk_buff_t*) ));
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
	pgm_assert_cmpuint (pgm_txw_max_length (window), ==, alloc_sqns);
	pgm_assert_cmpuint (pgm_txw_length (window), ==, 0);
	pgm_assert_cmpuint (pgm_txw_size (window), ==, 0);
	pgm_assert (pgm_txw_is_empty (window));
	pgm_assert (!pgm_txw_is_full (window));
	pgm_assert (!pgm_txw_retransmit_can_peek (window));

	return window;
}

/* destructor for transmit window.  must not be called more than once for same window.
 */

PGM_GNUC_INTERNAL
void
pgm_txw_shutdown (
	pgm_txw_t*const		window
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert_cmpuint (window->alloc, >, 0);

	pgm_debug ("shutdown (window:%p)", (const void*)window);

/* contents of window */
	while (!pgm_txw_is_empty (window)) {
		pgm_txw_remove_tail (window);
	}

/* window must now be empty */
	pgm_assert_cmpuint (pgm_txw_length (window), ==, 0);
	pgm_assert_cmpuint (pgm_txw_size (window), ==, 0);
	pgm_assert (pgm_txw_is_empty (window));
	pgm_assert (!pgm_txw_is_full (window));

/* retransmit queue must be empty */
	pgm_assert (!pgm_txw_retransmit_can_peek (window));

/* free reed-solomon state */
	if (window->is_fec_enabled) {
		pgm_free_skb (window->parity_buffer);
		pgm_rs_destroy (&window->rs);
	}

/* window */
	pgm_free (window);
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

PGM_GNUC_INTERNAL
void
pgm_txw_add (
	pgm_txw_t*	      const restrict window,
	struct pgm_sk_buff_t* const restrict skb		/* cannot be NULL */
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != skb);
	pgm_assert_cmpuint (pgm_txw_max_length (window), >, 0);
	pgm_assert (pgm_skb_is_valid (skb));
	pgm_assert (((const pgm_list_t*)skb)->next == NULL);
	pgm_assert (((const pgm_list_t*)skb)->prev == NULL);
	pgm_assert (pgm_tsi_is_null (&skb->tsi));
	pgm_assert ((char*)skb->data > (char*)skb->head);
	pgm_assert ((sizeof(struct pgm_header) + sizeof(struct pgm_data)) <= (size_t)((char*)skb->data - (char*)skb->head));

	pgm_debug ("add (window:%p skb:%p)", (const char*)window, (const char*)skb);

	if (pgm_txw_is_full (window))
	{
/* transmit window advancement scheme dependent action here */
		pgm_txw_remove_tail (window);
	}

/* generate new sequence number */
	pgm_atomic_inc32 (&window->lead);
	skb->sequence = window->lead;

/* add skb to window */
	const uint_fast32_t index_ = skb->sequence % pgm_txw_max_length (window);
	window->pdata[index_] = skb;

/* statistics */
	window->size += skb->len;

/* post-conditions */
	pgm_assert_cmpuint (pgm_txw_length (window), >, 0);
	pgm_assert_cmpuint (pgm_txw_length (window), <=, pgm_txw_max_length (window));
}

/* peek an entry from the window for retransmission.
 *
 * returns pointer to skbuff on success, returns NULL on invalid parameters.
 */

PGM_GNUC_INTERNAL
struct pgm_sk_buff_t*
pgm_txw_peek (
	const pgm_txw_t*const	window,
	const uint32_t		sequence
	)
{
	pgm_debug ("peek (window:%p sequence:%" PRIu32 ")",
		(const void*)window, sequence);
	return _pgm_txw_peek (window, sequence);
}

/* remove an entry from the trailing edge of the transmit window.
 */

static
void
pgm_txw_remove_tail (
	pgm_txw_t* const	window
	)
{
	struct pgm_sk_buff_t	*skb;
	pgm_txw_state_t		*state;

	pgm_debug ("pgm_txw_remove_tail (window:%p)", (const void*)window);

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (!pgm_txw_is_empty (window));

	skb = _pgm_txw_peek (window, pgm_txw_trail (window));
	pgm_assert (NULL != skb);
	pgm_assert (pgm_skb_is_valid (skb));
	pgm_assert (pgm_tsi_is_null (&skb->tsi));

	state = (pgm_txw_state_t*)&skb->cb;
	if (state->waiting_retransmit) {
		pgm_queue_unlink (&window->retransmit_queue, (pgm_list_t*)skb);
		state->waiting_retransmit = 0;
	}

/* statistics */
	window->size -= skb->len;
	if (state->retransmit_count > 0) {
		PGM_HISTOGRAM_COUNTS("Tx.RetransmitCount", state->retransmit_count);
	}
	if (state->nak_elimination_count > 0) {
		PGM_HISTOGRAM_COUNTS("Tx.NakEliminationCount", state->nak_elimination_count);
	}

/* remove reference to skb */
	if (PGM_UNLIKELY(pgm_mem_gc_friendly)) {
		const uint_fast32_t index_ = skb->sequence % pgm_txw_max_length (window);
		window->pdata[index_] = NULL;
	}
	pgm_free_skb (skb);

/* advance trailing pointer */
	pgm_atomic_inc32 (&window->trail);

/* post-conditions */
	pgm_assert (!pgm_txw_is_full (window));
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
 * returns FALSE if request was eliminated, returns TRUE if request was
 * added to queue.
 */

PGM_GNUC_INTERNAL
bool
pgm_txw_retransmit_push (
	pgm_txw_t* const	window,
	const uint32_t		sequence,
	const bool		is_parity,	/* parity NAK ⇒ sequence_number = transmission group | packet count */
	const uint8_t		tg_sqn_shift
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert_cmpuint (tg_sqn_shift, <, 8 * sizeof(uint32_t));

	pgm_debug ("retransmit_push (window:%p sequence:%" PRIu32 " is_parity:%s tg_sqn_shift:%u)",
		(const void*)window, sequence, is_parity ? "TRUE" : "FALSE", tg_sqn_shift);

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

static
bool
pgm_txw_retransmit_push_parity (
	pgm_txw_t* const	window,
	const uint32_t		sequence,
	const uint8_t		tg_sqn_shift
	)
{
	struct pgm_sk_buff_t	*skb;
	pgm_txw_state_t		*state;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert_cmpuint (tg_sqn_shift, <, 8 * sizeof(uint32_t));

	const uint32_t tg_sqn_mask = 0xffffffff << tg_sqn_shift;
	const uint32_t nak_tg_sqn  = sequence &  tg_sqn_mask;	/* left unshifted */
	const uint32_t nak_pkt_cnt = sequence & ~tg_sqn_mask;
	skb = _pgm_txw_peek (window, nak_tg_sqn);

	if (NULL == skb) {
		pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Transmission group lead #%" PRIu32 " not in window."), nak_tg_sqn);
		return FALSE;
	}

	pgm_assert (pgm_skb_is_valid (skb));
	pgm_assert (pgm_tsi_is_null (&skb->tsi));
	state = (pgm_txw_state_t*)&skb->cb;

/* check if request can be eliminated */
	if (state->waiting_retransmit)
	{
		pgm_assert (NULL != ((const pgm_list_t*)skb)->next);
		pgm_assert (NULL != ((const pgm_list_t*)skb)->prev);
		if (state->pkt_cnt_requested < nak_pkt_cnt) {
/* more parity packets requested than currently scheduled, simply bump up the count */
			state->pkt_cnt_requested = nak_pkt_cnt;
		}
		state->nak_elimination_count++;
		return FALSE;
	}
	else
	{
		pgm_assert (((const pgm_list_t*)skb)->next == NULL);
		pgm_assert (((const pgm_list_t*)skb)->prev == NULL);
	}

/* new request */
	state->pkt_cnt_requested++;
	pgm_queue_push_head_link (&window->retransmit_queue, (pgm_list_t*)skb);
	pgm_assert (!pgm_queue_is_empty (&window->retransmit_queue));
	state->waiting_retransmit = 1;
	return TRUE;
}

static
bool
pgm_txw_retransmit_push_selective (
	pgm_txw_t* const	window,
	const uint32_t		sequence
	)
{
	struct pgm_sk_buff_t	*skb;
	pgm_txw_state_t		*state;

/* pre-conditions */
	pgm_assert (NULL != window);

	skb = _pgm_txw_peek (window, sequence);
	if (NULL == skb) {
		pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Requested packet #%" PRIu32 " not in window."), sequence);
		return FALSE;
	}

	pgm_assert (pgm_skb_is_valid (skb));
	pgm_assert (pgm_tsi_is_null (&skb->tsi));
	state = (pgm_txw_state_t*)&skb->cb;

/* check if request can be eliminated */
	if (state->waiting_retransmit) {
		pgm_assert (!pgm_queue_is_empty (&window->retransmit_queue));
		state->nak_elimination_count++;
		return FALSE;
	}

	pgm_assert (((const pgm_list_t*)skb)->next == NULL);
	pgm_assert (((const pgm_list_t*)skb)->prev == NULL);

/* new request */
	pgm_queue_push_head_link (&window->retransmit_queue, (pgm_list_t*)skb);
	pgm_assert (!pgm_queue_is_empty (&window->retransmit_queue));
	state->waiting_retransmit = 1;
	return TRUE;
}

/* try to peek a request from the retransmit queue
 *
 * return pointer of first skb in queue, or return NULL if the queue is empty.
 */

PGM_GNUC_INTERNAL
struct pgm_sk_buff_t*
pgm_txw_retransmit_try_peek (
	pgm_txw_t* const	window
	)
{
	struct pgm_sk_buff_t	 *skb;
	pgm_txw_state_t		 *state;
	bool			  is_var_pktlen = FALSE;
	bool			  is_op_encoded = FALSE;
	uint16_t		  parity_length = 0;
	const pgm_gf8_t		**src;
	void			 *data;

/* pre-conditions */
	pgm_assert (NULL != window);

	src = pgm_newa (const pgm_gf8_t*, window->rs.k);

	pgm_debug ("retransmit_try_peek (window:%p)", (const void*)window);

/* no lock required to detect presence of a request */
	skb = (struct pgm_sk_buff_t*)pgm_queue_peek_tail_link (&window->retransmit_queue);
	if (PGM_UNLIKELY(NULL == skb)) {
		pgm_debug ("retransmit queue empty on peek.");
		return NULL;
	}

	pgm_assert (pgm_skb_is_valid (skb));
	state = (pgm_txw_state_t*)&skb->cb;

	if (!state->waiting_retransmit) {
		pgm_assert (((const pgm_list_t*)skb)->next == NULL);
		pgm_assert (((const pgm_list_t*)skb)->prev == NULL);
	}
/* packet payload still in transit */
	if (PGM_UNLIKELY(1 != pgm_atomic_read32 (&skb->users))) {
		pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Retransmit sqn #%" PRIu32 " is still in transit in transmit thread."), skb->sequence);
		return NULL;
	}
	if (!state->pkt_cnt_requested) {
		return skb;
	}

/* generate parity packet to satisify request */	
	const uint8_t rs_h = state->pkt_cnt_sent % (window->rs.n - window->rs.k);
	const uint32_t tg_sqn_mask = 0xffffffff << window->tg_sqn_shift;
	const uint32_t tg_sqn = skb->sequence & tg_sqn_mask;
	for (uint_fast8_t i = 0; i < window->rs.k; i++)
	{
		const struct pgm_sk_buff_t* odata_skb = pgm_txw_peek (window, tg_sqn + i);
		const uint16_t odata_tsdu_length = ntohs (odata_skb->pgm_header->pgm_tsdu_length);
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
	skb->pgm_data		= (void*)( skb->pgm_header + 1 );
	memcpy (skb->pgm_header->pgm_gsi, &window->tsi->gsi, sizeof(pgm_gsi_t));
	skb->pgm_header->pgm_options = PGM_OPT_PARITY;

/* append actual TSDU length if variable length packets, zero pad as necessary.
 */
	if (is_var_pktlen)
	{
		skb->pgm_header->pgm_options |= PGM_OPT_VAR_PKTLEN;

		for (uint_fast8_t i = 0; i < window->rs.k; i++)
		{
			struct pgm_sk_buff_t* odata_skb = pgm_txw_peek (window, tg_sqn + i);
			const uint16_t odata_tsdu_length = ntohs (odata_skb->pgm_header->pgm_tsdu_length);

			pgm_assert (odata_tsdu_length == odata_skb->len);
			pgm_assert (parity_length >= odata_tsdu_length);

			if (!odata_skb->zero_padded) {
				memset (odata_skb->tail, 0, parity_length - odata_tsdu_length);
				*(uint16_t*)((char*)odata_skb->data + parity_length) = odata_tsdu_length;
				odata_skb->zero_padded = 1;
			}
		}
		parity_length += 2;
	}

	skb->pgm_header->pgm_tsdu_length = htons (parity_length);

/* space for DATA */
	pgm_skb_put (skb, sizeof(struct pgm_data) + parity_length);

	skb->pgm_data->data_sqn	= htonl ( tg_sqn | rs_h );

	data = skb->pgm_data + 1;

/* encode every option separately, currently only one applies: opt_fragment
 */
	if (is_op_encoded)
	{
		struct pgm_opt_header	*opt_header;
		struct pgm_opt_length	*opt_len;
		struct pgm_opt_fragment	*opt_fragment, null_opt_fragment;
		const pgm_gf8_t		*opt_src[ window->rs.k ];

		skb->pgm_header->pgm_options |= PGM_OPT_PRESENT;

		memset (&null_opt_fragment, 0, sizeof(null_opt_fragment));
		*(uint8_t*)&null_opt_fragment |= PGM_OP_ENCODED_NULL;

		for (uint_fast8_t i = 0; i < window->rs.k; i++)
		{
			const struct pgm_sk_buff_t* odata_skb = pgm_txw_peek (window, tg_sqn + i);

			if (odata_skb->pgm_opt_fragment)
			{
				pgm_assert (odata_skb->pgm_header->pgm_options & PGM_OPT_PRESENT);
/* skip three bytes of header */
				opt_src[i] = (pgm_gf8_t*)((char*)odata_skb->pgm_opt_fragment + sizeof (struct pgm_opt_header));
			}
			else
			{
				opt_src[i] = (pgm_gf8_t*)&null_opt_fragment;
			}
		}

/* add options to this rdata packet */
		const uint16_t opt_total_length = sizeof(struct pgm_opt_length) +
						 sizeof(struct pgm_opt_header) +
						 sizeof(struct pgm_opt_fragment);

/* add space for PGM options */
		pgm_skb_put (skb, opt_total_length);

		opt_len					= data;
		opt_len->opt_type			= PGM_OPT_LENGTH;
		opt_len->opt_length			= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length		= htons ( opt_total_length );
		opt_header			 	= (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type			= PGM_OPT_FRAGMENT | PGM_OPT_END;
		opt_header->opt_length			= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_fragment);
		opt_header->opt_reserved 		= PGM_OP_ENCODED;
		opt_fragment				= (struct pgm_opt_fragment*)(opt_header + 1);

/* The cast below is the correct way to handle the problem. 
 * The (void *) cast is to avoid a GCC warning like: 
 *
 *   "warning: dereferencing type-punned pointer will break strict-aliasing rules"
 */
		pgm_rs_encode (&window->rs,
				opt_src,
				window->rs.k + rs_h,
				(pgm_gf8_t*)((char*)opt_fragment + sizeof(struct pgm_opt_header)),
				sizeof(struct pgm_opt_fragment) - sizeof(struct pgm_opt_header));

		data = opt_fragment + 1;
	}

/* encode payload */
	pgm_rs_encode (&window->rs,
			src,
			window->rs.k + rs_h,
			data,
			parity_length);

/* calculate partial checksum */
	const uint16_t tsdu_length = ntohs (skb->pgm_header->pgm_tsdu_length);
	state->unfolded_checksum = pgm_csum_partial ((char*)skb->tail - tsdu_length, tsdu_length, 0);
	return skb;
}

/* remove head entry from retransmit queue, will fail on assertion if queue is empty.
 */

PGM_GNUC_INTERNAL
void
pgm_txw_retransmit_remove_head (
	pgm_txw_t* const	window
	)
{
	struct pgm_sk_buff_t	*skb;
	pgm_txw_state_t		*state;

/* pre-conditions */
	pgm_assert (NULL != window);

	pgm_debug ("retransmit_remove_head (window:%p)",
		(const void*)window);

/* tail link is valid without lock */
	skb = (struct pgm_sk_buff_t*)pgm_queue_peek_tail_link (&window->retransmit_queue);
	pgm_assert (pgm_skb_is_valid (skb));
	pgm_assert (pgm_tsi_is_null (&skb->tsi));
	state = (pgm_txw_state_t*)&skb->cb;
	if (!state->waiting_retransmit)
	{
		pgm_assert (((const pgm_list_t*)skb)->next == NULL);
		pgm_assert (((const pgm_list_t*)skb)->prev == NULL);
	}
	if (state->pkt_cnt_requested)
	{
		state->pkt_cnt_sent++;

/* remove if all requested parity packets have been sent */
		if (state->pkt_cnt_sent == state->pkt_cnt_requested) {
			pgm_queue_pop_tail_link (&window->retransmit_queue);
			state->waiting_retransmit = 0;
		}
	}
	else	/* selective request */
	{
		pgm_queue_pop_tail_link (&window->retransmit_queue);
		state->waiting_retransmit = 0;
	}
}

/* eof */
