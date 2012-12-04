/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * A basic receive window: pointer array implementation.
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
#include <impl/rxw.h>


//#define RXW_DEBUG

#ifndef RXW_DEBUG
#	define PGM_DISABLE_ASSERT
#endif


/* testing function: is TSI null
 *
 * returns TRUE if null, returns FALSE if not null.
 */

static inline
bool
_pgm_tsi_is_null (
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

/* sequence state must be smaller than PGM skbuff control buffer */
PGM_STATIC_ASSERT(sizeof(struct pgm_rxw_state_t) <= sizeof(((struct pgm_sk_buff_t*)0)->cb));

static void _pgm_rxw_define (pgm_rxw_t*const, const uint32_t);
static void _pgm_rxw_update_trail (pgm_rxw_t*const, const uint32_t);
static inline uint32_t _pgm_rxw_update_lead (pgm_rxw_t*const, const uint32_t, const pgm_time_t, const pgm_time_t);
static inline uint32_t _pgm_rxw_tg_sqn (pgm_rxw_t*const, const uint32_t);
static inline uint32_t _pgm_rxw_pkt_sqn (pgm_rxw_t*const, const uint32_t);
static inline bool _pgm_rxw_is_first_of_tg_sqn (pgm_rxw_t*const, const uint32_t);
static inline bool _pgm_rxw_is_last_of_tg_sqn (pgm_rxw_t*const, const uint32_t);
static int _pgm_rxw_insert (pgm_rxw_t*const restrict, struct pgm_sk_buff_t*const restrict);
static int _pgm_rxw_append (pgm_rxw_t*const restrict, struct pgm_sk_buff_t*const restrict, const pgm_time_t);
static int _pgm_rxw_add_placeholder_range (pgm_rxw_t*const, const uint32_t, const pgm_time_t, const pgm_time_t);
static void _pgm_rxw_unlink (pgm_rxw_t*const restrict, struct pgm_sk_buff_t*const restrict);
static uint32_t _pgm_rxw_remove_trail (pgm_rxw_t*const);
static void _pgm_rxw_state (pgm_rxw_t*const restrict, struct pgm_sk_buff_t*const restrict, const int);
static inline void _pgm_rxw_shuffle_parity (pgm_rxw_t*const restrict, struct pgm_sk_buff_t*const restrict);
static inline ssize_t _pgm_rxw_incoming_read (pgm_rxw_t*const restrict, struct pgm_msgv_t**restrict, uint32_t);
static bool _pgm_rxw_is_apdu_complete (pgm_rxw_t*const, const uint32_t);
static inline ssize_t _pgm_rxw_incoming_read_apdu (pgm_rxw_t*const restrict, struct pgm_msgv_t**restrict);
static inline int _pgm_rxw_recovery_update (pgm_rxw_t*const, const uint32_t, const pgm_time_t);
static inline int _pgm_rxw_recovery_append (pgm_rxw_t*const, const pgm_time_t, const pgm_time_t);


/* returns the pointer at the given index of the window.
 */

static
struct pgm_sk_buff_t*
_pgm_rxw_peek (
	const pgm_rxw_t* const	window,
	const uint32_t		sequence
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);

	if (pgm_rxw_is_empty (window))
		return NULL;

	if (pgm_uint32_gte (sequence, window->trail) && pgm_uint32_lte (sequence, window->lead))
	{
		const uint_fast32_t index_ = sequence % pgm_rxw_max_length (window);
		struct pgm_sk_buff_t* skb = window->pdata[index_];
/* availability only guaranteed inside commit window */
		if (pgm_uint32_lt (sequence, window->commit_lead)) {
			pgm_assert (NULL != skb);
			pgm_assert (pgm_skb_is_valid (skb));
			pgm_assert (!_pgm_tsi_is_null (&skb->tsi));
		}
		return skb;
	}

	return NULL;
}

/* sections of the receive window:
 * 
 *  |     Commit       |   Incoming   |
 *  |<---------------->|<------------>|
 *  |                  |              |
 * trail         commit-lead        lead
 *
 * commit buffers are currently held by the application, the window trail
 * cannot be advanced if packets remain in the commit buffer.
 *
 * incoming buffers are waiting to be passed to the application.
 */

static inline
uint32_t
_pgm_rxw_commit_length (
	const pgm_rxw_t* const	window
	)
{
	pgm_assert (NULL != window);
	return window->commit_lead - window->trail;
}

static inline
bool
_pgm_rxw_commit_is_empty (
	const pgm_rxw_t* const	window
	)
{
	pgm_assert (NULL != window);
	return (_pgm_rxw_commit_length (window) == 0);
}

static inline
uint32_t
_pgm_rxw_incoming_length (
	const pgm_rxw_t* const	window
	)
{
	pgm_assert (NULL != window);
	return ( 1 + window->lead ) - window->commit_lead;
}

static inline
bool
_pgm_rxw_incoming_is_empty (
	const pgm_rxw_t* const	window
	)
{
	pgm_assert (NULL != window);
	return (_pgm_rxw_incoming_length (window) == 0);
}

/* constructor for receive window.  zero-length windows are not permitted.
 *
 * returns pointer to window.
 */

PGM_GNUC_INTERNAL
pgm_rxw_t*
pgm_rxw_create (
	const pgm_tsi_t*const	tsi,
	const uint16_t		tpdu_size,
	const unsigned		sqns,		/* receive window size in sequence numbers */
	const unsigned		secs,		/* size in seconds */
	const ssize_t		max_rte,	/* max bandwidth */
	const uint32_t		ack_c_p
	)
{
	pgm_rxw_t* window;

/* pre-conditions */
	pgm_assert (NULL != tsi);
	pgm_assert_cmpuint (tpdu_size, >, 0);
	if (sqns) {
		pgm_assert_cmpuint (sqns, >, 0);
		pgm_assert_cmpuint (sqns & PGM_UINT32_SIGN_BIT, ==, 0);
		pgm_assert_cmpuint (secs, ==, 0);
		pgm_assert_cmpuint (max_rte, ==, 0);
	} else {
		pgm_assert_cmpuint (secs, >, 0);
		pgm_assert_cmpuint (max_rte, >, 0);
	}

	pgm_debug ("create (tsi:%s max-tpdu:%" PRIu16 " sqns:%" PRIu32  " secs %u max-rte %" PRIzd " ack-c_p %" PRIu32 ")",
		pgm_tsi_print (tsi), tpdu_size, sqns, secs, max_rte, ack_c_p);

/* calculate receive window parameters */
	pgm_assert (sqns || (secs && max_rte));
	const unsigned alloc_sqns = sqns ? sqns : (unsigned)( (secs * max_rte) / tpdu_size );
	window = pgm_malloc0 (sizeof(pgm_rxw_t) + ( alloc_sqns * sizeof(struct pgm_sk_buff_t*) ));

	window->tsi		= tsi;
	window->max_tpdu	= tpdu_size;

/* empty state:
 *
 * trail = 0, lead = -1
 * commit_trail = commit_lead = rxw_trail = rxw_trail_init = 0
 */
	window->lead = -1;
	window->trail = window->lead + 1;

/* limit retransmit requests on late session joining */
	window->is_constrained = TRUE;

/* minimum value of RS::k = 1 */
	window->tg_size = 1;

/* PGMCC filter weight */
	window->ack_c_p = pgm_fp16 (ack_c_p);
	window->bitmap = 0xffffffff;

/* pointer array */
	window->alloc = alloc_sqns;

/* post-conditions */
	pgm_assert_cmpuint (pgm_rxw_max_length (window), ==, alloc_sqns);
	pgm_assert_cmpuint (pgm_rxw_length (window), ==, 0);
	pgm_assert_cmpuint (pgm_rxw_size (window), ==, 0);
	pgm_assert (pgm_rxw_is_empty (window));
	pgm_assert (!pgm_rxw_is_full (window));

	return window;
}

/* destructor for receive window.  must not be called more than once for same window.
 */

PGM_GNUC_INTERNAL
void
pgm_rxw_destroy (
	pgm_rxw_t* const	window
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert_cmpuint (window->alloc, >, 0);

	pgm_debug ("destroy (window:%p)", (const void*)window);

/* contents of window */
	while (!pgm_rxw_is_empty (window)) {
		_pgm_rxw_remove_trail (window);
	}

/* window must now be empty */
	pgm_assert_cmpuint (pgm_rxw_length (window), ==, 0);
	pgm_assert_cmpuint (pgm_rxw_size (window), ==, 0);
	pgm_assert (pgm_rxw_is_empty (window));
	pgm_assert (!pgm_rxw_is_full (window));

/* window */
	pgm_free (window);
}

/* add skb to receive window.  window has fixed size and will not grow.
 * PGM skbuff data/tail pointers must point to the PGM payload, and hence skb->len
 * is allowed to be zero.
 *
 * if the skb sequence number indicates lost packets placeholders will be defined
 * for each missing entry in the window.
 *
 * side effects:
 *
 * 1) sequence number is set in skb from PGM header value.
 * 2) window may be updated with new skb.
 * 3) placeholders may be created for detected lost packets.
 * 4) parity skbs may be shuffled to accomodate original data.
 *
 * returns:
 * PGM_RXW_INSERTED - packet filled a waiting placeholder, skb consumed.
 * PGM_RXW_APPENDED - packet advanced window lead, skb consumed.
 * PGM_RXW_MISSING - missing packets detected whilst window lead was adanced, skb consumed.
 * PGM_RXW_DUPLICATE - re-transmission of previously seen packet.
 * PGM_RXW_MALFORMED - corrupted or invalid packet.
 * PGM_RXW_BOUNDS - packet out of window.
 *
 * it is an error to try to free the skb after adding to the window.
 */

PGM_GNUC_INTERNAL
int
pgm_rxw_add (
	pgm_rxw_t*	      const restrict window,
	struct pgm_sk_buff_t* const restrict skb,
	const pgm_time_t		     now,
	const pgm_time_t		     nak_rb_expiry	/* calculated expiry time for this skb */
	)
{
	pgm_rxw_state_t* const state = (pgm_rxw_state_t*)&skb->cb;
	int status;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != skb);
	pgm_assert_cmpuint (nak_rb_expiry, >, 0);
	pgm_assert_cmpuint (pgm_rxw_max_length (window), >, 0);
	pgm_assert (pgm_skb_is_valid (skb));
	pgm_assert (((const pgm_list_t*)skb)->next == NULL);
	pgm_assert (((const pgm_list_t*)skb)->prev == NULL);
	pgm_assert (!_pgm_tsi_is_null (&skb->tsi));
	pgm_assert ((char*)skb->data > (char*)skb->head);
	pgm_assert (sizeof(struct pgm_header) + sizeof(struct pgm_data) <= (size_t)((char*)skb->data - (char*)skb->head));
	pgm_assert (skb->len == ((char*)skb->tail - (char*)skb->data));

	pgm_debug ("add (window:%p skb:%p nak_rb_expiry:%" PGM_TIME_FORMAT ")",
		(const void*)window, (const void*)skb, nak_rb_expiry);

	skb->sequence = ntohl (skb->pgm_data->data_sqn);

/* protocol sanity check: tsdu size */
	if (PGM_UNLIKELY(skb->len != ntohs (skb->pgm_header->pgm_tsdu_length)))
		return PGM_RXW_MALFORMED;

/* protocol sanity check: valid trail pointer wrt. sequence */
	if (PGM_UNLIKELY(skb->sequence - ntohl (skb->pgm_data->data_trail) >= ((UINT32_MAX/2)-1)))
		return PGM_RXW_BOUNDS;

/* verify fragment header for original data, parity packets include a
 * parity fragment header
 */
	if (!(skb->pgm_header->pgm_options & PGM_OPT_PARITY) &&
	    skb->pgm_opt_fragment)
	{
/* protocol sanity check: single fragment APDU */
		if (PGM_UNLIKELY(ntohl (skb->of_apdu_len) == skb->len))
			skb->pgm_opt_fragment = NULL;

/* protocol sanity check: minimum APDU length */
		if (PGM_UNLIKELY(ntohl (skb->of_apdu_len) < skb->len))
			return PGM_RXW_MALFORMED;

/* protocol sanity check: sequential ordering */
		if (PGM_UNLIKELY(pgm_uint32_gt (ntohl (skb->of_apdu_first_sqn), skb->sequence)))
			return PGM_RXW_MALFORMED;

/* protocol sanity check: maximum APDU length */
		if (PGM_UNLIKELY(ntohl (skb->of_apdu_len) > PGM_MAX_APDU))
			return PGM_RXW_MALFORMED;
	}

/* first packet of a session defines the window */
	if (PGM_UNLIKELY(!window->is_defined))
		_pgm_rxw_define (window, skb->sequence - 1);	/* previous_lead needed for append to occur */
	else
		_pgm_rxw_update_trail (window, ntohl (skb->pgm_data->data_trail));

/* bounds checking for parity data occurs at the transmission group sequence number */
	if (skb->pgm_header->pgm_options & PGM_OPT_PARITY)
	{
		if (pgm_uint32_lt (_pgm_rxw_tg_sqn (window, skb->sequence), _pgm_rxw_tg_sqn (window, window->commit_lead)))
			return PGM_RXW_DUPLICATE;

		if (pgm_uint32_lt (_pgm_rxw_tg_sqn (window, skb->sequence), _pgm_rxw_tg_sqn (window, window->lead))) {
			window->has_event = 1;
			return _pgm_rxw_insert (window, skb);
		}

		const struct pgm_sk_buff_t* const first_skb = _pgm_rxw_peek (window, _pgm_rxw_tg_sqn (window, skb->sequence));
		const pgm_rxw_state_t* const first_state = (pgm_rxw_state_t*)&first_skb->cb;

		if (_pgm_rxw_tg_sqn (window, skb->sequence) == _pgm_rxw_tg_sqn (window, window->lead)) {
			window->has_event = 1;
			if (NULL == first_state || first_state->is_contiguous) {
				state->is_contiguous = 1;
				return _pgm_rxw_append (window, skb, now);
			} else
				return _pgm_rxw_insert (window, skb);
		}

		pgm_assert (NULL != first_state);
		status = _pgm_rxw_add_placeholder_range (window, _pgm_rxw_tg_sqn (window, skb->sequence), now, nak_rb_expiry);
	}
	else
	{
		if (pgm_uint32_lt (skb->sequence, window->commit_lead)) {
			if (pgm_uint32_gte (skb->sequence, window->trail))
				return PGM_RXW_DUPLICATE;
			else
				return PGM_RXW_BOUNDS;
		}

		if (pgm_uint32_lte (skb->sequence, window->lead)) {
			window->has_event = 1;
			return _pgm_rxw_insert (window, skb);
		}

		if (skb->sequence == pgm_rxw_next_lead (window)) {
			window->has_event = 1;
			if (_pgm_rxw_is_first_of_tg_sqn (window, skb->sequence))
				state->is_contiguous = 1;
			return _pgm_rxw_append (window, skb, now);
		}

		status = _pgm_rxw_add_placeholder_range (window, skb->sequence, now, nak_rb_expiry);
	}

	if (PGM_RXW_APPENDED == status) {
		status = _pgm_rxw_append (window, skb, now);
		if (PGM_RXW_APPENDED == status)
			status = PGM_RXW_MISSING;
	}
	return status;
}

/* trail is the next packet to commit upstream, lead is the leading edge
 * of the receive window with possible gaps inside, rxw_trail is the transmit
 * window trail for retransmit requests.
 */

/* define window by parameters of first data packet.
 */

static
void
_pgm_rxw_define (
	pgm_rxw_t* const	window,
	const uint32_t		lead
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (pgm_rxw_is_empty (window));
	pgm_assert (_pgm_rxw_commit_is_empty (window));
	pgm_assert (_pgm_rxw_incoming_is_empty (window));
	pgm_assert (!window->is_defined);

	window->lead = lead;
	window->commit_lead = window->rxw_trail = window->rxw_trail_init = window->trail = window->lead + 1;
	window->is_constrained = window->is_defined = TRUE;

/* post-conditions */
	pgm_assert (pgm_rxw_is_empty (window));
	pgm_assert (_pgm_rxw_commit_is_empty (window));
	pgm_assert (_pgm_rxw_incoming_is_empty (window));
	pgm_assert (window->is_defined);
	pgm_assert (window->is_constrained);
}

/* update window with latest transmitted parameters.
 *
 * returns count of placeholders added into window, used to start sending naks.
 */

PGM_GNUC_INTERNAL
unsigned
pgm_rxw_update (
	pgm_rxw_t* const	window,
	const uint32_t		txw_lead,
	const uint32_t		txw_trail,
	const pgm_time_t	now,
	const pgm_time_t	nak_rb_expiry		/* packet expiration time */
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert_cmpuint (nak_rb_expiry, >, 0);

	pgm_debug ("pgm_rxw_update (window:%p txw-lead:%" PRIu32 " txw-trail:%" PRIu32 " nak-rb-expiry:%" PGM_TIME_FORMAT ")",
		(void*)window, txw_lead, txw_trail, nak_rb_expiry);

	if (PGM_UNLIKELY(!window->is_defined)) {
		_pgm_rxw_define (window, txw_lead);
		return 0;
	}

	_pgm_rxw_update_trail (window, txw_trail);
	return _pgm_rxw_update_lead (window, txw_lead, now, nak_rb_expiry);
}

/* update trailing edge of receive window
 */

static
void
_pgm_rxw_update_trail (
	pgm_rxw_t* const	window,
	const uint32_t		txw_trail
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);

/* advertised trail is less than the current value */
	if (PGM_UNLIKELY(pgm_uint32_lte (txw_trail, window->rxw_trail)))
		return;

/* protocol sanity check: advertised trail jumps too far ahead */
	if (PGM_UNLIKELY(txw_trail - window->rxw_trail > ((UINT32_MAX/2)-1)))
		return;

/* retransmissions requests are constrained on startup until the advertised trail advances
 * beyond the first data sequence number.
 */
	if (PGM_UNLIKELY(window->is_constrained))
	{
		if (pgm_uint32_gt (txw_trail, window->rxw_trail_init))
			window->is_constrained = FALSE;
		else
			return;
	}

	window->rxw_trail = txw_trail;

/* new value doesn't affect window */
	if (PGM_UNLIKELY(pgm_uint32_lte (window->rxw_trail, window->trail)))
		return;

/* jump remaining sequence numbers if window is empty */
	if (pgm_rxw_is_empty (window))
	{
		const uint32_t distance = (int32_t)(window->rxw_trail) - (int32_t)(window->trail);
		window->commit_lead = window->trail += distance;
		window->lead += distance;

/* add loss to bitmap */
		if (distance > 32)	window->bitmap = 0;
		else			window->bitmap <<= distance;

/* update the Exponential Moving Average (EMA) data loss with long jump:
 *  s_t = α × (p₁ + (1 - α) × p₂ + (1 - α)² × p₃ + ⋯)
 * omitting the weight by stopping after k terms,
 *      = α × ((1 - α)^^k + (1 - α)^^{k+1} +(1 - α)^^{k+1} + ⋯)
 *      = α × (1 - α)^^k × (1 + (1 - α) + (1 - α)² + ⋯)
 *      = (1 - α)^^k
 */
		window->data_loss = pgm_fp16mul (window->data_loss, pgm_fp16pow (pgm_fp16 (1) - window->ack_c_p, distance));

		window->cumulative_losses += distance;
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Data loss due to trailing edge update, fragment count %" PRIu32 "."),window->fragment_count);
		pgm_assert (pgm_rxw_is_empty (window));
		pgm_assert (_pgm_rxw_commit_is_empty (window));
		pgm_assert (_pgm_rxw_incoming_is_empty (window));
		return;
	}

/* remove all buffers between commit lead and advertised rxw_trail */
	for (uint32_t sequence = window->commit_lead;
	     pgm_uint32_gt (window->rxw_trail, sequence) && pgm_uint32_gte (window->lead, sequence);
	     sequence++)
	{
		struct pgm_sk_buff_t* skb;
		pgm_rxw_state_t* state;

		skb = _pgm_rxw_peek (window, sequence);
		pgm_assert (NULL != skb);
		state = (pgm_rxw_state_t*)&skb->cb;

		switch (state->pkt_state) {
		case PGM_PKT_STATE_HAVE_DATA:
		case PGM_PKT_STATE_HAVE_PARITY:
		case PGM_PKT_STATE_LOST_DATA:
			break;

		case PGM_PKT_STATE_ERROR:
			pgm_assert_not_reached();

		default:
			pgm_rxw_lost (window, sequence);
			break;
		}
	}

/* post-conditions: only after flush */
//	pgm_assert (!pgm_rxw_is_full (window));
}

/* update FEC parameters
 */

PGM_GNUC_INTERNAL
void
pgm_rxw_update_fec (
	pgm_rxw_t* const	window,
	const uint8_t		rs_k
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert_cmpuint (rs_k, >, 1);

	pgm_debug ("pgm_rxw_update_fec (window:%p rs(k):%u)",
		(void*)window, rs_k);

	if (window->is_fec_available) {
		if (rs_k == window->rs.k) return;
		pgm_rs_destroy (&window->rs);
	} else
		window->is_fec_available = 1;
	pgm_rs_create (&window->rs, PGM_RS_DEFAULT_N, rs_k);
	window->tg_sqn_shift = pgm_power2_log2 (rs_k);
	window->tg_size = window->rs.k;
}

/* add one placeholder to leading edge due to detected lost packet.
 */

static
void
_pgm_rxw_add_placeholder (
	pgm_rxw_t* const	window,
	const pgm_time_t	now,
	const pgm_time_t	nak_rb_expiry
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (!pgm_rxw_is_full (window));

/* advance lead */
	window->lead++;

/* add loss to bitmap */
	window->bitmap <<= 1;

/* update the Exponential Moving Average (EMA) data loss with loss:
 *     s_t = α × x_{t-1} + (1 - α) × s_{t-1}
 * x_{t-1} = 1
 *   ∴ s_t = α + (1 - α) × s_{t-1}
 */
	window->data_loss = window->ack_c_p + pgm_fp16mul ((pgm_fp16 (1) - window->ack_c_p), window->data_loss);

	skb			= pgm_alloc_skb (window->max_tpdu);
	state			= (pgm_rxw_state_t*)&skb->cb;
	skb->tstamp		= now;
	skb->sequence		= window->lead;
	state->timer_expiry	= nak_rb_expiry;

	if (!_pgm_rxw_is_first_of_tg_sqn (window, skb->sequence))
	{
		struct pgm_sk_buff_t* first_skb = _pgm_rxw_peek (window, _pgm_rxw_tg_sqn (window, skb->sequence));
		if (first_skb) {
			pgm_rxw_state_t* first_state = (pgm_rxw_state_t*)&first_skb->cb;
			first_state->is_contiguous = 0;
		}
	}

/* add skb to window */
	const uint_fast32_t index_	= skb->sequence % pgm_rxw_max_length (window);
	window->pdata[index_]		= skb;

	pgm_rxw_state (window, skb, PGM_PKT_STATE_BACK_OFF);

/* post-conditions */
	pgm_assert_cmpuint (pgm_rxw_length (window), >, 0);
	pgm_assert_cmpuint (pgm_rxw_length (window), <=, pgm_rxw_max_length (window));
	pgm_assert_cmpuint (_pgm_rxw_incoming_length (window), >, 0);
}

/* add a range of placeholders to the window.
 */

static
int
_pgm_rxw_add_placeholder_range (
	pgm_rxw_t* const	window,
	const uint32_t		sequence,
	const pgm_time_t	now,
	const pgm_time_t	nak_rb_expiry
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (pgm_uint32_gt (sequence, pgm_rxw_lead (window)));

/* check bounds of commit window */
	const uint32_t new_commit_sqns = ( 1 + sequence ) - window->trail;
        if ( !_pgm_rxw_commit_is_empty (window) &&
	     (new_commit_sqns >= pgm_rxw_max_length (window)) )
        {
		_pgm_rxw_update_lead (window, sequence, now, nak_rb_expiry);
		return PGM_RXW_BOUNDS;		/* effectively a slow consumer */
        }

	if (pgm_rxw_is_full (window)) {
		pgm_assert (_pgm_rxw_commit_is_empty (window));
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Receive window full on placeholder sequence."));
		_pgm_rxw_remove_trail (window);
	}

/* if packet is non-contiguous to current leading edge add place holders
 * TODO: can be rather inefficient on packet loss looping through dropped sequence numbers
 */
	while (pgm_rxw_next_lead (window) != sequence)
	{
		_pgm_rxw_add_placeholder (window, now, nak_rb_expiry);
		if (pgm_rxw_is_full (window)) {
			pgm_assert (_pgm_rxw_commit_is_empty (window));
			pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Receive window full on placeholder sequence."));
			_pgm_rxw_remove_trail (window);
		}
	}

/* post-conditions */
	pgm_assert (!pgm_rxw_is_full (window));

	return PGM_RXW_APPENDED;
}

/* update leading edge of receive window.
 *
 * returns number of place holders added.
 */

static
unsigned
_pgm_rxw_update_lead (
	pgm_rxw_t* const	window,
	const uint32_t		txw_lead,
	const pgm_time_t	now,
	const pgm_time_t	nak_rb_expiry
	)
{
	uint32_t lead;
	unsigned lost = 0;

/* pre-conditions */
	pgm_assert (NULL != window);

/* advertised lead is less than the current value */
	if (PGM_UNLIKELY(pgm_uint32_lte (txw_lead, window->lead)))
		return 0;

/* committed packets limit constrain the lead until they are released */
	if (!_pgm_rxw_commit_is_empty (window) &&
	    (txw_lead - window->trail) >= pgm_rxw_max_length (window))
	{
		lead = window->trail + pgm_rxw_max_length (window) - 1;
		if (lead == window->lead)
			return 0;
	}
	else
		lead = txw_lead;

/* count lost sequences */
	while (window->lead != lead)
	{
/* slow consumer or fast producer */
		if (pgm_rxw_is_full (window)) {
			pgm_assert (_pgm_rxw_commit_is_empty (window));
			pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Receive window full on window lead advancement."));
			_pgm_rxw_remove_trail (window);
		}
		_pgm_rxw_add_placeholder (window, now, nak_rb_expiry);
		lost++;
	}

	return lost;
}

/* checks whether an APDU is unrecoverable due to lost TPDUs.
 */

static inline
bool
_pgm_rxw_is_apdu_lost (
	pgm_rxw_t*	      const restrict window,
	struct pgm_sk_buff_t* const restrict skb
	)
{
	const pgm_rxw_state_t* state = (pgm_rxw_state_t*)&skb->cb;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != skb);

/* lost is lost */
	if (PGM_PKT_STATE_LOST_DATA == state->pkt_state)
		return TRUE;

/* by definition, a single-TPDU APDU is complete */
	if (!skb->pgm_opt_fragment)
		return FALSE;

	const uint32_t apdu_first_sqn = ntohl (skb->of_apdu_first_sqn);

/* by definition, first fragment indicates APDU is available */
	if (apdu_first_sqn == skb->sequence)
		return FALSE;

	const struct pgm_sk_buff_t* const first_skb = _pgm_rxw_peek (window, apdu_first_sqn);
/* first fragment out-of-bounds */
	if (NULL == first_skb)
		return TRUE;

	const pgm_rxw_state_t* first_state = (pgm_rxw_state_t*)&first_skb->cb;
	if (PGM_PKT_STATE_LOST_DATA == first_state->pkt_state)
		return TRUE;

	return FALSE;
}

/* return the first missing packet sequence in the specified transmission
 * group or NULL if not required.
 */

static inline
struct pgm_sk_buff_t*
_pgm_rxw_find_missing (
	pgm_rxw_t* const		window,
	const uint32_t			tg_sqn		/* tg_sqn | pkt_sqn */
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	pgm_assert (NULL != window);

	for (uint32_t i = tg_sqn, j = 0; j < window->tg_size; i++, j++)
	{
		skb = _pgm_rxw_peek (window, i);
		pgm_assert (NULL != skb);
		state = (pgm_rxw_state_t*)&skb->cb;
		switch (state->pkt_state) {
		case PGM_PKT_STATE_BACK_OFF:
		case PGM_PKT_STATE_WAIT_NCF:
		case PGM_PKT_STATE_WAIT_DATA:
		case PGM_PKT_STATE_LOST_DATA:
			return skb;

		case PGM_PKT_STATE_HAVE_DATA:
		case PGM_PKT_STATE_HAVE_PARITY:
			break;

		default: pgm_assert_not_reached(); break;
		}
	}

	return NULL;
}

/* returns TRUE if skb is a parity packet with packet length not
 * matching the transmission group length without the variable-packet-length
 * flag set.
 */

static inline
bool
_pgm_rxw_is_invalid_var_pktlen (
	pgm_rxw_t*		    const restrict window,
	const struct pgm_sk_buff_t* const restrict skb
	)
{
	const struct pgm_sk_buff_t* first_skb;

/* pre-conditions */
	pgm_assert (NULL != window);

	if (!window->is_fec_available)
		return FALSE;

	if (skb->pgm_header->pgm_options & PGM_OPT_VAR_PKTLEN)
		return FALSE;

	const uint32_t tg_sqn = _pgm_rxw_tg_sqn (window, skb->sequence);
	if (tg_sqn == skb->sequence)
		return FALSE;

	first_skb = _pgm_rxw_peek (window, tg_sqn);
	if (NULL == first_skb)
		return TRUE;	/* transmission group unrecoverable */

	if (first_skb->len == skb->len)
		return FALSE;

	return TRUE;
}

static inline
bool
_pgm_rxw_has_payload_op (
	const struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);
	pgm_assert (NULL != skb->pgm_header);

	return skb->pgm_opt_fragment || skb->pgm_header->pgm_options & PGM_OP_ENCODED;
}

/* returns TRUE is skb options are invalid when compared to the transmission group
 */

static inline
bool
_pgm_rxw_is_invalid_payload_op (
	pgm_rxw_t*		    const restrict window,
	const struct pgm_sk_buff_t* const restrict skb
	)
{
	const struct pgm_sk_buff_t* first_skb;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != skb);

	if (!window->is_fec_available)
		return FALSE;

	const uint32_t tg_sqn = _pgm_rxw_tg_sqn (window, skb->sequence);
	if (tg_sqn == skb->sequence)
		return FALSE;

	first_skb = _pgm_rxw_peek (window, tg_sqn);
	if (NULL == first_skb)
		return TRUE;	/* transmission group unrecoverable */

	if (_pgm_rxw_has_payload_op (first_skb) == _pgm_rxw_has_payload_op (skb))
		return FALSE;

	return TRUE;
}

/* insert skb into window range, discard if duplicate.  window will have placeholder,
 * parity, or data packet already matching sequence.
 *
 * returns:
 * PGM_RXW_INSERTED - packet filled a waiting placeholder, skb consumed.
 * PGM_RXW_DUPLICATE - re-transmission of previously seen packet.
 * PGM_RXW_MALFORMED - corrupted or invalid packet.
 * PGM_RXW_BOUNDS - packet out of window.
 */

static
int
_pgm_rxw_insert (
	pgm_rxw_t*	      const restrict window,
	struct pgm_sk_buff_t* const restrict new_skb
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != new_skb);
	pgm_assert (!_pgm_rxw_incoming_is_empty (window));

	if (PGM_UNLIKELY(_pgm_rxw_is_invalid_var_pktlen (window, new_skb) ||
	    _pgm_rxw_is_invalid_payload_op (window, new_skb)))
		return PGM_RXW_MALFORMED;

	if (new_skb->pgm_header->pgm_options & PGM_OPT_PARITY)
	{
		skb = _pgm_rxw_find_missing (window, new_skb->sequence);
		if (NULL == skb)
			return PGM_RXW_DUPLICATE;
		state = (pgm_rxw_state_t*)&skb->cb;
	}
	else
	{
		skb = _pgm_rxw_peek (window, new_skb->sequence);
		pgm_assert (NULL != skb);
		state = (pgm_rxw_state_t*)&skb->cb;

		if (state->pkt_state == PGM_PKT_STATE_HAVE_DATA)
			return PGM_RXW_DUPLICATE;
	}

/* APDU fragments are already declared lost */
	if (new_skb->pgm_opt_fragment &&
	    _pgm_rxw_is_apdu_lost (window, new_skb))
	{
		pgm_rxw_lost (window, skb->sequence);
		return PGM_RXW_BOUNDS;
	}

/* verify placeholder state */
	switch (state->pkt_state) {
	case PGM_PKT_STATE_BACK_OFF:
	case PGM_PKT_STATE_WAIT_NCF:
	case PGM_PKT_STATE_WAIT_DATA:
	case PGM_PKT_STATE_LOST_DATA:
		break;

	case PGM_PKT_STATE_HAVE_PARITY:
		_pgm_rxw_shuffle_parity (window, skb);
		break;

	default: pgm_assert_not_reached(); break;
	}

/* statistics */
	const uint32_t fill_time = (uint32_t)(new_skb->tstamp - skb->tstamp);
	PGM_HISTOGRAM_TIMES("Rx.RepairTime", fill_time);
	PGM_HISTOGRAM_COUNTS("Rx.NakTransmits", state->nak_transmit_count);
	PGM_HISTOGRAM_COUNTS("Rx.NcfRetries", state->ncf_retry_count);
	PGM_HISTOGRAM_COUNTS("Rx.DataRetries", state->data_retry_count);
	if (!window->max_fill_time) {
		window->max_fill_time = window->min_fill_time = fill_time;
	}
	else
	{
		if (fill_time > window->max_fill_time)
			window->max_fill_time = fill_time;
		else if (fill_time < window->min_fill_time)
			window->min_fill_time = fill_time;

		if (!window->max_nak_transmit_count) {
			window->max_nak_transmit_count = window->min_nak_transmit_count = state->nak_transmit_count;
		} else {
			if (state->nak_transmit_count > window->max_nak_transmit_count)
				window->max_nak_transmit_count = state->nak_transmit_count;
			else if (state->nak_transmit_count < window->min_nak_transmit_count)
				window->min_nak_transmit_count = state->nak_transmit_count;
		}
	}

/* add packet to bitmap */
	const uint_fast32_t pos = window->lead - new_skb->sequence;
	if (pos < 32) {
		window->bitmap |= 1 << pos;
	}

/* update the Exponential Moving Average (EMA) data loss with repair data.
 *     s_t = α × x_{t-1} + (1 - α) × s_{t-1}
 * x_{t-1} = 0
 *   ∴ s_t = (1 - α) × s_{t-1}
 */
	const uint_fast32_t s = pgm_fp16pow (pgm_fp16 (1) - window->ack_c_p, pos);
	if (s > window->data_loss)	window->data_loss = 0;
	else				window->data_loss -= s;

/* replace place holder skb with incoming skb */
	memcpy (new_skb->cb, skb->cb, sizeof(skb->cb));
	state = (void*)new_skb->cb;
	state->pkt_state = PGM_PKT_STATE_ERROR;
	_pgm_rxw_unlink (window, skb);
	pgm_free_skb (skb);
	const uint_fast32_t index_ = new_skb->sequence % pgm_rxw_max_length (window);
	window->pdata[index_] = new_skb;
	if (new_skb->pgm_header->pgm_options & PGM_OPT_PARITY)
		_pgm_rxw_state (window, new_skb, PGM_PKT_STATE_HAVE_PARITY);
	else
		_pgm_rxw_state (window, new_skb, PGM_PKT_STATE_HAVE_DATA);
	window->size += new_skb->len;

	return PGM_RXW_INSERTED;
}

/* shuffle parity packet at skb->sequence to any other needed spot.
 */

static inline
void
_pgm_rxw_shuffle_parity (
	pgm_rxw_t*	      const restrict window,
	struct pgm_sk_buff_t* const restrict skb
	)
{
	struct pgm_sk_buff_t* restrict missing;
	char cb[48];

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != skb);

	missing = _pgm_rxw_find_missing (window, skb->sequence);
	if (NULL == missing)
		return;

/* replace place holder skb with parity skb */
	_pgm_rxw_unlink (window, missing);
	memcpy (cb, skb->cb, sizeof(skb->cb));
	memcpy (skb->cb, missing->cb, sizeof(skb->cb));
	memcpy (missing->cb, cb, sizeof(skb->cb));
	const uint32_t parity_index = skb->sequence % pgm_rxw_max_length (window);
	window->pdata[parity_index] = skb;
	const uint32_t missing_index = missing->sequence % pgm_rxw_max_length (window);
	window->pdata[missing_index] = missing;
}

/* skb advances the window lead.
 *
 * returns:
 * PGM_RXW_APPENDED - packet advanced window lead, skb consumed.
 * PGM_RXW_MALFORMED - corrupted or invalid packet.
 * PGM_RXW_BOUNDS - packet out of window.
 */

static
int
_pgm_rxw_append (
	pgm_rxw_t*	      const restrict window,
	struct pgm_sk_buff_t* const restrict skb,
	const pgm_time_t		     now
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != skb);
	if (skb->pgm_header->pgm_options & PGM_OPT_PARITY) {
		pgm_assert (_pgm_rxw_tg_sqn (window, skb->sequence) == _pgm_rxw_tg_sqn (window, pgm_rxw_lead (window)));
	} else {
		pgm_assert (skb->sequence == pgm_rxw_next_lead (window));
	}

	if (PGM_UNLIKELY(_pgm_rxw_is_invalid_var_pktlen (window, skb) ||
	    _pgm_rxw_is_invalid_payload_op (window, skb)))
		return PGM_RXW_MALFORMED;

	if (pgm_rxw_is_full (window)) {
		if (_pgm_rxw_commit_is_empty (window)) {
			pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Receive window full on new data."));
			_pgm_rxw_remove_trail (window);
		} else {
			return PGM_RXW_BOUNDS;		/* constrained by commit window */
		}
	}

/* advance leading edge */
	window->lead++;

/* add packet to bitmap */
	window->bitmap = (window->bitmap << 1) | 1;

/* update the Exponential Moving Average (EMA) data loss with data:
 *     s_t = α × x_{t-1} + (1 - α) × s_{t-1}
 * x_{t-1} = 0
 *   ∴ s_t = (1 - α) × s_{t-1}
 */
	window->data_loss = pgm_fp16mul (window->data_loss, pgm_fp16 (1) - window->ack_c_p);

/* APDU fragments are already declared lost */
	if (PGM_UNLIKELY(skb->pgm_opt_fragment &&
	    _pgm_rxw_is_apdu_lost (window, skb)))
	{
		struct pgm_sk_buff_t* lost_skb	= pgm_alloc_skb (window->max_tpdu);
		lost_skb->tstamp		= now;
		lost_skb->sequence		= skb->sequence;

/* add lost-placeholder skb to window */
		const uint_fast32_t index_	= lost_skb->sequence % pgm_rxw_max_length (window);
		window->pdata[index_]		= lost_skb;

		_pgm_rxw_state (window, lost_skb, PGM_PKT_STATE_LOST_DATA);
		return PGM_RXW_BOUNDS;
	}

/* add skb to window */
	if (skb->pgm_header->pgm_options & PGM_OPT_PARITY)
	{
		const uint_fast32_t index_	= skb->sequence % pgm_rxw_max_length (window);
		window->pdata[index_]		= skb;
		_pgm_rxw_state (window, skb, PGM_PKT_STATE_HAVE_PARITY);
	}
	else
	{
		const uint_fast32_t index_	= skb->sequence % pgm_rxw_max_length (window);
		window->pdata[index_]		= skb;
		_pgm_rxw_state (window, skb, PGM_PKT_STATE_HAVE_DATA);
	}

/* statistics */
	window->size += skb->len;

	return PGM_RXW_APPENDED;
}

/* remove references to all commit packets not in the same transmission group
 * as the commit-lead
 */

PGM_GNUC_INTERNAL
void
pgm_rxw_remove_commit (
	pgm_rxw_t* const	window
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);

	const uint32_t tg_sqn_of_commit_lead = _pgm_rxw_tg_sqn (window, window->commit_lead);

	while (!_pgm_rxw_commit_is_empty (window) &&
	       tg_sqn_of_commit_lead != _pgm_rxw_tg_sqn (window, window->trail))
	{
		_pgm_rxw_remove_trail (window);
	}
}

/* flush packets but instead of calling on_data append the contiguous data packets
 * to the provided scatter/gather vector.
 *
 * when transmission groups are enabled, packets remain in the windows tagged committed
 * until the transmission group has been completely committed.  this allows the packet
 * data to be used in parity calculations to recover the missing packets.
 *
 * returns -1 on nothing read, returns length of bytes read, 0 is a valid read length.
 *
 * PGM skbuffs will have an increased reference count and must be unreferenced by the 
 * calling application.
 */

PGM_GNUC_INTERNAL
ssize_t
pgm_rxw_readv (
	pgm_rxw_t*    const restrict window,
	struct pgm_msgv_t** restrict pmsg,		/* message array, updated as messages appended */
	const unsigned		     pmsglen		/* number of items in pmsg */
	)
{
	const struct pgm_msgv_t* msg_end;
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;
	ssize_t bytes_read;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != pmsg);
	pgm_assert_cmpuint (pmsglen, >, 0);

	pgm_debug ("readv (window:%p pmsg:%p pmsglen:%u)",
		(void*)window, (void*)pmsg, pmsglen);

	msg_end = *pmsg + pmsglen - 1;

	if (_pgm_rxw_incoming_is_empty (window))
		return -1;

	skb = _pgm_rxw_peek (window, window->commit_lead);
	pgm_assert (NULL != skb);

	state = (pgm_rxw_state_t*)&skb->cb;
	switch (state->pkt_state) {
	case PGM_PKT_STATE_HAVE_DATA:
		bytes_read = _pgm_rxw_incoming_read (window, pmsg, (unsigned)(msg_end - *pmsg + 1));
		break;

	case PGM_PKT_STATE_LOST_DATA:
/* do not purge in situ sequence */
		if (_pgm_rxw_commit_is_empty (window)) {
			pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Removing lost trail from window"));
			_pgm_rxw_remove_trail (window);
		} else {
			pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Locking trail at commit window"));
		}
/* fall through */
	case PGM_PKT_STATE_BACK_OFF:
	case PGM_PKT_STATE_WAIT_NCF:
	case PGM_PKT_STATE_WAIT_DATA:
	case PGM_PKT_STATE_HAVE_PARITY:
		bytes_read = -1;
		break;

	case PGM_PKT_STATE_COMMIT_DATA:
	case PGM_PKT_STATE_ERROR:
	default:
		pgm_assert_not_reached();
		break;
	}

	return bytes_read;
}

/* remove lost sequences from the trailing edge of the window.  lost sequence
 * at lead of commit window invalidates all parity-data packets as any 
 * transmission group is now unrecoverable.
 *
 * returns number of sequences purged.
 */

static
unsigned
_pgm_rxw_remove_trail (
	pgm_rxw_t* const	window
	)
{
	struct pgm_sk_buff_t* skb;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (!pgm_rxw_is_empty (window));

	skb = _pgm_rxw_peek (window, window->trail);
	pgm_assert (NULL != skb);
	_pgm_rxw_unlink (window, skb);
	window->size -= skb->len;
/* remove reference to skb */
	if (PGM_UNLIKELY(pgm_mem_gc_friendly)) {
		const uint_fast32_t index_ = skb->sequence % pgm_rxw_max_length (window);
		window->pdata[index_] = NULL;
	}
	pgm_free_skb (skb);
	if (window->trail++ == window->commit_lead) {
/* data-loss */
		window->commit_lead++;
		window->cumulative_losses++;
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Data loss due to pulled trailing edge, fragment count %" PRIu32 "."),window->fragment_count);
		return 1;
	}
	return 0;
}

PGM_GNUC_INTERNAL
unsigned
pgm_rxw_remove_trail (
	pgm_rxw_t* const	window
	)
{
	pgm_debug ("remove_trail (window:%p)", (const void*)window);
	return _pgm_rxw_remove_trail (window);
}

/* read contiguous APDU-grouped sequences from the incoming window.
 *
 * side effects:
 *
 * 1) increments statics for window messages and bytes read.
 *
 * returns count of bytes read.
 */

static inline
ssize_t
_pgm_rxw_incoming_read (
	pgm_rxw_t*    const restrict window,
	struct pgm_msgv_t** restrict pmsg,		/* message array, updated as messages appended */
	unsigned		     pmsglen		/* number of items in pmsg */
	)
{
	const struct pgm_msgv_t* msg_end;
	struct pgm_sk_buff_t* skb;
	ssize_t bytes_read = 0;
	size_t  data_read  = 0;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != pmsg);
	pgm_assert_cmpuint (pmsglen, >, 0);
	pgm_assert (!_pgm_rxw_incoming_is_empty (window));

	pgm_debug ("_pgm_rxw_incoming_read (window:%p pmsg:%p pmsglen:%u)",
		 (void*)window, (void*)pmsg, pmsglen);

	msg_end = *pmsg + pmsglen - 1;
	do {
		skb = _pgm_rxw_peek (window, window->commit_lead);
		pgm_assert (NULL != skb);
		if (_pgm_rxw_is_apdu_complete (window,
					      skb->pgm_opt_fragment ? ntohl (skb->of_apdu_first_sqn) : skb->sequence))
		{
			bytes_read += _pgm_rxw_incoming_read_apdu (window, pmsg);
			data_read  ++;
		}
		else
		{
			break;
		}
	} while (*pmsg <= msg_end && !_pgm_rxw_incoming_is_empty (window));

	window->bytes_delivered += bytes_read;
	window->msgs_delivered  += data_read;
	return data_read > 0 ? bytes_read : -1;
}

/* returns TRUE if transmission group is lost.
 *
 * checking is lightly limited to bounds.
 */

static inline
bool
_pgm_rxw_is_tg_sqn_lost (
	pgm_rxw_t* const	window,
	const uint32_t		tg_sqn		/* transmission group sequence */
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert_cmpuint (_pgm_rxw_pkt_sqn (window, tg_sqn), ==, 0);

	if (pgm_rxw_is_empty (window))
		return TRUE;

	if (pgm_uint32_lt (tg_sqn, window->trail))
		return TRUE;

	return FALSE;
}

/* reconstruct missing sequences in a transmission group using embedded parity data.
 */

static
void
_pgm_rxw_reconstruct (
	pgm_rxw_t* const	window,
	const uint32_t		tg_sqn		/* transmission group sequence */
	)
{
	struct pgm_sk_buff_t	*skb;
	pgm_rxw_state_t		*state;
	struct pgm_sk_buff_t   **tg_skbs;
	pgm_gf8_t	       **tg_data, **tg_opts;
	uint8_t			*offsets;
	uint8_t			 rs_h = 0;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (1 == window->is_fec_available);
	pgm_assert_cmpuint (_pgm_rxw_pkt_sqn (window, tg_sqn), ==, 0);

/* use stack memory */
	tg_skbs = pgm_newa (struct pgm_sk_buff_t*, window->rs.n);
	tg_data = pgm_newa (pgm_gf8_t*, window->rs.n);
	tg_opts = pgm_newa (pgm_gf8_t*, window->rs.n);
	offsets = pgm_newa (uint8_t, window->rs.k);

	skb = _pgm_rxw_peek (window, tg_sqn);
	pgm_assert (NULL != skb);

	const bool is_var_pktlen = skb->pgm_header->pgm_options & PGM_OPT_VAR_PKTLEN;
	const bool is_op_encoded = skb->pgm_header->pgm_options & PGM_OPT_PRESENT;
	const uint16_t parity_length = ntohs (skb->pgm_header->pgm_tsdu_length);

	for (uint32_t i = tg_sqn, j = 0; i != (tg_sqn + window->rs.k); i++, j++)
	{
		skb = _pgm_rxw_peek (window, i);
		pgm_assert (NULL != skb);
		state = (pgm_rxw_state_t*)&skb->cb;
		switch (state->pkt_state) {
		case PGM_PKT_STATE_HAVE_DATA:
			tg_skbs[ j ] = skb;
			tg_data[ j ] = skb->data;
			tg_opts[ j ] = (pgm_gf8_t*)skb->pgm_opt_fragment;
			offsets[ j ] = j;
			break;

		case PGM_PKT_STATE_HAVE_PARITY:
			tg_skbs[ window->rs.k + rs_h ] = skb;
			tg_data[ window->rs.k + rs_h ] = skb->data;
			tg_opts[ window->rs.k + rs_h ] = (pgm_gf8_t*)skb->pgm_opt_fragment;
			offsets[ j ] = window->rs.k + rs_h;
			++rs_h;
/* fall through and alloc new skb for reconstructed data */
		case PGM_PKT_STATE_BACK_OFF:
		case PGM_PKT_STATE_WAIT_NCF:
		case PGM_PKT_STATE_WAIT_DATA:
		case PGM_PKT_STATE_LOST_DATA:
			skb = pgm_alloc_skb (window->max_tpdu);
			pgm_skb_reserve (skb, sizeof(struct pgm_header) + sizeof(struct pgm_data));
			skb->pgm_header = skb->head;
			skb->pgm_data = (void*)( skb->pgm_header + 1 );
			if (is_op_encoded) {
				const uint16_t opt_total_length = sizeof(struct pgm_opt_length) +
								 sizeof(struct pgm_opt_header) +
								 sizeof(struct pgm_opt_fragment);
				pgm_skb_reserve (skb, opt_total_length);
				skb->pgm_opt_fragment = (void*)( skb->pgm_data + 1 );
				pgm_skb_put (skb, parity_length);
				memset (skb->pgm_opt_fragment, 0, opt_total_length + parity_length);
			} else {
				pgm_skb_put (skb, parity_length);
				memset (skb->data, 0, parity_length);
			}
			tg_skbs[ j ] = skb;
			tg_data[ j ] = skb->data;
			tg_opts[ j ] = (void*)skb->pgm_opt_fragment;
			break;

		default: pgm_assert_not_reached(); break;
		}

		if (!skb->zero_padded) {
			memset (skb->tail, 0, parity_length - skb->len);
			skb->zero_padded = 1;
		}

	}

/* reconstruct payload */
	pgm_rs_decode_parity_appended (&window->rs,
				       tg_data,
				       offsets,
				       parity_length);

/* reconstruct opt_fragment option */
	if (is_op_encoded)
		pgm_rs_decode_parity_appended (&window->rs,
					       tg_opts,
					       offsets,
					       sizeof(struct pgm_opt_fragment));

/* swap parity skbs with reconstructed skbs */
	for (uint_fast8_t i = 0; i < window->rs.k; i++)
	{
		struct pgm_sk_buff_t* repair_skb;

		if (offsets[i] < window->rs.k)
			continue;

		repair_skb = tg_skbs[i];

		if (is_var_pktlen)
		{
			const uint16_t pktlen = *(uint16_t*)( (char*)repair_skb->tail - sizeof(uint16_t));
			if (pktlen > parity_length) {
				pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Invalid encoded variable packet length in reconstructed packet, dropping entire transmission group."));
				pgm_free_skb (repair_skb);
				for (uint_fast8_t j = i; j < window->rs.k; j++)
				{
					if (offsets[j] < window->rs.k)
						continue;
					pgm_rxw_lost (window, tg_skbs[offsets[j]]->sequence);
				}
				break;
			}
			const uint16_t padding = parity_length - pktlen;
			repair_skb->len -= padding;
			repair_skb->tail = (char*)repair_skb->tail - padding;
		}

#ifdef PGM_DISABLE_ASSERT
		_pgm_rxw_insert (window, repair_skb);
#else
		pgm_assert_cmpint (_pgm_rxw_insert (window, repair_skb), ==, PGM_RXW_INSERTED);
#endif
	}
}

/* check every TPDU in an APDU and verify that the data has arrived
 * and is available to commit to the application.
 *
 * if APDU sits in a transmission group that can be reconstructed use parity
 * data then the entire group will be decoded and any missing data packets
 * replaced by the recovery calculation.
 *
 * packets with single fragment fragment headers must be normalised as regular
 * packets before calling.
 *
 * APDUs exceeding PGM_MAX_FRAGMENTS or PGM_MAX_APDU length will be discarded.
 *
 * returns FALSE if APDU is incomplete or longer than max_len sequences.
 */

static
bool
_pgm_rxw_is_apdu_complete (
	pgm_rxw_t* const	window,
	const uint32_t		first_sequence
	)
{
	struct pgm_sk_buff_t	*skb;
	unsigned		 contiguous_tpdus = 0;
	size_t			 contiguous_size = 0;
	bool			 check_parity = FALSE;

/* pre-conditions */
	pgm_assert (NULL != window);

	pgm_debug ("_pgm_rxw_is_apdu_complete (window:%p first-sequence:%" PRIu32 ")",
		(const void*)window, first_sequence);

	skb = _pgm_rxw_peek (window, first_sequence);
	if (PGM_UNLIKELY(NULL == skb)) {
		return FALSE;
	}

	const size_t apdu_size = skb->pgm_opt_fragment ? ntohl (skb->of_apdu_len) : skb->len;
	const uint32_t  tg_sqn = _pgm_rxw_tg_sqn (window, first_sequence);

	pgm_assert_cmpuint (apdu_size, >=, skb->len);

/* protocol sanity check: maximum length */
	if (PGM_UNLIKELY(apdu_size > PGM_MAX_APDU)) {
		pgm_rxw_lost (window, first_sequence);
		return FALSE;
	}

	for (uint32_t sequence = first_sequence;
	     skb;
	     skb = _pgm_rxw_peek (window, ++sequence))
	{
		pgm_rxw_state_t* state = (pgm_rxw_state_t*)&skb->cb;

		if (!check_parity &&
		    PGM_PKT_STATE_HAVE_DATA != state->pkt_state)
		{
			if (window->is_fec_available &&
			    !_pgm_rxw_is_tg_sqn_lost (window, tg_sqn) )
			{
				check_parity = TRUE;
/* pre-seed committed sequence count */
				if (pgm_uint32_lte (tg_sqn, window->commit_lead))
					contiguous_tpdus += window->commit_lead - tg_sqn;
			}
			else
			{
				return FALSE;
			}
		}

		if (check_parity)
		{
			if (PGM_PKT_STATE_HAVE_DATA == state->pkt_state ||
			    PGM_PKT_STATE_HAVE_PARITY == state->pkt_state)
				++contiguous_tpdus;

/* have sufficient been received for reconstruction */
			if (contiguous_tpdus >= window->tg_size) {
				_pgm_rxw_reconstruct (window, tg_sqn);
				return _pgm_rxw_is_apdu_complete (window, first_sequence);
			}
		}
		else
		{
/* single packet APDU, already complete */
			if (PGM_PKT_STATE_HAVE_DATA == state->pkt_state &&
			    !skb->pgm_opt_fragment)
				return TRUE;

/* protocol sanity check: matching first sequence reference */
			if (PGM_UNLIKELY(ntohl (skb->of_apdu_first_sqn) != first_sequence)) {
				pgm_rxw_lost (window, first_sequence);
				return FALSE;
			}

/* protocol sanity check: matching apdu length */
			if (PGM_UNLIKELY(ntohl (skb->of_apdu_len) != apdu_size)) {
				pgm_rxw_lost (window, first_sequence);
				return FALSE;
			}

/* protocol sanity check: maximum number of fragments per apdu */
			if (PGM_UNLIKELY(++contiguous_tpdus > PGM_MAX_FRAGMENTS)) {
				pgm_rxw_lost (window, first_sequence);
				return FALSE;
			}

			contiguous_size += skb->len;
			if (apdu_size == contiguous_size)
				return TRUE;
			else if (PGM_UNLIKELY(apdu_size < contiguous_size)) {
				pgm_rxw_lost (window, first_sequence);
				return FALSE;
			}
		}
	}

/* pending */
	return FALSE;
}

/* read one APDU consisting of one or more TPDUs.  target array is guaranteed
 * to be big enough to store complete APDU.
 */

static inline
ssize_t
_pgm_rxw_incoming_read_apdu (
	pgm_rxw_t*    const restrict window,
	struct pgm_msgv_t** restrict pmsg		/* message array, updated as messages appended */
	)
{
	struct pgm_sk_buff_t *skb;
	size_t		      contiguous_len = 0;
	unsigned	      count = 0;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != pmsg);

	pgm_debug ("_pgm_rxw_incoming_read_apdu (window:%p pmsg:%p)",
		(const void*)window, (const void*)pmsg);

	skb = _pgm_rxw_peek (window, window->commit_lead);
	pgm_assert (NULL != skb);

	const size_t apdu_len = skb->pgm_opt_fragment ? ntohl (skb->of_apdu_len) : skb->len;
	pgm_assert_cmpuint (apdu_len, >=, skb->len);

	do {
		_pgm_rxw_state (window, skb, PGM_PKT_STATE_COMMIT_DATA);
		(*pmsg)->msgv_skb[ count++ ] = skb;
		contiguous_len += skb->len;
		window->commit_lead++;
		if (apdu_len == contiguous_len)
			break;
		skb = _pgm_rxw_peek (window, window->commit_lead);
	} while (apdu_len > contiguous_len);

	(*pmsg)->msgv_len = count;
	(*pmsg)++;

/* post-conditions */
	pgm_assert (!_pgm_rxw_commit_is_empty (window));

	return contiguous_len;
}

/* returns transmission group sequence (TG_SQN) from sequence (SQN).
 */

static inline
uint32_t
_pgm_rxw_tg_sqn (
	pgm_rxw_t* const	window,
	const uint32_t		sequence
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);

	const uint32_t tg_sqn_mask = 0xffffffff << window->tg_sqn_shift;
	return sequence & tg_sqn_mask;
}

/* returns packet number (PKT_SQN) from sequence (SQN).
 */

static inline
uint32_t
_pgm_rxw_pkt_sqn (
	pgm_rxw_t* const	window,
	const uint32_t		sequence
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);

	const uint32_t tg_sqn_mask = 0xffffffff << window->tg_sqn_shift;
	return sequence & ~tg_sqn_mask;
}

/* returns TRUE when the sequence is the first of a transmission group.
 */

static inline
bool
_pgm_rxw_is_first_of_tg_sqn (
	pgm_rxw_t* const	window,
	const uint32_t		sequence
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);

	return _pgm_rxw_pkt_sqn (window, sequence) == 0;
}

/* returns TRUE when the sequence is the last of a transmission group
 */

static inline
bool
_pgm_rxw_is_last_of_tg_sqn (
	pgm_rxw_t* const	window,
	const uint32_t		sequence
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);

	return _pgm_rxw_pkt_sqn (window, sequence) == window->tg_size - 1;
}

/* set PGM skbuff to new FSM state.
 */

static
void
_pgm_rxw_state (
	pgm_rxw_t*	      const restrict window,
	struct pgm_sk_buff_t* const restrict skb,
	const int			     new_pkt_state
	)
{
	pgm_rxw_state_t* state;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != skb);

	state = (pgm_rxw_state_t*)&skb->cb;

/* remove current state */
	if (PGM_PKT_STATE_ERROR != state->pkt_state)
		_pgm_rxw_unlink (window, skb);

	switch (new_pkt_state) {
	case PGM_PKT_STATE_BACK_OFF:
		pgm_queue_push_head_link (&window->nak_backoff_queue, (pgm_list_t*)skb);
		break;

	case PGM_PKT_STATE_WAIT_NCF:
		pgm_queue_push_head_link (&window->wait_ncf_queue, (pgm_list_t*)skb);
		break;

	case PGM_PKT_STATE_WAIT_DATA:
		pgm_queue_push_head_link (&window->wait_data_queue, (pgm_list_t*)skb);
		break;

	case PGM_PKT_STATE_HAVE_DATA:
		window->fragment_count++;
		pgm_assert_cmpuint (window->fragment_count, <=, pgm_rxw_length (window));
		break;

	case PGM_PKT_STATE_HAVE_PARITY:
		window->parity_count++;
		pgm_assert_cmpuint (window->parity_count, <=, pgm_rxw_length (window));
		break;

	case PGM_PKT_STATE_COMMIT_DATA:
		window->committed_count++;
		pgm_assert_cmpuint (window->committed_count, <=, pgm_rxw_length (window));
		break;

	case PGM_PKT_STATE_LOST_DATA:
		window->lost_count++;
		window->cumulative_losses++;
		window->has_event = 1;
		pgm_assert_cmpuint (window->lost_count, <=, pgm_rxw_length (window));
		break;

	case PGM_PKT_STATE_ERROR:
		break;

	default: pgm_assert_not_reached(); break;
	}

	state->pkt_state = new_pkt_state;
}

PGM_GNUC_INTERNAL
void
pgm_rxw_state (
	pgm_rxw_t*	      const restrict window,
	struct pgm_sk_buff_t* const restrict skb,
	const int			     new_pkt_state
	)
{
	pgm_debug ("state (window:%p skb:%p new_pkt_state:%s)",
		(const void*)window, (const void*)skb, pgm_pkt_state_string (new_pkt_state));
	_pgm_rxw_state (window, skb, new_pkt_state);
}

/* remove current state from sequence.
 */

static
void
_pgm_rxw_unlink (
	pgm_rxw_t*	      const restrict window,
	struct pgm_sk_buff_t* const restrict skb
	)
{
	pgm_rxw_state_t* state;
	pgm_queue_t* queue;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (NULL != skb);

	state = (pgm_rxw_state_t*)&skb->cb;

	switch (state->pkt_state) {
	case PGM_PKT_STATE_BACK_OFF:
		pgm_assert (!pgm_queue_is_empty (&window->nak_backoff_queue));
		queue = &window->nak_backoff_queue;
		goto unlink_queue;

	case PGM_PKT_STATE_WAIT_NCF:
		pgm_assert (!pgm_queue_is_empty (&window->wait_ncf_queue));
		queue = &window->wait_ncf_queue;
		goto unlink_queue;

	case PGM_PKT_STATE_WAIT_DATA:
		pgm_assert (!pgm_queue_is_empty (&window->wait_data_queue));
		queue = &window->wait_data_queue;
unlink_queue:
		pgm_queue_unlink (queue, (pgm_list_t*)skb);
		break;

	case PGM_PKT_STATE_HAVE_DATA:
		pgm_assert_cmpuint (window->fragment_count, >, 0);
		window->fragment_count--;
		break;

	case PGM_PKT_STATE_HAVE_PARITY:
		pgm_assert_cmpuint (window->parity_count, >, 0);
		window->parity_count--;
		break;

	case PGM_PKT_STATE_COMMIT_DATA:
		pgm_assert_cmpuint (window->committed_count, >, 0);
		window->committed_count--;
		break;

	case PGM_PKT_STATE_LOST_DATA:
		pgm_assert_cmpuint (window->lost_count, >, 0);
		window->lost_count--;
		break;

	case PGM_PKT_STATE_ERROR:
		break;

	default: pgm_assert_not_reached(); break;
	}

	state->pkt_state = PGM_PKT_STATE_ERROR;
	pgm_assert (((pgm_list_t*)skb)->next == NULL);
	pgm_assert (((pgm_list_t*)skb)->prev == NULL);
}

/* returns the pointer at the given index of the window.
 */

PGM_GNUC_INTERNAL
struct pgm_sk_buff_t*
pgm_rxw_peek (
	pgm_rxw_t* const	window,
	const uint32_t		sequence
	)
{
	pgm_debug ("peek (window:%p sequence:%" PRIu32 ")", (void*)window, sequence);
	return _pgm_rxw_peek (window, sequence);
}

/* mark an existing sequence lost due to failed recovery.
 */

PGM_GNUC_INTERNAL
void
pgm_rxw_lost (
	pgm_rxw_t* const	window,
	const uint32_t		sequence
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	pgm_assert (NULL != window);
	pgm_assert (!pgm_rxw_is_empty (window));

	pgm_debug ("lost (window:%p sequence:%" PRIu32 ")",
		 (const void*)window, sequence);

	skb = _pgm_rxw_peek (window, sequence);
	pgm_assert (NULL != skb);

	state = (pgm_rxw_state_t*)&skb->cb;

	if (PGM_UNLIKELY(!(state->pkt_state == PGM_PKT_STATE_BACK_OFF  ||
	                 state->pkt_state == PGM_PKT_STATE_WAIT_NCF  ||
	                 state->pkt_state == PGM_PKT_STATE_WAIT_DATA ||
			 state->pkt_state == PGM_PKT_STATE_HAVE_DATA ||	/* fragments */
			 state->pkt_state == PGM_PKT_STATE_HAVE_PARITY)))
	{
		pgm_fatal (_("Unexpected state %s(%u)"), pgm_pkt_state_string (state->pkt_state), state->pkt_state);
		pgm_assert_not_reached();
	}

	_pgm_rxw_state (window, skb, PGM_PKT_STATE_LOST_DATA);
}

/* received a uni/multicast ncf, search for a matching nak & tag or extend window if
 * beyond lead
 *
 * returns:
 * PGM_RXW_BOUNDS - sequence is outside of window, or window is undefined.
 * PGM_RXW_UPDATED - receiver state updated, waiting for data.
 * PGM_RXW_DUPLICATE - data already exists at sequence.
 * PGM_RXW_APPENDED - lead is extended with state set waiting for data.
 */

PGM_GNUC_INTERNAL
int
pgm_rxw_confirm (
	pgm_rxw_t* const	window,
	const uint32_t		sequence,
	const pgm_time_t	now,
	const pgm_time_t	nak_rdata_expiry,		/* pre-calculated expiry times */
	const pgm_time_t	nak_rb_expiry
	)
{
/* pre-conditions */
	pgm_assert (NULL != window);

	pgm_debug ("confirm (window:%p sequence:%" PRIu32 " nak_rdata_expiry:%" PGM_TIME_FORMAT " nak_rb_expiry:%" PGM_TIME_FORMAT ")",
		(void*)window, sequence, nak_rdata_expiry, nak_rb_expiry);

/* NCFs do not define the transmit window */
	if (PGM_UNLIKELY(!window->is_defined))
		return PGM_RXW_BOUNDS;

/* sequence already committed */
	if (pgm_uint32_lt (sequence, window->commit_lead)) {
		if (pgm_uint32_gte (sequence, window->trail))
			return PGM_RXW_DUPLICATE;
		else
			return PGM_RXW_BOUNDS;
	}

	if (pgm_uint32_lte (sequence, window->lead))
		return _pgm_rxw_recovery_update (window, sequence, nak_rdata_expiry);

	if (sequence == window->lead) 
		return _pgm_rxw_recovery_append (window, now, nak_rdata_expiry);
	else {
		_pgm_rxw_add_placeholder_range (window, sequence, now, nak_rb_expiry);
		return _pgm_rxw_recovery_append (window, now, nak_rdata_expiry);
	}
}

/* update an incoming sequence with state transition to WAIT-DATA.
 *
 * returns:
 * PGM_RXW_UPDATED - receiver state updated, waiting for data.
 * PGM_RXW_DUPLICATE - data already exists at sequence.
 */

static inline
int
_pgm_rxw_recovery_update (
	pgm_rxw_t* const	window,
	const uint32_t		sequence,
	const pgm_time_t	nak_rdata_expiry		/* pre-calculated expiry times */
	)
{
	pgm_rxw_state_t* state;
	struct pgm_sk_buff_t* skb;

/* pre-conditions */
	pgm_assert (NULL != window);

/* fetch skb from window and bump expiration times */
	skb = _pgm_rxw_peek (window, sequence);
	pgm_assert (NULL != skb);
	state = (pgm_rxw_state_t*)&skb->cb;
	switch (state->pkt_state) {
	case PGM_PKT_STATE_BACK_OFF:
	case PGM_PKT_STATE_WAIT_NCF:
		pgm_rxw_state (window, skb, PGM_PKT_STATE_WAIT_DATA);

/* fall through */
	case PGM_PKT_STATE_WAIT_DATA:
		state->timer_expiry = nak_rdata_expiry;
		return PGM_RXW_UPDATED;

	case PGM_PKT_STATE_HAVE_DATA:
	case PGM_PKT_STATE_HAVE_PARITY:
	case PGM_PKT_STATE_COMMIT_DATA:
	case PGM_PKT_STATE_LOST_DATA:
		break;

	default: pgm_assert_not_reached(); break;
	}

	return PGM_RXW_DUPLICATE;
}

/* append an skb to the incoming window with WAIT-DATA state.
 *
 * returns:
 * PGM_RXW_APPENDED - lead is extended with state set waiting for data.
 * PGM_RXW_BOUNDS   - constrained by commit window
 */

static inline
int
_pgm_rxw_recovery_append (
	pgm_rxw_t* const	window,
	const pgm_time_t	now,
	const pgm_time_t	nak_rdata_expiry		/* pre-calculated expiry times */
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	pgm_assert (NULL != window);

	if (pgm_rxw_is_full (window)) {
		if (_pgm_rxw_commit_is_empty (window)) {
			pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Receive window full on confirmed sequence."));
			_pgm_rxw_remove_trail (window);
		} else {
			return PGM_RXW_BOUNDS;		/* constrained by commit window */
		}
	}

/* advance leading edge */
	window->lead++;

/* add loss to bitmap */
	window->bitmap <<= 1;

/* update the Exponential Moving Average (EMA) data loss with loss:
 *     s_t = α × x_{t-1} + (1 - α) × s_{t-1}
 * x_{t-1} = 1
 *   ∴ s_t = α + (1 - α) × s_{t-1}
 */
	window->data_loss = window->ack_c_p + pgm_fp16mul (pgm_fp16 (1) - window->ack_c_p, window->data_loss);

	skb			= pgm_alloc_skb (window->max_tpdu);
	state			= (pgm_rxw_state_t*)&skb->cb;
	skb->tstamp		= now;
	skb->sequence		= window->lead;
	state->timer_expiry	= nak_rdata_expiry;

	const uint_fast32_t index_	= pgm_rxw_lead (window) % pgm_rxw_max_length (window);
	window->pdata[index_]		= skb;
	_pgm_rxw_state (window, skb, PGM_PKT_STATE_WAIT_DATA);

	return PGM_RXW_APPENDED;
}

/* dumps window state to stdout
 */

PGM_GNUC_INTERNAL
void
pgm_rxw_dump (
	const pgm_rxw_t* const	window
	)
{
	pgm_info ("window = {"
		"tsi = {gsi = {identifier = %i.%i.%i.%i.%i.%i}, sport = %" PRIu16 "}, "
		"nak_backoff_queue = {head = %p, tail = %p, length = %u}, "
		"wait_ncf_queue = {head = %p, tail = %p, length = %u}, "
		"wait_data_queue = {head = %p, tail = %p, length = %u}, "
		"lost_count = %" PRIu32 ", "
		"fragment_count = %" PRIu32 ", "
		"parity_count = %" PRIu32 ", "
		"committed_count = %" PRIu32 ", "
		"max_tpdu = %" PRIu16 ", "
		"tg_size = %" PRIu32 ", "
		"tg_sqn_shift = %u, "
		"lead = %" PRIu32 ", "
		"trail = %" PRIu32 ", "
		"rxw_trail = %" PRIu32 ", "
		"rxw_trail_init = %" PRIu32 ", "
		"commit_lead = %" PRIu32 ", "
		"is_constrained = %u, "
		"is_defined = %u, "
		"has_event = %u, "
		"is_fec_available = %u, "
		"min_fill_time = %" PRIu32 ", "
		"max_fill_time = %" PRIu32 ", "
		"min_nak_transmit_count = %" PRIu32 ", "
		"max_nak_transmit_count = %" PRIu32 ", "
		"cumulative_losses = %" PRIu32 ", "
		"bytes_delivered = %" PRIu32 ", "
		"msgs_delivered = %" PRIu32 ", "
		"size = %" PRIzu ", "
		"alloc = %" PRIu32 ", "
		"pdata = []"
		"}",
		window->tsi->gsi.identifier[0], 
			window->tsi->gsi.identifier[1],
			window->tsi->gsi.identifier[2],
			window->tsi->gsi.identifier[3],
			window->tsi->gsi.identifier[4],
			window->tsi->gsi.identifier[5],
			ntohs (window->tsi->sport),
		(void*)window->nak_backoff_queue.head,
			(void*)window->nak_backoff_queue.tail,
			window->nak_backoff_queue.length,
		(void*)window->wait_ncf_queue.head,
			(void*)window->wait_ncf_queue.tail,
			window->wait_ncf_queue.length,
		(void*)window->wait_data_queue.head,
			(void*)window->wait_data_queue.tail,
			window->wait_data_queue.length,
		window->lost_count,
		window->fragment_count,
		window->parity_count,
		window->committed_count,
		window->max_tpdu,
		window->tg_size,
		window->tg_sqn_shift,
		window->lead,
		window->trail,
		window->rxw_trail,
		window->rxw_trail_init,
		window->commit_lead,
		window->is_constrained,
		window->is_defined,
		window->has_event,
		window->is_fec_available,
		window->min_fill_time,
		window->max_fill_time,
		window->min_nak_transmit_count,
		window->max_nak_transmit_count,
		window->cumulative_losses,
		window->bytes_delivered,
		window->msgs_delivered,
		window->size,
		window->alloc
	);
}

/* state string helper
 */

PGM_GNUC_INTERNAL
const char*
pgm_pkt_state_string (
	const int		pkt_state
	)
{
	const char* c;

	switch (pkt_state) {
	case PGM_PKT_STATE_BACK_OFF:	c = "PGM_PKT_STATE_BACK_OFF"; break;
	case PGM_PKT_STATE_WAIT_NCF:	c = "PGM_PKT_STATE_WAIT_NCF"; break;
	case PGM_PKT_STATE_WAIT_DATA:	c = "PGM_PKT_STATE_WAIT_DATA"; break;
	case PGM_PKT_STATE_HAVE_DATA:	c = "PGM_PKT_STATE_HAVE_DATA"; break;
	case PGM_PKT_STATE_HAVE_PARITY:	c = "PGM_PKT_STATE_HAVE_PARITY"; break;
	case PGM_PKT_STATE_COMMIT_DATA: c = "PGM_PKT_STATE_COMMIT_DATA"; break;
	case PGM_PKT_STATE_LOST_DATA:	c = "PGM_PKT_STATE_LOST_DATA"; break;
	case PGM_PKT_STATE_ERROR:	c = "PGM_PKT_STATE_ERROR"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

PGM_GNUC_INTERNAL
const char*
pgm_rxw_returns_string (
	const int		rxw_returns
	)
{
	const char* c;

	switch (rxw_returns) {
	case PGM_RXW_OK:			c = "PGM_RXW_OK"; break;
	case PGM_RXW_INSERTED:			c = "PGM_RXW_INSERTED"; break;
	case PGM_RXW_APPENDED:			c = "PGM_RXW_APPENDED"; break;
	case PGM_RXW_UPDATED:			c = "PGM_RXW_UPDATED"; break;
	case PGM_RXW_MISSING:			c = "PGM_RXW_MISSING"; break;
	case PGM_RXW_DUPLICATE:			c = "PGM_RXW_DUPLICATE"; break;
	case PGM_RXW_MALFORMED:			c = "PGM_RXW_MALFORMED"; break;
	case PGM_RXW_BOUNDS:			c = "PGM_RXW_BOUNDS"; break;
	case PGM_RXW_SLOW_CONSUMER:		c = "PGM_RXW_SLOW_CONSUMER"; break;
	case PGM_RXW_UNKNOWN:			c = "PGM_RXW_UNKNOWN"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

/* eof */
