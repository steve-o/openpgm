/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * A basic receive window: pointer array implementation.
 *
 * Copyright (c) 2006-2009 Miru Limited.
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

#ifdef G_OS_UNIX
#	include <sys/uio.h>
#endif

#include <pgm/skbuff.h>
#include <pgm/rxwi.h>
#include <pgm/sn.h>
#include <pgm/time.h>
#include <pgm/tsi.h>
#include <pgm/math.h>
#include <pgm/reed_solomon.h>
#include <pgm/histogram.h>


//#define RXW_DEBUG

#ifndef RXW_DEBUG
#	define G_DISABLE_ASSERT
#	ifdef g_assert_cmpuint
#		undef g_assert_cmpuint
#	endif
#	define g_assert_cmpuint(n1, cmp, n2)	do { (void) 0; } while (0)
#endif

#ifndef RXW_DEBUG
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
_pgm_tsi_is_null (
	pgm_tsi_t* const	tsi
	)
{
	pgm_tsi_t nulltsi;

/* pre-conditions */
	g_assert (tsi);

	memset (&nulltsi, 0, sizeof(nulltsi));
	return 0 == memcmp (&nulltsi, tsi, sizeof(nulltsi));
}

static void _pgm_rxw_define (pgm_rxw_t* const, const guint32);
static void _pgm_rxw_update_trail (pgm_rxw_t* const, const guint32);
static inline guint32 _pgm_rxw_update_lead (pgm_rxw_t* const, const guint32, const pgm_time_t, const pgm_time_t);
static inline guint32 _pgm_rxw_tg_sqn (pgm_rxw_t* const, const guint32);
static inline guint32 _pgm_rxw_pkt_sqn (pgm_rxw_t* const, const guint32);
static inline gboolean _pgm_rxw_is_first_of_tg_sqn (pgm_rxw_t* const, const guint32);
static inline gboolean _pgm_rxw_is_last_of_tg_sqn (pgm_rxw_t* const, const guint32);
static inline void _pgm_rxw_remove_tg_sqn (pgm_rxw_t* const, const guint32);
static int _pgm_rxw_insert (pgm_rxw_t* const, struct pgm_sk_buff_t* const);
static int _pgm_rxw_append (pgm_rxw_t* const, struct pgm_sk_buff_t* const, const pgm_time_t);
static int _pgm_rxw_add_placeholder_range (pgm_rxw_t* const, const guint32, const pgm_time_t, const pgm_time_t);
static inline void _pgm_rxw_unlink (pgm_rxw_t* const, struct pgm_sk_buff_t*);
static guint _pgm_rxw_remove_trail (pgm_rxw_t* const);
static inline void _pgm_rxw_lost (pgm_rxw_t* const, const guint32);
static void _pgm_rxw_state (pgm_rxw_t*, struct pgm_sk_buff_t*, pgm_pkt_state_e);
static inline void _pgm_rxw_shuffle_parity (pgm_rxw_t* const, struct pgm_sk_buff_t* const);
static inline gssize _pgm_rxw_incoming_read (pgm_rxw_t* const, pgm_msgv_t**, guint);
static inline gboolean _pgm_rxw_is_apdu_complete (pgm_rxw_t* const, const guint32);
static inline gssize _pgm_rxw_incoming_read_apdu (pgm_rxw_t* const, pgm_msgv_t**);
static inline int _pgm_rxw_recovery_update (pgm_rxw_t* const, const guint32, const pgm_time_t);
static inline int _pgm_rxw_recovery_append (pgm_rxw_t* const, const pgm_time_t, const pgm_time_t);


/* returns the pointer at the given index of the window.
 */

static
struct pgm_sk_buff_t*
_pgm_rxw_peek (
	const pgm_rxw_t* const	window,
	const guint32		sequence
	)
{
/* pre-conditions */
	g_assert (window);

	if (pgm_rxw_is_empty (window))
		return NULL;

	if (pgm_uint32_gte (sequence, window->trail) && pgm_uint32_lte (sequence, window->lead))
	{
		const guint32 index_ = sequence % pgm_rxw_max_length (window);
		struct pgm_sk_buff_t* skb = window->pdata[index_];
/* availability only guaranteed inside commit window */
		if (pgm_uint32_lt (sequence, window->commit_lead)) {
			g_assert (skb);
			g_assert (pgm_skb_is_valid (skb));
			g_assert (!_pgm_tsi_is_null (&skb->tsi));
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
guint32
_pgm_rxw_commit_length (
	const pgm_rxw_t* const	window
	)
{
	g_assert (window);
	return window->commit_lead - window->trail;
}

static inline
gboolean
_pgm_rxw_commit_is_empty (
	const pgm_rxw_t* const	window
	)
{
	g_assert (window);
	return _pgm_rxw_commit_length (window) == 0;
}

static inline
guint32
_pgm_rxw_incoming_length (
	const pgm_rxw_t* const	window
	)
{
	g_assert (window);
	return ( 1 + window->lead ) - window->commit_lead;
}

static inline
gboolean
_pgm_rxw_incoming_is_empty (
	const pgm_rxw_t* const	window
	)
{
	g_assert (window);
	return _pgm_rxw_incoming_length (window) == 0;
}

/* constructor for receive window.  zero-length windows are not permitted.
 *
 * returns pointer to window.
 */

pgm_rxw_t*
pgm_rxw_create (
	const pgm_tsi_t*	tsi,
	const guint16		tpdu_size,
	const guint32		sqns,		/* transmit window size in sequence numbers */
	const guint		secs,		/* size in seconds */
	const guint		max_rte		/* max bandwidth */
	)
{
	pgm_rxw_t* window;

/* pre-conditions */
	g_assert (tsi);
	g_assert_cmpuint (tpdu_size, >, 0);
	if (sqns) {
		g_assert_cmpuint (sqns, >, 0);
		g_assert_cmpuint (sqns & PGM_UINT32_SIGN_BIT, ==, 0);
		g_assert_cmpuint (secs, ==, 0);
		g_assert_cmpuint (max_rte, ==, 0);
	} else {
		g_assert_cmpuint (secs, >, 0);
		g_assert_cmpuint (max_rte, >, 0);
	}

	g_trace ("create (tsi:%s max-tpdu:%" G_GUINT16_FORMAT " sqns:%" G_GUINT32_FORMAT  " secs %u max-rte %u).\n",
		pgm_tsi_print (tsi), tpdu_size, sqns, secs, max_rte);

/* calculate receive window parameters */
	guint32 alloc_sqns;

	if (sqns)
	{
		alloc_sqns = sqns;
	}
	else if (secs && max_rte)
	{
		alloc_sqns = (secs * max_rte) / tpdu_size;
	}
	else
	{
		g_assert_not_reached();
	}

	window = g_slice_alloc0 (sizeof(pgm_rxw_t) + ( alloc_sqns * sizeof(struct pgm_sk_buff_t*) ));

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

/* pointer array */
	window->alloc = alloc_sqns;

/* post-conditions */
	g_assert_cmpuint (pgm_rxw_max_length (window), ==, alloc_sqns);
	g_assert_cmpuint (pgm_rxw_length (window), ==, 0);
	g_assert_cmpuint (pgm_rxw_size (window), ==, 0);
	g_assert (pgm_rxw_is_empty (window));
	g_assert (!pgm_rxw_is_full (window));

	return window;
}

/* destructor for receive window.  must not be called more than once for same window.
 */

void
pgm_rxw_destroy (
	pgm_rxw_t* const	window
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert_cmpuint (window->alloc, >, 0);

	g_trace ("destroy (window:%p)", (gpointer)window);

/* contents of window */
	while (!pgm_rxw_is_empty (window)) {
		_pgm_rxw_remove_trail (window);
	}

/* window must now be empty */
	g_assert_cmpuint (pgm_rxw_length (window), ==, 0);
	g_assert_cmpuint (pgm_rxw_size (window), ==, 0);
	g_assert (pgm_rxw_is_empty (window));
	g_assert (!pgm_rxw_is_full (window));

/* window */
	g_slice_free1 (sizeof(pgm_rxw_t) + ( window->alloc * sizeof(struct pgm_sk_buff_t*) ), window);
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

int
pgm_rxw_add (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb,
	const pgm_time_t		now,
	const pgm_time_t		nak_rb_expiry	/* calculated expiry time for this skb */
	)
{
	pgm_rxw_state_t* const state = (pgm_rxw_state_t*)&skb->cb;
	int status;

/* pre-conditions */
	g_assert (window);
	g_assert (skb);
	g_assert_cmpuint (nak_rb_expiry, >, 0);
	g_assert_cmpuint (pgm_rxw_max_length (window), >, 0);
	g_assert (pgm_skb_is_valid (skb));
	g_assert (((const GList*)skb)->next == NULL);
	g_assert (((const GList*)skb)->prev == NULL);
	g_assert (!_pgm_tsi_is_null (&skb->tsi));
	g_assert (sizeof(struct pgm_header) + sizeof(struct pgm_data) <= (guint)((guint8*)skb->data - (guint8*)skb->head));
	g_assert (skb->len == (guint8*)skb->tail - (guint8*)skb->data);

	g_trace ("add (window:%p skb:%p, nak_rb_expiry:%" PGM_TIME_FORMAT ")",
		(gpointer)window, (gpointer)skb, nak_rb_expiry);

	skb->sequence = g_ntohl (skb->pgm_data->data_sqn);

/* protocol sanity check: tsdu size */
	if (skb->len != g_ntohs (skb->pgm_header->pgm_tsdu_length))
		return PGM_RXW_MALFORMED;

/* protocol sanity check: valid trail pointer wrt. sequence */
	if (skb->sequence - g_ntohl (skb->pgm_data->data_trail) >= ((UINT32_MAX/2)-1))
		return PGM_RXW_MALFORMED;

/* verify fragment header for original data, parity packets include a
 * parity fragment header
 */
	if (!(skb->pgm_header->pgm_options & PGM_OPT_PARITY) &&
	    skb->pgm_opt_fragment)
	{
/* protocol sanity check: single fragment APDU */
		if (g_ntohl (skb->of_apdu_len) == skb->len)
			skb->pgm_opt_fragment = NULL;

/* protocol sanity check: minimum APDU length */
		if (g_ntohl (skb->of_apdu_len) < skb->len)
			return PGM_RXW_MALFORMED;

/* protocol sanity check: sequential ordering */
		if (pgm_uint32_gt (g_ntohl (skb->of_apdu_first_sqn), skb->sequence))
			return PGM_RXW_MALFORMED;

/* protocol sanity check: maximum APDU length */
		if (g_ntohl (skb->of_apdu_len) > PGM_MAX_APDU)
			return PGM_RXW_MALFORMED;
	}

/* first packet of a session defines the window */
	if (!window->is_defined)
		_pgm_rxw_define (window, skb->sequence - 1);	/* previous_lead needed for append to occur */
	else
		_pgm_rxw_update_trail (window, g_ntohl (skb->pgm_data->data_trail));

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

		g_assert (first_state);
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
	const guint32		lead
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert (pgm_rxw_is_empty (window));
	g_assert (_pgm_rxw_commit_is_empty (window));
	g_assert (_pgm_rxw_incoming_is_empty (window));
	g_assert (!window->is_defined);

	window->lead = lead;
	window->commit_lead = window->rxw_trail = window->rxw_trail_init = window->trail = window->lead + 1;
	window->is_constrained = window->is_defined = TRUE;

/* post-conditions */
	g_assert (pgm_rxw_is_empty (window));
	g_assert (_pgm_rxw_commit_is_empty (window));
	g_assert (_pgm_rxw_incoming_is_empty (window));
	g_assert (window->is_defined);
	g_assert (window->is_constrained);
}

/* update window with latest transmitted parameters.
 *
 * returns count of placeholders added into window, used to start sending naks.
 */

guint
pgm_rxw_update (
	pgm_rxw_t* const	window,
	const guint32		txw_lead,
	const guint32		txw_trail,
	const pgm_time_t	now,
	const pgm_time_t	nak_rb_expiry		/* packet expiration time */
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert_cmpuint (nak_rb_expiry, >, 0);

	g_trace ("pgm_rxw_update (window:%p txw-lead:%" G_GUINT32_FORMAT " txw-trail:%" G_GUINT32_FORMAT " nak-rb-expiry:%" PGM_TIME_FORMAT ")",
		(gpointer)window, txw_lead, txw_trail, nak_rb_expiry);

	if (!window->is_defined) {
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
	const guint32		txw_trail
	)
{
/* pre-conditions */
	g_assert (window);

/* advertised trail is less than the current value */
	if (pgm_uint32_lte (txw_trail, window->rxw_trail))
		return;

/* protocol sanity check: advertised trail jumps too far ahead */
	if (txw_trail - window->rxw_trail > ((UINT32_MAX/2)-1))
		return;

/* retransmissions requests are constrained on startup until the advertised trail advances
 * beyond the first data sequence number.
 */
	if (window->is_constrained)
	{
		if (pgm_uint32_gt (txw_trail, window->rxw_trail_init))
			window->is_constrained = FALSE;
		else
			return;
	}

	window->rxw_trail = txw_trail;

/* new value doesn't affect window */
	if (pgm_uint32_lte (window->rxw_trail, window->trail))
		return;

/* jump remaining sequence numbers if window is empty */
	if (pgm_rxw_is_empty (window))
	{
		const guint32 distance = (gint32)(window->rxw_trail) - (gint32)(window->trail);
		window->commit_lead = window->trail += distance;
		window->lead += distance;
		window->cumulative_losses += distance;
		g_assert (pgm_rxw_is_empty (window));
		g_assert (_pgm_rxw_commit_is_empty (window));
		g_assert (_pgm_rxw_incoming_is_empty (window));
		return;
	}

/* remove all buffers between commit lead and advertised rxw_trail */
	for (guint32 sequence = window->commit_lead;
	     pgm_uint32_gt (window->rxw_trail, sequence) && pgm_uint32_gte (window->lead, sequence);
	     sequence++)
	{
		struct pgm_sk_buff_t* skb;
		pgm_rxw_state_t* state;

		skb = _pgm_rxw_peek (window, sequence);
		g_assert (skb);
		state = (pgm_rxw_state_t*)&skb->cb;

		switch (state->state) {
		case PGM_PKT_HAVE_DATA_STATE:
		case PGM_PKT_HAVE_PARITY_STATE:
		case PGM_PKT_LOST_DATA_STATE:
			break;

		case PGM_PKT_ERROR_STATE:
			g_assert_not_reached();

		default:
			_pgm_rxw_lost (window, sequence);
			break;
		}
	}

/* post-conditions: only after flush */
//	g_assert (!pgm_rxw_is_full (window));
}

/* update FEC parameters
 */

void
pgm_rxw_update_fec (
	pgm_rxw_t* const	window,
	const guint		rs_k
	)
{
/* pre-conditions */
	g_assert (window);

	g_trace ("pgm_rxw_update_fec (window:%p rs(k):%u)",
		(gpointer)window, rs_k);

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

/* pre-conditions */
	g_assert (window);
	g_assert (!pgm_rxw_is_full (window));

/* advance lead */
	window->lead++;

	skb			= pgm_alloc_skb (window->max_tpdu);
	pgm_rxw_state_t* state	= (pgm_rxw_state_t*)&skb->cb;
	skb->tstamp		= now;
	skb->sequence		= window->lead;
	state->nak_rb_expiry	= nak_rb_expiry;

	if (!_pgm_rxw_is_first_of_tg_sqn (window, skb->sequence))
	{
		struct pgm_sk_buff_t* first_skb = _pgm_rxw_peek (window, _pgm_rxw_tg_sqn (window, skb->sequence));
		if (first_skb) {
			pgm_rxw_state_t* first_state = (pgm_rxw_state_t*)&first_skb->cb;
			first_state->is_contiguous = 0;
		}
	}

/* add skb to window */
	const guint32 index_	= skb->sequence % pgm_rxw_max_length (window);
	window->pdata[index_]	= skb;

	pgm_rxw_state (window, skb, PGM_PKT_BACK_OFF_STATE);

/* post-conditions */
	g_assert_cmpuint (pgm_rxw_length (window), >, 0);
	g_assert_cmpuint (pgm_rxw_length (window), <=, pgm_rxw_max_length (window));
	g_assert_cmpuint (_pgm_rxw_incoming_length (window), >, 0);
}

/* add a range of placeholders to the window.
 */

static
int
_pgm_rxw_add_placeholder_range (
	pgm_rxw_t* const	window,
	const guint32		sequence,
	const pgm_time_t	now,
	const pgm_time_t	nak_rb_expiry
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert (pgm_uint32_gt (sequence, pgm_rxw_lead (window)));

/* check bounds of commit window */
	const guint32 new_commit_sqns = ( 1 + sequence ) - window->trail;
        if ( !_pgm_rxw_commit_is_empty (window) &&
	     (new_commit_sqns >= pgm_rxw_max_length (window)) )
        {
		_pgm_rxw_update_lead (window, sequence, now, nak_rb_expiry);
		return PGM_RXW_BOUNDS;		/* effectively a slow consumer */
        }

	if (pgm_rxw_is_full (window))
		_pgm_rxw_remove_trail (window);

/* if packet is non-contiguous to current leading edge add place holders
 * TODO: can be rather inefficient on packet loss looping through dropped sequence numbers
 */
	while (pgm_rxw_next_lead (window) != sequence)
	{
		_pgm_rxw_add_placeholder (window, now, nak_rb_expiry);
		if (pgm_rxw_is_full (window))
			_pgm_rxw_remove_trail (window);
	}

/* post-conditions */
	g_assert (!pgm_rxw_is_full (window));

	return PGM_RXW_APPENDED;
}

/* update leading edge of receive window.
 *
 * returns number of place holders added.
 */

static
guint
_pgm_rxw_update_lead (
	pgm_rxw_t* const	window,
	const guint32		txw_lead,
	const pgm_time_t	now,
	const pgm_time_t	nak_rb_expiry
	)
{
/* pre-conditions */
	g_assert (window);

/* advertised lead is less than the current value */
	if (pgm_uint32_lte (txw_lead, window->lead))
		return 0;

	guint32 lead;

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

	guint lost = 0;

	while (window->lead != lead)
	{
/* slow consumer or fast producer */
		if (pgm_rxw_is_full (window))
		{
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
gboolean
_pgm_rxw_is_apdu_lost (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb
	)
{
	const pgm_rxw_state_t* state = (pgm_rxw_state_t*)&skb->cb;

/* pre-conditions */
	g_assert (window);
	g_assert (skb);

/* lost is lost */
	if (PGM_PKT_LOST_DATA_STATE == state->state)
		return TRUE;

/* by definition, a single-TPDU APDU is complete */
	if (!skb->pgm_opt_fragment)
		return FALSE;

	const guint32 apdu_first_sqn = g_ntohl (skb->of_apdu_first_sqn);

/* by definition, first fragment indicates APDU is available */
	if (apdu_first_sqn == skb->sequence)
		return FALSE;

	const struct pgm_sk_buff_t* const first_skb = _pgm_rxw_peek (window, apdu_first_sqn);
/* first fragment out-of-bounds */
	if (NULL == first_skb)
		return TRUE;

	const pgm_rxw_state_t* first_state = (pgm_rxw_state_t*)&first_skb->cb;
	if (PGM_PKT_LOST_DATA_STATE == first_state->state)
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
	const guint32			tg_sqn		/* tg_sqn | pkt_sqn */
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	g_assert (window);

	for (guint32 i = tg_sqn, j = 0; j < window->tg_size; i++, j++)
	{
		skb = _pgm_rxw_peek (window, i);
		g_assert (skb);
		state = (pgm_rxw_state_t*)&skb->cb;
		switch (state->state) {
		case PGM_PKT_BACK_OFF_STATE:
		case PGM_PKT_WAIT_NCF_STATE:
		case PGM_PKT_WAIT_DATA_STATE:
		case PGM_PKT_LOST_DATA_STATE:
			return skb;

		case PGM_PKT_HAVE_DATA_STATE:
		case PGM_PKT_HAVE_PARITY_STATE:
			break;

		default: g_assert_not_reached(); break;
		}
	}

	return NULL;
}

/* returns TRUE if skb is a parity packet with packet length not
 * matching the transmission group length without the variable-packet-length
 * flag set.
 */

static inline
gboolean
_pgm_rxw_is_invalid_var_pktlen (
	pgm_rxw_t* const			window,
	const struct pgm_sk_buff_t* const	skb
	)
{
	const struct pgm_sk_buff_t* first_skb;

/* pre-conditions */
	g_assert (window);

	if (!window->is_fec_available)
		return FALSE;

	if (skb->pgm_header->pgm_options & PGM_OPT_VAR_PKTLEN)
		return FALSE;

	const guint32 tg_sqn = _pgm_rxw_tg_sqn (window, skb->sequence);
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
gboolean
_pgm_rxw_has_payload_op (
	const struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (skb);
	g_assert (skb->pgm_header);

	return skb->pgm_opt_fragment || skb->pgm_header->pgm_options & PGM_OP_ENCODED;
}

/* returns TRUE is skb options are invalid when compared to the transmission group
 */

static inline
gboolean
_pgm_rxw_is_invalid_payload_op (
	pgm_rxw_t* const			window,
	const struct pgm_sk_buff_t* const	skb
	)
{
	const struct pgm_sk_buff_t* first_skb;

/* pre-conditions */
	g_assert (window);
	g_assert (skb);

	if (!window->is_fec_available)
		return FALSE;

	const guint32 tg_sqn = _pgm_rxw_tg_sqn (window, skb->sequence);
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
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	new_skb
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	g_assert (window);
	g_assert (new_skb);
	g_assert (!_pgm_rxw_incoming_is_empty (window));

	if (_pgm_rxw_is_invalid_var_pktlen (window, new_skb) ||
	    _pgm_rxw_is_invalid_payload_op (window, new_skb))
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
		g_assert (skb);
		state = (pgm_rxw_state_t*)&skb->cb;

		if (state->state == PGM_PKT_HAVE_DATA_STATE)
			return PGM_RXW_DUPLICATE;
	}

/* APDU fragments are already declared lost */
	if (new_skb->pgm_opt_fragment &&
	    _pgm_rxw_is_apdu_lost (window, new_skb))
	{
		_pgm_rxw_lost (window, skb->sequence);
		return PGM_RXW_BOUNDS;
	}

/* verify placeholder state */
	switch (state->state) {
	case PGM_PKT_BACK_OFF_STATE:
	case PGM_PKT_WAIT_NCF_STATE:
	case PGM_PKT_WAIT_DATA_STATE:
	case PGM_PKT_LOST_DATA_STATE:
		break;

	case PGM_PKT_HAVE_PARITY_STATE:
		_pgm_rxw_shuffle_parity (window, skb);
		break;

	default: g_assert_not_reached(); break;
	}

/* statistics */
	const pgm_time_t fill_time = skb->tstamp - new_skb->tstamp;
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

/* replace place holder skb with incoming skb */
	memcpy (new_skb->cb, skb->cb, sizeof(skb->cb));
	((pgm_rxw_state_t*)new_skb->cb)->state = PGM_PKT_ERROR_STATE;
	_pgm_rxw_unlink (window, skb);
	pgm_free_skb (skb);
	const guint32 index_ = new_skb->sequence % pgm_rxw_max_length (window);
	window->pdata[index_] = new_skb;
	if (new_skb->pgm_header->pgm_options & PGM_OPT_PARITY)
		_pgm_rxw_state (window, new_skb, PGM_PKT_HAVE_PARITY_STATE);
	else
		_pgm_rxw_state (window, new_skb, PGM_PKT_HAVE_DATA_STATE);
	window->size += new_skb->len;

	return PGM_RXW_INSERTED;
}

/* shuffle parity packet at skb->sequence to any other needed spot.
 */

static inline
void
_pgm_rxw_shuffle_parity (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb
	)
{
	guint32 index_;

/* pre-conditions */
	g_assert (window);
	g_assert (skb);

	struct pgm_sk_buff_t* missing = _pgm_rxw_find_missing (window, skb->sequence);
	if (NULL == missing)
		return;

/* replace place holder skb with parity skb */
	char cb[48];
	_pgm_rxw_unlink (window, missing);
	memcpy (cb, skb->cb, sizeof(skb->cb));
	memcpy (skb->cb, missing->cb, sizeof(skb->cb));
	memcpy (missing->cb, cb, sizeof(skb->cb));
	index_ = skb->sequence % pgm_rxw_max_length (window);
	window->pdata[index_] = skb;
	index_ = missing->sequence % pgm_rxw_max_length (window);
	window->pdata[index_] = missing;
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
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb,
	const pgm_time_t		now
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert (skb);
	if (skb->pgm_header->pgm_options & PGM_OPT_PARITY) {
		g_assert (_pgm_rxw_tg_sqn (window, skb->sequence) == _pgm_rxw_tg_sqn (window, pgm_rxw_lead (window)));
	} else {
		g_assert (skb->sequence == pgm_rxw_next_lead (window));
	}

	if (_pgm_rxw_is_invalid_var_pktlen (window, skb) ||
	    _pgm_rxw_is_invalid_payload_op (window, skb))
		return PGM_RXW_MALFORMED;

	if (pgm_rxw_is_full (window))
		_pgm_rxw_remove_trail (window);

/* advance leading edge */
	window->lead++;

/* APDU fragments are already declared lost */
	if (skb->pgm_opt_fragment &&
	    _pgm_rxw_is_apdu_lost (window, skb))
	{
		struct pgm_sk_buff_t* lost_skb	= pgm_alloc_skb (window->max_tpdu);
		lost_skb->tstamp		= now;
		lost_skb->sequence		= skb->sequence;

/* add lost-placeholder skb to window */
		const guint32 index_	= lost_skb->sequence % pgm_rxw_max_length (window);
		window->pdata[index_]	= lost_skb;

		_pgm_rxw_state (window, lost_skb, PGM_PKT_LOST_DATA_STATE);
		return PGM_RXW_BOUNDS;
	}

/* add skb to window */
	if (skb->pgm_header->pgm_options & PGM_OPT_PARITY)
	{
		const guint32 index_	= skb->sequence % pgm_rxw_max_length (window);
		window->pdata[index_]	= skb;
		_pgm_rxw_state (window, skb, PGM_PKT_HAVE_PARITY_STATE);
	}
	else
	{
		const guint32 index_	= skb->sequence % pgm_rxw_max_length (window);
		window->pdata[index_]	= skb;
		_pgm_rxw_state (window, skb, PGM_PKT_HAVE_DATA_STATE);
	}

/* statistics */
	window->size += skb->len;

	return PGM_RXW_APPENDED;
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

gssize
pgm_rxw_readv (
	pgm_rxw_t* const	window,
	pgm_msgv_t**		pmsg,		/* message array, updated as messages appended */
	const guint		pmsglen		/* number of items in pmsg */
	)
{
	const pgm_msgv_t* msg_end;
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;
	gssize bytes_read;

/* pre-conditions */
	g_assert (window);
	g_assert (pmsg);
	g_assert_cmpuint (pmsglen, >, 0);

	g_trace ("readv (window:%p pmsg:%p pmsglen:%u)",
		(gpointer)window, (gpointer)pmsg, pmsglen);

	msg_end = *pmsg + pmsglen;

	if (_pgm_rxw_incoming_is_empty (window))
		return -1;

	skb = _pgm_rxw_peek (window, window->commit_lead);
	g_assert (skb);

	state = (pgm_rxw_state_t*)&skb->cb;
	switch (state->state) {
	case PGM_PKT_HAVE_DATA_STATE:
		bytes_read = _pgm_rxw_incoming_read (window, pmsg, msg_end - *pmsg);
		break;

	case PGM_PKT_LOST_DATA_STATE:
		window->lost_count += _pgm_rxw_remove_trail (window);
/* fall through */
	case PGM_PKT_BACK_OFF_STATE:
	case PGM_PKT_WAIT_NCF_STATE:
	case PGM_PKT_WAIT_DATA_STATE:
	case PGM_PKT_HAVE_PARITY_STATE:
		bytes_read = -1;
		break;

	case PGM_PKT_COMMIT_DATA_STATE:
	case PGM_PKT_ERROR_STATE:
	default:
		g_assert_not_reached();
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
guint
_pgm_rxw_remove_trail (
	pgm_rxw_t* const	window
	)
{
	struct pgm_sk_buff_t* skb;

/* pre-conditions */
	g_assert (window);
	g_assert (!pgm_rxw_is_empty (window));

	skb = _pgm_rxw_peek (window, window->trail);
	g_assert (skb);
	_pgm_rxw_unlink (window, skb);
	window->size -= skb->len;
	pgm_free_skb (skb);
	if (window->trail++ == window->commit_lead) {
/* data-loss */
		window->commit_lead++;
		window->cumulative_losses++;
		return 1;
	}
	return 0;
}

guint
pgm_rxw_remove_trail (
	pgm_rxw_t* const	window
	)
{
	g_trace ("remove_trail (window:%p)", (gpointer)window);
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
gssize
_pgm_rxw_incoming_read (
	pgm_rxw_t* const	window,
	pgm_msgv_t**		pmsg,		/* message array, updated as messages appended */
	guint			pmsglen		/* number of items in pmsg */
	)
{
	const pgm_msgv_t* msg_end;
	struct pgm_sk_buff_t* skb;

/* pre-conditions */
	g_assert (window);
	g_assert (pmsg);
	g_assert_cmpuint (pmsglen, >, 0);
	g_assert (!_pgm_rxw_incoming_is_empty (window));

	g_trace ("_pgm_rxw_incoming_read (window:%p pmsg:%p pmsglen:%u)",
		 (gpointer)window, (gpointer)pmsg, pmsglen);

	msg_end = *pmsg + pmsglen;
	gssize bytes_read = 0;
	guint data_read = 0;

	do {
		skb = _pgm_rxw_peek (window, window->commit_lead);
		g_assert (skb);
		if (_pgm_rxw_is_apdu_complete (window,
					      skb->pgm_opt_fragment ?
							g_ntohl (skb->of_apdu_first_sqn) :
							skb->sequence))
		{
			bytes_read += _pgm_rxw_incoming_read_apdu (window, pmsg);
			data_read  ++;
		}
		else break;
	} while (!_pgm_rxw_incoming_is_empty (window));

	return data_read > 0 ? bytes_read : -1;
}

/* returns TRUE if transmission group is lost.
 *
 * checking is lightly limited to bounds.
 */

static inline
gboolean
_pgm_rxw_is_tg_sqn_lost (
	pgm_rxw_t* const	window,
	const guint32		tg_sqn		/* transmission group sequence */
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert_cmpuint (_pgm_rxw_pkt_sqn (window, tg_sqn), ==, 0);

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
	const guint32		tg_sqn		/* transmission group sequence */
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	g_assert (NULL != window);
	g_assert (1 == window->is_fec_available);
	g_assert_cmpuint (_pgm_rxw_pkt_sqn (window, tg_sqn), ==, 0);

	skb = _pgm_rxw_peek (window, tg_sqn);
	g_assert (skb);

	const gboolean is_var_pktlen = skb->pgm_header->pgm_options & PGM_OPT_VAR_PKTLEN;
	const gboolean is_op_encoded = skb->pgm_header->pgm_options & PGM_OPT_PRESENT;
	const gsize parity_length = g_ntohs (skb->pgm_header->pgm_tsdu_length);
	struct pgm_sk_buff_t* tg_skbs[ window->rs.n ];
	guint8* tg_data[ window->rs.n ];
	guint8* tg_opts[ window->rs.n ];
	guint32 offsets[ window->rs.k ];
	guint rs_h = 0;

	for (guint32 i = tg_sqn, j = 0; i != (tg_sqn + window->rs.k); i++, j++)
	{
		skb = _pgm_rxw_peek (window, i);
		g_assert (skb);
		state = (pgm_rxw_state_t*)&skb->cb;
		switch (state->state) {
		case PGM_PKT_HAVE_DATA_STATE:
			tg_skbs[ j ] = skb;
			tg_data[ j ] = skb->data;
			tg_opts[ j ] = (gpointer)skb->pgm_opt_fragment;
			offsets[ j ] = j;
			break;

		case PGM_PKT_HAVE_PARITY_STATE:
			tg_skbs[ window->rs.k + rs_h ] = skb;
			tg_data[ window->rs.k + rs_h ] = skb->data;
			tg_opts[ window->rs.k + rs_h ] = (gpointer)skb->pgm_opt_fragment;
			offsets[ j ] = window->rs.k + rs_h;
			++rs_h;
/* fall through and alloc new skb for reconstructed data */
		case PGM_PKT_BACK_OFF_STATE:
		case PGM_PKT_WAIT_NCF_STATE:
		case PGM_PKT_WAIT_DATA_STATE:
		case PGM_PKT_LOST_DATA_STATE:
			skb = pgm_alloc_skb (window->max_tpdu);
			pgm_skb_reserve (skb, sizeof(struct pgm_header) + sizeof(struct pgm_data));
			skb->pgm_header = skb->head;
			skb->pgm_data = (gpointer)( skb->pgm_header + 1 );
			if (is_op_encoded) {
				const guint16 opt_total_length = sizeof(struct pgm_opt_length) +
								 sizeof(struct pgm_opt_header) +
								 sizeof(struct pgm_opt_fragment);
				pgm_skb_reserve (skb, opt_total_length);
				skb->pgm_opt_fragment = (gpointer)( skb->pgm_data + 1 );
				pgm_skb_put (skb, parity_length);
				memset (skb->pgm_opt_fragment, 0, opt_total_length + parity_length);
			} else {
				pgm_skb_put (skb, parity_length);
				memset (skb->data, 0, parity_length);
			}
			tg_skbs[ j ] = skb;
			tg_data[ j ] = skb->data;
			tg_opts[ j ] = (gpointer)skb->pgm_opt_fragment;
			break;

		default: g_assert_not_reached(); break;
		}

		if (!skb->zero_padded) {
			memset (skb->tail, 0, parity_length - skb->len);
			skb->zero_padded = 1;
		}

	}

/* reconstruct payload */
	pgm_rs_decode_parity_appended (&window->rs,
				       (void**)(void*)tg_data,
				       offsets,
				       parity_length);

/* reconstruct opt_fragment option */
	if (is_op_encoded)
		pgm_rs_decode_parity_appended (&window->rs,
					       (void**)(void*)tg_opts,
					       offsets,
					       sizeof(struct pgm_opt_fragment));

/* swap parity skbs with reconstructed skbs */
	for (guint32 i = 0; i < window->rs.k; i++)
	{
		if (offsets[i] < window->rs.k)
			continue;

		struct pgm_sk_buff_t* repair_skb = tg_skbs[i];

		if (is_var_pktlen)
		{
			const guint16 pktlen = *(guint16*)( (guint8*)repair_skb->tail - sizeof(guint16));
			if (pktlen > parity_length) {
				g_warning ("Invalid encoded variable packet length in reconstructed packet, dropping entire transmission group.");
				pgm_free_skb (repair_skb);
				for (guint32 j = i; j < window->rs.k; j++)
				{
					if (offsets[j] < window->rs.k)
						continue;
					pgm_rxw_lost (window, tg_skbs[offsets[j]]->sequence);
				}
				break;
			}
			const guint padding = parity_length - pktlen;
			repair_skb->len -= padding;
			repair_skb->tail = (guint8*)repair_skb->tail - padding;
		}

#ifdef G_DISABLE_ASSERT
		_pgm_rxw_insert (window, repair_skb);
#else
		g_assert_cmpint (_pgm_rxw_insert (window, repair_skb), ==, PGM_RXW_INSERTED);
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

static inline
gboolean
_pgm_rxw_is_apdu_complete (
	pgm_rxw_t* const	window,
	const guint32		first_sequence
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	g_assert (window);

	g_trace ("_pgm_rxw_is_apdu_complete (window:%p first-sequence:%" G_GUINT32_FORMAT ")",
		(gpointer)window, first_sequence);

	skb = _pgm_rxw_peek (window, first_sequence);
	g_assert (skb);

	const gsize apdu_size = skb->pgm_opt_fragment ? g_ntohl (skb->of_apdu_len) : skb->len;
	const guint32 tg_sqn = _pgm_rxw_tg_sqn (window, first_sequence);
	guint32 sequence = first_sequence;
	guint contiguous_tpdus = 0;
	gsize contiguous_size = 0;
	gboolean check_parity = FALSE;

	g_assert_cmpuint (apdu_size, >=, skb->len);

/* protocol sanity check: maximum length */
	if (apdu_size > PGM_MAX_APDU) {
		_pgm_rxw_lost (window, first_sequence);
		return FALSE;
	}

	do {
		state = (pgm_rxw_state_t*)&skb->cb;

		if (!check_parity &&
		    PGM_PKT_HAVE_DATA_STATE != state->state)
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
				return FALSE;
		}

		if (check_parity)
		{
			if (PGM_PKT_HAVE_DATA_STATE == state->state ||
			    PGM_PKT_HAVE_PARITY_STATE == state->state)
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
			if (PGM_PKT_HAVE_DATA_STATE == state->state &&
			    !skb->pgm_opt_fragment)
				return TRUE;

/* protocol sanity check: matching first sequence reference */
			if (g_ntohl (skb->of_apdu_first_sqn) != first_sequence) {
				_pgm_rxw_lost (window, first_sequence);
				return FALSE;
			}

/* protocol sanity check: matching apdu length */
			if (g_ntohl (skb->of_apdu_len) != apdu_size) {
				_pgm_rxw_lost (window, first_sequence);
				return FALSE;
			}

/* protocol sanity check: maximum number of fragments per apdu */
			if (++contiguous_tpdus > PGM_MAX_FRAGMENTS) {
				_pgm_rxw_lost (window, first_sequence);
				return FALSE;
			}

			contiguous_size += skb->len;
			if (apdu_size == contiguous_size)
				return TRUE;
			if (apdu_size < contiguous_size) {
				_pgm_rxw_lost (window, first_sequence);
				return FALSE;
			}
		}

		skb = _pgm_rxw_peek (window, ++sequence);
	} while (skb);

/* pending */
	return FALSE;
}

/* read one APDU consisting of one or more TPDUs.  target array is guaranteed
 * to be big enough to store complete APDU.
 */

static inline
gssize
_pgm_rxw_incoming_read_apdu (
	pgm_rxw_t* const	window,
	pgm_msgv_t**		pmsg		/* message array, updated as messages appended */
	)
{
	struct pgm_sk_buff_t *skb;

/* pre-conditions */
	g_assert (window);
	g_assert (pmsg);

	g_trace ("_pgm_rxw_incoming_read_apdu (window:%p pmsg:%p)",
		(gpointer)window, (gpointer)pmsg);

	skb = _pgm_rxw_peek (window, window->commit_lead);
	gsize contiguous_len = 0;
	const gsize apdu_len = skb->pgm_opt_fragment ? g_ntohl (skb->of_apdu_len) : skb->len;
	guint i = 0;
	g_assert_cmpuint (apdu_len, >=, skb->len);
	(*pmsg)->msgv_len = 0;
	do {
		_pgm_rxw_state (window, skb, PGM_PKT_COMMIT_DATA_STATE);
		(*pmsg)->msgv_skb[i++] = skb;
		(*pmsg)->msgv_len++;
		contiguous_len += skb->len;
		if (_pgm_rxw_is_last_of_tg_sqn (window, window->commit_lead))
			_pgm_rxw_remove_tg_sqn (window, _pgm_rxw_tg_sqn (window, window->commit_lead));
		window->commit_lead++;
		if (apdu_len == contiguous_len)
			break;
		skb = _pgm_rxw_peek (window, window->commit_lead);
	} while (apdu_len > contiguous_len);

	(*pmsg)++;

/* post-conditions */
	g_assert (!_pgm_rxw_commit_is_empty (window));

	return contiguous_len;
}

/* returns transmission group sequence (TG_SQN) from sequence (SQN).
 */

static inline
guint32
_pgm_rxw_tg_sqn (
	pgm_rxw_t* const	window,
	const guint32		sequence
	)
{
/* pre-conditions */
	g_assert (window);

	const guint32 tg_sqn_mask = 0xffffffff << window->tg_sqn_shift;
	return sequence & tg_sqn_mask;
}

/* returns packet number (PKT_SQN) from sequence (SQN).
 */

static inline
guint32
_pgm_rxw_pkt_sqn (
	pgm_rxw_t* const	window,
	const guint32		sequence
	)
{
/* pre-conditions */
	g_assert (window);

	const guint32 tg_sqn_mask = 0xffffffff << window->tg_sqn_shift;
	return sequence & ~tg_sqn_mask;
}

/* returns TRUE when the sequence is the first of a transmission group.
 */

static inline
gboolean
_pgm_rxw_is_first_of_tg_sqn (
	pgm_rxw_t* const	window,
	const guint32		sequence
	)
{
/* pre-conditions */
	g_assert (window);

	return _pgm_rxw_pkt_sqn (window, sequence) == 0;
}

/* returns TRUE when the sequence is the last of a transmission group
 */

static inline
gboolean
_pgm_rxw_is_last_of_tg_sqn (
	pgm_rxw_t* const	window,
	const guint32		sequence
	)
{
/* pre-conditions */
	g_assert (window);

	return _pgm_rxw_pkt_sqn (window, sequence) == window->tg_size - 1;
}

/* remove matching transmission group at trail of window
 */

static inline
void
_pgm_rxw_remove_tg_sqn (
	pgm_rxw_t* const	window,
	const guint32		tg_sqn		/* transmission group sequence */
	)
{
/* pre-conditions */
	g_assert (window);
	g_assert_cmpuint (_pgm_rxw_pkt_sqn (window, tg_sqn), ==, 0);

	while (!_pgm_rxw_commit_is_empty (window) &&
		_pgm_rxw_tg_sqn (window, window->trail) == tg_sqn)
	{
		_pgm_rxw_remove_trail (window);
	}
}

/* set PGM skbuff to new FSM state.
 */

static
void
_pgm_rxw_state (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb,
	const pgm_pkt_state_e		new_state
	)
{
	pgm_rxw_state_t* state = (pgm_rxw_state_t*)&skb->cb;

/* pre-conditions */
	g_assert (NULL != window);
	g_assert (NULL != skb);

/* remove current state */
	if (PGM_PKT_ERROR_STATE != state->state)
		_pgm_rxw_unlink (window, skb);

	switch (new_state) {
	case PGM_PKT_BACK_OFF_STATE:
		g_queue_push_head_link (&window->backoff_queue, (GList*)skb);
		break;

	case PGM_PKT_WAIT_NCF_STATE:
		g_queue_push_head_link (&window->wait_ncf_queue, (GList*)skb);
		break;

	case PGM_PKT_WAIT_DATA_STATE:
		g_queue_push_head_link (&window->wait_data_queue, (GList*)skb);
		break;

	case PGM_PKT_HAVE_DATA_STATE:
		window->fragment_count++;
		g_assert_cmpuint (window->fragment_count, <=, pgm_rxw_length (window));
		break;

	case PGM_PKT_HAVE_PARITY_STATE:
		window->parity_count++;
		g_assert_cmpuint (window->parity_count, <=, pgm_rxw_length (window));
		break;

	case PGM_PKT_COMMIT_DATA_STATE:
		window->committed_count++;
		g_assert_cmpuint (window->committed_count, <=, pgm_rxw_length (window));
		break;

	case PGM_PKT_LOST_DATA_STATE:
		window->lost_count++;
		window->cumulative_losses++;
		window->has_event = 1;
		g_assert_cmpuint (window->lost_count, <=, pgm_rxw_length (window));
		break;

	case PGM_PKT_ERROR_STATE:
		break;

	default: g_assert_not_reached(); break;
	}

	state->state = new_state;
}

void
pgm_rxw_state (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb,
	const pgm_pkt_state_e		new_state
	)
{
	g_trace ("state (window:%p skb:%p new_state:%s)",
		(gpointer)window, (gpointer)skb, pgm_pkt_state_string (new_state));
	_pgm_rxw_state (window, skb, new_state);
}

/* remove current state from sequence.
 */

static inline
void
_pgm_rxw_unlink (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb
	)
{
	GQueue* queue;

/* pre-conditions */
	g_assert (NULL != window);
	g_assert (NULL != skb);

	pgm_rxw_state_t* state = (pgm_rxw_state_t*)&skb->cb;

	switch (state->state) {
	case PGM_PKT_BACK_OFF_STATE:
		g_assert (!g_queue_is_empty (&window->backoff_queue));
		queue = &window->backoff_queue;
		goto unlink_queue;

	case PGM_PKT_WAIT_NCF_STATE:
		g_assert (!g_queue_is_empty (&window->wait_ncf_queue));
		queue = &window->wait_ncf_queue;
		goto unlink_queue;

	case PGM_PKT_WAIT_DATA_STATE:
		g_assert (!g_queue_is_empty (&window->wait_data_queue));
		queue = &window->wait_data_queue;
unlink_queue:
		g_queue_unlink (queue, (GList*)skb);
		break;

	case PGM_PKT_HAVE_DATA_STATE:
		g_assert_cmpuint (window->fragment_count, >, 0);
		window->fragment_count--;
		break;

	case PGM_PKT_HAVE_PARITY_STATE:
		g_assert_cmpuint (window->parity_count, >, 0);
		window->parity_count--;
		break;

	case PGM_PKT_COMMIT_DATA_STATE:
		g_assert_cmpuint (window->committed_count, >, 0);
		window->committed_count--;
		break;

	case PGM_PKT_LOST_DATA_STATE:
		g_assert_cmpuint (window->lost_count, >, 0);
		window->lost_count--;
		break;

	case PGM_PKT_ERROR_STATE:
		break;

	default: g_assert_not_reached(); break;
	}

	state->state = PGM_PKT_ERROR_STATE;
	g_assert (((GList*)skb)->next == NULL);
	g_assert (((GList*)skb)->prev == NULL);
}

/* returns the pointer at the given index of the window.
 */

struct pgm_sk_buff_t*
pgm_rxw_peek (
	pgm_rxw_t* const	window,
	const guint32		sequence
	)
{
	g_trace ("peek (window:%p sequence:%" G_GUINT32_FORMAT ")", (gpointer)window, sequence);
	return _pgm_rxw_peek (window, sequence);
}

/* mark an existing sequence lost due to failed recovery.
 */

static inline
void
_pgm_rxw_lost (
	pgm_rxw_t* const	window,
	const guint32		sequence
	)
{
	struct pgm_sk_buff_t* skb;
	pgm_rxw_state_t* state;

/* pre-conditions */
	g_assert (window);
	g_assert (!pgm_rxw_is_empty (window));

	skb = _pgm_rxw_peek (window, sequence);
	g_assert (skb);

	state = (pgm_rxw_state_t*)&skb->cb;

	g_assert( state->state == PGM_PKT_BACK_OFF_STATE ||
		  state->state == PGM_PKT_WAIT_NCF_STATE ||
		  state->state == PGM_PKT_WAIT_DATA_STATE  );

	_pgm_rxw_state (window, skb, PGM_PKT_LOST_DATA_STATE);
}

void
pgm_rxw_lost (
	pgm_rxw_t* const	window,
	const guint32		sequence
	)
{
	g_trace ("lost (window:%p sequence:%" G_GUINT32_FORMAT ")",
		 (gpointer)window, sequence);
	_pgm_rxw_lost (window, sequence);
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

int
pgm_rxw_confirm (
	pgm_rxw_t* const	window,
	const guint32		sequence,
	const pgm_time_t	now,
	const pgm_time_t	nak_rdata_expiry,		/* pre-calculated expiry times */
	const pgm_time_t	nak_rb_expiry
	)
{
/* pre-conditions */
	g_assert (window);

	g_trace ("confirm (window:%p sequence:%" G_GUINT32_FORMAT " nak_rdata_expiry:%" PGM_TIME_FORMAT " nak_rb_expiry:%" PGM_TIME_FORMAT ")",
		(gpointer)window, sequence, nak_rdata_expiry, nak_rb_expiry);

/* NCFs do not define the transmit window */
	if (!window->is_defined)
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
	const guint32		sequence,
	const pgm_time_t	nak_rdata_expiry		/* pre-calculated expiry times */
	)
{
/* pre-conditions */
	g_assert (window);

/* fetch skb from window and bump expiration times */
	struct pgm_sk_buff_t* skb = _pgm_rxw_peek (window, sequence);
	g_assert (skb);
	pgm_rxw_state_t* state = (pgm_rxw_state_t*)&skb->cb;
	switch (state->state) {
	case PGM_PKT_BACK_OFF_STATE:
	case PGM_PKT_WAIT_NCF_STATE:
		pgm_rxw_state (window, skb, PGM_PKT_WAIT_DATA_STATE);

/* fall through */
	case PGM_PKT_WAIT_DATA_STATE:
		state->nak_rdata_expiry = nak_rdata_expiry;
		return PGM_RXW_UPDATED;

	case PGM_PKT_HAVE_DATA_STATE:
	case PGM_PKT_HAVE_PARITY_STATE:
	case PGM_PKT_COMMIT_DATA_STATE:
	case PGM_PKT_LOST_DATA_STATE:
		break;

	default: g_assert_not_reached(); break;
	}

	return PGM_RXW_DUPLICATE;
}

/* append an skb to the incoming window with WAIT-DATA state.
 *
 * returns:
 * PGM_RXW_APPENDED - lead is extended with state set waiting for data.
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

/* pre-conditions */
	g_assert (window);

	if (pgm_rxw_is_full (window))
		_pgm_rxw_remove_trail (window);

/* advance leading edge */
	window->lead++;

	skb			= pgm_alloc_skb (window->max_tpdu);
	pgm_rxw_state_t* state	= (pgm_rxw_state_t*)&skb->cb;
	skb->tstamp		= now;
	skb->sequence		= window->lead;
	state->nak_rdata_expiry	= nak_rdata_expiry;

	const guint32 index_	= pgm_rxw_lead (window) % pgm_rxw_max_length (window);
	window->pdata[index_]	= skb;
	_pgm_rxw_state (window, skb, PGM_PKT_WAIT_DATA_STATE);

	return PGM_RXW_APPENDED;
}

/* dumps window state to stdout
 */

void
pgm_rxw_dump (
	const pgm_rxw_t* const	window
	)
{
	g_message ("window = {"
		"tsi = {gsi = {identifier = %i.%i.%i.%i.%i.%i}, sport = %" G_GUINT16_FORMAT "}, "
		"backoff_queue = {head = %p, tail = %p, length = %u}, "
		"wait_ncf_queue = {head = %p, tail = %p, length = %u}, "
		"wait_data_queue = {head = %p, tail = %p, length = %u}, "
		"lost_count = %" G_GUINT32_FORMAT ", "
		"fragment_count = %" G_GUINT32_FORMAT ", "
		"parity_count = %" G_GUINT32_FORMAT ", "
		"committed_count = %" G_GUINT32_FORMAT ", "
		"max_tpdu = %" G_GUINT16_FORMAT ", "
		"tg_size = %" G_GUINT32_FORMAT ", "
		"tg_sqn_shift = %u, "
		"lead = %" G_GUINT32_FORMAT ", "
		"trail = %" G_GUINT32_FORMAT ", "
		"rxw_trail = %" G_GUINT32_FORMAT ", "
		"rxw_trail_init = %" G_GUINT32_FORMAT ", "
		"commit_lead = %" G_GUINT32_FORMAT ", "
		"is_constrained = %u, "
		"is_defined = %u, "
		"has_event = %u, "
		"is_fec_available = %u, "
		"min_fill_time = %" G_GUINT32_FORMAT ", "
		"max_fill_time = %" G_GUINT32_FORMAT ", "
		"min_nak_transmit_count = %" G_GUINT32_FORMAT ", "
		"max_nak_transmit_count = %" G_GUINT32_FORMAT ", "
		"cumulative_losses = %" G_GUINT32_FORMAT ", "
		"bytes_delivered = %" G_GUINT32_FORMAT ", "
		"msgs_delivered = %" G_GUINT32_FORMAT ", "
		"size = %" G_GSIZE_FORMAT ", "
		"alloc = %" G_GUINT32_FORMAT ", "
		"pdata = []"
		"}",
		window->tsi->gsi.identifier[0], 
			window->tsi->gsi.identifier[1],
			window->tsi->gsi.identifier[2],
			window->tsi->gsi.identifier[3],
			window->tsi->gsi.identifier[4],
			window->tsi->gsi.identifier[5],
			g_ntohs (window->tsi->sport),
		(gpointer)window->backoff_queue.head,
			(gpointer)window->backoff_queue.tail,
			window->backoff_queue.length,
		(gpointer)window->wait_ncf_queue.head,
			(gpointer)window->wait_ncf_queue.tail,
			window->wait_ncf_queue.length,
		(gpointer)window->wait_data_queue.head,
			(gpointer)window->wait_data_queue.tail,
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

const char*
pgm_pkt_state_string (
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
