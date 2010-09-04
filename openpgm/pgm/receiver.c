/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM receiver socket.
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
#ifdef _MSC_VER
#	include <pgm/wininttypes.h>
#else
#	include <inttypes.h>
#endif
#include <errno.h>
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/receiver.h>
#include <impl/sqn_list.h>
#include <impl/timer.h>
#include <impl/packet_parse.h>
#include <impl/net.h>


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

static bool send_spmr (pgm_sock_t*const restrict, pgm_peer_t*const restrict);
static bool send_nak (pgm_sock_t*const restrict, pgm_peer_t*const restrict, const uint32_t);
static bool send_parity_nak (pgm_sock_t*const restrict, pgm_peer_t*const restrict, const unsigned, const unsigned);
static bool send_nak_list (pgm_sock_t*const restrict, pgm_peer_t*const restrict, const struct pgm_sqn_list_t*const restrict);
static bool nak_rb_state (pgm_peer_t*, const pgm_time_t);
static void nak_rpt_state (pgm_peer_t*, const pgm_time_t);
static void nak_rdata_state (pgm_peer_t*, const pgm_time_t);
static inline pgm_peer_t* _pgm_peer_ref (pgm_peer_t*);
static bool on_general_poll (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_sk_buff_t*const restrict);
static bool on_dlr_poll (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_sk_buff_t*const restrict);


/* helpers for pgm_peer_t */
static inline
pgm_time_t
next_ack_rb_expiry (
	const pgm_rxw_t*	window
	)
{
	pgm_assert (NULL != window);
	pgm_assert (NULL != window->ack_backoff_queue.tail);

	const struct pgm_peer_t* peer = (const struct pgm_peer_t*)window->ack_backoff_queue.tail;
	pgm_assert (peer->sock->use_pgmcc);
	return peer->ack_rb_expiry;
}

static inline
pgm_time_t
next_nak_rb_expiry (
	const pgm_rxw_t*	window
	)
{
	pgm_assert (NULL != window);
	pgm_assert (NULL != window->nak_backoff_queue.tail);

	const struct pgm_sk_buff_t* skb = (const struct pgm_sk_buff_t*)window->nak_backoff_queue.tail;
	const pgm_rxw_state_t* state = (const pgm_rxw_state_t*)&skb->cb;
	return state->timer_expiry;
}

static inline
pgm_time_t
next_nak_rpt_expiry (
	const pgm_rxw_t*	window
	)
{
	pgm_assert (NULL != window);
	pgm_assert (NULL != window->wait_ncf_queue.tail);

	const struct pgm_sk_buff_t* skb = (const struct pgm_sk_buff_t*)window->wait_ncf_queue.tail;
	const pgm_rxw_state_t* state = (const pgm_rxw_state_t*)&skb->cb;
	return state->timer_expiry;
}

static inline
pgm_time_t
next_nak_rdata_expiry (
	const pgm_rxw_t*	window
	)
{
	pgm_assert (NULL != window);
	pgm_assert (NULL != window->wait_data_queue.tail);

	const struct pgm_sk_buff_t* skb = (const struct pgm_sk_buff_t*)window->wait_data_queue.tail;
	const pgm_rxw_state_t* state = (const pgm_rxw_state_t*)&skb->cb;
	return state->timer_expiry;
}

/* calculate ACK_RB_IVL.
 */
static inline
uint32_t
ack_rb_ivl (
	pgm_sock_t*		sock
	)	/* not const as rand() updates the seed */
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (sock->use_pgmcc);
	pgm_assert_cmpuint (sock->ack_bo_ivl, >, 1);

	return pgm_rand_int_range (&sock->rand_, 1 /* us */, sock->ack_bo_ivl);
}

/* calculate NAK_RB_IVL as random time interval 1 - NAK_BO_IVL.
 */
static inline
uint32_t
nak_rb_ivl (
	pgm_sock_t*		sock
	)	/* not const as rand() updates the seed */
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert_cmpuint (sock->nak_bo_ivl, >, 1);

	return pgm_rand_int_range (&sock->rand_, 1 /* us */, sock->nak_bo_ivl);
}

/* mark sequence as recovery failed.
 */

static
void
cancel_skb (
	pgm_sock_t*	    	    restrict sock,
	pgm_peer_t*		    restrict peer,
	const struct pgm_sk_buff_t* restrict skb,
	const pgm_time_t		     now
	)
{
	pgm_assert (NULL != sock);
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
	pgm_peer_set_pending (sock, peer);
}

/* check whether this receiver is the designated acker for the source
 */

static inline
bool
_pgm_is_acker (
	const pgm_peer_t*	    restrict peer,
	const struct pgm_sk_buff_t* restrict skb
	)
{
	struct sockaddr_storage acker_nla;

/* pre-conditions */
	pgm_assert (NULL != peer);
	pgm_assert (peer->sock->use_pgmcc);
	pgm_assert (NULL != skb);
	pgm_assert (NULL != skb->pgm_opt_pgmcc_data);

	pgm_nla_to_sockaddr (&skb->pgm_opt_pgmcc_data->opt_nla_afi, (struct sockaddr*)&acker_nla);
	return (0 == pgm_sockaddr_cmp ((struct sockaddr*)&acker_nla, (struct sockaddr*)&peer->sock->send_addr));
}

/* is the source holding an acker election
 */

static inline
bool
_pgm_is_acker_election (
	const struct pgm_sk_buff_t* restrict skb
	)
{
	pgm_assert (NULL != skb);
	pgm_assert (NULL != skb->pgm_opt_pgmcc_data);

	const unsigned acker_afi = ntohs (skb->pgm_opt_pgmcc_data->opt_nla_afi);
	switch (acker_afi) {
	case AFI_IP:
		if (INADDR_ANY == skb->pgm_opt_pgmcc_data->opt_nla.s_addr)
			return TRUE;
		break;

	case AFI_IP6:
		if (0 == memcmp (&skb->pgm_opt_pgmcc_data->opt_nla, &in6addr_any, sizeof(in6addr_any)))
			return TRUE;
		break;

	default: break;
	}

	return FALSE;
}

/* add state for an ACK on a data packet.
 */

static inline
void
_pgm_add_ack (
	pgm_peer_t*	      const restrict peer,
	const pgm_time_t		     ack_rb_expiry
	)
{
	pgm_assert (NULL != peer);
	pgm_assert (peer->sock->use_pgmcc);

	peer->ack_rb_expiry = ack_rb_expiry;
	pgm_queue_push_head_link (&peer->window->ack_backoff_queue, &peer->ack_link);
}

/* remove outstanding ACK
 */

static inline
void
_pgm_remove_ack (
	pgm_peer_t*	      const restrict peer
	)
{
	pgm_assert (NULL != peer);
	pgm_assert (peer->sock->use_pgmcc);
	pgm_assert (!pgm_queue_is_empty (&peer->window->ack_backoff_queue));

	pgm_queue_unlink (&peer->window->ack_backoff_queue, &peer->ack_link);
	peer->ack_rb_expiry = 0;
}

/* increase reference count for peer object
 *
 * on success, returns peer object.
 */

static inline
pgm_peer_t*
_pgm_peer_ref (
	pgm_peer_t*		peer
	)
{
/* pre-conditions */
	pgm_assert (NULL != peer);

	pgm_atomic_inc32 (&peer->ref_count);
	return peer;
}

/* decrease reference count of peer object, destroying on last reference.
 */

void
pgm_peer_unref (
	pgm_peer_t*		peer
	)
{
/* pre-conditions */
	pgm_assert (NULL != peer);

	if (pgm_atomic_exchange_and_add32 (&peer->ref_count, (uint32_t)-1) != 1)
		return;

/* receive window */
	pgm_rxw_destroy (peer->window);
	peer->window = NULL;

/* object */
	pgm_free (peer);
	peer = NULL;
}

/* find PGM options in received SKB.
 *
 * returns TRUE if opt_fragment is found, otherwise FALSE is returned.
 */

static
bool
get_pgm_options (
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);
	pgm_assert (NULL != skb->pgm_data);

	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(skb->pgm_data + 1);
	bool found_opt = FALSE;

	pgm_assert (opt_header->opt_type   == PGM_OPT_LENGTH);
	pgm_assert (opt_header->opt_length == sizeof(struct pgm_opt_length));

	pgm_debug ("get_pgm_options (skb:%p)",
		(const void*)skb);

	skb->pgm_opt_fragment = NULL;
	skb->pgm_opt_pgmcc_data = NULL;

/* always at least two options, first is always opt_length */
	do {
		opt_header = (struct pgm_opt_header*)((char*)opt_header + opt_header->opt_length);
/* option overflow */
		if (PGM_UNLIKELY((char*)opt_header > (char*)skb->data))
			break;

		switch (opt_header->opt_type & PGM_OPT_MASK) {
		case PGM_OPT_FRAGMENT:
			skb->pgm_opt_fragment = (struct pgm_opt_fragment*)(opt_header + 1);
			found_opt = TRUE;
			break;

		case PGM_OPT_PGMCC_DATA:
			skb->pgm_opt_pgmcc_data = (struct pgm_opt_pgmcc_data*)(opt_header + 1);
			found_opt = TRUE;
			break;

		default: break;
		}

	} while (!(opt_header->opt_type & PGM_OPT_END));
	return found_opt;
}

/* a peer in the context of the sock is another party on the network sending PGM
 * packets.  for each peer we need a receive window and network layer address (nla) to
 * which nak requests can be forwarded to.
 *
 * on success, returns new peer object.
 */

pgm_peer_t*
pgm_new_peer (
	pgm_sock_t*            const restrict sock,
	const pgm_tsi_t*       const restrict tsi,
	const struct sockaddr* const restrict src_addr,
	const socklen_t			      src_addrlen,
	const struct sockaddr* const restrict dst_addr,
	const socklen_t			      dst_addrlen,
	const pgm_time_t		      now
	)
{
	pgm_peer_t* peer;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != src_addr);
	pgm_assert (src_addrlen > 0);
	pgm_assert (NULL != dst_addr);
	pgm_assert (dst_addrlen > 0);

#ifdef PGM_DEBUG
	char saddr[INET6_ADDRSTRLEN], daddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (src_addr, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (dst_addr, daddr, sizeof(daddr));
	pgm_debug ("pgm_new_peer (sock:%p tsi:%s src-addr:%s src-addrlen:%u dst-addr:%s dst-addrlen:%u)",
		(void*)sock, pgm_tsi_print (tsi), saddr, (unsigned)src_addrlen, daddr, (unsigned)dst_addrlen);
#endif

	peer = pgm_new0 (pgm_peer_t, 1);
	peer->expiry = now + sock->peer_expiry;
	peer->sock = sock;
	memcpy (&peer->tsi, tsi, sizeof(pgm_tsi_t));
	memcpy (&peer->group_nla, dst_addr, dst_addrlen);
	memcpy (&peer->local_nla, src_addr, src_addrlen);
/* port at same location for sin/sin6 */
	((struct sockaddr_in*)&peer->local_nla)->sin_port = htons (sock->udp_encap_ucast_port);
	((struct sockaddr_in*)&peer->nla)->sin_port       = htons (sock->udp_encap_ucast_port);

/* lock on rx window */
	peer->window = pgm_rxw_create (&peer->tsi,
					sock->max_tpdu,
					sock->rxw_sqns,
					sock->rxw_secs,
					sock->rxw_max_rte,
					sock->ack_c_p);
	peer->spmr_expiry = now + sock->spmr_expiry;

/* add peer to hash table and linked list */
	pgm_rwlock_writer_lock (&sock->peers_lock);
	pgm_peer_t* entry = _pgm_peer_ref (peer);
	pgm_hashtable_insert (sock->peers_hashtable, &peer->tsi, entry);
	peer->peers_link.data = peer;
	sock->peers_list = pgm_list_prepend_link (sock->peers_list, &peer->peers_link);
	pgm_rwlock_writer_unlock (&sock->peers_lock);

	pgm_timer_lock (sock);
	if (pgm_time_after( sock->next_poll, peer->spmr_expiry ))
		sock->next_poll = peer->spmr_expiry;
	pgm_timer_unlock (sock);
	return peer;
}

/* copy any contiguous buffers in the peer list to the provided 
 * message vector.
 * returns -ENOBUFS if the vector is full, returns -ECONNRESET if
 * data loss is detected, returns 0 when all peers flushed.
 */

int
pgm_flush_peers_pending (
	pgm_sock_t* 	 	 const restrict	sock,
	struct pgm_msgv_t**    	       restrict	pmsg,
	const struct pgm_msgv_t* const		msg_end,	/* at least pmsg + 1, same object */
	size_t*		 	 const restrict	bytes_read,	/* added to, not set */
	unsigned*	 	 const restrict	data_read
	)
{
	int retval = 0;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != pmsg);
	pgm_assert (NULL != *pmsg);
	pgm_assert (NULL != msg_end);
	pgm_assert (NULL != bytes_read);
	pgm_assert (NULL != data_read);

	pgm_debug ("pgm_flush_peers_pending (sock:%p pmsg:%p msg-end:%p bytes-read:%p data-read:%p)",
		(const void*)sock, (const void*)pmsg, (const void*)msg_end, (const void*)bytes_read, (const void*)data_read);

	while (sock->peers_pending)
	{
		pgm_peer_t* peer = sock->peers_pending->data;
		if (peer->last_commit && peer->last_commit < sock->last_commit)
			pgm_rxw_remove_commit (peer->window);
		const ssize_t peer_bytes = pgm_rxw_readv (peer->window, pmsg, msg_end - *pmsg + 1);

		if (peer->last_cumulative_losses != ((pgm_rxw_t*)peer->window)->cumulative_losses)
		{
			sock->is_reset = TRUE;
			peer->lost_count = ((pgm_rxw_t*)peer->window)->cumulative_losses - peer->last_cumulative_losses;
			peer->last_cumulative_losses = ((pgm_rxw_t*)peer->window)->cumulative_losses;
		}
	
		if (peer_bytes >= 0)
		{
			(*bytes_read) += peer_bytes;
			(*data_read)  ++;
			peer->last_commit = sock->last_commit;
			if (*pmsg > msg_end) {			/* commit full */
				retval = -ENOBUFS;
				break;
			}
		} else
			peer->last_commit = 0;
		if (PGM_UNLIKELY(sock->is_reset)) {
			retval = -ECONNRESET;
			break;
		}
/* clear this reference and move to next */
		sock->peers_pending = pgm_slist_remove_first (sock->peers_pending);
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
	pgm_sock_t* const restrict sock,
	pgm_peer_t* const restrict peer
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != peer);

	if (peer->pending_link.data) return;
	peer->pending_link.data = peer;
	sock->peers_pending = pgm_slist_prepend_link (sock->peers_pending, &peer->pending_link);
}

/* Create a new error SKB detailing data loss.
 */

void
pgm_set_reset_error (
	pgm_sock_t*	   const restrict sock,
	pgm_peer_t*	   const restrict source,
	struct pgm_msgv_t* const restrict msgv
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != source);
	pgm_assert (NULL != msgv);

	struct pgm_sk_buff_t* error_skb = pgm_alloc_skb (0);
	error_skb->sock	= sock;
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
	pgm_sock_t*	      const restrict sock,
	pgm_peer_t*	      const restrict source,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != source);
	pgm_assert (NULL != skb);

	pgm_info("pgm_on_spm (sock:%p source:%p skb:%p)",
		(const void*)sock, (const void*)source, (const void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_spm (skb))) {
		pgm_trace(PGM_LOG_ROLE_NETWORK,_("Discarded invalid SPM."));
		source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
		return FALSE;
	}

	const struct pgm_spm*  spm  = (struct pgm_spm*) skb->data;
	const struct pgm_spm6* spm6 = (struct pgm_spm6*)skb->data;
	const uint32_t spm_sqn = ntohl (spm->spm_sqn);

/* check for advancing sequence number, or first SPM */
	if (PGM_LIKELY(pgm_uint32_gte (spm_sqn, source->spm_sqn)))
	{
/* copy NLA for replies */
		pgm_nla_to_sockaddr (&spm->spm_nla_afi, (struct sockaddr*)&source->nla);

/* save sequence number */
		source->spm_sqn = spm_sqn;

/* update receive window */
		const pgm_time_t nak_rb_expiry = skb->tstamp + nak_rb_ivl (sock);
		const unsigned naks = pgm_rxw_update (source->window,
						      ntohl (spm->spm_lead),
						      ntohl (spm->spm_trail),
						      skb->tstamp,
						      nak_rb_expiry);
		if (naks) {
			pgm_timer_lock (sock);
			if (pgm_time_after (sock->next_poll, nak_rb_expiry))
				sock->next_poll = nak_rb_expiry;
			pgm_timer_unlock (sock);
		}

/* mark receiver window for flushing on next recv() */
		const pgm_rxw_t* window = source->window;
		if (window->cumulative_losses != source->last_cumulative_losses &&
		    !source->pending_link.data)
		{
			sock->is_reset = TRUE;
			source->lost_count = window->cumulative_losses - source->last_cumulative_losses;
			source->last_cumulative_losses = window->cumulative_losses;
			pgm_peer_set_pending (sock, source);
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
	source->expiry = skb->tstamp + sock->peer_expiry;
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
	pgm_sock_t*           const restrict sock,
	pgm_peer_t*	      const restrict peer,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != peer);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_peer_nak (sock:%p peer:%p skb:%p)",
		(const void*)sock, (const void*)peer, (const void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_nak (skb)))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded invalid multicast NAK."));
		peer->cumulative_stats[PGM_PC_RECEIVER_NAK_ERRORS]++;
		return FALSE;
	}

	const struct pgm_nak*  nak  = (struct pgm_nak*) skb->data;
	const struct pgm_nak6* nak6 = (struct pgm_nak6*)skb->data;
		
/* NAK_SRC_NLA must not contain our sock unicast NLA */
	struct sockaddr_storage nak_src_nla;
	pgm_nla_to_sockaddr (&nak->nak_src_nla_afi, (struct sockaddr*)&nak_src_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nak_src_nla, (struct sockaddr*)&sock->send_addr) == 0))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded multicast NAK on NLA mismatch."));
		return FALSE;
	}

/* NAK_GRP_NLA contains one of our sock receive multicast groups: the sources send multicast group */ 
	struct sockaddr_storage nak_grp_nla;
	pgm_nla_to_sockaddr ((AF_INET6 == nak_src_nla.ss_family) ? &nak6->nak6_grp_nla_afi : &nak->nak_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
	bool found = FALSE;
	for (unsigned i = 0; i < sock->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_grp_nla, (struct sockaddr*)&sock->recv_gsr[i].gsr_group) == 0)
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
				      skb->tstamp + sock->nak_rdata_ivl,
				      skb->tstamp + nak_rb_ivl(sock));
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
					  skb->tstamp + sock->nak_rdata_ivl,
					  skb->tstamp + nak_rb_ivl(sock));
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
		sock->is_reset = TRUE;
		peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
		peer->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (sock, peer);
	}
	return TRUE;
}

/* NCF confirming receipt of a NAK from this sock or another on the LAN segment.
 *
 * Packet contents will match exactly the sent NAK, although not really that helpful.
 *
 * if NCF is valid, returns TRUE.  on error, FALSE is returned.
 */

bool
pgm_on_ncf (
	pgm_sock_t*           const restrict sock,
	pgm_peer_t*	      const restrict source,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != source);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_ncf (sock:%p source:%p skb:%p)",
		(const void*)sock, (const void*)source, (const void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_ncf (skb)))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded invalid NCF."));
		source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
		return FALSE;
	}

	const struct pgm_nak*  ncf  = (struct pgm_nak*) skb->data;
	const struct pgm_nak6* ncf6 = (struct pgm_nak6*)skb->data;
		
/* NCF_SRC_NLA may contain our sock unicast NLA, we don't really care */
	struct sockaddr_storage ncf_src_nla;
	pgm_nla_to_sockaddr (&ncf->nak_src_nla_afi, (struct sockaddr*)&ncf_src_nla);

#if 0
	if (PGM(pgm_sockaddr_cmp ((struct sockaddr*)&ncf_src_nla, (struct sockaddr*)&sock->send_addr) != 0)) {
		g_trace ("INFO", "Discarded NCF on NLA mismatch.");
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		return FALSE;
	}
#endif

/* NCF_GRP_NLA contains our sock multicast group */ 
	struct sockaddr_storage ncf_grp_nla;
	pgm_nla_to_sockaddr ((AF_INET6 == ncf_src_nla.ss_family) ? &ncf6->nak6_grp_nla_afi : &ncf->nak_grp_nla_afi, (struct sockaddr*)&ncf_grp_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&ncf_grp_nla, (struct sockaddr*)&sock->send_gsr.gsr_group) != 0))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded NCF on multicast group mismatch."));
		return FALSE;
	}

	const pgm_time_t ncf_rdata_ivl = skb->tstamp + sock->nak_rdata_ivl;
	const pgm_time_t ncf_rb_ivl    = skb->tstamp + nak_rb_ivl(sock);
	int status = pgm_rxw_confirm (source->window,
				      ntohl (ncf->nak_sqn),
				      skb->tstamp,
				      ncf_rdata_ivl,
				      ncf_rb_ivl);
	if (PGM_RXW_UPDATED == status || PGM_RXW_APPENDED == status)
	{
		const pgm_time_t ncf_ivl = (PGM_RXW_APPENDED == status) ? ncf_rb_ivl : ncf_rdata_ivl;
		pgm_timer_lock (sock);
		if (pgm_time_after (sock->next_poll, ncf_ivl)) {
			sock->next_poll = ncf_ivl;
		}
		pgm_timer_unlock (sock);
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
		sock->is_reset = TRUE;
		source->lost_count = window->cumulative_losses - source->last_cumulative_losses;
		source->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (sock, source);
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
	pgm_sock_t* const restrict sock,
	pgm_peer_t* const restrict source
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != source);

	pgm_info ("send_spmr (sock:%p source:%p)",
		(const void*)sock, (const void*)source);

	const size_t tpdu_length = sizeof(struct pgm_header);
	char buf[ tpdu_length ];
	struct pgm_header* header = (struct pgm_header*)buf;
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));
/* dport & sport reversed communicating upstream */
	header->pgm_sport	= sock->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type	= PGM_SPMR;
	header->pgm_options	= 0;
	header->pgm_tsdu_length	= 0;
	header->pgm_checksum	= 0;
	header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

/* send multicast SPMR TTL 1 */
	pgm_sockaddr_multicast_hops (sock->send_sock, sock->send_gsr.gsr_group.ss_family, 1);
	ssize_t sent = pgm_sendto (sock,
				   FALSE,			/* not rate limited */
				   FALSE,			/* regular socket */
				   header,
				   tpdu_length,
				   (struct sockaddr*)&sock->send_gsr.gsr_group,
				   pgm_sockaddr_len ((struct sockaddr*)&sock->send_gsr.gsr_group));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno))
		return FALSE;

/* send unicast SPMR with regular TTL */
	pgm_sockaddr_multicast_hops (sock->send_sock, sock->send_gsr.gsr_group.ss_family, sock->hops);
	sent = pgm_sendto (sock,
			   FALSE,
			   FALSE,
			   header,
			   tpdu_length,
			   (struct sockaddr*)&source->local_nla,
			   pgm_sockaddr_len ((struct sockaddr*)&source->local_nla));
	if (sent < 0 && EAGAIN == errno)
		return FALSE;

	sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += tpdu_length * 2;
	return TRUE;
}

/* send selective NAK for one sequence number.
 *
 * on success, TRUE is returned, returns FALSE if would block on operation.
 */

static
bool
send_nak (
	pgm_sock_t* const restrict sock,
	pgm_peer_t* const restrict source,
	const uint32_t		   sequence
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != source);

	pgm_debug ("send_nak (sock:%p peer:%p sequence:%" PRIu32 ")",
		(void*)sock, (void*)source, sequence);

	size_t tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	if (AF_INET6 == source->nla.ss_family)
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	char buf[ tpdu_length ];
	struct pgm_header* header = (struct pgm_header*)buf;
	struct pgm_nak*  nak  = (struct pgm_nak* )(header + 1);
	struct pgm_nak6* nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= sock->dport;
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

	const ssize_t sent = pgm_sendto (sock,
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
	pgm_sock_t* const restrict sock,
	pgm_peer_t* const restrict source,
	const uint32_t		   nak_tg_sqn,	/* transmission group (shifted) */
	const uint32_t		   nak_pkt_cnt	/* count of parity packets to request */
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != source);
	pgm_assert (nak_pkt_cnt > 0);

	pgm_debug ("send_parity_nak (sock:%p source:%p nak-tg-sqn:%" PRIu32 " nak-pkt-cnt:%" PRIu32 ")",
		(void*)sock, (void*)source, nak_tg_sqn, nak_pkt_cnt);

	size_t tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	if (AF_INET6 == source->nla.ss_family)
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	char buf[ tpdu_length ];
	struct pgm_header* header = (struct pgm_header*)buf;
	struct pgm_nak*  nak  = (struct pgm_nak* )(header + 1);
	struct pgm_nak6* nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= sock->dport;
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

	const ssize_t sent = pgm_sendto (sock,
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
	pgm_sock_t*	     	     const restrict sock,
	pgm_peer_t*		     const restrict source,
	const struct pgm_sqn_list_t* const restrict sqn_list
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
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
	pgm_debug("send_nak_list (sock:%p source:%p sqn-list:[%s])",
		(const void*)sock, (const void*)source, list);
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
	header->pgm_sport	= sock->dport;
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

	const ssize_t sent = pgm_sendto (sock,
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

/* send ACK upstream to source
 *
 * on success, TRUE is returned.  on error, FALSE is returned.
 */

static
bool
send_ack (
	pgm_sock_t*           const restrict sock,
	pgm_peer_t*           const restrict source,
	const pgm_time_t		     now
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (sock->use_pgmcc);
	pgm_assert (NULL != source);

	pgm_debug ("send_ack (sock:%p source:%p now:%" PGM_TIME_FORMAT ")",
		(const void*)sock, (const void*)source, now);

	size_t tpdu_length = sizeof(struct pgm_header) +
			     sizeof(struct pgm_ack) +
			     sizeof(struct pgm_opt_length) +		/* includes header */
			     sizeof(struct pgm_opt_header) +
			     sizeof(struct pgm_opt_pgmcc_feedback);
	if (AF_INET6 == sock->send_addr.ss_family)
		tpdu_length += sizeof(struct pgm_opt6_pgmcc_feedback) - sizeof(struct pgm_opt_pgmcc_feedback);
	char buf[ tpdu_length ];
	if (PGM_UNLIKELY(pgm_mem_gc_friendly))
		memset (buf, 0, tpdu_length);
	struct pgm_header* header = (struct pgm_header*)buf;
	struct pgm_ack* ack = (struct pgm_ack*)(header + 1);
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for an ack */
	header->pgm_sport	= sock->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type	= PGM_ACK;
	header->pgm_options	= PGM_OPT_PRESENT;
	header->pgm_tsdu_length = 0;

/* ACK */
	ack->ack_rx_max		= htonl (pgm_rxw_lead (source->window));
	ack->ack_bitmap		= htonl (source->window->bitmap);

/* OPT_PGMCC_FEEDBACK */
	struct pgm_opt_length* opt_len = (struct pgm_opt_length*)(ack + 1);
	opt_len->opt_type	= PGM_OPT_LENGTH;
	opt_len->opt_length	= sizeof(struct pgm_opt_length);
	opt_len->opt_total_length = htons (	sizeof(struct pgm_opt_length) +
						sizeof(struct pgm_opt_header) +
						(AF_INET6 == sock->send_addr.ss_family) ?
							sizeof(struct pgm_opt6_pgmcc_feedback) :
							sizeof(struct pgm_opt_pgmcc_feedback) );
	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
	opt_header->opt_type	= PGM_OPT_PGMCC_FEEDBACK | PGM_OPT_END;
	opt_header->opt_length	= sizeof(struct pgm_opt_header) +
				  ( (AF_INET6 == sock->send_addr.ss_family) ?
					sizeof(struct pgm_opt6_pgmcc_feedback) :
					sizeof(struct pgm_opt_pgmcc_feedback) );
	struct pgm_opt_pgmcc_feedback* opt_pgmcc_feedback = (struct pgm_opt_pgmcc_feedback*)(opt_header + 1);
	opt_pgmcc_feedback->opt_reserved = 0;

	const uint32_t t = source->ack_last_tstamp + pgm_to_msecs( now - source->last_data_tstamp );
	opt_pgmcc_feedback->opt_tstamp = htonl (t);
	pgm_sockaddr_to_nla ((struct sockaddr*)&sock->send_addr, (char*)&opt_pgmcc_feedback->opt_nla_afi);
	opt_pgmcc_feedback->opt_loss_rate = htons ((uint16_t)source->window->data_loss);

	header->pgm_checksum	= 0;
	header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

	const ssize_t sent = pgm_sendto (sock,
					 FALSE,			/* not rate limited */
					 FALSE,			/* regular socket */
					 header,
					 tpdu_length,
					 (struct sockaddr*)&source->nla,
					 pgm_sockaddr_len((struct sockaddr*)&source->nla));
	if ( sent != (ssize_t)tpdu_length )
		return FALSE;

	source->cumulative_stats[PGM_PC_RECEIVER_ACKS_SENT]++;
	return TRUE;
}

/* check all receiver windows for ACKer elections, on expiration send an ACK.
 *
 * returns TRUE on success, returns FALSE if operation would block.
 */

static
bool
ack_rb_state (
	pgm_peer_t*		peer,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	pgm_assert (NULL != peer);
	pgm_assert (peer->sock->use_pgmcc);

	pgm_debug ("ack_rb_state (peer:%p now:%" PGM_TIME_FORMAT ")",
		(const void*)peer, now);

	pgm_rxw_t* window = peer->window;
	pgm_sock_t* sock = peer->sock;
	pgm_list_t* list;

	list = window->ack_backoff_queue.tail;
	if (!list) {
		pgm_assert (window->ack_backoff_queue.head == NULL);
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Backoff queue is empty in ack_rb_state."));
		return TRUE;
	} else {
		pgm_assert (window->ack_backoff_queue.head != NULL);
	}

/* have not learned this peers NLA */
	const bool is_valid_nla = (0 != peer->nla.ss_family);

	while (list)
	{
		pgm_list_t* next_list_el = list->prev;

/* check for ACK backoff expiration */
		if (pgm_time_after_eq(now, peer->ack_rb_expiry))
		{
/* unreliable delivery */
			_pgm_remove_ack (peer);

			if (PGM_UNLIKELY(!is_valid_nla)) {
				pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("Unable to send ACK due to unknown NLA."));
				list = next_list_el;
				continue;
			}

			pgm_assert (!pgm_sockaddr_is_addr_unspecified ((struct sockaddr*)&peer->nla));

			if (!send_ack (sock, peer, now))
				return FALSE;
		}
		else
		{	/* packet expires some time later */
			break;
		}

		list = next_list_el;
	}

	if (window->ack_backoff_queue.length == 0)
	{
		pgm_assert ((struct rxw_packet*)window->ack_backoff_queue.head == NULL);
		pgm_assert ((struct rxw_packet*)window->ack_backoff_queue.tail == NULL);
	}
	else
	{
		pgm_assert ((struct rxw_packet*)window->ack_backoff_queue.head != NULL);
		pgm_assert ((struct rxw_packet*)window->ack_backoff_queue.tail != NULL);
	}

	if (window->ack_backoff_queue.tail)
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Next expiry set in %f seconds."),
			pgm_to_secsf(next_ack_rb_expiry(window) - now));
	}
	else
	{
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("ACK backoff queue empty."));
	}
	return TRUE;
}

/* check all receiver windows for packets in BACK-OFF_STATE, on expiration send a NAK.
 * update sock::next_nak_rb_timestamp for next expiration time.
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
	pgm_sock_t* sock = peer->sock;
	pgm_list_t* list;
	struct pgm_sqn_list_t nak_list = { .len = 0 };

/* send all NAKs first, lack of data is blocking contiguous processing and its 
 * better to get the notification out a.s.a.p. even though it might be waiting
 * in a kernel queue.
 *
 * alternative: after each packet check for incoming data and return to the
 * event loop.  bias for shorter loops as retry count increases.
 */
	list = window->nak_backoff_queue.tail;
	if (!list) {
		pgm_assert (window->nak_backoff_queue.head == NULL);
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Backoff queue is empty in nak_rb_state."));
		return TRUE;
	} else {
		pgm_assert (window->nak_backoff_queue.head != NULL);
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
			if (pgm_time_after_eq (now, state->timer_expiry))
			{
				if (PGM_UNLIKELY(!is_valid_nla)) {
					dropped_invalid++;
					pgm_rxw_lost (window, skb->sequence);
/* mark receiver window for flushing on next recv() */
					pgm_peer_set_pending (sock, peer);
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
					state->timer_expiry += sock->nak_rpt_ivl;
					while (pgm_time_after_eq (now, state->timer_expiry)) {
						state->timer_expiry += sock->nak_rpt_ivl;
						state->ncf_retry_count++;
					}
#else
					state->timer_expiry = now + sock->nak_rpt_ivl;
#endif
					pgm_timer_lock (sock);
					if (pgm_time_after (sock->next_poll, state->timer_expiry))
						sock->next_poll = state->timer_expiry;
					pgm_timer_unlock (sock);
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

		if (nak_pkt_cnt && !send_parity_nak (sock, peer, nak_tg_sqn, nak_pkt_cnt))
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
			if (pgm_time_after_eq(now, state->timer_expiry))
			{
				if (PGM_UNLIKELY(!is_valid_nla)) {
pgm_info ("invalid nla");
					dropped_invalid++;
					pgm_rxw_lost (window, skb->sequence);
/* mark receiver window for flushing on next recv() */
					pgm_peer_set_pending (sock, peer);
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
				state->timer_expiry += sock->nak_rpt_ivl;
				while (pgm_time_after_eq(now, state->timer_expiry)){
					state->timer_expiry += sock->nak_rpt_ivl;
					state->ncf_retry_count++;
				}
#else
				state->timer_expiry = now + sock->nak_rpt_ivl;
pgm_trace(PGM_LOG_ROLE_NETWORK,_("nak_rpt_expiry in %f seconds."),
		pgm_to_secsf( state->timer_expiry - now ) );
#endif
				pgm_timer_lock (sock);
				if (pgm_time_after (sock->next_poll, state->timer_expiry))
					sock->next_poll = state->timer_expiry;
				pgm_timer_unlock (sock);

				if (nak_list.len == PGM_N_ELEMENTS(nak_list.sqn)) {
					if (sock->can_send_nak && !send_nak_list (sock, peer, &nak_list))
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

		if (sock->can_send_nak && nak_list.len)
		{
			if (nak_list.len > 1 && !send_nak_list (sock, peer, &nak_list))
				return FALSE;
			else if (!send_nak (sock, peer, nak_list.sqn[0]))
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
			sock->is_reset = TRUE;
			peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
			peer->last_cumulative_losses = window->cumulative_losses;
			pgm_peer_set_pending (sock, peer);
		}
	}

	if (window->nak_backoff_queue.length == 0)
	{
		pgm_assert ((struct rxw_packet*)window->nak_backoff_queue.head == NULL);
		pgm_assert ((struct rxw_packet*)window->nak_backoff_queue.tail == NULL);
	}
	else
	{
		pgm_assert ((struct rxw_packet*)window->nak_backoff_queue.head != NULL);
		pgm_assert ((struct rxw_packet*)window->nak_backoff_queue.tail != NULL);
	}

	if (window->nak_backoff_queue.tail)
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Next expiry set in %f seconds."),
			pgm_to_secsf(next_nak_rb_expiry(window) - now));
	}
	else
	{
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("NAK backoff queue empty."));
	}
	return TRUE;
}

/* check this peer for NAK state timers, uses the tail of each queue for the nearest
 * timer execution.
 *
 * returns TRUE on complete sweep, returns FALSE if operation would block.
 */

bool
pgm_check_peer_state (
	pgm_sock_t*const	sock,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);

	pgm_debug ("pgm_check_peer_state (sock:%p now:%" PGM_TIME_FORMAT ")",
		(const void*)sock, now);

	if (!sock->peers_list)
		return TRUE;

	pgm_list_t* list = sock->peers_list;
	do {
		pgm_list_t* next = list->next;
		pgm_peer_t* peer = list->data;
		pgm_rxw_t* window = peer->window;

		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (now, peer->spmr_expiry))
			{
				if (sock->can_send_nak) {
					if (!send_spmr (sock, peer)) {
						return FALSE;
					}
					peer->spmr_tstamp = now;
				}
				peer->spmr_expiry = 0;
			}
		}

		if (window->ack_backoff_queue.tail)
		{
			pgm_assert (sock->use_pgmcc);

			if (pgm_time_after_eq (now, next_ack_rb_expiry (window)))
				if (!ack_rb_state (peer, now)) {
					return FALSE;
				}
		}

		if (window->nak_backoff_queue.tail)
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
			if (peer->pending_link.data)
			{
				pgm_trace (PGM_LOG_ROLE_SESSION,_("Peer expiration postponed due to committing data, tsi %s"), pgm_tsi_print (&peer->tsi));
				peer->expiry += sock->peer_expiry;
			}
			else if (window->committed_count)
			{
				pgm_trace (PGM_LOG_ROLE_SESSION,_("Peer expiration postponed due to committed data, tsi %s"), pgm_tsi_print (&peer->tsi));
				peer->expiry += sock->peer_expiry;
			}
			else
			{
				pgm_trace (PGM_LOG_ROLE_SESSION,_("Peer expired, tsi %s"), pgm_tsi_print (&peer->tsi));
				pgm_hashtable_remove (sock->peers_hashtable, &peer->tsi);
				sock->peers_list = pgm_list_remove_link (sock->peers_list, &peer->peers_link);
				if (sock->last_hash_value == peer)
					sock->last_hash_value = NULL;
				pgm_peer_unref (peer);
			}
		}

		list = next;
	} while (list);

/* check for waiting contiguous packets */
	if (sock->peers_pending && !sock->is_pending_read)
	{
		pgm_debug ("prod rx thread");
		pgm_notify_send (&sock->pending_notify);
		sock->is_pending_read = TRUE;
	}
	return TRUE;
}

/* find the next state expiration time among the socks peers.
 *
 * on success, returns the earliest of the expiration parameter or next
 * peer expiration time.
 */

pgm_time_t
pgm_min_receiver_expiry (
	pgm_time_t	expiration,		/* absolute time */
	pgm_sock_t*	sock
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);

	pgm_debug ("pgm_min_receiver_expiry (expiration:%" PGM_TIME_FORMAT " sock:%p)",
		expiration, (const void*)sock);

	if (!sock->peers_list)
		return expiration;

	pgm_list_t* list = sock->peers_list;
	do {
		pgm_list_t* next = list->next;
		pgm_peer_t* peer = (pgm_peer_t*)list->data;
		pgm_rxw_t* window = peer->window;
	
		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (expiration, peer->spmr_expiry))
				expiration = peer->spmr_expiry;
		}

		if (window->ack_backoff_queue.tail)
		{
			pgm_assert (sock->use_pgmcc);
			if (pgm_time_after_eq (expiration, next_ack_rb_expiry (window)))
				expiration = next_ack_rb_expiry (window);
		}

		if (window->nak_backoff_queue.tail)
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
	pgm_sock_t* sock = peer->sock;
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
		if (pgm_time_after_eq (now, state->timer_expiry))
		{
			if (PGM_UNLIKELY(!is_valid_nla)) {
				dropped_invalid++;
				pgm_rxw_lost (window, skb->sequence);
/* mark receiver window for flushing on next recv() */
				pgm_peer_set_pending (sock, peer);
				list = next_list_el;
				continue;
			}

			if (++state->ncf_retry_count >= sock->nak_ncf_retries)
			{
				dropped++;
				cancel_skb (sock, peer, skb, now);
				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_NCF_RETRIES_EXCEEDED]++;
			}
			else
			{
/* retry */
//				state->timer_expiry += nak_rb_ivl(sock);
				state->timer_expiry = now + nak_rb_ivl (sock);
				pgm_rxw_state (window, skb, PGM_PKT_STATE_BACK_OFF);
				pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("NCF retry #%u attempt %u/%u."), skb->sequence, state->ncf_retry_count, sock->nak_ncf_retries);
			}
		}
		else
		{
/* packet expires some time later */
			pgm_trace(PGM_LOG_ROLE_RX_WINDOW,_("NCF retry #%u is delayed %f seconds."),
				skb->sequence, pgm_to_secsf (state->timer_expiry - now));
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
				window->nak_backoff_queue.length,
				window->wait_ncf_queue.length,
				window->wait_data_queue.length,
				window->lost_count,
				window->fragment_count);
	}

/* mark receiver window for flushing on next recv() */
	if (PGM_UNLIKELY(window->cumulative_losses != peer->last_cumulative_losses &&
	    !peer->pending_link.data))
	{
		sock->is_reset = TRUE;
		peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
		peer->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (sock, peer);
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
	pgm_sock_t* sock = peer->sock;
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
		if (pgm_time_after_eq (now, rdata_state->timer_expiry))
		{
			if (PGM_UNLIKELY(!is_valid_nla)) {
				dropped_invalid++;
				pgm_rxw_lost (window, rdata_skb->sequence);
/* mark receiver window for flushing on next recv() */
				pgm_peer_set_pending (sock, peer);
				list = next_list_el;
				continue;
			}

			if (++rdata_state->data_retry_count >= sock->nak_data_retries)
			{
				dropped++;
				cancel_skb (sock, peer, rdata_skb, now);
				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_DATA_RETRIES_EXCEEDED]++;
				list = next_list_el;
				continue;
			}

//			rdata_state->timer_expiry += nak_rb_ivl(sock);
			rdata_state->timer_expiry = now + nak_rb_ivl (sock);
			pgm_rxw_state (window, rdata_skb, PGM_PKT_STATE_BACK_OFF);

/* retry back to back-off state */
			pgm_trace(PGM_LOG_ROLE_RX_WINDOW,_("Data retry #%u attempt %u/%u."), rdata_skb->sequence, rdata_state->data_retry_count, sock->nak_data_retries);
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
		sock->is_reset = TRUE;
		peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
		peer->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (sock, peer);
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
	pgm_sock_t*	      const restrict sock,
	pgm_peer_t*	      const restrict source,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != source);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_data (sock:%p source:%p skb:%p)",
		(void*)sock, (void*)source, (void*)skb);

	unsigned msg_count = 0;
	const pgm_time_t nak_rb_expiry = skb->tstamp + nak_rb_ivl (sock);
	pgm_time_t ack_rb_expiry = 0;
	const unsigned tsdu_length = ntohs (skb->pgm_header->pgm_tsdu_length);

	skb->pgm_data = skb->data;

	const unsigned opt_total_length = (skb->pgm_header->pgm_options & PGM_OPT_PRESENT) ? ntohs(*(uint16_t*)( (char*)( skb->pgm_data + 1 ) + sizeof(uint16_t))) : 0;

/* advance data pointer to payload */
	pgm_skb_pull (skb, sizeof(struct pgm_data) + opt_total_length);

	if (opt_total_length > 0 &&			/* there are options */
	    get_pgm_options (skb) &&			/* valid options */
	    sock->use_pgmcc &&				/* PGMCC is enabled */
	    NULL != skb->pgm_opt_pgmcc_data &&		/* PGMCC options */
	    0 == source->ack_rb_expiry)			/* not partaking in a current election */
	{
		ack_rb_expiry = skb->tstamp + ack_rb_ivl (sock);
	}

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

/* congestion control */
	if (0 != ack_rb_expiry)
	{
/* save source timestamp and local timestamp for RTT calculation */
		source->ack_last_tstamp = ntohl (skb->pgm_opt_pgmcc_data->opt_tstamp);
		source->last_data_tstamp = skb->tstamp;
		if (_pgm_is_acker (source, skb))
		{
			if (PGM_UNLIKELY(pgm_sockaddr_is_addr_unspecified ((struct sockaddr*)&source->nla)))
			{
				pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("Unable to send ACK due to unknown NLA."));
			}
			else if (PGM_UNLIKELY(!send_ack (sock, source, skb->tstamp)))
			{
				pgm_debug ("send_ack failed");
			}
			ack_rb_expiry = 0;
		}
		else if (_pgm_is_acker_election (skb))
		{
			pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("ACKer election."));
			_pgm_add_ack (source, ack_rb_expiry);
		}
		else if (0 != source->window->ack_backoff_queue.length)
		{
/* purge ACK backoff queue as host is not elected ACKer */
			_pgm_remove_ack (source);
			ack_rb_expiry = 0;
		}
		else
		{
/* no election, not the elected ACKer, no outstanding ACKs */
			ack_rb_expiry = 0;
		}
	}

	if (flush_naks || 0 != ack_rb_expiry) {
/* flush out 1st time nak packets */
		pgm_timer_lock (sock);
		if (flush_naks && pgm_time_after (sock->next_poll, nak_rb_expiry))
			sock->next_poll = nak_rb_expiry;
		if (0 != ack_rb_expiry && pgm_time_after (sock->next_poll, ack_rb_expiry))
			sock->next_poll = ack_rb_expiry;
		pgm_timer_unlock (sock);
	}
	return TRUE;
}

/* POLLs are generated by PGM Parents (Sources or Network Elements).
 *
 * returns TRUE on valid packet, FALSE on invalid packet.
 */

bool
pgm_on_poll (
	pgm_sock_t*	      const restrict sock,
	pgm_peer_t*	      const restrict source,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != source);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_poll (sock:%p source:%p skb:%p)",
		(void*)sock, (void*)source, (void*)skb);

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
	    (sock->rand_node_id & poll_mask) != poll_rand)
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
		return on_general_poll (sock, source, skb);

	case PGM_POLL_DLR:
		return on_dlr_poll (sock, source, skb);

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
	pgm_sock_t*	      const restrict sock,
	pgm_peer_t*	      const restrict source,
	struct pgm_sk_buff_t* const restrict skb
	)
{
	struct pgm_poll*  poll4 = (struct pgm_poll*) skb->data;
	struct pgm_poll6* poll6 = (struct pgm_poll6*)skb->data;

/* TODO: cancel any pending poll-response */

/* defer response based on provided back-off interval */
	const uint32_t poll_bo_ivl = (AFI_IP6 == ntohs (poll4->poll_nla_afi)) ? ntohl (poll6->poll6_bo_ivl) : ntohl (poll4->poll_bo_ivl);
	source->polr_expiry = skb->tstamp + pgm_rand_int_range (&sock->rand_, 0, poll_bo_ivl);
	pgm_nla_to_sockaddr (&poll4->poll_nla_afi, (struct sockaddr*)&source->poll_nla);
/* TODO: schedule poll-response */

	return TRUE;
}

/* Used to count off-tree DLRs */

static
bool
on_dlr_poll (
	PGM_GNUC_UNUSED pgm_sock_t*	      const restrict sock,
	PGM_GNUC_UNUSED pgm_peer_t*	      const restrict source,
	PGM_GNUC_UNUSED struct pgm_sk_buff_t* const restrict skb
	)
{
/* we are not a DLR */
	return FALSE;
}

/* eof */
