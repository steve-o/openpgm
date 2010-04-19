/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM receiver transport.
 *
 * Copyright (c) 2006-2010 Miru Limited.
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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <errno.h>
#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <pgm/framework.h>
#include "pgm/receiver.h"
#include "pgm/sqn_list.h"
#include "pgm/timer.h"
#include "pgm/packet_parse.h"
#include "pgm/net.h"


//#define RECEIVER_DEBUG
//#define SPM_DEBUG

#ifndef RECEIVER_DEBUG
#	define PGM_DISABLE_ASSERT
#endif

#if !defined(ENOBUFS) && defined(WSAENOBUFS)
#	define ENOBUFS		WSAENOBUFS
#endif
#if !defined(ECONNRESET) && defined(WSAECONNRESET)
#	define ECONNRESET	WSAECONNRESET
#endif

static bool send_spmr (pgm_transport_t* const, pgm_peer_t* const);
static bool send_nak (pgm_transport_t* const, pgm_peer_t* const, const uint32_t);
static bool send_parity_nak (pgm_transport_t* const, pgm_peer_t* const, const unsigned, const unsigned);
static bool send_nak_list (pgm_transport_t* const, pgm_peer_t* const, const struct pgm_sqn_list_t* const);
static bool nak_rb_state (pgm_peer_t*, const pgm_time_t);
static void nak_rpt_state (pgm_peer_t*, const pgm_time_t);
static void nak_rdata_state (pgm_peer_t*, const pgm_time_t);
static inline pgm_peer_t* _pgm_peer_ref (pgm_peer_t*);
static bool on_general_poll (pgm_transport_t* const, pgm_peer_t* const, struct pgm_sk_buff_t* const);
static bool on_dlr_poll (pgm_transport_t* const, pgm_peer_t* const, struct pgm_sk_buff_t* const);


/* helpers for pgm_peer_t */
static inline
pgm_time_t
next_nak_rb_expiry (
	const pgm_rxw_t* window
	)
{
	pgm_assert (NULL != window);
	pgm_assert (NULL != window->backoff_queue.tail);

	const struct pgm_sk_buff_t* skb = (const struct pgm_sk_buff_t*)window->backoff_queue.tail;
	const pgm_rxw_state_t* state = (const pgm_rxw_state_t*)&skb->cb;
	return state->nak_rb_expiry;
}

static inline
pgm_time_t
next_nak_rpt_expiry (
	const pgm_rxw_t* window
	)
{
	pgm_assert (NULL != window);
	pgm_assert (NULL != window->wait_ncf_queue.tail);

	const struct pgm_sk_buff_t* skb = (const struct pgm_sk_buff_t*)window->wait_ncf_queue.tail;
	const pgm_rxw_state_t* state = (const pgm_rxw_state_t*)&skb->cb;
	return state->nak_rpt_expiry;
}

static inline
pgm_time_t
next_nak_rdata_expiry (
	const pgm_rxw_t* window
	)
{
	pgm_assert (NULL != window);
	pgm_assert (NULL != window->wait_data_queue.tail);

	const struct pgm_sk_buff_t* skb = (const struct pgm_sk_buff_t*)window->wait_data_queue.tail;
	const pgm_rxw_state_t* state = (const pgm_rxw_state_t*)&skb->cb;
	return state->nak_rdata_expiry;
}

/* calculate NAK_RB_IVL as random time interval 1 - NAK_BO_IVL.
 */
static inline
uint32_t
nak_rb_ivl (
	pgm_transport_t* transport
	)	/* not const as rand() updates the seed */
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert_cmpuint (transport->nak_bo_ivl, >, 1);

	return pgm_rand_int_range (&transport->rand_, 1 /* us */, transport->nak_bo_ivl);
}

/* mark sequence as recovery failed.
 */

static
void
cancel_skb (
	pgm_transport_t*	    restrict transport,
	pgm_peer_t*		    restrict peer,
	const struct pgm_sk_buff_t* restrict skb,
	const pgm_time_t		     now
	)
{
	pgm_assert (NULL != transport);
	pgm_assert (NULL != peer);
	pgm_assert (NULL != skb);
	pgm_assert_cmpuint (now, >=, skb->tstamp);

	pgm_trace (PGM_LOG_ROLE_RX_WINDOW, _("Lost data #%u due to cancellation."), skb->sequence);

	const uint32_t fail_time = now - skb->tstamp;
	if (!peer->max_fail_time)
		peer->max_fail_time = peer->min_fail_time = fail_time;
	else if (fail_time > peer->max_fail_time)
		peer->max_fail_time = fail_time;
	else if (fail_time < peer->min_fail_time)
		peer->min_fail_time = fail_time;

	pgm_rxw_lost (peer->window, skb->sequence);
	PGM_HISTOGRAM_TIMES("Rx.FailTime", fail_time);

/* mark receiver window for flushing on next recv() */
	pgm_peer_set_pending (transport, peer);
}

/* increase reference count for peer object
 *
 * on success, returns peer object.
 */

static inline
pgm_peer_t*
_pgm_peer_ref (
	pgm_peer_t*	peer
	)
{
/* pre-conditions */
	pgm_assert (NULL != peer);

	pgm_atomic_int32_inc (&peer->ref_count);
	return peer;
}

/* decrease reference count of peer object, destroying on last reference.
 */

void
pgm_peer_unref (
	pgm_peer_t*	peer
	)
{
/* pre-conditions */
	pgm_assert (NULL != peer);

	const bool is_zero = pgm_atomic_int32_dec_and_test (&peer->ref_count);
	if (PGM_UNLIKELY (is_zero))
	{
/* receive window */
		pgm_rxw_destroy (peer->window);
		peer->window = NULL;

/* object */
		pgm_free (peer);
		peer = NULL;
	}
}

/* TODO: this should be in on_io_data to be more streamlined, or a generic options parser.
 *
 * returns TRUE if opt_fragment is found, otherwise FALSE is returned.
 */

static
bool
get_opt_fragment (
	struct pgm_opt_header*		opt_header,
	struct pgm_opt_fragment**	opt_fragment
	)
{
/* pre-conditions */
	pgm_assert (NULL != opt_header);
	pgm_assert (NULL != opt_fragment);
	pgm_assert (opt_header->opt_type   == PGM_OPT_LENGTH);
	pgm_assert (opt_header->opt_length == sizeof(struct pgm_opt_length));

	pgm_debug ("get_opt_fragment (opt-header:%p opt-fragment:%p)",
		(const void*)opt_header, (const void*)opt_fragment);

/* always at least two options, first is always opt_length */
	do {
		opt_header = (struct pgm_opt_header*)((char*)opt_header + opt_header->opt_length);

		if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_FRAGMENT)
		{
			*opt_fragment = (struct pgm_opt_fragment*)(opt_header + 1);
			return TRUE;
		}

	} while (!(opt_header->opt_type & PGM_OPT_END));

	*opt_fragment = NULL;
	return FALSE;
}

/* set interval timer & expiration timeout for peer expiration, very lax checking.
 * the ambient SPM interval MUST be set before calling this function.
 *
 * 0 < 2 * spm_ambient_interval <= peer_expiry
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_peer_expiry (
	pgm_transport_t* const	transport,
	const unsigned		peer_expiry	/* in microseconds */
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (peer_expiry > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed ||
	    (peer_expiry < (2 * transport->spm_ambient_interval)))
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->peer_expiry = peer_expiry;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* set maximum back off range for listening for multicast SPMR
 * the ambient SPM interval MUST be set before calling this function.
 *
 * 0 < spmr_expiry < spm_ambient_interval
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_spmr_expiry (
	pgm_transport_t* const	transport,
	const unsigned		spmr_expiry	/* in microseconds */
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (spmr_expiry > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed ||
	    ( transport->can_send_data &&
	      spmr_expiry >= transport->spm_ambient_interval ))
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->spmr_expiry = spmr_expiry;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < rxw_sqns < one less than half sequence space
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_rxw_sqns (
	pgm_transport_t* const	transport,
	const unsigned		sqns
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (sqns < ((UINT32_MAX/2)-1), FALSE);
	pgm_return_val_if_fail (sqns > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->rxw_sqns = sqns;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < secs < ( rxw_sqns / rxw_max_rte )
 *
 * can only be enforced upon bind.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_rxw_secs (
	pgm_transport_t* const	transport,
	const unsigned		secs
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (secs > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->rxw_secs = secs;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < rxw_max_rte < interface capacity
 *
 *  10mb :   1250000
 * 100mb :  12500000
 *   1gb : 125000000
 *
 * no practical way to determine upper limit and enforce.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_rxw_max_rte (
	pgm_transport_t* const	transport,
	const ssize_t		max_rte			/* limited by rate control math */
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (max_rte > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->rxw_max_rte = max_rte;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* Actual NAK back-off, NAK_RB_IVL, is random time interval 1 < NAK_BO_IVL,
 * randomized to reduce storms.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_nak_bo_ivl (
	pgm_transport_t* const	transport,
	const unsigned		usec		/* microseconds */
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (usec > 1, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->nak_bo_ivl = usec;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* Set NAK_RPT_IVL, the repeat interval before re-sending a NAK.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_nak_rpt_ivl (
	pgm_transport_t* const	transport,
	const unsigned		usec		/* microseconds */
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->nak_rpt_ivl = usec;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* Set NAK_RDATA_IVL, the interval waiting for data.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_nak_rdata_ivl (
	pgm_transport_t* const	transport,
	const unsigned		usec		/* microseconds */
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->nak_rdata_ivl = usec;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* statistics are limited to uint8_t, i.e. 255 retries
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_nak_data_retries (
	pgm_transport_t* const	transport,
	const unsigned		cnt
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->nak_data_retries = cnt;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* statistics are limited to uint8_t, i.e. 255 retries
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_nak_ncf_retries (
	pgm_transport_t* const	transport,
	const unsigned		cnt
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->nak_ncf_retries = cnt;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* a peer in the context of the transport is another party on the network sending PGM
 * packets.  for each peer we need a receive window and network layer address (nla) to
 * which nak requests can be forwarded to.
 *
 * on success, returns new peer object.
 */

pgm_peer_t*
pgm_new_peer (
	pgm_transport_t* const		transport,
	const pgm_tsi_t* const		tsi,
	const struct sockaddr* const	src_addr,
	const socklen_t			src_addrlen,
	const struct sockaddr* const	dst_addr,
	const socklen_t			dst_addrlen,
	const pgm_time_t		now
	)
{
	pgm_peer_t* peer;

/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != src_addr);
	pgm_assert (src_addrlen > 0);
	pgm_assert (NULL != dst_addr);
	pgm_assert (dst_addrlen > 0);

#ifdef RECEIVER_DEBUG
	char saddr[INET6_ADDRSTRLEN], daddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (src_addr, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (dst_addr, daddr, sizeof(daddr));
	pgm_debug ("pgm_new_peer (transport:%p tsi:%s src-addr:%s src-addrlen:%zu dst-addr:%s dst-addrlen:%zu)",
		(void*)transport, pgm_tsi_print (tsi), saddr, src_addrlen, daddr, dst_addrlen);
#endif

	peer = pgm_new0 (pgm_peer_t, 1);
	peer->expiry = now + transport->peer_expiry;
	peer->transport = transport;
	memcpy (&peer->tsi, tsi, sizeof(pgm_tsi_t));
	memcpy (&peer->group_nla, dst_addr, dst_addrlen);
	memcpy (&peer->local_nla, src_addr, src_addrlen);
/* port at same location for sin/sin6 */
	((struct sockaddr_in*)&peer->local_nla)->sin_port = htons (transport->udp_encap_ucast_port);
	((struct sockaddr_in*)&peer->nla)->sin_port       = htons (transport->udp_encap_ucast_port);

/* lock on rx window */
	peer->window = pgm_rxw_create (&peer->tsi,
					transport->max_tpdu,
					transport->rxw_sqns,
					transport->rxw_secs,
					transport->rxw_max_rte);
	peer->spmr_expiry = now + transport->spmr_expiry;

/* add peer to hash table and linked list */
	pgm_rwlock_writer_lock (&transport->peers_lock);
	pgm_peer_t* entry = _pgm_peer_ref (peer);
	pgm_hashtable_insert (transport->peers_hashtable, &peer->tsi, entry);
	peer->peers_link.data = peer;
	transport->peers_list = pgm_list_prepend_link (transport->peers_list, &peer->peers_link);
	pgm_rwlock_writer_unlock (&transport->peers_lock);

	pgm_timer_lock (transport);
	if (pgm_time_after( transport->next_poll, peer->spmr_expiry ))
		transport->next_poll = peer->spmr_expiry;
	pgm_timer_unlock (transport);
	return peer;
}

/* copy any contiguous buffers in the peer list to the provided 
 * message vector.
 * returns -ENOBUFS if the vector is full, returns -ECONNRESET if
 * data loss is detected, returns 0 when all peers flushed.
 */

int
pgm_flush_peers_pending (
	pgm_transport_t* const		transport,
	struct pgm_msgv_t**		pmsg,
	const struct pgm_msgv_t* const	msg_end,
	size_t* const			bytes_read,	/* added to, not set */
	unsigned* const			data_read
	)
{
	int retval = 0;

/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != pmsg);
	pgm_assert (NULL != *pmsg);
	pgm_assert (NULL != msg_end);
	pgm_assert (NULL != bytes_read);
	pgm_assert (NULL != data_read);

	pgm_debug ("pgm_flush_peers_pending (transport:%p pmsg:%p msg-end:%p bytes-read:%p data-read:%p)",
		(const void*)transport, (const void*)pmsg, (const void*)msg_end, (const void*)bytes_read, (const void*)data_read);

	while (transport->peers_pending)
	{
		pgm_peer_t* peer = transport->peers_pending->data;
		if (peer->last_commit && peer->last_commit < transport->last_commit)
			pgm_rxw_remove_commit (peer->window);
		const ssize_t peer_bytes = pgm_rxw_readv (peer->window, pmsg, msg_end - *pmsg + 1);

		if (peer->last_cumulative_losses != ((pgm_rxw_t*)peer->window)->cumulative_losses)
		{
			transport->is_reset = TRUE;
			peer->lost_count = ((pgm_rxw_t*)peer->window)->cumulative_losses - peer->last_cumulative_losses;
			peer->last_cumulative_losses = ((pgm_rxw_t*)peer->window)->cumulative_losses;
		}
	
		if (peer_bytes >= 0)
		{
			(*bytes_read) += peer_bytes;
			(*data_read)  ++;
			peer->last_commit = transport->last_commit;
			if (*pmsg > msg_end) {			/* commit full */
				retval = -ENOBUFS;
				break;
			}
		} else
			peer->last_commit = 0;
		if (PGM_UNLIKELY(transport->is_reset)) {
			retval = -ECONNRESET;
			break;
		}
/* clear this reference and move to next */
		transport->peers_pending = pgm_slist_remove_first (transport->peers_pending);
	}

	return retval;
}

/* edge trigerred has receiver pending events
 */

bool
pgm_peer_has_pending (
	pgm_peer_t* const	peer
	)
{
/* pre-conditions */
	pgm_assert (NULL != peer);

	if (NULL == peer->pending_link.data && ((pgm_rxw_t*)peer->window)->has_event) {
		((pgm_rxw_t*)peer->window)->has_event = 0;
		return TRUE;
	}
	return FALSE;
}

/* set receiver in pending event queue
 */

void
pgm_peer_set_pending (
	pgm_transport_t* const	transport,
	pgm_peer_t* const	peer
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != peer);

	if (peer->pending_link.data) return;
	peer->pending_link.data = peer;
	transport->peers_pending = pgm_slist_prepend_link (transport->peers_pending, &peer->pending_link);
}

/* Create a new error SKB detailing data loss.
 */

void
pgm_set_reset_error (
	pgm_transport_t*const	transport,
	pgm_peer_t*const	source,
	struct pgm_msgv_t*const	msgv
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != source);
	pgm_assert (NULL != msgv);

	struct pgm_sk_buff_t* error_skb = pgm_alloc_skb (0);
	error_skb->transport	= transport;
	error_skb->tstamp	= pgm_time_update_now ();
	memcpy (&error_skb->tsi, &source->tsi, sizeof(pgm_tsi_t));
	error_skb->sequence	= source->lost_count;
	msgv->msgv_skb[0]	= error_skb;
	msgv->msgv_len		= 1;
}

/* SPM indicate start of a session, continued presence of a session, or flushing final packets
 * of a session.
 *
 * returns TRUE on valid packet, FALSE on invalid packet or duplicate SPM sequence number.
 */

bool
pgm_on_spm (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != source);
	pgm_assert (NULL != skb);

	pgm_debug("pgm_on_spm (transport:%p source:%p skb:%p)",
		(const void*)transport, (const void*)source, (const void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_spm (skb))) {
		pgm_trace(PGM_LOG_ROLE_NETWORK,_("Discarded invalid SPM."));
		source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
		return FALSE;
	}

	const struct pgm_spm*  spm  = (struct pgm_spm*) skb->data;
	const struct pgm_spm6* spm6 = (struct pgm_spm6*)skb->data;
	const uint32_t spm_sqn = ntohl (spm->spm_sqn);

/* check for advancing sequence number, or first SPM */
	if (PGM_LIKELY(pgm_uint32_gte (spm_sqn, source->spm_sqn) ||
	     ((struct sockaddr*)&source->nla)->sa_family == 0))
	{
/* copy NLA for replies */
		pgm_nla_to_sockaddr (&spm->spm_nla_afi, (struct sockaddr*)&source->nla);

/* save sequence number */
		source->spm_sqn = spm_sqn;

/* update receive window */
		const pgm_time_t nak_rb_expiry = skb->tstamp + nak_rb_ivl (transport);
		const unsigned naks = pgm_rxw_update (source->window,
						      ntohl (spm->spm_lead),
						      ntohl (spm->spm_trail),
						      skb->tstamp,
						      nak_rb_expiry);
		if (naks) {
			pgm_timer_lock (transport);
			if (pgm_time_after (transport->next_poll, nak_rb_expiry))
				transport->next_poll = nak_rb_expiry;
			pgm_timer_unlock (transport);
		}

/* mark receiver window for flushing on next recv() */
		const pgm_rxw_t* window = source->window;
		if (window->cumulative_losses != source->last_cumulative_losses &&
		    !source->pending_link.data)
		{
			transport->is_reset = TRUE;
			source->lost_count = window->cumulative_losses - source->last_cumulative_losses;
			source->last_cumulative_losses = window->cumulative_losses;
			pgm_peer_set_pending (transport, source);
		}
	}
	else
	{	/* does not advance SPM sequence number */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded duplicate SPM."));
		source->cumulative_stats[PGM_PC_RECEIVER_DUP_SPMS]++;
		return FALSE;
	}

/* check whether peer can generate parity packets */
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (AF_INET6 == source->nla.ss_family) ?
							(const struct pgm_opt_length*)(spm6 + 1) :
							(const struct pgm_opt_length*)(spm  + 1);
		if (PGM_UNLIKELY(opt_len->opt_type != PGM_OPT_LENGTH))
		{
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded malformed SPM."));
			source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
			return FALSE;
		}
		if (PGM_UNLIKELY(opt_len->opt_length != sizeof(struct pgm_opt_length)))
		{
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded malformed SPM."));
			source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
			return FALSE;
		}
/* TODO: check for > 16 options & past packet end */
		const struct pgm_opt_header* opt_header = (const struct pgm_opt_header*)opt_len;
		do {
			opt_header = (const struct pgm_opt_header*)((const char*)opt_header + opt_header->opt_length);
			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_PARITY_PRM)
			{
				const struct pgm_opt_parity_prm* opt_parity_prm = (const struct pgm_opt_parity_prm*)(opt_header + 1);
				if (PGM_UNLIKELY((opt_parity_prm->opt_reserved & PGM_PARITY_PRM_MASK) == 0))
				{
					pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded malformed SPM."));
					source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
					return FALSE;
				}

				const uint32_t parity_prm_tgs = ntohl (opt_parity_prm->parity_prm_tgs);
				if (PGM_UNLIKELY(parity_prm_tgs < 2 || parity_prm_tgs > 128))
				{
					pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded malformed SPM."));
					source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
					return FALSE;
				}
			
				source->has_proactive_parity = opt_parity_prm->opt_reserved & PGM_PARITY_PRM_PRO;
				source->has_ondemand_parity  = opt_parity_prm->opt_reserved & PGM_PARITY_PRM_OND;
				if (source->has_proactive_parity || source->has_ondemand_parity) {
					source->is_fec_enabled = 1;
					pgm_rxw_update_fec (source->window, parity_prm_tgs);
				}
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

/* either way bump expiration timer */
	source->expiry = skb->tstamp + transport->peer_expiry;
	source->spmr_expiry = 0;
	if (source->spmr_tstamp > 0) {
		PGM_HISTOGRAM_TIMES("Rx.SpmRequestResponseTime", skb->tstamp - source->spmr_tstamp);
		source->spmr_tstamp = 0;
	}
	return TRUE;
}

/* Multicast peer-to-peer NAK handling, pretty much the same as a NCF but different direction
 *
 * if NAK is valid, returns TRUE.  on error, FALSE is returned.
 */

bool
pgm_on_peer_nak (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		peer,
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != peer);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_peer_nak (transport:%p peer:%p skb:%p)",
		(const void*)transport, (const void*)peer, (const void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_nak (skb)))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded invalid multicast NAK."));
		peer->cumulative_stats[PGM_PC_RECEIVER_NAK_ERRORS]++;
		return FALSE;
	}

	const struct pgm_nak*  nak  = (struct pgm_nak*) skb->data;
	const struct pgm_nak6* nak6 = (struct pgm_nak6*)skb->data;
		
/* NAK_SRC_NLA must not contain our transport unicast NLA */
	struct sockaddr_storage nak_src_nla;
	pgm_nla_to_sockaddr (&nak->nak_src_nla_afi, (struct sockaddr*)&nak_src_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nak_src_nla, (struct sockaddr*)&transport->send_addr) == 0))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded multicast NAK on NLA mismatch."));
		return FALSE;
	}

/* NAK_GRP_NLA contains one of our transport receive multicast groups: the sources send multicast group */ 
	struct sockaddr_storage nak_grp_nla;
	pgm_nla_to_sockaddr ((AF_INET6 == nak_src_nla.ss_family) ? &nak6->nak6_grp_nla_afi : &nak->nak_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
	bool found = FALSE;
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_grp_nla, (struct sockaddr*)&transport->recv_gsr[i].gsr_group) == 0)
		{
			found = TRUE;
			break;
		}
	}

	if (PGM_UNLIKELY(!found)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded multicast NAK on multicast group mismatch."));
		return FALSE;
	}

/* handle as NCF */
	int status = pgm_rxw_confirm (peer->window,
				      ntohl (nak->nak_sqn),
				      skb->tstamp,
				      skb->tstamp + transport->nak_rdata_ivl,
				      skb->tstamp + nak_rb_ivl(transport));
	if (PGM_RXW_UPDATED == status || PGM_RXW_APPENDED == status)
		peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED]++;

/* check NAK list */
	const uint32_t* nak_list = NULL;
	unsigned nak_list_len = 0;
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (AF_INET6 == nak_src_nla.ss_family) ?
							(const struct pgm_opt_length*)(nak6 + 1) :
							(const struct pgm_opt_length*)(nak + 1);
		if (PGM_UNLIKELY(opt_len->opt_type != PGM_OPT_LENGTH))
		{
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded malformed multicast NAK."));
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			return FALSE;
		}
		if (PGM_UNLIKELY(opt_len->opt_length != sizeof(struct pgm_opt_length)))
		{
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded malformed multicast NAK."));
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			return FALSE;
		}
/* TODO: check for > 16 options & past packet end */
		const struct pgm_opt_header* opt_header = (const struct pgm_opt_header*)opt_len;
		do {
			opt_header = (const struct pgm_opt_header*)((const char*)opt_header + opt_header->opt_length);
			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST)
			{
				nak_list = ((const struct pgm_opt_nak_list*)(opt_header + 1))->opt_sqn;
				nak_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(uint8_t) ) / sizeof(uint32_t);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

	while (nak_list_len) {
		status = pgm_rxw_confirm (peer->window,
					  ntohl (*nak_list),
					  skb->tstamp,
					  skb->tstamp + transport->nak_rdata_ivl,
					  skb->tstamp + nak_rb_ivl(transport));
		if (PGM_RXW_UPDATED == status || PGM_RXW_APPENDED == status)
			peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED]++;
		nak_list++;
		nak_list_len--;
	}

/* mark receiver window for flushing on next recv() */
	const pgm_rxw_t* window = peer->window;
	if (window->cumulative_losses != peer->last_cumulative_losses &&
	    !peer->pending_link.data)
	{
		transport->is_reset = TRUE;
		peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
		peer->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (transport, peer);
	}
	return TRUE;
}

/* NCF confirming receipt of a NAK from this transport or another on the LAN segment.
 *
 * Packet contents will match exactly the sent NAK, although not really that helpful.
 *
 * if NCF is valid, returns TRUE.  on error, FALSE is returned.
 */

bool
pgm_on_ncf (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != source);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_ncf (transport:%p source:%p skb:%p)",
		(const void*)transport, (const void*)source, (const void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_ncf (skb)))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded invalid NCF."));
		source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
		return FALSE;
	}

	const struct pgm_nak*  ncf  = (struct pgm_nak*) skb->data;
	const struct pgm_nak6* ncf6 = (struct pgm_nak6*)skb->data;
		
/* NCF_SRC_NLA may contain our transport unicast NLA, we don't really care */
	struct sockaddr_storage ncf_src_nla;
	pgm_nla_to_sockaddr (&ncf->nak_src_nla_afi, (struct sockaddr*)&ncf_src_nla);

#if 0
	if (PGM(pgm_sockaddr_cmp ((struct sockaddr*)&ncf_src_nla, (struct sockaddr*)&transport->send_addr) != 0)) {
		g_trace ("INFO", "Discarded NCF on NLA mismatch.");
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		return FALSE;
	}
#endif

/* NCF_GRP_NLA contains our transport multicast group */ 
	struct sockaddr_storage ncf_grp_nla;
	pgm_nla_to_sockaddr ((AF_INET6 == ncf_src_nla.ss_family) ? &ncf6->nak6_grp_nla_afi : &ncf->nak_grp_nla_afi, (struct sockaddr*)&ncf_grp_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&ncf_grp_nla, (struct sockaddr*)&transport->send_gsr.gsr_group) != 0))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded NCF on multicast group mismatch."));
		return FALSE;
	}

	const pgm_time_t ncf_rdata_ivl = skb->tstamp + transport->nak_rdata_ivl;
	const pgm_time_t ncf_rb_ivl    = skb->tstamp + nak_rb_ivl(transport);
	int status = pgm_rxw_confirm (source->window,
				      ntohl (ncf->nak_sqn),
				      skb->tstamp,
				      ncf_rdata_ivl,
				      ncf_rb_ivl);
	if (PGM_RXW_UPDATED == status || PGM_RXW_APPENDED == status)
	{
		const pgm_time_t ncf_ivl = (PGM_RXW_APPENDED == status) ? ncf_rb_ivl : ncf_rdata_ivl;
		pgm_timer_lock (transport);
		if (pgm_time_after (transport->next_poll, ncf_ivl)) {
			transport->next_poll = ncf_ivl;
		}
		pgm_timer_unlock (transport);
		source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED]++;
	}

/* check NCF list */
	const uint32_t* ncf_list = NULL;
	unsigned ncf_list_len = 0;
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (AF_INET6 == ncf_src_nla.ss_family) ?
							(const struct pgm_opt_length*)(ncf6 + 1) :
							(const struct pgm_opt_length*)(ncf  + 1);
		if (PGM_UNLIKELY(opt_len->opt_type != PGM_OPT_LENGTH))
		{
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded malformed NCF."));
			source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			return FALSE;
		}
		if (PGM_UNLIKELY(opt_len->opt_length != sizeof(struct pgm_opt_length)))
		{
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded malformed NCF."));
			source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			return FALSE;
		}
/* TODO: check for > 16 options & past packet end */
		const struct pgm_opt_header* opt_header = (const struct pgm_opt_header*)opt_len;
		do {
			opt_header = (const struct pgm_opt_header*)((const char*)opt_header + opt_header->opt_length);
			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST)
			{
				ncf_list = ((const struct pgm_opt_nak_list*)(opt_header + 1))->opt_sqn;
				ncf_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(uint8_t) ) / sizeof(uint32_t);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

	pgm_debug ("NCF contains 1+%d sequence numbers.", ncf_list_len);
	while (ncf_list_len)
	{
		status = pgm_rxw_confirm (source->window,
					  ntohl (*ncf_list),
					  skb->tstamp,
					  ncf_rdata_ivl,
					  ncf_rb_ivl);
		if (PGM_RXW_UPDATED == status || PGM_RXW_APPENDED == status)
			source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED]++;
		ncf_list++;
		ncf_list_len--;
	}

/* mark receiver window for flushing on next recv() */
	const pgm_rxw_t* window = source->window;
	if (window->cumulative_losses != source->last_cumulative_losses &&
	    !source->pending_link.data)
	{
		transport->is_reset = TRUE;
		source->lost_count = window->cumulative_losses - source->last_cumulative_losses;
		source->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (transport, source);
	}
	return TRUE;
}

/* send SPM-request to a new peer, this packet type has no contents
 *
 * on success, TRUE is returned, if operation would block FALSE is
 * returned.
 */

static
bool
send_spmr (
	pgm_transport_t* const	transport,
	pgm_peer_t*      const	source
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != source);

	pgm_debug ("send_spmr (transport:%p source:%p)",
		(const void*)transport, (const void*)source);

	const size_t tpdu_length = sizeof(struct pgm_header);
	char buf[ tpdu_length ];
	struct pgm_header* header = (struct pgm_header*)buf;
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));
/* dport & sport reversed communicating upstream */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type	= PGM_SPMR;
	header->pgm_options	= 0;
	header->pgm_tsdu_length	= 0;
	header->pgm_checksum	= 0;
	header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

/* send multicast SPMR TTL 1 */
	pgm_sockaddr_multicast_hops (transport->send_sock, transport->send_gsr.gsr_group.ss_family, 1);
	ssize_t sent = pgm_sendto (transport,
				   FALSE,			/* not rate limited */
				   FALSE,			/* regular socket */
				   header,
				   tpdu_length,
				   (struct sockaddr*)&transport->send_gsr.gsr_group,
				   pgm_sockaddr_len ((struct sockaddr*)&transport->send_gsr.gsr_group));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno))
		return FALSE;

/* send unicast SPMR with regular TTL */
	pgm_sockaddr_multicast_hops (transport->send_sock, transport->send_gsr.gsr_group.ss_family, transport->hops);
	sent = pgm_sendto (transport,
			   FALSE,
			   FALSE,
			   header,
			   tpdu_length,
			   (struct sockaddr*)&source->local_nla,
			   pgm_sockaddr_len ((struct sockaddr*)&source->local_nla));
	if (sent < 0 && EAGAIN == errno)
		return FALSE;

	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += tpdu_length * 2;
	return TRUE;
}

/* send selective NAK for one sequence number.
 *
 * on success, TRUE is returned, returns FALSE if would block on operation.
 */

static
bool
send_nak (
	pgm_transport_t* const	transport,
	pgm_peer_t* const	source,
	const uint32_t		sequence
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != source);

	pgm_debug ("send_nak (transport:%p peer:%p sequence:%" PRIu32 ")",
		(void*)transport, (void*)source, sequence);

	size_t tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	if (AF_INET6 == source->nla.ss_family)
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	char buf[ tpdu_length ];
	struct pgm_header* header = (struct pgm_header*)buf;
	struct pgm_nak*  nak  = (struct pgm_nak* )(header + 1);
	struct pgm_nak6* nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= htonl (sequence);

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->nla, (char*)&nak->nak_src_nla_afi);

/* group nla: we match the NAK NLA to the same as advertised by the source, we might
 * be listening to multiple multicast groups
 */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->group_nla,
				(AF_INET6 == source->nla.ss_family) ? (char*)&nak6->nak6_grp_nla_afi : (char*)&nak->nak_grp_nla_afi);

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

	const ssize_t sent = pgm_sendto (transport,
					FALSE,			/* not rate limited */
					TRUE,			/* with router alert */
					header,
					tpdu_length,
					(struct sockaddr*)&source->nla,
					pgm_sockaddr_len((struct sockaddr*)&source->nla));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno))
		return FALSE;

	source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT]++;
	source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT]++;
	return TRUE;
}

/* Send a parity NAK requesting on-demand parity packet generation.
 *
 * on success, TRUE is returned, returns FALSE if operation would block.
 */

static
bool
send_parity_nak (
	pgm_transport_t* const	transport,
	pgm_peer_t* const	source,
	const uint32_t		nak_tg_sqn,	/* transmission group (shifted) */
	const uint32_t		nak_pkt_cnt	/* count of parity packets to request */
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != source);
	pgm_assert (nak_pkt_cnt > 0);

	pgm_debug ("send_parity_nak (transport:%p source:%p nak-tg-sqn:%" PRIu32 " nak-pkt-cnt:%" PRIu32 ")",
		(void*)transport, (void*)source, nak_tg_sqn, nak_pkt_cnt);

	size_t tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	if (AF_INET6 == source->nla.ss_family)
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	char buf[ tpdu_length ];
	struct pgm_header* header = (struct pgm_header*)buf;
	struct pgm_nak*  nak  = (struct pgm_nak* )(header + 1);
	struct pgm_nak6* nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = PGM_OPT_PARITY;	/* this is a parity packet */
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= htonl (nak_tg_sqn | (nak_pkt_cnt - 1) );

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->nla, (char*)&nak->nak_src_nla_afi);

/* group nla: we match the NAK NLA to the same as advertised by the source, we might
 * be listening to multiple multicast groups
 */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->group_nla,
				(AF_INET6 == source->nla.ss_family) ? (char*)&nak6->nak6_grp_nla_afi : (char*)&nak->nak_grp_nla_afi );
        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

	const ssize_t sent = pgm_sendto (transport,
					 FALSE,		/* not rate limited */
					 TRUE,		/* with router alert */
					 header,
					 tpdu_length,
					 (struct sockaddr*)&source->nla,
					 pgm_sockaddr_len((struct sockaddr*)&source->nla));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno))
		return FALSE;

	source->cumulative_stats[PGM_PC_RECEIVER_PARITY_NAK_PACKETS_SENT]++;
	source->cumulative_stats[PGM_PC_RECEIVER_PARITY_NAKS_SENT]++;
	return TRUE;
}

/* A NAK packet with a OPT_NAK_LIST option extension
 *
 * on success, TRUE is returned.  on error, FALSE is returned.
 */

static
bool
send_nak_list (
	pgm_transport_t* const			transport,
	pgm_peer_t* const			source,
	const struct pgm_sqn_list_t*const	sqn_list
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != source);
	pgm_assert (NULL != sqn_list);
	pgm_assert_cmpuint (sqn_list->len, >, 1);
	pgm_assert_cmpuint (sqn_list->len, <=, 63);

#ifdef RECEIVER_DEBUG
	char list[1024];
	sprintf (list, "%" PRIu32, sqn_list->sqn[0]);
	for (unsigned i = 1; i < sqn_list->len; i++) {
		char sequence[2 + strlen("4294967295")];
		sprintf (sequence, " %" PRIu32, sqn_list->sqn[i]);
		strcat (list, sequence);
	}
	pgm_debug("send_nak_list (transport:%p source:%p sqn-list:[%s])",
		(const void*)transport, (const void*)source, list);
#endif

	size_t tpdu_length = sizeof(struct pgm_header) +
			    sizeof(struct pgm_nak) +
			    sizeof(struct pgm_opt_length) +		/* includes header */
			    sizeof(struct pgm_opt_header) +
			    sizeof(struct pgm_opt_nak_list) +
			    ( (sqn_list->len-1) * sizeof(uint32_t) );
	if (AF_INET6 == source->nla.ss_family)
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	char buf[ tpdu_length ];
	if (PGM_UNLIKELY(pgm_mem_gc_friendly))
		memset (buf, 0, tpdu_length);
	struct pgm_header* header = (struct pgm_header*)buf;
	struct pgm_nak*  nak  = (struct pgm_nak* )(header + 1);
	struct pgm_nak6* nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = PGM_OPT_PRESENT | PGM_OPT_NETWORK;
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= htonl (sqn_list->sqn[0]);

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->nla, (char*)&nak->nak_src_nla_afi);

/* group nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->group_nla,
				(AF_INET6 == source->nla.ss_family) ? (char*)&nak6->nak6_grp_nla_afi : (char*)&nak->nak_grp_nla_afi);

/* OPT_NAK_LIST */
	struct pgm_opt_length* opt_len = (AF_INET6 == source->nla.ss_family) ? (struct pgm_opt_length*)(nak6 + 1) : (struct pgm_opt_length*)(nak + 1);
	opt_len->opt_type	= PGM_OPT_LENGTH;
	opt_len->opt_length	= sizeof(struct pgm_opt_length);
	opt_len->opt_total_length = htons (	sizeof(struct pgm_opt_length) +
						sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_nak_list) +
						( (sqn_list->len-1) * sizeof(uint32_t) ) );
	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
	opt_header->opt_type	= PGM_OPT_NAK_LIST | PGM_OPT_END;
	opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
				+ ( (sqn_list->len-1) * sizeof(uint32_t) );
	struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
	opt_nak_list->opt_reserved = 0;

	for (unsigned i = 1; i < sqn_list->len; i++)
		opt_nak_list->opt_sqn[i-1] = htonl (sqn_list->sqn[i]);

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

	const ssize_t sent = pgm_sendto (transport,
					FALSE,			/* not rate limited */
					FALSE,			/* regular socket */
					header,
					tpdu_length,
					(struct sockaddr*)&source->nla,
					pgm_sockaddr_len((struct sockaddr*)&source->nla));
	if ( sent != (ssize_t)tpdu_length )
		return FALSE;

	source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT]++;
	source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT] += 1 + sqn_list->len;
	return TRUE;
}

/* check all receiver windows for packets in BACK-OFF_STATE, on expiration send a NAK.
 * update transport::next_nak_rb_timestamp for next expiration time.
 *
 * peer object is locked before entry.
 *
 * returns TRUE on success, returns FALSE if operation would block.
 */

static
bool
nak_rb_state (
	pgm_peer_t*		peer,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	pgm_assert (NULL != peer);

	pgm_debug ("nak_rb_state (peer:%p now:%" PGM_TIME_FORMAT ")",
		(const void*)peer, now);

	pgm_rxw_t* window = peer->window;
	pgm_transport_t* transport = peer->transport;
	pgm_list_t* list;
	struct pgm_sqn_list_t nak_list = { .len = 0 };

/* send all NAKs first, lack of data is blocking contiguous processing and its 
 * better to get the notification out a.s.a.p. even though it might be waiting
 * in a kernel queue.
 *
 * alternative: after each packet check for incoming data and return to the
 * event loop.  bias for shorter loops as retry count increases.
 */
	list = window->backoff_queue.tail;
	if (!list) {
		pgm_assert (window->backoff_queue.head == NULL);
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Backoff queue is empty in nak_rb_state."));
		return TRUE;
	} else {
		pgm_assert (window->backoff_queue.head != NULL);
	}

	unsigned dropped_invalid = 0;

/* have not learned this peers NLA */
	const bool is_valid_nla = 0 != peer->nla.ss_family;

/* TODO: process BOTH selective and parity NAKs? */

/* calculate current transmission group for parity enabled peers */
	if (peer->has_ondemand_parity)
	{
		const uint32_t tg_sqn_mask = 0xffffffff << window->tg_sqn_shift;

/* NAKs only generated previous to current transmission group */
		const uint32_t current_tg_sqn = window->lead & tg_sqn_mask;

		uint32_t nak_tg_sqn = 0;
		uint32_t nak_pkt_cnt = 0;

/* parity NAK generation */

		while (list)
		{
			pgm_list_t* next_list_el = list->prev;
			struct pgm_sk_buff_t* skb	= (struct pgm_sk_buff_t*)list;
			pgm_rxw_state_t* state		= (pgm_rxw_state_t*)&skb->cb;

/* check this packet for state expiration */
			if (pgm_time_after_eq (now, state->nak_rb_expiry))
			{
				if (PGM_UNLIKELY(!is_valid_nla)) {
					dropped_invalid++;
					pgm_rxw_lost (window, skb->sequence);
/* mark receiver window for flushing on next recv() */
					pgm_peer_set_pending (transport, peer);
					list = next_list_el;
					continue;
				}

/* TODO: parity nak lists */
				const uint32_t tg_sqn = skb->sequence & tg_sqn_mask;
				if (	(  nak_pkt_cnt && tg_sqn == nak_tg_sqn ) ||
					( !nak_pkt_cnt && tg_sqn != current_tg_sqn )	)
				{
					pgm_rxw_state (window, skb, PGM_PKT_STATE_WAIT_NCF);

					if (!nak_pkt_cnt++)
						nak_tg_sqn = tg_sqn;
					state->nak_transmit_count++;

#ifdef PGM_ABSOLUTE_EXPIRY
					state->nak_rpt_expiry = state->nak_rb_expiry + transport->nak_rpt_ivl;
					while (pgm_time_after_eq (now, state->nak_rpt_expiry)) {
						state->nak_rpt_expiry += transport->nak_rpt_ivl;
						state->ncf_retry_count++;
					}
#else
					state->nak_rpt_expiry = now + transport->nak_rpt_ivl;
#endif
					pgm_timer_lock (transport);
					if (pgm_time_after (transport->next_poll, state->nak_rpt_expiry))
						transport->next_poll = state->nak_rpt_expiry;
					pgm_timer_unlock (transport);
				}
				else
				{	/* different transmission group */
					break;
				}
			}
			else
			{	/* packet expires some time later */
				break;
			}

			list = next_list_el;
		}

		if (nak_pkt_cnt && !send_parity_nak (transport, peer, nak_tg_sqn, nak_pkt_cnt))
			return FALSE;
	}
	else
	{

/* select NAK generation */

		while (list)
		{
			pgm_list_t* next_list_el = list->prev;
			struct pgm_sk_buff_t* skb	= (struct pgm_sk_buff_t*)list;
			pgm_rxw_state_t* state		= (pgm_rxw_state_t*)&skb->cb;

/* check this packet for state expiration */
			if (pgm_time_after_eq(now, state->nak_rb_expiry))
			{
				if (PGM_UNLIKELY(!is_valid_nla)) {
					dropped_invalid++;
					pgm_rxw_lost (window, skb->sequence);
/* mark receiver window for flushing on next recv() */
					pgm_peer_set_pending (transport, peer);
					list = next_list_el;
					continue;
				}

				pgm_rxw_state (window, skb, PGM_PKT_STATE_WAIT_NCF);
				nak_list.sqn[nak_list.len++] = skb->sequence;
				state->nak_transmit_count++;

/* we have two options here, calculate the expiry time in the new state relative to the current
 * state execution time, skipping missed expirations due to delay in state processing, or base
 * from the actual current time.
 */
#ifdef PGM_ABSOLUTE_EXPIRY
				state->nak_rpt_expiry = state->nak_rb_expiry + transport->nak_rpt_ivl;
				while (pgm_time_after_eq(now, state->nak_rpt_expiry)){
					state->nak_rpt_expiry += transport->nak_rpt_ivl;
					state->ncf_retry_count++;
				}
#else
				state->nak_rpt_expiry = now + transport->nak_rpt_ivl;
pgm_trace(PGM_LOG_ROLE_NETWORK,_("rp->nak_rpt_expiry in %f seconds."),
		pgm_to_secsf( state->nak_rpt_expiry - now ) );
#endif
				pgm_timer_lock (transport);
				if (pgm_time_after (transport->next_poll, state->nak_rpt_expiry))
					transport->next_poll = state->nak_rpt_expiry;
				pgm_timer_unlock (transport);

				if (nak_list.len == PGM_N_ELEMENTS(nak_list.sqn)) {
					if (transport->can_send_nak && !send_nak_list (transport, peer, &nak_list))
						return FALSE;
					nak_list.len = 0;
				}
			}
			else
			{	/* packet expires some time later */
				break;
			}

			list = next_list_el;
		}

		if (transport->can_send_nak && nak_list.len)
		{
			if (nak_list.len > 1 && !send_nak_list (transport, peer, &nak_list))
				return FALSE;
			else if (!send_nak (transport, peer, nak_list.sqn[0]))
				return FALSE;
		}

	}

	if (PGM_UNLIKELY(dropped_invalid))
	{
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Dropped %u messages due to invalid NLA."), dropped_invalid);

/* mark receiver window for flushing on next recv() */
		if (window->cumulative_losses != peer->last_cumulative_losses &&
		    !peer->pending_link.data)
		{
			transport->is_reset = TRUE;
			peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
			peer->last_cumulative_losses = window->cumulative_losses;
			pgm_peer_set_pending (transport, peer);
		}
	}

	if (window->backoff_queue.length == 0)
	{
		pgm_assert ((struct rxw_packet*)window->backoff_queue.head == NULL);
		pgm_assert ((struct rxw_packet*)window->backoff_queue.tail == NULL);
	}
	else
	{
		pgm_assert ((struct rxw_packet*)window->backoff_queue.head != NULL);
		pgm_assert ((struct rxw_packet*)window->backoff_queue.tail != NULL);
	}

	if (window->backoff_queue.tail)
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Next expiry set in %f seconds."),
			pgm_to_secsf(next_nak_rb_expiry(window) - now));
	}
	else
	{
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Backoff queue empty."));
	}
	return TRUE;
}

/* check this peer for NAK state timers, uses the tail of each queue for the nearest
 * timer execution.
 *
 * returns TRUE on complete sweep, returns FALSE if operation would block.
 */

bool
pgm_check_peer_nak_state (
	pgm_transport_t*	transport,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);

	pgm_debug ("pgm_check_peer_nak_state (transport:%p now:%" PGM_TIME_FORMAT ")",
		(const void*)transport, now);

	if (!transport->peers_list)
		return TRUE;

	pgm_list_t* list = transport->peers_list;
	do {
		pgm_list_t* next = list->next;
		pgm_peer_t* peer = list->data;
		pgm_rxw_t* window = peer->window;

		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (now, peer->spmr_expiry))
			{
				if (transport->can_send_nak) {
					if (!send_spmr (transport, peer)) {
						return FALSE;
					}
					peer->spmr_tstamp = now;
				}
				peer->spmr_expiry = 0;
			}
		}

		if (window->backoff_queue.tail)
		{
			if (pgm_time_after_eq (now, next_nak_rb_expiry (window)))
				if (!nak_rb_state (peer, now)) {
					return FALSE;
				}
		}
		
		if (window->wait_ncf_queue.tail)
		{
			if (pgm_time_after_eq (now, next_nak_rpt_expiry (window)))
				nak_rpt_state (peer, now);
		}

		if (window->wait_data_queue.tail)
		{
			if (pgm_time_after_eq (now, next_nak_rdata_expiry (window)))
				nak_rdata_state (peer, now);
		}

/* expired, remove from hash table and linked list */
		if (pgm_time_after_eq (now, peer->expiry))
		{
			if (window->committed_count)
			{
				pgm_trace (PGM_LOG_ROLE_SESSION,_("Peer expiration postponed due to committed data, tsi %s"), pgm_tsi_print (&peer->tsi));
				peer->expiry += transport->peer_expiry;
			}
			else
			{
				pgm_trace (PGM_LOG_ROLE_SESSION,_("Peer expired, tsi %s"), pgm_tsi_print (&peer->tsi));
				pgm_hashtable_remove (transport->peers_hashtable, &peer->tsi);
				transport->peers_list = pgm_list_remove_link (transport->peers_list, &peer->peers_link);
				pgm_peer_unref (peer);
			}
		}

		list = next;
	} while (list);

/* check for waiting contiguous packets */
	if (transport->peers_pending && !transport->is_pending_read)
	{
		pgm_debug ("prod rx thread");
		pgm_notify_send (&transport->pending_notify);
		transport->is_pending_read = TRUE;
	}
	return TRUE;
}

/* find the next state expiration time among the transports peers.
 *
 * on success, returns the earliest of the expiration parameter or next
 * peer expiration time.
 */

pgm_time_t
pgm_min_nak_expiry (
	pgm_time_t		expiration,		/* absolute time */
	pgm_transport_t*	transport
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);

	pgm_debug ("pgm_min_nak_expiry (expiration:%" PGM_TIME_FORMAT " transport:%p)",
		expiration, (const void*)transport);

	if (!transport->peers_list)
		return expiration;

	pgm_list_t* list = transport->peers_list;
	do {
		pgm_list_t* next = list->next;
		pgm_peer_t* peer = (pgm_peer_t*)list->data;
		pgm_rxw_t* window = peer->window;
	
		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (expiration, peer->spmr_expiry))
				expiration = peer->spmr_expiry;
		}

		if (window->backoff_queue.tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rb_expiry (window)))
				expiration = next_nak_rb_expiry (window);
		}

		if (window->wait_ncf_queue.tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rpt_expiry (window)))
				expiration = next_nak_rpt_expiry (window);
		}

		if (window->wait_data_queue.tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rdata_expiry (window)))
				expiration = next_nak_rdata_expiry (window);
		}
	
		list = next;
	} while (list);

	return expiration;
}

/* check WAIT_NCF_STATE, on expiration move back to BACK-OFF_STATE, on exceeding NAK_NCF_RETRIES
 * cancel the sequence number.
 */
static
void
nak_rpt_state (
	pgm_peer_t*		peer,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	pgm_assert (NULL != peer);

	pgm_debug ("nak_rpt_state (peer:%p now:%" PGM_TIME_FORMAT ")",
		(void*)peer, now);

	pgm_rxw_t* window = peer->window;
	pgm_transport_t* transport = peer->transport;
	pgm_list_t* list = window->wait_ncf_queue.tail;

	unsigned dropped_invalid = 0;
	unsigned dropped = 0;

/* have not learned this peers NLA */
	const bool is_valid_nla = 0 != peer->nla.ss_family;

	while (list)
	{
		pgm_list_t* next_list_el	= list->prev;
		struct pgm_sk_buff_t* skb	= (struct pgm_sk_buff_t*)list;
		pgm_rxw_state_t* state		= (pgm_rxw_state_t*)&skb->cb;

/* check this packet for state expiration */
		if (pgm_time_after_eq (now, state->nak_rpt_expiry))
		{
			if (PGM_UNLIKELY(!is_valid_nla)) {
				dropped_invalid++;
				pgm_rxw_lost (window, skb->sequence);
/* mark receiver window for flushing on next recv() */
				pgm_peer_set_pending (transport, peer);
				list = next_list_el;
				continue;
			}

			if (++state->ncf_retry_count >= transport->nak_ncf_retries)
			{
				dropped++;
				cancel_skb (transport, peer, skb, now);
				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_NCF_RETRIES_EXCEEDED]++;
			}
			else
			{
/* retry */
//				state->nak_rb_expiry = pkt->nak_rpt_expiry + nak_rb_ivl(transport);
				state->nak_rb_expiry = now + nak_rb_ivl (transport);
				pgm_rxw_state (window, skb, PGM_PKT_STATE_BACK_OFF);
				pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("NCF retry #%u attempt %u/%u."), skb->sequence, state->ncf_retry_count, transport->nak_ncf_retries);
			}
		}
		else
		{
/* packet expires some time later */
			pgm_trace(PGM_LOG_ROLE_RX_WINDOW,_("NCF retry #%u is delayed %f seconds."),
				skb->sequence, pgm_to_secsf (state->nak_rpt_expiry - now));
			break;
		}
		
		list = next_list_el;
	}

	if (window->wait_ncf_queue.length == 0)
	{
		pgm_assert ((pgm_rxw_state_t*)window->wait_ncf_queue.head == NULL);
		pgm_assert ((pgm_rxw_state_t*)window->wait_ncf_queue.tail == NULL);
	}
	else
	{
		pgm_assert ((pgm_rxw_state_t*)window->wait_ncf_queue.head != NULL);
		pgm_assert ((pgm_rxw_state_t*)window->wait_ncf_queue.tail != NULL);
	}

	if (PGM_UNLIKELY(dropped_invalid)) {
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Dropped %u messages due to invalid NLA."), dropped_invalid);
	}

	if (PGM_UNLIKELY(dropped)) {
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Dropped %u messages due to ncf cancellation, "
				"rxw_sqns %" PRIu32
				" bo %" PRIu32
				" ncf %" PRIu32
				" wd %" PRIu32
				" lost %" PRIu32
				" frag %" PRIu32),
				dropped,
				pgm_rxw_length (window),
				window->backoff_queue.length,
				window->wait_ncf_queue.length,
				window->wait_data_queue.length,
				window->lost_count,
				window->fragment_count);
	}

/* mark receiver window for flushing on next recv() */
	if (PGM_UNLIKELY(window->cumulative_losses != peer->last_cumulative_losses &&
	    !peer->pending_link.data))
	{
		transport->is_reset = TRUE;
		peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
		peer->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (transport, peer);
	}

	if (window->wait_ncf_queue.tail)
	{
		if (next_nak_rpt_expiry (window) > now)
		{
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Next expiry set in %f seconds."), pgm_to_secsf (next_nak_rpt_expiry (window) - now));
		} else {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Next expiry set in -%f seconds."), pgm_to_secsf (now - next_nak_rpt_expiry (window)));
		}
	}
	else
	{
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Wait ncf queue empty."));
	}
}

/* check WAIT_DATA_STATE, on expiration move back to BACK-OFF_STATE, on exceeding NAK_DATA_RETRIES
 * canel the sequence number.
 */
static
void
nak_rdata_state (
	pgm_peer_t*		peer,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	pgm_assert (NULL != peer);

	pgm_debug ("nak_rdata_state (peer:%p now:%" PGM_TIME_FORMAT ")",
		(const void*)peer, now);

	pgm_rxw_t* window = peer->window;
	pgm_transport_t* transport = peer->transport;
	pgm_list_t* list = window->wait_data_queue.tail;

	unsigned dropped_invalid = 0;
	unsigned dropped = 0;

/* have not learned this peers NLA */
	const bool is_valid_nla = 0 != peer->nla.ss_family;

	while (list)
	{
		pgm_list_t* next_list_el	= list->prev;
		struct pgm_sk_buff_t* rdata_skb	= (struct pgm_sk_buff_t*)list;
		pgm_assert (NULL != rdata_skb);
		pgm_rxw_state_t* rdata_state	= (pgm_rxw_state_t*)&rdata_skb->cb;

/* check this packet for state expiration */
		if (pgm_time_after_eq (now, rdata_state->nak_rdata_expiry))
		{
			if (PGM_UNLIKELY(!is_valid_nla)) {
				dropped_invalid++;
				pgm_rxw_lost (window, rdata_skb->sequence);
/* mark receiver window for flushing on next recv() */
				pgm_peer_set_pending (transport, peer);
				list = next_list_el;
				continue;
			}

			if (++rdata_state->data_retry_count >= transport->nak_data_retries)
			{
				dropped++;
				cancel_skb (transport, peer, rdata_skb, now);
				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_DATA_RETRIES_EXCEEDED]++;
				list = next_list_el;
				continue;
			}

//			rdata_state->nak_rb_expiry = rdata_pkt->nak_rdata_expiry + nak_rb_ivl(transport);
			rdata_state->nak_rb_expiry = now + nak_rb_ivl (transport);
			pgm_rxw_state (window, rdata_skb, PGM_PKT_STATE_BACK_OFF);

/* retry back to back-off state */
			pgm_trace(PGM_LOG_ROLE_RX_WINDOW,_("Data retry #%u attempt %u/%u."), rdata_skb->sequence, rdata_state->data_retry_count, transport->nak_data_retries);
		}
		else
		{	/* packet expires some time later */
			break;
		}
		

		list = next_list_el;
	}

	if (window->wait_data_queue.length == 0)
	{
		pgm_assert (NULL == (pgm_rxw_state_t*)window->wait_data_queue.head);
		pgm_assert (NULL == (pgm_rxw_state_t*)window->wait_data_queue.tail);
	}
	else
	{
		pgm_assert (NULL != (pgm_rxw_state_t*)window->wait_data_queue.head);
		pgm_assert (NULL != (pgm_rxw_state_t*)window->wait_data_queue.tail);
	}

	if (PGM_UNLIKELY(dropped_invalid)) {
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Dropped %u messages due to invalid NLA."), dropped_invalid);
	}

	if (PGM_UNLIKELY(dropped)) {
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Dropped %u messages due to data cancellation."), dropped);
	}

/* mark receiver window for flushing on next recv() */
	if (PGM_UNLIKELY(window->cumulative_losses != peer->last_cumulative_losses &&
	    !peer->pending_link.data))
	{
		transport->is_reset = TRUE;
		peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
		peer->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (transport, peer);
	}

	if (window->wait_data_queue.tail) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Next expiry set in %f seconds."), pgm_to_secsf (next_nak_rdata_expiry (window) - now));
	} else {
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Wait data queue empty."));
	}
}

/* ODATA or RDATA packet with any of the following options:
 *
 * OPT_FRAGMENT - this TPDU part of a larger APDU.
 *
 * Ownership of skb is taken and must be passed to the receive window or destroyed.
 *
 * returns TRUE is skb has been replaced, FALSE is remains unchanged and can be recycled.
 */

bool
pgm_on_data (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != source);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_data (transport:%p source:%p skb:%p)",
		(void*)transport, (void*)source, (void*)skb);

	unsigned msg_count = 0;
	const pgm_time_t nak_rb_expiry = skb->tstamp + nak_rb_ivl (transport);
	const unsigned tsdu_length = ntohs (skb->pgm_header->pgm_tsdu_length);

	skb->pgm_data = skb->data;

	const unsigned opt_total_length = (skb->pgm_header->pgm_options & PGM_OPT_PRESENT) ? ntohs(*(uint16_t*)( (char*)( skb->pgm_data + 1 ) + sizeof(uint16_t))) : 0;

/* advance data pointer to payload */
	pgm_skb_pull (skb, sizeof(struct pgm_data) + opt_total_length);

	if (opt_total_length > 0)
		 get_opt_fragment ((void*)(skb->pgm_data + 1), &skb->pgm_opt_fragment);

	const int add_status = pgm_rxw_add (source->window, skb, skb->tstamp, nak_rb_expiry);

/* skb reference is now invalid */
	bool flush_naks = FALSE;

	switch (add_status) {
	case PGM_RXW_MISSING:
		flush_naks = TRUE;
/* fall through */
	case PGM_RXW_INSERTED:
	case PGM_RXW_APPENDED:
		msg_count++;
		break;

	case PGM_RXW_DUPLICATE:
		source->cumulative_stats[PGM_PC_RECEIVER_DUP_DATAS]++;
		goto discarded;

	case PGM_RXW_MALFORMED:
		source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_ODATA]++;
/* fall through */
	case PGM_RXW_BOUNDS:
discarded:
		return FALSE;

	default: pgm_assert_not_reached(); break;
	}

/* valid data */
	PGM_HISTOGRAM_COUNTS("Rx.DataBytesReceived", tsdu_length);
	source->cumulative_stats[PGM_PC_RECEIVER_DATA_BYTES_RECEIVED] += tsdu_length;
	source->cumulative_stats[PGM_PC_RECEIVER_DATA_MSGS_RECEIVED]  += msg_count;

	if (flush_naks) {
/* flush out 1st time nak packets */
		pgm_timer_lock (transport);
		if (pgm_time_after (transport->next_poll, nak_rb_expiry))
			transport->next_poll = nak_rb_expiry;
		pgm_timer_unlock (transport);
	}
	return TRUE;
}

/* POLLs are generated by PGM Parents (Sources or Network Elements).
 *
 * returns TRUE on valid packet, FALSE on invalid packet.
 */

bool
pgm_on_poll (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != source);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_poll (transport:%p source:%p skb:%p)",
		(void*)transport, (void*)source, (void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_poll (skb))) {
		pgm_trace(PGM_LOG_ROLE_NETWORK,_("Discarded invalid POLL."));
		return FALSE;
	}

	struct pgm_poll*  poll4 = (struct pgm_poll*) skb->data;
	struct pgm_poll6* poll6 = (struct pgm_poll6*)skb->data;
	uint32_t poll_rand;
	memcpy (&poll_rand, (AFI_IP6 == ntohs (poll4->poll_nla_afi)) ? poll6->poll6_rand : poll4->poll_rand, sizeof(poll_rand));
	const uint32_t poll_mask = (AFI_IP6 == ntohs (poll4->poll_nla_afi)) ? ntohl (poll6->poll6_mask) : ntohl (poll4->poll_mask);

/* Check for probability match */
	if (poll_mask &&
	    (transport->rand_node_id & poll_mask) != poll_rand)
	{
/* discard early */
		return FALSE;
	}

/* scoped per path nla
 * TODO: manage list of pollers per peer
 */
	const uint32_t poll_sqn   = ntohl (poll4->poll_sqn);
	const uint16_t poll_round = ntohs (poll4->poll_round);

/* Check for new poll round */
	if (poll_round &&
	    poll_sqn != source->last_poll_sqn)
	{
		return FALSE;
	}

/* save sequence and round of valid poll */
	source->last_poll_sqn   = poll_sqn;
	source->last_poll_round = poll_round;

	const uint16_t poll_s_type = ntohs (poll4->poll_s_type);

/* Check poll type */
	switch (poll_s_type) {
	case PGM_POLL_GENERAL:
		return on_general_poll (transport, source, skb);

	case PGM_POLL_DLR:
		return on_dlr_poll (transport, source, skb);

	default:
/* unknown sub-type, discard */
		break;
	}

	return FALSE;
}

/* Used to count PGM children */

static
bool
on_general_poll (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	struct pgm_sk_buff_t* const	skb
	)
{
	struct pgm_poll*  poll4 = (struct pgm_poll*) skb->data;
	struct pgm_poll6* poll6 = (struct pgm_poll6*)skb->data;

/* TODO: cancel any pending poll-response */

/* defer response based on provided back-off interval */
	const uint32_t poll_bo_ivl = (AFI_IP6 == ntohs (poll4->poll_nla_afi)) ? ntohl (poll6->poll6_bo_ivl) : ntohl (poll4->poll_bo_ivl);
	source->polr_expiry = skb->tstamp + pgm_rand_int_range (&transport->rand_, 0, poll_bo_ivl);
	pgm_nla_to_sockaddr (&poll4->poll_nla_afi, (struct sockaddr*)&source->poll_nla);
/* TODO: schedule poll-response */

	return TRUE;
}

/* Used to count off-tree DLRs */

static
bool
on_dlr_poll (
	PGM_GNUC_UNUSED pgm_transport_t* const		transport,
	PGM_GNUC_UNUSED pgm_peer_t* const			source,
	PGM_GNUC_UNUSED struct pgm_sk_buff_t* const	skb
	)
{
/* we are not a DLR */
	return FALSE;
}

/* eof */
