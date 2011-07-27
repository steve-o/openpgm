/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM source socket.
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
#include <errno.h>
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/socket.h>
#include <impl/source.h>
#include <impl/sqn_list.h>
#include <impl/packet_parse.h>
#include <impl/net.h>


//#define SOURCE_DEBUG

#ifndef SOURCE_DEBUG
#	define PGM_DISABLE_ASSERT
#endif


/* locals */
static inline bool peer_is_source (const pgm_peer_t*) PGM_GNUC_CONST;
static inline bool peer_is_peer (const pgm_peer_t*) PGM_GNUC_CONST;
static void reset_heartbeat_spm (pgm_sock_t*const, const pgm_time_t);
static bool send_ncf (pgm_sock_t*const restrict, const struct sockaddr*const restrict, const struct sockaddr*const restrict, const uint32_t, const bool);
static bool send_ncf_list (pgm_sock_t*const restrict, const struct sockaddr*const restrict, const struct sockaddr*const restrict, struct pgm_sqn_list_t*const restrict, const bool);
static int send_odata (pgm_sock_t*const restrict, struct pgm_sk_buff_t*const restrict, size_t*restrict);
static int send_odata_copy (pgm_sock_t*const restrict, const void*restrict, const uint16_t, size_t*restrict);
static int send_odatav (pgm_sock_t*const restrict, const struct pgm_iovec*const restrict, const unsigned, size_t*restrict);
static bool send_rdata (pgm_sock_t*restrict, struct pgm_sk_buff_t*restrict);


static inline
unsigned
_pgm_popcount (
	uint32_t		n
	)
{
#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
	return __builtin_popcount (n);
#elif defined(_MSC_VER)
#	include <intrin.h>
	return __popcnt (n);
#else
/* MIT HAKMEM 169 */
	const uint32_t t = n - ((n >> 1) & 033333333333)
			     - ((n >> 2) & 011111111111);
	return ((t + (t >> 3) & 030707070707)) % 63;
#endif
}

static inline
bool
peer_is_source (
	const pgm_peer_t*	peer
	)
{
	return (NULL == peer);
}

static inline
bool
peer_is_peer (
	const pgm_peer_t*	peer
	)
{
	return (NULL != peer);
}

static inline
void
reset_spmr_timer (
	pgm_peer_t* const	peer
	)
{
	peer->spmr_expiry = 0;
}

static inline
size_t
source_max_tsdu (
	const pgm_sock_t*	sock,
	const bool		can_fragment
	)
{
	size_t max_tsdu = can_fragment ? sock->max_tsdu_fragment : sock->max_tsdu;
	if (sock->use_var_pktlen /* OPT_VAR_PKT_LEN */)
		max_tsdu -= sizeof (uint16_t);
	return max_tsdu;
}

/* prototype of function to send pro-active parity NAKs.
 */

static
bool
pgm_schedule_proactive_nak (
	pgm_sock_t*		sock,
	uint32_t		nak_tg_sqn	/* transmission group (shifted) */
	)
{
	pgm_return_val_if_fail (NULL != sock, FALSE);
	const bool status = pgm_txw_retransmit_push (sock->window,
						     nak_tg_sqn | sock->rs_proactive_h,
						     TRUE /* is_parity */,
						     sock->tg_sqn_shift);
	return status;
}

/* a deferred request for RDATA, now processing in the timer thread, we check the transmit
 * window to see if the packet exists and forward on, maintaining a lock until the queue is
 * empty.
 *
 * returns TRUE on success, returns FALSE if operation would block.
 */

PGM_GNUC_INTERNAL
bool
pgm_on_deferred_nak (
	pgm_sock_t* const	sock
	)
{
	struct pgm_sk_buff_t* skb;

/* pre-conditions */
	pgm_assert (NULL != sock);

/* We can flush queue and block all odata, or process one set, or process each
 * sequence number individually.
 */

/* parity packets are re-numbered across the transmission group with index h, sharing the space
 * with the original packets.  beyond the transmission group size (k), the PGM option OPT_PARITY_GRP
 * provides the extra offset value.
 */

/* peek from the retransmit queue so we can eliminate duplicate NAKs up until the repair packet
 * has been retransmitted.
 */
	pgm_spinlock_lock (&sock->txw_spinlock);
	skb = pgm_txw_retransmit_try_peek (sock->window);
	if (skb) {
		skb = pgm_skb_get (skb);
		pgm_spinlock_unlock (&sock->txw_spinlock);
		if (!send_rdata (sock, skb)) {
			pgm_free_skb (skb);
			pgm_notify_send (&sock->rdata_notify);
			return FALSE;
		}
		pgm_free_skb (skb);
/* now remove sequence number from retransmit queue, re-enabling NAK processing for this sequence number */
		pgm_txw_retransmit_remove_head (sock->window);
	} else
		pgm_spinlock_unlock (&sock->txw_spinlock);
	return TRUE;
}

/* SPMR indicates if multicast to cancel own SPMR, or unicast to send SPM.
 *
 * rate limited to 1/IHB_MIN per TSI (13.4).
 *
 * if SPMR was valid, returns TRUE, if invalid returns FALSE.
 */

PGM_GNUC_INTERNAL
bool
pgm_on_spmr (
	pgm_sock_t*           const restrict sock,
	pgm_peer_t*	      const restrict peer,	/* maybe NULL if socket is source */
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_spmr (sock:%p peer:%p skb:%p)",
		(void*)sock, (void*)peer, (void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_spmr (skb))) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed SPMR rejected."));
		return FALSE;
	}

	if (peer_is_source (peer)) {
		const bool send_status = pgm_send_spm (sock, 0);
		if (PGM_UNLIKELY(!send_status)) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Failed to send SPM on SPM-Request."));
		}
	} else {
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Suppressing SPMR due to peer multicast SPMR."));
		reset_spmr_timer (peer);
	}
	return TRUE;
}

/* Process opt_pgmcc_feedback PGM option that ships attached to ACK or NAK.
 * Contents use to elect best ACKer.
 *
 * returns TRUE if peer is the elected ACKer.
 */

static
bool
on_opt_pgmcc_feedback (
	pgm_sock_t*           	       const restrict sock,
	const struct pgm_sk_buff_t*    const restrict skb,
	const struct pgm_opt_pgmcc_feedback* restrict opt_pgmcc_feedback
	)
{
	struct sockaddr_storage peer_nla;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);
	pgm_assert (NULL != opt_pgmcc_feedback);

	const uint32_t opt_tstamp = ntohl (opt_pgmcc_feedback->opt_tstamp);
	const uint16_t opt_loss_rate = ntohs (opt_pgmcc_feedback->opt_loss_rate);

	const uint32_t rtt = (uint32_t)(pgm_to_msecs (skb->tstamp) - opt_tstamp);
	const uint64_t peer_loss = rtt * rtt * opt_loss_rate;

	pgm_nla_to_sockaddr (&opt_pgmcc_feedback->opt_nla_afi, (struct sockaddr*)&peer_nla);

/* ACKer elections */
	if (PGM_UNLIKELY(pgm_sockaddr_is_addr_unspecified ((const struct sockaddr*)&sock->acker_nla)))
	{
		pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("Elected first ACKer"));
		memcpy (&sock->acker_nla, &peer_nla, pgm_sockaddr_storage_len (&peer_nla));
	}
	else if (peer_loss > sock->acker_loss &&
		 0 != pgm_sockaddr_cmp ((const struct sockaddr*)&peer_nla, (const struct sockaddr*)&sock->acker_nla))
	{
		pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("Elected new ACKer"));
		memcpy (&sock->acker_nla, &peer_nla, pgm_sockaddr_storage_len (&peer_nla));
	}

/* update ACKer state */
	if (0 == pgm_sockaddr_cmp ((const struct sockaddr*)&peer_nla, (const struct sockaddr*)&sock->acker_nla))
	{
		sock->acker_loss = peer_loss;
		return TRUE;
	}

	return FALSE;
}

/* NAK requesting RDATA transmission for a sending sock, only valid if
 * sequence number(s) still in transmission window.
 *
 * we can potentially have different IP versions for the NAK packet to the send group.
 *
 * TODO: fix IPv6 AFIs
 *
 * take in a NAK and pass off to an asynchronous queue for another thread to process
 *
 * if NAK is valid, returns TRUE.  on error, FALSE is returned.
 */

PGM_GNUC_INTERNAL
bool
pgm_on_nak (
	pgm_sock_t*           const restrict sock,
	struct pgm_sk_buff_t* const restrict skb
	)
{
	const struct pgm_nak	*nak;
	const struct pgm_nak6	*nak6;
	struct sockaddr_storage	 nak_src_nla, nak_grp_nla;
	const uint32_t		*nak_list = NULL;
	uint_fast8_t		 nak_list_len = 0;
	struct pgm_sqn_list_t	 sqn_list;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_nak (sock:%p skb:%p)",
		(const void*)sock, (const void*)skb);

	const bool is_parity = skb->pgm_header->pgm_options & PGM_OPT_PARITY;
	if (is_parity) {
		sock->cumulative_stats[PGM_PC_SOURCE_PARITY_NAKS_RECEIVED]++;
		if (!sock->use_ondemand_parity) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Parity NAK rejected as on-demand parity is not enabled."));
			sock->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
			return FALSE;
		}
	} else
		sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED]++;

	if (PGM_UNLIKELY(!pgm_verify_nak (skb))) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed NAK rejected."));
		sock->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
		return FALSE;
	}

	nak  = (struct pgm_nak *)skb->data;
	nak6 = (struct pgm_nak6*)skb->data;
		
/* NAK_SRC_NLA contains our sock unicast NLA */
	pgm_nla_to_sockaddr (&nak->nak_src_nla_afi, (struct sockaddr*)&nak_src_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nak_src_nla, (struct sockaddr*)&sock->send_addr) != 0))
	{
		char saddr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&nak_src_nla, saddr, sizeof(saddr));
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("NAK rejected for unmatched NLA: %s"), saddr);
		sock->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
		return FALSE;
	}

/* NAK_GRP_NLA containers our sock multicast group */ 
	pgm_nla_to_sockaddr ((AF_INET6 == nak_src_nla.ss_family) ? &nak6->nak6_grp_nla_afi : &nak->nak_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nak_grp_nla, (struct sockaddr*)&sock->send_gsr.gsr_group) != 0))
	{
		char sgroup[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&nak_src_nla, sgroup, sizeof(sgroup));
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("NAK rejected as targeted for different multicast group: %s"), sgroup);
		sock->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
		return FALSE;
	}

/* create queue object */
	sqn_list.sqn[0] = ntohl (nak->nak_sqn);
	sqn_list.len = 1;

	pgm_debug ("nak_sqn %" PRIu32, sqn_list.sqn[0]);

/* check NAK list */
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_header *opt_header;
		const struct pgm_opt_length *opt_len;

		opt_len = (AF_INET6 == nak_src_nla.ss_family) ?
				(const struct pgm_opt_length*)(nak6 + 1) :
				(const struct pgm_opt_length*)(nak  + 1);
		if (PGM_UNLIKELY(opt_len->opt_type != PGM_OPT_LENGTH)) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed NAK rejected."));
			sock->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
			return FALSE;
		}
		if (PGM_UNLIKELY(opt_len->opt_length != sizeof(struct pgm_opt_length))) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed NAK rejected."));
			sock->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
			return FALSE;
		}
/* TODO: check for > 16 options & past packet end */
		opt_header = (const struct pgm_opt_header*)opt_len;
		do {
			opt_header = (const struct pgm_opt_header*)((const char*)opt_header + opt_header->opt_length);
			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST) {
				nak_list = ((const struct pgm_opt_nak_list*)(opt_header + 1))->opt_sqn;
				nak_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(uint8_t) ) / sizeof(uint32_t);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

/* nak list numbers */
	if (PGM_UNLIKELY(nak_list_len > 62)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed NAK rejected on sequence list overrun, %d rported NAKs."), nak_list_len);
		return FALSE;
	}
		
	for (uint_fast8_t i = 0; i < nak_list_len; i++)
	{
		sqn_list.sqn[sqn_list.len++] = ntohl (*nak_list);
		nak_list++;
	}

/* send NAK confirm packet immediately, then defer to timer thread for a.s.a.p
 * delivery of the actual RDATA packets.  blocking send for NCF is ignored as RDATA
 * broadcast will be sent later.
 */
	if (nak_list_len)
		send_ncf_list (sock, (struct sockaddr*)&nak_src_nla, (struct sockaddr*)&nak_grp_nla, &sqn_list, is_parity);
	else
		send_ncf (sock, (struct sockaddr*)&nak_src_nla, (struct sockaddr*)&nak_grp_nla, sqn_list.sqn[0], is_parity);

/* queue retransmit requests */
	for (uint_fast8_t i = 0; i < sqn_list.len; i++) {
		const bool push_status = pgm_txw_retransmit_push (sock->window, sqn_list.sqn[i], is_parity, sock->tg_sqn_shift);
		if (PGM_UNLIKELY(!push_status)) {
			pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Failed to push retransmit request for #%" PRIu32), sqn_list.sqn[i]);
		}
	}
	return TRUE;
}

/* Null-NAK, or N-NAK propogated by a DLR for hand waving excitement
 *
 * if NNAK is valid, returns TRUE.  on error, FALSE is returned.
 */

PGM_GNUC_INTERNAL
bool
pgm_on_nnak (
	pgm_sock_t*           const restrict sock,
	struct pgm_sk_buff_t* const restrict skb
	)
{
	const struct pgm_nak	*nnak;
	const struct pgm_nak6	*nnak6;
	struct sockaddr_storage	 nnak_src_nla, nnak_grp_nla;
	uint_fast8_t		 nnak_list_len = 0;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_nnak (sock:%p skb:%p)",
		(void*)sock, (void*)skb);

	sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAK_PACKETS_RECEIVED]++;

	if (PGM_UNLIKELY(!pgm_verify_nnak (skb))) {
		sock->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
		return FALSE;
	}

	nnak  = (struct pgm_nak *)skb->data;
	nnak6 = (struct pgm_nak6*)skb->data;
		
/* NAK_SRC_NLA contains our sock unicast NLA */
	pgm_nla_to_sockaddr (&nnak->nak_src_nla_afi, (struct sockaddr*)&nnak_src_nla);

	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nnak_src_nla, (struct sockaddr*)&sock->send_addr) != 0))
	{
		sock->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
		return FALSE;
	}

/* NAK_GRP_NLA containers our sock multicast group */ 
	pgm_nla_to_sockaddr ((AF_INET6 == nnak_src_nla.ss_family) ? &nnak6->nak6_grp_nla_afi : &nnak->nak_grp_nla_afi, (struct sockaddr*)&nnak_grp_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nnak_grp_nla, (struct sockaddr*)&sock->send_gsr.gsr_group) != 0))
	{
		sock->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
		return FALSE;
	}

/* check NNAK list */
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (AF_INET6 == nnak_src_nla.ss_family) ?
							(const struct pgm_opt_length*)(nnak6 + 1) :
							(const struct pgm_opt_length*)(nnak + 1);
		if (PGM_UNLIKELY(opt_len->opt_type != PGM_OPT_LENGTH)) {
			sock->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
			return FALSE;
		}
		if (PGM_UNLIKELY(opt_len->opt_length != sizeof(struct pgm_opt_length))) {
			sock->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
			return FALSE;
		}
/* TODO: check for > 16 options & past packet end */
		const struct pgm_opt_header* opt_header = (const struct pgm_opt_header*)opt_len;
		do {
			opt_header = (const struct pgm_opt_header*)((const char*)opt_header + opt_header->opt_length);
			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST) {
				nnak_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(uint8_t) ) / sizeof(uint32_t);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

	sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAKS_RECEIVED] += 1 + nnak_list_len;
	return TRUE;
}

/* ACK, sent upstream by one selected ACKER for congestion control feedback.
 *
 * if ACK is valid, returns TRUE.  on error, FALSE is returned.
 */

PGM_GNUC_INTERNAL
bool
pgm_on_ack (
	pgm_sock_t*           const restrict sock,
	struct pgm_sk_buff_t* const restrict skb
	)
{
	const struct pgm_ack	*ack;
	bool			 is_acker = FALSE;
	uint32_t		 ack_bitmap;
	unsigned		 new_acks;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_ack (sock:%p skb:%p)",
		(const void*)sock, (const void*)skb);

	sock->cumulative_stats[PGM_PC_SOURCE_ACK_PACKETS_RECEIVED]++;

	if (PGM_UNLIKELY(!pgm_verify_ack (skb))) {
		sock->cumulative_stats[PGM_PC_SOURCE_ACK_ERRORS]++;
		return FALSE;
	}

	if (!sock->use_pgmcc)
		return FALSE;

	ack = (struct pgm_ack*)skb->data;

/* check PGMCC feedback option for new elections */
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_header *opt_header;
		const struct pgm_opt_length *opt_len;

		opt_len = (const struct pgm_opt_length*)(ack + 1);
		if (PGM_UNLIKELY(opt_len->opt_type != PGM_OPT_LENGTH)) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed ACK rejected."));
			return FALSE;
		}
		if (PGM_UNLIKELY(opt_len->opt_length != sizeof(struct pgm_opt_length))) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed ACK rejected."));
			return FALSE;
		}
		opt_header = (const struct pgm_opt_header*)opt_len;
		do {
			opt_header = (const struct pgm_opt_header*)((const char*)opt_header + opt_header->opt_length);
			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_PGMCC_FEEDBACK) {
				const struct pgm_opt_pgmcc_feedback* opt_pgmcc_feedback = (const struct pgm_opt_pgmcc_feedback*)(opt_header + 1);
				is_acker = on_opt_pgmcc_feedback (sock, skb, opt_pgmcc_feedback);
				break;	/* ignore other options */
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

/* ignore ACKs from other receivers or sessions */
	if (!is_acker)
		return TRUE;

/* reset ACK expiration */
	sock->next_crqst = 0;

/* count new ACK sequences */
	const uint32_t ack_rx_max = ntohl (ack->ack_rx_max);
	const int32_t delta = ack_rx_max - sock->ack_rx_max;
/* ignore older ACKs when multiple active ACKers */
	if (pgm_uint32_gt (ack_rx_max, sock->ack_rx_max))
		sock->ack_rx_max = ack_rx_max;
	ack_bitmap = ntohl (ack->ack_bitmap);
	if (delta > 32)		sock->ack_bitmap = 0;		/* sequence jump ahead beyond past bitmap */
	else if (delta > 0)	sock->ack_bitmap <<= delta;	/* immediate sequence */
	else if (delta > -32)	ack_bitmap <<= -delta;		/* repair sequence scoped by bitmap */
	else			ack_bitmap = 0;			/* old sequence */
	new_acks = _pgm_popcount (ack_bitmap & ~sock->ack_bitmap);
	sock->ack_bitmap |= ack_bitmap;

	if (0 == new_acks)
		return TRUE;

	const bool is_congestion_limited = (sock->tokens < pgm_fp8 (1));

/* after loss detection cancel any further manipulation of the window
 * until feedback is received for the next transmitted packet.
 */
	if (sock->is_congested)
	{
		if (pgm_uint32_lte (ack_rx_max, sock->suspended_sqn))
		{
			pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("PGMCC window token manipulation suspended due to congestion (T:%u W:%u)"),
				   pgm_fp8tou (sock->tokens), pgm_fp8tou (sock->cwnd_size));
			const uint_fast32_t token_inc = pgm_fp8mul (pgm_fp8 (new_acks), pgm_fp8 (1) + pgm_fp8div (pgm_fp8 (1), sock->cwnd_size));
			sock->tokens = MIN( sock->tokens + token_inc, sock->cwnd_size );
			goto notify_tx;
		}
		sock->is_congested = FALSE;
	}

/* count outstanding lost sequences */
	const unsigned total_lost = _pgm_popcount (~sock->ack_bitmap);

/* no detected data loss at ACKer, increase congestion window size */
	if (0 == total_lost)
	{
		uint_fast32_t n, token_inc;

		new_acks += sock->acks_after_loss;
		sock->acks_after_loss = 0;
		n = pgm_fp8 (new_acks);
		token_inc = 0;

/* slow-start phase, exponential increase to SSTHRESH */
		if (sock->cwnd_size < sock->ssthresh) {
			const uint_fast32_t d = MIN( n, sock->ssthresh - sock->cwnd_size );
			n -= d;
			token_inc	 = d + d;
			sock->cwnd_size += d;
		}

		const uint_fast32_t iw = pgm_fp8div (pgm_fp8 (1), sock->cwnd_size);

/* linear window increase */
		token_inc	+= pgm_fp8mul (n, pgm_fp8 (1) + iw);
		sock->cwnd_size += pgm_fp8mul (n, iw);
		sock->tokens	 = MIN( sock->tokens + token_inc, sock->cwnd_size );
//		pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("PGMCC++ (T:%u W:%u)"),
//			   pgm_fp8tou (sock->tokens), pgm_fp8tou (sock->cwnd_size));
	}
	else
	{
/* Look for an unacknowledged data packet which is followed by at least three
 * acknowledged data packets, then the packet is assumed to be lost and PGMCC
 * reacts by halving the window.
 *
 * Common value will be 0xfffffff7.
 */
		sock->acks_after_loss += new_acks;
		if (sock->acks_after_loss >= 3)
		{
			sock->acks_after_loss = 0;
			sock->suspended_sqn = ack_rx_max;
			sock->is_congested = TRUE;
			sock->cwnd_size = pgm_fp8div (sock->cwnd_size, pgm_fp8 (2));
			if (sock->cwnd_size > sock->tokens)
				sock->tokens = 0;
			else
				sock->tokens -= sock->cwnd_size;
			sock->ack_bitmap = 0xffffffff;
			pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("PGMCC congestion, half window size (T:%u W:%u)"),
				   pgm_fp8tou (sock->tokens), pgm_fp8tou (sock->cwnd_size));
		}
	}

/* token is now available so notify tx thread that transmission time is available */
notify_tx:
	if (is_congestion_limited &&
	    sock->tokens >= pgm_fp8 (1))
	{
		pgm_notify_send (&sock->ack_notify);
	}
	return TRUE;
}

/* ambient/heartbeat SPM's
 *
 * heartbeat: ihb_tmr decaying between ihb_min and ihb_max 2x after last packet
 *
 * on success, TRUE is returned, if operation would block, FALSE is returned.
 */

PGM_GNUC_INTERNAL
bool
pgm_send_spm (
	pgm_sock_t* const	sock,
	const int		flags
	)
{
	size_t		   tpdu_length;
	char		  *buf;
	struct pgm_header *header;
	struct pgm_spm	  *spm;
	struct pgm_spm6	  *spm6;
	ssize_t		   sent;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != sock->window);

	pgm_debug ("pgm_send_spm (sock:%p flags:%d)",
		(const void*)sock, flags);

	tpdu_length = sizeof(struct pgm_header);
	if (AF_INET == sock->send_gsr.gsr_group.ss_family)
		tpdu_length += sizeof(struct pgm_spm);
	else
		tpdu_length += sizeof(struct pgm_spm6);
	if (sock->use_proactive_parity ||
	    sock->use_ondemand_parity ||
	    sock->is_pending_crqst ||
	    PGM_OPT_FIN == flags)
	{
		tpdu_length += sizeof(struct pgm_opt_length);
/* forward error correction */
		if (sock->use_proactive_parity ||
		    sock->use_ondemand_parity)
			tpdu_length += sizeof(struct pgm_opt_header) +
				       sizeof(struct pgm_opt_parity_prm);
/* congestion report request */
		if (sock->is_pending_crqst)
			tpdu_length += sizeof(struct pgm_opt_header) +
				       sizeof(struct pgm_opt_crqst);
/* end of session */
		if (PGM_OPT_FIN == flags)
			tpdu_length += sizeof(struct pgm_opt_header) +
				       sizeof(struct pgm_opt_fin);
	}
	buf = pgm_alloca (tpdu_length);
	header = (struct pgm_header*)buf;
	spm  = (struct pgm_spm *)(header + 1);
	spm6 = (struct pgm_spm6*)(header + 1);
	memcpy (header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = sock->tsi.sport;
	header->pgm_dport       = sock->dport;
	header->pgm_type        = PGM_SPM;
	header->pgm_options     = 0;
	header->pgm_tsdu_length = 0;

/* SPM */
	spm->spm_sqn		= htonl (sock->spm_sqn);
	spm->spm_trail		= htonl (pgm_txw_trail_atomic (sock->window));
	spm->spm_lead		= htonl (pgm_txw_lead_atomic (sock->window));
	spm->spm_reserved	= 0;
/* our nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&sock->send_addr, (char*)&spm->spm_nla_afi);

/* PGM options */
	if (sock->use_proactive_parity ||
	    sock->use_ondemand_parity ||
	    sock->is_pending_crqst ||
	    PGM_OPT_FIN == flags)
	{
		struct pgm_opt_header *opt_header, *last_opt_header;
		struct pgm_opt_length* opt_len;
		uint16_t opt_total_length;

		if (AF_INET == sock->send_gsr.gsr_group.ss_family)
			opt_header = (struct pgm_opt_header*)(spm + 1);
		else
			opt_header = (struct pgm_opt_header*)(spm6 + 1);
		header->pgm_options |= PGM_OPT_PRESENT;
		opt_len			= (struct pgm_opt_length*)opt_header;
		opt_len->opt_type	= PGM_OPT_LENGTH;
		opt_len->opt_length	= sizeof(struct pgm_opt_length);
		opt_total_length	= sizeof(struct pgm_opt_length);
		last_opt_header = opt_header = (struct pgm_opt_header*)(opt_len + 1);

/* OPT_PARITY_PRM */
		if (sock->use_proactive_parity ||
		    sock->use_ondemand_parity)
		{
			struct pgm_opt_parity_prm *opt_parity_prm;

			header->pgm_options |= PGM_OPT_NETWORK;
			opt_total_length += sizeof(struct pgm_opt_header) +
					    sizeof(struct pgm_opt_parity_prm);
			opt_header->opt_type	= PGM_OPT_PARITY_PRM;
			opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_parity_prm);
			opt_parity_prm = (struct pgm_opt_parity_prm*)(opt_header + 1);
			opt_parity_prm->opt_reserved = (sock->use_proactive_parity ? PGM_PARITY_PRM_PRO : 0) |
						       (sock->use_ondemand_parity ? PGM_PARITY_PRM_OND : 0);
			opt_parity_prm->parity_prm_tgs = htonl (sock->rs_k);
			last_opt_header = opt_header;
			opt_header = (struct pgm_opt_header*)(opt_parity_prm + 1);
		}

/* OPT_CRQST */
		if (sock->is_pending_crqst)
		{
			struct pgm_opt_crqst *opt_crqst;

			header->pgm_options |= PGM_OPT_NETWORK;
			opt_total_length += sizeof(struct pgm_opt_header) +
					    sizeof(struct pgm_opt_crqst);
			opt_header->opt_type	= PGM_OPT_CRQST;
			opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_crqst);
			opt_crqst = (struct pgm_opt_crqst*)(opt_header + 1);
/* request receiver worst path report, OPT_CR_RX_WP */
			opt_crqst->opt_reserved = PGM_OPT_CRQST_RXP;
			sock->is_pending_crqst = FALSE;
			last_opt_header = opt_header;
			opt_header = (struct pgm_opt_header*)(opt_crqst + 1);
		}

/* OPT_FIN */
		if (PGM_OPT_FIN == flags)
		{
			struct pgm_opt_fin *opt_fin;

			opt_total_length += sizeof(struct pgm_opt_header) +
					    sizeof(struct pgm_opt_fin);
			opt_header->opt_type	= PGM_OPT_FIN;
			opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_fin);
			opt_fin = (struct pgm_opt_fin*)(opt_header + 1);
			opt_fin->opt_reserved = 0;
			last_opt_header = opt_header;
			opt_header = (struct pgm_opt_header*)(opt_fin + 1);
		}

		last_opt_header->opt_type |= PGM_OPT_END;
		opt_len->opt_total_length = htons (opt_total_length);
	}

/* checksum optional for SPMs */
	header->pgm_checksum = 0;
	header->pgm_checksum = pgm_csum_fold (pgm_csum_partial (buf, (uint16_t)tpdu_length, 0));

	sent = pgm_sendto (sock,
			   flags != PGM_OPT_SYN && sock->is_controlled_spm,	/* rate limited */
			   NULL,
			   TRUE,		/* with router alert */
			   buf,
			   tpdu_length,
			   (struct sockaddr*)&sock->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	if (sent < 0) {
		const int save_errno = pgm_get_last_sock_error();
		if (PGM_LIKELY(PGM_SOCK_EAGAIN == save_errno || PGM_SOCK_ENOBUFS == save_errno))
		{
			sock->blocklen = tpdu_length + sock->iphdr_len;
			return FALSE;
		}
/* fall through silently on other errors */
	}

/* advance SPM sequence only on successful transmission */
	sock->spm_sqn++;
	pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)tpdu_length);
	return TRUE;
}

/* send a NAK confirm (NCF) message with provided sequence number list.
 *
 * on success, TRUE is returned, returns FALSE if operation would block.
 */

static
bool
send_ncf (
	pgm_sock_t*            const restrict sock,
	const struct sockaddr* const restrict nak_src_nla,
	const struct sockaddr* const restrict nak_grp_nla,
	const uint32_t			      sequence,
	const bool			      is_parity		/* send parity NCF */
	)
{
	size_t		   tpdu_length;
	char		  *buf;
	struct pgm_header *header;
	struct pgm_nak	  *ncf;
	struct pgm_nak6	  *ncf6;
	ssize_t		   sent;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != nak_src_nla);
	pgm_assert (NULL != nak_grp_nla);
	pgm_assert (nak_src_nla->sa_family == nak_grp_nla->sa_family);

#ifdef SOURCE_DEBUG
	char saddr[INET6_ADDRSTRLEN], gaddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (nak_src_nla, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (nak_grp_nla, gaddr, sizeof(gaddr));
	pgm_debug ("send_ncf (sock:%p nak-src-nla:%s nak-grp-nla:%s sequence:%" PRIu32" is-parity:%s)",
		(void*)sock,
		saddr,
		gaddr,
		sequence,
		is_parity ? "TRUE": "FALSE"
		);
#endif

	tpdu_length = sizeof(struct pgm_header);
	tpdu_length += (AF_INET == nak_src_nla->sa_family) ?
				sizeof(struct pgm_nak) :
				sizeof(struct pgm_nak6);
	buf = pgm_alloca (tpdu_length);
	header = (struct pgm_header*)buf;
	ncf  = (struct pgm_nak *)(header + 1);
	ncf6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport	= sock->tsi.sport;
	header->pgm_dport	= sock->dport;
	header->pgm_type        = PGM_NCF;
        header->pgm_options     = is_parity ? PGM_OPT_PARITY : 0;
        header->pgm_tsdu_length = 0;

/* NCF */
	ncf->nak_sqn		= htonl (sequence);

/* source nla */
	pgm_sockaddr_to_nla (nak_src_nla, (char*)&ncf->nak_src_nla_afi);

/* group nla */
	pgm_sockaddr_to_nla (nak_grp_nla, (AF_INET6 == nak_src_nla->sa_family) ?
						(char*)&ncf6->nak6_grp_nla_afi :
						(char*)&ncf->nak_grp_nla_afi );
        header->pgm_checksum = 0;
        header->pgm_checksum = pgm_csum_fold (pgm_csum_partial (buf, (uint16_t)tpdu_length, 0));

	sent = pgm_sendto (sock,
			   FALSE,			/* not rate limited */
			   NULL,
			   TRUE,			/* with router alert */
			   buf,
			   tpdu_length,
			   (struct sockaddr*)&sock->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	if (sent < 0 && PGM_LIKELY(PGM_SOCK_EAGAIN == pgm_get_last_sock_error()))
		return FALSE;
/* fall through silently on other errors */
			
	pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)tpdu_length);
	return TRUE;
}

/* A NCF packet with a OPT_NAK_LIST option extension
 *
 * on success, TRUE is returned.  on error, FALSE is returned.
 */

static
bool
send_ncf_list (
	pgm_sock_t*            const restrict sock,
	const struct sockaddr* const restrict nak_src_nla,
	const struct sockaddr* const restrict nak_grp_nla,
	struct pgm_sqn_list_t* const restrict sqn_list,		/* will change to network-order */
	const bool			      is_parity		/* send parity NCF */
	)
{
	size_t			 tpdu_length;
	char			*buf;
	struct pgm_header	*header;
	struct pgm_nak		*ncf;
	struct pgm_nak6		*ncf6;
	struct pgm_opt_header	*opt_header;
	struct pgm_opt_length	*opt_len;
	struct pgm_opt_nak_list	*opt_nak_list;
	ssize_t			 sent;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != nak_src_nla);
	pgm_assert (NULL != nak_grp_nla);
	pgm_assert (sqn_list->len > 1);
	pgm_assert (sqn_list->len <= 63);
	pgm_assert (nak_src_nla->sa_family == nak_grp_nla->sa_family);

#ifdef SOURCE_DEBUG
	char saddr[INET6_ADDRSTRLEN], gaddr[INET6_ADDRSTRLEN];
	char list[1024];
	pgm_sockaddr_ntop (nak_src_nla, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (nak_grp_nla, gaddr, sizeof(gaddr));
	sprintf (list, "%" PRIu32, sqn_list->sqn[0]);
	for (uint_fast8_t i = 1; i < sqn_list->len; i++) {
		char sequence[ 2 + strlen("4294967295") ];
		sprintf (sequence, " %" PRIu32, sqn_list->sqn[i]);
		strcat (list, sequence);
	}
	pgm_debug ("send_ncf_list (sock:%p nak-src-nla:%s nak-grp-nla:%s sqn-list:[%s] is-parity:%s)",
		(void*)sock,
		saddr,
		gaddr,
		list,
		is_parity ? "TRUE": "FALSE"
		);
#endif

	tpdu_length = sizeof(struct pgm_header) +
			     sizeof(struct pgm_opt_length) +		/* includes header */
			     sizeof(struct pgm_opt_header) + sizeof(uint8_t) +
			     ( (sqn_list->len-1) * sizeof(uint32_t) );
	tpdu_length += (AF_INET == nak_src_nla->sa_family) ? sizeof(struct pgm_nak) : sizeof(struct pgm_nak6);
	buf = pgm_alloca (tpdu_length);
	header = (struct pgm_header*)buf;
	ncf  = (struct pgm_nak *)(header + 1);
	ncf6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport	= sock->tsi.sport;
	header->pgm_dport	= sock->dport;
	header->pgm_type        = PGM_NCF;
        header->pgm_options     = is_parity ? (PGM_OPT_PRESENT | PGM_OPT_NETWORK | PGM_OPT_PARITY) : (PGM_OPT_PRESENT | PGM_OPT_NETWORK);
        header->pgm_tsdu_length = 0;
/* NCF */
	ncf->nak_sqn		= htonl (sqn_list->sqn[0]);

/* source nla */
	pgm_sockaddr_to_nla (nak_src_nla, (char*)&ncf->nak_src_nla_afi);

/* group nla */
	pgm_sockaddr_to_nla (nak_grp_nla, (AF_INET6 == nak_src_nla->sa_family) ? (char*)&ncf6->nak6_grp_nla_afi : (char*)&ncf->nak_grp_nla_afi );

/* OPT_NAK_LIST */
	opt_len = (AF_INET6 == nak_src_nla->sa_family) ? (struct pgm_opt_length*)(ncf6 + 1) : (struct pgm_opt_length*)(ncf + 1);
	opt_len->opt_type	= PGM_OPT_LENGTH;
	opt_len->opt_length	= sizeof(struct pgm_opt_length);
	opt_len->opt_total_length = htons ((uint16_t)(sizeof(struct pgm_opt_length) +
						sizeof(struct pgm_opt_header) +
						sizeof(uint8_t) +
						( (sqn_list->len-1) * sizeof(uint32_t) )));
	opt_header = (struct pgm_opt_header*)(opt_len + 1);
	opt_header->opt_type	= PGM_OPT_NAK_LIST | PGM_OPT_END;
	opt_header->opt_length	= sizeof(struct pgm_opt_header) +
				  sizeof(uint8_t) +
				  ( (sqn_list->len-1) * sizeof(uint32_t) );
	opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
	opt_nak_list->opt_reserved = 0;
/* to network-order */
	for (uint_fast8_t i = 1; i < sqn_list->len; i++)
		opt_nak_list->opt_sqn[i-1] = htonl (sqn_list->sqn[i]);

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (buf, (uint16_t)tpdu_length, 0));

	sent = pgm_sendto (sock,
			   FALSE,			/* not rate limited */
			   NULL,
			   TRUE,			/* with router alert */
			   buf,
			   tpdu_length,
			   (struct sockaddr*)&sock->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	if (sent < 0 && PGM_LIKELY(PGM_SOCK_EAGAIN == pgm_get_last_sock_error()))
		return FALSE;
/* fall through silently on other errors */

	pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)tpdu_length);
	return TRUE;
}

/* cancel any pending heartbeat SPM and schedule a new one
 */

static
void
reset_heartbeat_spm (
	pgm_sock_t*const	sock,
	const pgm_time_t	now
	)
{
	pgm_mutex_lock (&sock->timer_mutex);
	const pgm_time_t next_poll = sock->next_poll;
	const pgm_time_t spm_heartbeat_interval = sock->spm_heartbeat_interval[ sock->spm_heartbeat_state = 1 ];
	sock->next_heartbeat_spm = now + spm_heartbeat_interval;
	if (pgm_time_after( next_poll, sock->next_heartbeat_spm ))
	{
		sock->next_poll = sock->next_heartbeat_spm;
		if (!sock->is_pending_read) {
			pgm_notify_send (&sock->pending_notify);
			sock->is_pending_read = TRUE;
		}
	}
	pgm_mutex_unlock (&sock->timer_mutex);
}

/* state helper for resuming sends
 */
#define STATE(x)	(sock->pkt_dontwait_state.x)

/* send one PGM data packet, transmit window owned memory.
 *
 * On success, returns PGM_IO_STATUS_NORMAL and the number of data bytes pushed
 * into the transmit window and attempted to send to the socket layer is saved 
 * into bytes_written.  On non-blocking sockets, PGM_IO_STATUS_WOULD_BLOCK is 
 * returned if the send would block.  PGM_IO_STATUS_RATE_LIMITED is returned if
 * the packet sizes would exceed the current rate limit.
 *
 * ! always returns successful if data is pushed into the transmit window, even if
 * sendto() double fails ¡  we don't want the application to try again as that is the
 * reliable socks role.
 */

static
int
send_odata (
	pgm_sock_t*           const restrict sock,
	struct pgm_sk_buff_t* const restrict skb,
	size_t*			    restrict bytes_written
	)
{
	void	*data;
	ssize_t	 sent;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);
	pgm_assert (skb->len <= sock->max_tsdu);

	pgm_debug ("send_odata (sock:%p skb:%p bytes-written:%p)",
		(void*)sock, (void*)skb, (void*)bytes_written);

	const uint16_t    tsdu_length  = skb->len;
	const sa_family_t pgmcc_family = sock->use_pgmcc ? sock->family : 0;
	const size_t      tpdu_length  = tsdu_length + pgm_pkt_offset (FALSE, pgmcc_family);

/* continue if send would block */
	if (sock->is_apdu_eagain) {
		STATE(skb)->tstamp = pgm_time_update_now();
		goto retry_send;
	}

/* add PGM header to skbuff */
	STATE(skb) = pgm_skb_get(skb);
	STATE(skb)->sock = sock;
	STATE(skb)->tstamp = pgm_time_update_now();

	STATE(skb)->pgm_header = (struct pgm_header*)STATE(skb)->head;
	STATE(skb)->pgm_data   = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
	memcpy (STATE(skb)->pgm_header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
	STATE(skb)->pgm_header->pgm_sport	= sock->tsi.sport;
	STATE(skb)->pgm_header->pgm_dport	= sock->dport;
	STATE(skb)->pgm_header->pgm_type        = PGM_ODATA;
        STATE(skb)->pgm_header->pgm_options     = sock->use_pgmcc ? PGM_OPT_PRESENT : 0;
        STATE(skb)->pgm_header->pgm_tsdu_length = htons (tsdu_length);

/* ODATA */
        STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(sock->window));
        STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(sock->window));

        STATE(skb)->pgm_header->pgm_checksum    = 0;
	data = STATE(skb)->pgm_data + 1;
	if (sock->use_pgmcc) {
		struct pgm_opt_header	   *opt_header;
		struct pgm_opt_length	   *opt_len;
		struct pgm_opt_pgmcc_data  *pgmcc_data;
		struct pgm_opt6_pgmcc_data *pgmcc_data6;

		opt_len = data;
		opt_len->opt_type	= PGM_OPT_LENGTH;
		opt_len->opt_length	= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length = htons ((uint16_t)(sizeof(struct pgm_opt_length) +
							sizeof(struct pgm_opt_header) +
							((AF_INET6 == sock->acker_nla.ss_family) ?
								sizeof(struct pgm_opt6_pgmcc_data) :
								sizeof(struct pgm_opt_pgmcc_data))  ));
		opt_header = (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type	= PGM_OPT_PGMCC_DATA | PGM_OPT_END;
		opt_header->opt_length	= sizeof(struct pgm_opt_header) +
					  ((AF_INET6 == sock->acker_nla.ss_family) ?
						sizeof(struct pgm_opt6_pgmcc_data) :
						sizeof(struct pgm_opt_pgmcc_data));
		pgmcc_data  = (struct pgm_opt_pgmcc_data *)(opt_header + 1);
		pgmcc_data6 = (struct pgm_opt6_pgmcc_data*)(opt_header + 1);

		pgmcc_data->opt_tstamp = htonl ((uint32_t)pgm_to_msecs (STATE(skb)->tstamp));
/* acker nla */
		pgm_sockaddr_to_nla ((struct sockaddr*)&sock->acker_nla, (char*)&pgmcc_data->opt_nla_afi);
		if (AF_INET6 == sock->acker_nla.ss_family)
			data = (char*)pgmcc_data6 + sizeof(struct pgm_opt6_pgmcc_data);
		else
			data = (char*)pgmcc_data  + sizeof(struct pgm_opt_pgmcc_data);
	}
	const size_t   pgm_header_len		= (char*)data - (char*)STATE(skb)->pgm_header;
	const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, (uint16_t)pgm_header_len, 0);
	STATE(unfolded_odata)			= pgm_csum_partial (data, (uint16_t)tsdu_length, 0);
        STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), (uint16_t)pgm_header_len));

/* add to transmit window, skb::data set to payload */
	pgm_spinlock_lock (&sock->txw_spinlock);
	pgm_txw_add (sock->window, STATE(skb));
	pgm_spinlock_unlock (&sock->txw_spinlock);

/* check rate limit at last moment */
	STATE(is_rate_limited) = FALSE;
	if (sock->is_nonblocking && sock->is_controlled_odata)
	{
		if (!pgm_rate_check2 (&sock->rate_control,		/* total rate limit */
				      &sock->odata_rate_control,	/* original data limit */
				      tpdu_length,			/* excludes IP header len */
				      sock->is_nonblocking))
		{
			sock->is_apdu_eagain = TRUE;
			sock->blocklen = tpdu_length + sock->iphdr_len;
			return PGM_IO_STATUS_RATE_LIMITED;
		}
		STATE(is_rate_limited) = TRUE;
	}

/* the transmit window MUST check the user count to ensure it does not 
 * attempt to send a repair-data packet based on in transit original data.
 */
retry_send:

/* congestion control */
	if (sock->use_pgmcc &&
	    sock->tokens < pgm_fp8 (1))
	{
//		pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("Token limit reached."));
		sock->is_apdu_eagain = TRUE;
		sock->blocklen = tpdu_length + sock->iphdr_len;
		return PGM_IO_STATUS_CONGESTION;	/* peer expiration to re-elect ACKer */
	}

	sent = pgm_sendto (sock,
			   !STATE(is_rate_limited),	/* rate limit on blocking */
			   &sock->odata_rate_control,
			   FALSE,			/* regular socket */
			   STATE(skb)->head,
			   tpdu_length,
			   (struct sockaddr*)&sock->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	if (sent < 0) {
		const int save_errno = pgm_get_last_sock_error();
		if (PGM_LIKELY(PGM_SOCK_EAGAIN == save_errno || PGM_SOCK_ENOBUFS == save_errno))
		{
			sock->is_apdu_eagain = TRUE;
			sock->blocklen = tpdu_length + sock->iphdr_len;
			if (PGM_SOCK_ENOBUFS == save_errno)
				return PGM_IO_STATUS_RATE_LIMITED;
			if (sock->use_pgmcc)
				pgm_notify_clear (&sock->ack_notify);
			return PGM_IO_STATUS_WOULD_BLOCK;
		}
/* fall through silently on other errors */
	}

/* save unfolded odata for retransmissions */
	pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

	sock->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (sock, STATE(skb)->tstamp);
	if (sock->use_pgmcc) {
		sock->tokens -= pgm_fp8 (1);
		sock->ack_expiry = STATE(skb)->tstamp + sock->ack_expiry_ivl;
	}

	if (PGM_LIKELY((size_t)sent == tpdu_length)) {
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += tsdu_length;
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  ++;
		pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)(tpdu_length + sock->iphdr_len));
	}

/* check for end of transmission group */
	if (sock->use_proactive_parity) {
		const uint32_t odata_sqn = ntohl (STATE(skb)->pgm_data->data_sqn);
		const uint32_t tg_sqn_mask = 0xffffffff << sock->tg_sqn_shift;
		if (!((odata_sqn + 1) & ~tg_sqn_mask))
			pgm_schedule_proactive_nak (sock, odata_sqn & tg_sqn_mask);
	}

/* remove applications reference to skbuff */
	pgm_free_skb (STATE(skb));
	if (bytes_written)
		*bytes_written = tsdu_length;
	return PGM_IO_STATUS_NORMAL;
}

/* send one PGM original data packet, callee owned memory.
 *
 * on success, returns PGM_IO_STATUS_NORMAL, on block for non-blocking sockets
 * returns PGM_IO_STATUS_WOULD_BLOCK, returns PGM_IO_STATUS_RATE_LIMITED if
 * packet size exceeds the current rate limit.
 */

static
int
send_odata_copy (
	pgm_sock_t*      const restrict	sock,
	const void*	       restrict	tsdu,
	const uint16_t			tsdu_length,
	size_t*		       restrict	bytes_written
	)
{
	void	*data;
	ssize_t	 sent;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (tsdu_length <= sock->max_tsdu);
	if (PGM_LIKELY(tsdu_length)) pgm_assert (NULL != tsdu);

	pgm_debug ("send_odata_copy (sock:%p tsdu:%p tsdu_length:%u bytes-written:%p)",
		(void*)sock, tsdu, tsdu_length, (void*)bytes_written);

	const sa_family_t pgmcc_family = sock->use_pgmcc ? sock->family : 0;
	const size_t      tpdu_length  = tsdu_length + pgm_pkt_offset (FALSE, pgmcc_family);

/* continue if blocked mid-apdu, updating timestamp */
	if (sock->is_apdu_eagain) {
		STATE(skb)->tstamp = pgm_time_update_now();
		goto retry_send;
	}

	STATE(skb) = pgm_alloc_skb (sock->max_tpdu);
	STATE(skb)->sock = sock;
	STATE(skb)->tstamp = pgm_time_update_now();
	pgm_skb_reserve (STATE(skb), (uint16_t)pgm_pkt_offset (FALSE, pgmcc_family));
	pgm_skb_put (STATE(skb), (uint16_t)tsdu_length);

	STATE(skb)->pgm_header	= (struct pgm_header*)STATE(skb)->head;
	STATE(skb)->pgm_data	= (struct pgm_data*)(STATE(skb)->pgm_header + 1);
	memcpy (STATE(skb)->pgm_header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
	STATE(skb)->pgm_header->pgm_sport	= sock->tsi.sport;
	STATE(skb)->pgm_header->pgm_dport	= sock->dport;
	STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
	STATE(skb)->pgm_header->pgm_options	= sock->use_pgmcc ? PGM_OPT_PRESENT : 0;
	STATE(skb)->pgm_header->pgm_tsdu_length = htons (tsdu_length);

/* ODATA */
	STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(sock->window));
	STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(sock->window));

	STATE(skb)->pgm_header->pgm_checksum	= 0;
	data = STATE(skb)->pgm_data + 1;
	if (sock->use_pgmcc) {
		struct pgm_opt_header		*opt_header;
		struct pgm_opt_length		*opt_len;
		struct pgm_opt_pgmcc_data	*pgmcc_data;
/* unused */
//		struct pgm_opt6_pgmcc_data	*pgmcc_data6;

		opt_len = data;
		opt_len->opt_type	= PGM_OPT_LENGTH;
		opt_len->opt_length	= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length = htons ((uint16_t)(sizeof(struct pgm_opt_length) +
							sizeof(struct pgm_opt_header) +
							((AF_INET6 == sock->acker_nla.ss_family) ?
								sizeof(struct pgm_opt6_pgmcc_data) :
								sizeof(struct pgm_opt_pgmcc_data))  ));
		opt_header = (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type	= PGM_OPT_PGMCC_DATA | PGM_OPT_END;
		opt_header->opt_length	= sizeof(struct pgm_opt_header) +
					  ((AF_INET6 == sock->acker_nla.ss_family) ?
						sizeof(struct pgm_opt6_pgmcc_data) :
						sizeof(struct pgm_opt_pgmcc_data));
		pgmcc_data  = (struct pgm_opt_pgmcc_data *)(opt_header + 1);
//		pgmcc_data6 = (struct pgm_opt6_pgmcc_data*)(opt_header + 1);

		pgmcc_data->opt_reserved = 0;
		pgmcc_data->opt_tstamp = htonl ((uint32_t)pgm_to_msecs (STATE(skb)->tstamp));
/* acker nla */
		pgm_sockaddr_to_nla ((struct sockaddr*)&sock->acker_nla, (char*)&pgmcc_data->opt_nla_afi);
		data = (char*)opt_header + opt_header->opt_length;
	}
	const size_t   pgm_header_len		= (char*)data - (char*)STATE(skb)->pgm_header;
	const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, (uint16_t)pgm_header_len, 0);
	STATE(unfolded_odata)			= pgm_csum_partial_copy (tsdu, data, (uint16_t)tsdu_length, 0);
	STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), (uint16_t)pgm_header_len));

/* add to transmit window, skb::data set to payload */
	pgm_spinlock_lock (&sock->txw_spinlock);
	pgm_txw_add (sock->window, STATE(skb));
	pgm_spinlock_unlock (&sock->txw_spinlock);

/* check rate limit at last moment */
	STATE(is_rate_limited) = FALSE;
	if (sock->is_nonblocking && sock->is_controlled_odata)
	{
		if (!pgm_rate_check2 (&sock->rate_control,		/* total rate limit */
				      &sock->odata_rate_control,	/* original data limit */
				      tpdu_length,			/* excludes IP header len */
				      sock->is_nonblocking))
		{
			sock->is_apdu_eagain = TRUE;
			sock->blocklen = tpdu_length + sock->iphdr_len;
			return PGM_IO_STATUS_RATE_LIMITED;
		}
		STATE(is_rate_limited) = TRUE;
	}
retry_send:

/* congestion control */
	if (sock->use_pgmcc && 
	    sock->tokens < pgm_fp8 (1))
	{
//		pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("Token limit reached."));
		sock->is_apdu_eagain = TRUE;
		sock->blocklen = tpdu_length + sock->iphdr_len;
		return PGM_IO_STATUS_CONGESTION;
	}

	sent = pgm_sendto (sock,
			   !STATE(is_rate_limited),	/* rate limit on blocking */
			   &sock->odata_rate_control,
			   FALSE,			/* regular socket */
			   STATE(skb)->head,
			   tpdu_length,
			   (struct sockaddr*)&sock->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	if (sent < 0) {
		const int save_errno = pgm_get_last_sock_error();
		if (PGM_LIKELY(PGM_SOCK_EAGAIN == save_errno || PGM_SOCK_ENOBUFS == save_errno))
		{
			sock->is_apdu_eagain = TRUE;
			sock->blocklen = tpdu_length + sock->iphdr_len;
			if (PGM_SOCK_ENOBUFS == save_errno)
				return PGM_IO_STATUS_RATE_LIMITED;
			if (sock->use_pgmcc)
				pgm_notify_clear (&sock->ack_notify);
			return PGM_IO_STATUS_WOULD_BLOCK;
		}
/* fall through silently on other errors */
	}

	if (sock->use_pgmcc) {
		sock->tokens -= pgm_fp8 (1);
		pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("PGMCC tokens-- (T:%u W:%u)"),
		 	   pgm_fp8tou (sock->tokens), pgm_fp8tou (sock->cwnd_size));
		sock->ack_expiry = STATE(skb)->tstamp + sock->ack_expiry_ivl;
	}

/* save unfolded odata for retransmissions */
	pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

	sock->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (sock, STATE(skb)->tstamp);

	if (PGM_LIKELY((size_t)sent == tpdu_length)) {
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += tsdu_length;
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  ++;
		pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)(tpdu_length + sock->iphdr_len));
	}

/* check for end of transmission group */
	if (sock->use_proactive_parity) {
		const uint32_t odata_sqn = ntohl (STATE(skb)->pgm_data->data_sqn);
		const uint32_t tg_sqn_mask = 0xffffffff << sock->tg_sqn_shift;
		if (!((odata_sqn + 1) & ~tg_sqn_mask))
			pgm_schedule_proactive_nak (sock, odata_sqn & tg_sqn_mask);
	}

/* return data payload length sent */
	if (bytes_written)
		*bytes_written = tsdu_length;
	return PGM_IO_STATUS_NORMAL;
}

/* send one PGM original data packet, callee owned scatter/gather io vector
 *
 *    ⎢ DATA₀ ⎢
 *    ⎢ DATA₁ ⎢ → send_odatav() →  ⎢ TSDU₀ ⎢ → libc
 *    ⎢   ⋮   ⎢
 *
 * on success, returns PGM_IO_STATUS_NORMAL, on block for non-blocking sockets
 * returns PGM_IO_STATUS_WOULD_BLOCK, returns PGM_IO_STATUS_RATE_LIMITED if
 * packet size exceeds the current rate limit.
 */

static
int
send_odatav (
	pgm_sock_t*		const restrict sock,
	const struct pgm_iovec* const restrict vector,
	const unsigned			       count,		/* number of items in vector */
	size_t*		 	      restrict bytes_written
	)
{
	char		*dst;
	size_t		 tpdu_length;
	ssize_t		 sent;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (count <= PGM_MAX_FRAGMENTS);
	if (PGM_LIKELY(count)) pgm_assert (NULL != vector);

	pgm_debug ("send_odatav (sock:%p vector:%p count:%u bytes-written:%p)",
		(const void*)sock, (const void*)vector, count, (const void*)bytes_written);

	if (PGM_UNLIKELY(0 == count))
		return send_odata_copy (sock, NULL, 0, bytes_written);

/* continue if blocked on send */
	if (sock->is_apdu_eagain) {
		pgm_assert ((char*)STATE(skb)->tail > (char*)STATE(skb)->head);
		tpdu_length = (char*)STATE(skb)->tail - (char*)STATE(skb)->head;
		goto retry_send;
	}

	STATE(tsdu_length) = 0;
	for (unsigned i = 0; i < count; i++)
	{
#ifdef TRANSPORT_DEBUG
		if (PGM_LIKELY(vector[i].iov_len)) {
			pgm_assert( vector[i].iov_base );
		}
#endif
		STATE(tsdu_length) += vector[i].iov_len;
	}
	pgm_return_val_if_fail (STATE(tsdu_length) <= sock->max_tsdu, PGM_IO_STATUS_ERROR);

	STATE(skb) = pgm_alloc_skb (sock->max_tpdu);
	STATE(skb)->sock = sock;
	STATE(skb)->tstamp = pgm_time_update_now();
	const sa_family_t pgmcc_family = sock->use_pgmcc ? sock->family : 0;
	pgm_skb_reserve (STATE(skb), (uint16_t)pgm_pkt_offset (FALSE, pgmcc_family));
	pgm_skb_put (STATE(skb), (uint16_t)STATE(tsdu_length));

	STATE(skb)->pgm_header  = (struct pgm_header*)STATE(skb)->data;
	STATE(skb)->pgm_data    = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
	memcpy (STATE(skb)->pgm_header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
	STATE(skb)->pgm_header->pgm_sport	= sock->tsi.sport;
	STATE(skb)->pgm_header->pgm_dport	= sock->dport;
	STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
	STATE(skb)->pgm_header->pgm_options	= 0;
	STATE(skb)->pgm_header->pgm_tsdu_length = htons ((uint16_t)STATE(tsdu_length));

/* ODATA */
	STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(sock->window));
	STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(sock->window));

	STATE(skb)->pgm_header->pgm_checksum	= 0;
	const size_t   pgm_header_len		= (char*)(STATE(skb)->pgm_data + 1) - (char*)STATE(skb)->pgm_header;
	const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, (uint16_t)pgm_header_len, 0);

/* unroll first iteration to make friendly branch prediction */
	dst			= (char*)(STATE(skb)->pgm_data + 1);
	STATE(unfolded_odata)	= pgm_csum_partial_copy ((const char*)vector[0].iov_base, dst, (uint16_t)vector[0].iov_len, 0);

/* iterate over one or more vector elements to perform scatter/gather checksum & copy */
	for (unsigned i = 1; i < count; i++) {
		dst += vector[i-1].iov_len;
		const uint32_t unfolded_element = pgm_csum_partial_copy ((const char*)vector[i].iov_base, dst, (uint16_t)vector[i].iov_len, 0);
		STATE(unfolded_odata) = pgm_csum_block_add (STATE(unfolded_odata), unfolded_element, (uint16_t)vector[i-1].iov_len);
	}

	STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), (uint16_t)pgm_header_len));

/* add to transmit window, skb::data set to payload */
	pgm_spinlock_lock (&sock->txw_spinlock);
	pgm_txw_add (sock->window, STATE(skb));
	pgm_spinlock_unlock (&sock->txw_spinlock);

	pgm_assert ((char*)STATE(skb)->tail > (char*)STATE(skb)->head);
	tpdu_length = (char*)STATE(skb)->tail - (char*)STATE(skb)->head;

/* check rate limit at last moment */
	STATE(is_rate_limited) = FALSE;
	if (sock->is_nonblocking && sock->is_controlled_odata)
	{
		if (!pgm_rate_check2 (&sock->rate_control,		/* total rate limit */
				      &sock->odata_rate_control,	/* original data limit */
				      tpdu_length,			/* excludes IP header len */
				      sock->is_nonblocking))
		{
			sock->is_apdu_eagain = TRUE;
			sock->blocklen = tpdu_length + sock->iphdr_len;
			return PGM_IO_STATUS_RATE_LIMITED;
		}
		STATE(is_rate_limited) = TRUE;
	}

retry_send:
	sent = pgm_sendto (sock,
			   !STATE(is_rate_limited),	/* rate limit on blocking */
			   &sock->odata_rate_control,
			   FALSE,			/* regular socket */
			   STATE(skb)->head,
			   tpdu_length,
			   (struct sockaddr*)&sock->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	if (sent < 0) {
		const int save_errno = pgm_get_last_sock_error();
		if (PGM_LIKELY(PGM_SOCK_EAGAIN == save_errno || PGM_SOCK_ENOBUFS == save_errno))
		{
			sock->is_apdu_eagain = TRUE;
			sock->blocklen = tpdu_length + sock->iphdr_len;
			if (PGM_SOCK_ENOBUFS == save_errno)
				return PGM_IO_STATUS_RATE_LIMITED;
			if (sock->use_pgmcc)
				pgm_notify_clear (&sock->ack_notify);
			return PGM_IO_STATUS_WOULD_BLOCK;
		}
/* fall through silently on other errors */
	}

/* save unfolded odata for retransmissions */
	pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

	sock->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (sock, STATE(skb)->tstamp);

	if (PGM_LIKELY((size_t)sent == STATE(skb)->len)) {
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += STATE(tsdu_length);
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  ++;
		pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)(tpdu_length + sock->iphdr_len));
	}

/* check for end of transmission group */
	if (sock->use_proactive_parity) {
		const uint32_t odata_sqn   = ntohl (STATE(skb)->pgm_data->data_sqn);
		const uint32_t tg_sqn_mask = 0xffffffff << sock->tg_sqn_shift;
		if (!((odata_sqn + 1) & ~tg_sqn_mask))
			pgm_schedule_proactive_nak (sock, odata_sqn & tg_sqn_mask);
	}

/* return data payload length sent */
	if (bytes_written)
		*bytes_written = STATE(tsdu_length);
	return PGM_IO_STATUS_NORMAL;
}

/* send PGM original data, callee owned memory.  if larger than maximum TPDU
 * size will be fragmented.
 *
 * on success, returns PGM_IO_STATUS_NORMAL, on block for non-blocking sockets
 * returns PGM_IO_STATUS_WOULD_BLOCK, returns PGM_IO_STATUS_RATE_LIMITED if
 * packet size exceeds the current rate limit.
 */

static
int
send_apdu (
	pgm_sock_t* 	 const restrict	sock,
	const void*	       restrict	apdu,
	const size_t			apdu_length,
	size_t*		       restrict	bytes_written
	)
{
	size_t		bytes_sent = 0;		/* counted at IP layer */
	unsigned	packets_sent = 0;	/* IP packets */
	size_t		data_bytes_sent = 0;
	int		save_errno;

	pgm_assert (NULL != sock);
	pgm_assert (NULL != apdu);

	const sa_family_t pgmcc_family = sock->use_pgmcc ? sock->family : 0;

/* continue if blocked mid-apdu */
	if (sock->is_apdu_eagain)
		goto retry_send;

/* if non-blocking calculate total wire size and check rate limit */
	STATE(is_rate_limited) = FALSE;
	if (sock->is_nonblocking && sock->is_controlled_odata)
	{
		const size_t header_length = pgm_pkt_offset (TRUE, pgmcc_family);
		size_t tpdu_length = 0;
		size_t offset_	   = 0;

		do {
			const uint_fast16_t tsdu_length = (uint_fast16_t)MIN( source_max_tsdu (sock, TRUE), apdu_length - offset_ );
			tpdu_length += sock->iphdr_len + header_length + tsdu_length;
			offset_ += tsdu_length;
		} while (offset_ < apdu_length);

		if (!pgm_rate_check2 (&sock->rate_control,
				      &sock->odata_rate_control,
				      tpdu_length - sock->iphdr_len,	/* includes 1 × IP header len */
				      sock->is_nonblocking))
		{
			sock->blocklen = tpdu_length;
			return PGM_IO_STATUS_RATE_LIMITED;
		}
		STATE(is_rate_limited) = TRUE;
	}

	STATE(data_bytes_offset)	= 0;
	STATE(first_sqn)		= pgm_txw_next_lead(sock->window);

	do {
		size_t			 tpdu_length, header_length;
		struct pgm_opt_header	*opt_header;
		struct pgm_opt_length	*opt_len;
		ssize_t			 sent;

/* retrieve packet storage from transmit window */
		header_length = pgm_pkt_offset (TRUE, pgmcc_family);
		STATE(tsdu_length) = MIN( source_max_tsdu (sock, TRUE), apdu_length - STATE(data_bytes_offset) );

		STATE(skb) = pgm_alloc_skb (sock->max_tpdu);
		STATE(skb)->sock = sock;
		STATE(skb)->tstamp = pgm_time_update_now();
		pgm_skb_reserve (STATE(skb), (uint16_t)header_length);
		pgm_skb_put (STATE(skb), (uint16_t)STATE(tsdu_length));

		STATE(skb)->pgm_header  = (struct pgm_header*)STATE(skb)->head;
		STATE(skb)->pgm_data    = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
		memcpy (STATE(skb)->pgm_header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
		STATE(skb)->pgm_header->pgm_sport	= sock->tsi.sport;
		STATE(skb)->pgm_header->pgm_dport	= sock->dport;
		STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
		STATE(skb)->pgm_header->pgm_options	= PGM_OPT_PRESENT;
		STATE(skb)->pgm_header->pgm_tsdu_length = htons ((uint16_t)STATE(tsdu_length));

/* ODATA */
		STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(sock->window));
		STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(sock->window));

/* OPT_LENGTH */
		opt_len					= (struct pgm_opt_length*)(STATE(skb)->pgm_data + 1);
		opt_len->opt_type			= PGM_OPT_LENGTH;
		opt_len->opt_length			= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length		= htons ((uint16_t)(sizeof(struct pgm_opt_length) +
									sizeof(struct pgm_opt_header) +
									sizeof(struct pgm_opt_fragment)));
/* OPT_FRAGMENT */
		opt_header				= (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type			= PGM_OPT_FRAGMENT | PGM_OPT_END;
		opt_header->opt_length			= sizeof(struct pgm_opt_header) +
						  	  sizeof(struct pgm_opt_fragment);
		STATE(skb)->pgm_opt_fragment			= (struct pgm_opt_fragment*)(opt_header + 1);
		STATE(skb)->pgm_opt_fragment->opt_reserved	= 0;
		STATE(skb)->pgm_opt_fragment->opt_sqn		= htonl (STATE(first_sqn));
		STATE(skb)->pgm_opt_fragment->opt_frag_off	= htonl ((uint32_t)STATE(data_bytes_offset));
		STATE(skb)->pgm_opt_fragment->opt_frag_len	= htonl ((uint32_t)apdu_length);

/* TODO: the assembly checksum & copy routine is faster than memcpy & pgm_cksum on >= opteron hardware */
		STATE(skb)->pgm_header->pgm_checksum	= 0;
		const size_t   pgm_header_len		= (char*)(STATE(skb)->pgm_opt_fragment + 1) - (char*)STATE(skb)->pgm_header;
		const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, (uint16_t)pgm_header_len, 0);
		STATE(unfolded_odata)			= pgm_csum_partial_copy ((const char*)apdu + STATE(data_bytes_offset), STATE(skb)->pgm_opt_fragment + 1, (uint16_t)STATE(tsdu_length), 0);
		STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), (uint16_t)pgm_header_len));

/* add to transmit window, skb::data set to payload */
		pgm_spinlock_lock (&sock->txw_spinlock);
		pgm_txw_add (sock->window, STATE(skb));
		pgm_spinlock_unlock (&sock->txw_spinlock);

retry_send:
		pgm_assert ((char*)STATE(skb)->tail > (char*)STATE(skb)->head);
		tpdu_length = (char*)STATE(skb)->tail - (char*)STATE(skb)->head;
		sent = pgm_sendto (sock,
				   !STATE(is_rate_limited),	/* rate limit on blocking */
			   	   &sock->odata_rate_control,
				   FALSE,			/* regular socket */
				   STATE(skb)->head,
				   tpdu_length,
				   (struct sockaddr*)&sock->send_gsr.gsr_group,
				   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
		if (sent < 0) {
			save_errno = pgm_get_last_sock_error();
			if (PGM_LIKELY(PGM_SOCK_EAGAIN == save_errno || PGM_SOCK_ENOBUFS == save_errno))
			{
				sock->is_apdu_eagain = TRUE;
				sock->blocklen = tpdu_length + sock->iphdr_len;
				goto blocked;
			}
/* fall through silently on other errors */
		}

/* save unfolded odata for retransmissions */
		pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

		if (PGM_LIKELY((size_t)sent == tpdu_length)) {
			bytes_sent += tpdu_length + sock->iphdr_len;	/* as counted at IP layer */
			packets_sent++;					/* IP packets */
			data_bytes_sent += STATE(tsdu_length);
		}

		STATE(data_bytes_offset) += STATE(tsdu_length);

/* check for end of transmission group */
		if (sock->use_proactive_parity) {
			const uint32_t odata_sqn = ntohl (STATE(skb)->pgm_data->data_sqn);
			const uint32_t tg_sqn_mask = 0xffffffff << sock->tg_sqn_shift;
			if (!((odata_sqn + 1) & ~tg_sqn_mask))
				pgm_schedule_proactive_nak (sock, odata_sqn & tg_sqn_mask);
		}

	} while ( STATE(data_bytes_offset)  < apdu_length);
	pgm_assert( STATE(data_bytes_offset) == apdu_length );

	sock->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (sock, STATE(skb)->tstamp);

	pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)bytes_sent);
	sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
	sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	if (bytes_written)
		*bytes_written = apdu_length;
	return PGM_IO_STATUS_NORMAL;

blocked:
	if (bytes_sent) {
		reset_heartbeat_spm (sock, STATE(skb)->tstamp);
		pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)bytes_sent);
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	}
	if (PGM_SOCK_ENOBUFS == save_errno)
		return PGM_IO_STATUS_RATE_LIMITED;
	if (sock->use_pgmcc)
		pgm_notify_clear (&sock->ack_notify);
	return PGM_IO_STATUS_WOULD_BLOCK;
}

/* Send one APDU, whether it fits within one TPDU or more.
 *
 * on success, returns PGM_IO_STATUS_NORMAL, on block for non-blocking sockets
 * returns PGM_IO_STATUS_WOULD_BLOCK, returns PGM_IO_STATUS_RATE_LIMITED if
 * packet size exceeds the current rate limit.
 */

int
pgm_send (
	pgm_sock_t* 	 const restrict sock,
	const void*	       restrict	apdu,
	const size_t			apdu_length,
	size_t*	       	       restrict	bytes_written
	)
{
	pgm_debug ("pgm_send (sock:%p apdu:%p apdu-length:%" PRIzu " bytes-written:%p)",
		(void*)sock, apdu, apdu_length, (void*)bytes_written);

/* parameters */
	pgm_return_val_if_fail (NULL != sock, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(apdu_length)) pgm_return_val_if_fail (NULL != apdu, PGM_IO_STATUS_ERROR);

/* shutdown */
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&sock->lock)))
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);

/* state */
	if (PGM_UNLIKELY(!sock->is_bound ||
	    sock->is_destroyed ||
	    apdu_length > sock->max_apdu))
	{
		pgm_rwlock_reader_unlock (&sock->lock);
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	}

/* source */
	pgm_mutex_lock (&sock->source_mutex);

/* pass on non-fragment calls */
	if (apdu_length <= sock->max_tsdu)
	{
		const int status = send_odata_copy (sock, apdu, (uint16_t)apdu_length, bytes_written);
		pgm_mutex_unlock (&sock->source_mutex);
		pgm_rwlock_reader_unlock (&sock->lock);
		return status;
	}
	else
	{
		const int status = send_apdu (sock, apdu, (uint16_t)apdu_length, bytes_written);
		pgm_mutex_unlock (&sock->source_mutex);
		pgm_rwlock_reader_unlock (&sock->lock);
		return status;
	}
}

/* send PGM original data, callee owned scatter/gather IO vector.  if larger than maximum TPDU
 * size will be fragmented.
 *
 * is_one_apdu = true:
 *
 *    ⎢ DATA₀ ⎢
 *    ⎢ DATA₁ ⎢ → pgm_sendv() →  ⎢ ⋯ TSDU₁ TSDU₀ ⎢ → libc
 *    ⎢   ⋮   ⎢
 *
 * is_one_apdu = false:
 *
 *    ⎢ APDU₀ ⎢                  ⎢ ⋯ TSDU₁,₀ TSDU₀,₀ ⎢
 *    ⎢ APDU₁ ⎢ → pgm_sendv() →  ⎢ ⋯ TSDU₁,₁ TSDU₀,₁ ⎢ → libc
 *    ⎢   ⋮   ⎢                  ⎢     ⋮       ⋮     ⎢
 *
 * on success, returns PGM_IO_STATUS_NORMAL, on block for non-blocking sockets
 * returns PGM_IO_STATUS_WOULD_BLOCK, returns PGM_IO_STATUS_RATE_LIMITED if
 * packet size exceeds the current rate limit.
 */

int
pgm_sendv (
	pgm_sock_t*		const restrict sock,
	const struct pgm_iovec* const restrict vector,
	const unsigned			       count,		/* number of items in vector */
	const bool			       is_one_apdu,	/* true  = vector = apdu, false = vector::iov_base = apdu */
        size_t*                       restrict bytes_written
	)
{
	unsigned	packets_sent = 0;
	size_t		bytes_sent = 0;
	size_t		data_bytes_sent = 0;
	int		save_errno;

	pgm_debug ("pgm_sendv (sock:%p vector:%p count:%u is-one-apdu:%s bytes-written:%p)",
		(const void*)sock,
		(const void*)vector,
		count,
		is_one_apdu ? "TRUE" : "FALSE",
		(const void*)bytes_written);

	pgm_return_val_if_fail (NULL != sock, PGM_IO_STATUS_ERROR);
	pgm_return_val_if_fail (count <= PGM_MAX_FRAGMENTS, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(count)) pgm_return_val_if_fail (NULL != vector, PGM_IO_STATUS_ERROR);
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&sock->lock)))
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	if (PGM_UNLIKELY(!sock->is_bound ||
	    sock->is_destroyed))
	{
		pgm_rwlock_reader_unlock (&sock->lock);
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	}

	pgm_mutex_lock (&sock->source_mutex);

/* pass on zero length as cannot count vector lengths */
	if (PGM_UNLIKELY(0 == count))
	{
		const int status = send_odata_copy (sock, NULL, 0, bytes_written);
		pgm_mutex_unlock (&sock->source_mutex);
		pgm_rwlock_reader_unlock (&sock->lock);
		return status;
	}

	const sa_family_t pgmcc_family = sock->use_pgmcc ? sock->family : 0;

/* continue if blocked mid-apdu */
	if (sock->is_apdu_eagain) {
		if (is_one_apdu) {
			if (STATE(apdu_length) <= sock->max_tsdu)
			{
				const int status = send_odatav (sock, vector, count, bytes_written);
				pgm_mutex_unlock (&sock->source_mutex);
				pgm_rwlock_reader_unlock (&sock->lock);
				return status;
			}
			else
				goto retry_one_apdu_send;
		} else {
			goto retry_send;
		}
	}

/* calculate (total) APDU length */
	STATE(apdu_length)	= 0;
	for (unsigned i = 0; i < count; i++)
	{
#ifdef TRANSPORT_DEBUG
		if (PGM_LIKELY(vector[i].iov_len)) {
			pgm_assert( vector[i].iov_base );
		}
#endif
		if (!is_one_apdu &&
		    vector[i].iov_len > sock->max_apdu)
		{
			pgm_mutex_unlock (&sock->source_mutex);
			pgm_rwlock_reader_unlock (&sock->lock);
			pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
		}
		STATE(apdu_length) += vector[i].iov_len;
	}

/* pass on non-fragment calls */
	if (is_one_apdu) {
		if (STATE(apdu_length) <= sock->max_tsdu) {
			const int status = send_odatav (sock, vector, count, bytes_written);
			pgm_mutex_unlock (&sock->source_mutex);
			pgm_rwlock_reader_unlock (&sock->lock);
			return status;
		} else if (STATE(apdu_length) > sock->max_apdu) {
			pgm_mutex_unlock (&sock->source_mutex);
			pgm_rwlock_reader_unlock (&sock->lock);
			pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
		}
	}

/* non-fragmented packets can be forwarded onto basic send() */
	if (!is_one_apdu)
	{
		for (STATE(data_pkt_offset) = 0; STATE(data_pkt_offset) < count; STATE(data_pkt_offset)++)
		{
			int	status;
			size_t	wrote_bytes;
retry_send:
			status = send_apdu (sock,
					    vector[STATE(data_pkt_offset)].iov_base,
					    vector[STATE(data_pkt_offset)].iov_len,
					    &wrote_bytes);
			switch (status) {
			case PGM_IO_STATUS_NORMAL:
				break;
			case PGM_IO_STATUS_WOULD_BLOCK:
			case PGM_IO_STATUS_RATE_LIMITED:
				sock->is_apdu_eagain = TRUE;
				pgm_mutex_unlock (&sock->source_mutex);
				pgm_rwlock_reader_unlock (&sock->lock);
				return status;
			case PGM_IO_STATUS_ERROR:
				pgm_mutex_unlock (&sock->source_mutex);
				pgm_rwlock_reader_unlock (&sock->lock);
				return status;
			default:
				pgm_assert_not_reached();
			}
			data_bytes_sent += wrote_bytes;
		}

		sock->is_apdu_eagain = FALSE;
		if (bytes_written)
			*bytes_written = data_bytes_sent;
		pgm_mutex_unlock (&sock->source_mutex);
		pgm_rwlock_reader_unlock (&sock->lock);
		return PGM_IO_STATUS_NORMAL;
	}

/* if non-blocking calculate total wire size and check rate limit */
	STATE(is_rate_limited) = FALSE;
	if (sock->is_nonblocking && sock->is_controlled_odata)
        {
		const size_t header_length = pgm_pkt_offset (TRUE, pgmcc_family);
                size_t tpdu_length = 0;
		size_t offset_	   = 0;

		do {
			const uint_fast16_t tsdu_length = (uint_fast16_t)MIN( source_max_tsdu (sock, TRUE), STATE(apdu_length) - offset_ );
			tpdu_length += sock->iphdr_len + header_length + tsdu_length;
			offset_     += tsdu_length;
		} while (offset_ < STATE(apdu_length));

                if (!pgm_rate_check2 (&sock->rate_control,
				      &sock->odata_rate_control,
				      tpdu_length - sock->iphdr_len,	/* includes 1 × IP header len */
				      sock->is_nonblocking))
		{
			sock->blocklen = tpdu_length;
			pgm_mutex_unlock (&sock->source_mutex);
			pgm_rwlock_reader_unlock (&sock->lock);
			return PGM_IO_STATUS_RATE_LIMITED;
		}
		STATE(is_rate_limited) = TRUE;
        }

	STATE(data_bytes_offset)	= 0;
	STATE(vector_index)		= 0;
	STATE(vector_offset)		= 0;

	STATE(first_sqn)		= pgm_txw_next_lead(sock->window);

	do {
		size_t			 tpdu_length, header_length;
		struct pgm_opt_header	*opt_header;
		struct pgm_opt_length	*opt_len;
		const char		*src;
		char			*dst;
		size_t			 src_length, dst_length, copy_length;
		ssize_t			 sent;

/* retrieve packet storage from transmit window */
		header_length = pgm_pkt_offset (TRUE, pgmcc_family);
		STATE(tsdu_length) = MIN( source_max_tsdu (sock, TRUE), STATE(apdu_length) - STATE(data_bytes_offset) );
		STATE(skb) = pgm_alloc_skb (sock->max_tpdu);
		STATE(skb)->sock = sock;
		STATE(skb)->tstamp = pgm_time_update_now();
		pgm_skb_reserve (STATE(skb), (uint16_t)header_length);
		pgm_skb_put (STATE(skb), (uint16_t)STATE(tsdu_length));

		STATE(skb)->pgm_header  = (struct pgm_header*)STATE(skb)->head;
		STATE(skb)->pgm_data    = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
		memcpy (STATE(skb)->pgm_header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
		STATE(skb)->pgm_header->pgm_sport	= sock->tsi.sport;
		STATE(skb)->pgm_header->pgm_dport	= sock->dport;
		STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
		STATE(skb)->pgm_header->pgm_options	= PGM_OPT_PRESENT;
		STATE(skb)->pgm_header->pgm_tsdu_length = htons ((uint16_t)STATE(tsdu_length));

/* ODATA */
		STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(sock->window));
		STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(sock->window));

/* OPT_LENGTH */
		opt_len					= (struct pgm_opt_length*)(STATE(skb)->pgm_data + 1);
		opt_len->opt_type			= PGM_OPT_LENGTH;
		opt_len->opt_length			= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length		= htons ((uint16_t)(sizeof(struct pgm_opt_length) +
									sizeof(struct pgm_opt_header) +
									sizeof(struct pgm_opt_fragment)));
/* OPT_FRAGMENT */
		opt_header				= (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type			= PGM_OPT_FRAGMENT | PGM_OPT_END;
		opt_header->opt_length			= sizeof(struct pgm_opt_header) +
							  sizeof(struct pgm_opt_fragment);
		STATE(skb)->pgm_opt_fragment			= (struct pgm_opt_fragment*)(opt_header + 1);
		STATE(skb)->pgm_opt_fragment->opt_reserved	= 0;
		STATE(skb)->pgm_opt_fragment->opt_sqn		= htonl (STATE(first_sqn));
		STATE(skb)->pgm_opt_fragment->opt_frag_off	= htonl ((uint32_t)STATE(data_bytes_offset));
		STATE(skb)->pgm_opt_fragment->opt_frag_len	= htonl ((uint32_t)STATE(apdu_length));

/* checksum & copy */
		STATE(skb)->pgm_header->pgm_checksum	= 0;
		const size_t   pgm_header_len		= (char*)(STATE(skb)->pgm_opt_fragment + 1) - (char*)STATE(skb)->pgm_header;
		const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, (uint16_t)pgm_header_len, 0);

/* iterate over one or more vector elements to perform scatter/gather checksum & copy
 *
 * STATE(vector_index)	- index into application scatter/gather vector
 * STATE(vector_offset) - current offset into current vector element
 * STATE(unfolded_odata)- checksum accumulator
 */
		src		= (const char*)vector[STATE(vector_index)].iov_base + STATE(vector_offset);
		dst		= (char*)(STATE(skb)->pgm_opt_fragment + 1);
		src_length	= vector[STATE(vector_index)].iov_len - STATE(vector_offset);
		dst_length	= 0;
		copy_length	= MIN( STATE(tsdu_length), src_length );
		STATE(unfolded_odata)	= pgm_csum_partial_copy (src, dst, (uint16_t)copy_length, 0);

		for(;;)
		{
			if (copy_length == src_length) {
/* application packet complete */
				STATE(vector_index)++;
				STATE(vector_offset) = 0;
			} else {
/* data still remaining */
				STATE(vector_offset) += copy_length;
			}

			dst_length += copy_length;

/* sock packet complete */
			if (dst_length == STATE(tsdu_length))
				break;

			src		= (const char*)vector[STATE(vector_index)].iov_base + STATE(vector_offset);
			dst	       += copy_length;
			src_length	= vector[STATE(vector_index)].iov_len - STATE(vector_offset);
			copy_length	= MIN( STATE(tsdu_length) - dst_length, src_length );
			const uint32_t unfolded_element = pgm_csum_partial_copy (src, dst, (uint16_t)copy_length, 0);
			STATE(unfolded_odata) = pgm_csum_block_add (STATE(unfolded_odata), unfolded_element, (uint16_t)dst_length);
		}

		STATE(skb)->pgm_header->pgm_checksum = pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), (uint16_t)pgm_header_len));

/* add to transmit window, skb::data set to payload */
		pgm_spinlock_lock (&sock->txw_spinlock);
		pgm_txw_add (sock->window, STATE(skb));
		pgm_spinlock_unlock (&sock->txw_spinlock);

retry_one_apdu_send:
		tpdu_length = (char*)STATE(skb)->tail - (char*)STATE(skb)->head;
		sent = pgm_sendto (sock,
				   !STATE(is_rate_limited),	/* rate limited on blocking */
			   	   &sock->odata_rate_control,
				   FALSE,			/* regular socket */
				   STATE(skb)->head,
				   tpdu_length,
				   (struct sockaddr*)&sock->send_gsr.gsr_group,
				   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
		if (sent < 0) {
			save_errno = pgm_get_last_sock_error();
			if (PGM_LIKELY(PGM_SOCK_EAGAIN == save_errno || PGM_SOCK_ENOBUFS == save_errno))
			{
				sock->is_apdu_eagain = TRUE;
				sock->blocklen = tpdu_length + sock->iphdr_len;
				goto blocked;
			}
/* fall through silently on other errors */
		}

/* save unfolded odata for retransmissions */
		pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

		if (PGM_LIKELY((size_t)sent == tpdu_length)) {
			bytes_sent += tpdu_length + sock->iphdr_len;	/* as counted at IP layer */
			packets_sent++;					/* IP packets */
			data_bytes_sent += STATE(tsdu_length);
		}

		STATE(data_bytes_offset) += STATE(tsdu_length);

/* check for end of transmission group */
		if (sock->use_proactive_parity) {
			const uint32_t odata_sqn = ntohl (STATE(skb)->pgm_data->data_sqn);
			const uint32_t tg_sqn_mask = 0xffffffff << sock->tg_sqn_shift;
			if (!((odata_sqn + 1) & ~tg_sqn_mask))
				pgm_schedule_proactive_nak (sock, odata_sqn & tg_sqn_mask);
		}

	} while ( STATE(data_bytes_offset)  < STATE(apdu_length) );
	pgm_assert( STATE(data_bytes_offset) == STATE(apdu_length) );

	sock->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (sock, STATE(skb)->tstamp);

	pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)bytes_sent);
	sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
	sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	if (bytes_written)
		*bytes_written = STATE(apdu_length);
	pgm_mutex_unlock (&sock->source_mutex);
	pgm_rwlock_reader_unlock (&sock->lock);
	return PGM_IO_STATUS_NORMAL;

blocked:
	if (bytes_sent) {
		reset_heartbeat_spm (sock, STATE(skb)->tstamp);
		pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)bytes_sent);
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	}
	pgm_mutex_unlock (&sock->source_mutex);
	pgm_rwlock_reader_unlock (&sock->lock);
	if (PGM_SOCK_ENOBUFS == save_errno)
		return PGM_IO_STATUS_RATE_LIMITED;
	if (sock->use_pgmcc)
		pgm_notify_clear (&sock->ack_notify);
	return PGM_IO_STATUS_WOULD_BLOCK;
}

/* send PGM original data, transmit window owned scatter/gather IO vector.
 *
 *    ⎢ TSDU₀ ⎢
 *    ⎢ TSDU₁ ⎢ → pgm_send_skbv() →  ⎢ ⋯ TSDU₁ TSDU₀ ⎢ → libc
 *    ⎢   ⋮   ⎢
 *
 * on success, returns PGM_IO_STATUS_NORMAL, on block for non-blocking sockets
 * returns PGM_IO_STATUS_WOULD_BLOCK, returns PGM_IO_STATUS_RATE_LIMITED if
 * packet size exceeds the current rate limit.
 */

int
pgm_send_skbv (
	pgm_sock_t*            const restrict sock,
	struct pgm_sk_buff_t** const restrict vector,		/* array of skb pointers vs. array of skbs */
	const unsigned			      count,
	const bool			      is_one_apdu,	/* true: vector = apdu, false: vector::iov_base = apdu */
	size_t*		 	     restrict bytes_written
	)
{
	unsigned	packets_sent = 0;
	size_t		bytes_sent = 0;
	size_t		data_bytes_sent = 0;
	int		save_errno;

	pgm_debug ("pgm_send_skbv (sock:%p vector:%p count:%u is-one-apdu:%s bytes-written:%p)",
		(const void*)sock,
		(const void*)vector,
		count,
		is_one_apdu ? "TRUE" : "FALSE",
		(const void*)bytes_written);

	pgm_return_val_if_fail (NULL != sock, PGM_IO_STATUS_ERROR);
	pgm_return_val_if_fail (count <= PGM_MAX_FRAGMENTS, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(count)) pgm_return_val_if_fail (NULL != vector, PGM_IO_STATUS_ERROR);
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&sock->lock)))
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	if (PGM_UNLIKELY(!sock->is_bound ||
	    sock->is_destroyed))
	{
		pgm_rwlock_reader_unlock (&sock->lock);
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	}

	pgm_mutex_lock (&sock->source_mutex);

/* pass on zero length as cannot count vector lengths */
	if (PGM_UNLIKELY(0 == count))
	{
		const int status = send_odata_copy (sock, NULL, 0, bytes_written);
		pgm_mutex_unlock (&sock->source_mutex);
		pgm_rwlock_reader_unlock (&sock->lock);
		return status;
	}
	else if (1 == count)
	{
		const int status = send_odata (sock, vector[0], bytes_written);
		pgm_mutex_unlock (&sock->source_mutex);
		pgm_rwlock_reader_unlock (&sock->lock);
		return status;
	}

	const sa_family_t pgmcc_family = sock->use_pgmcc ? sock->family : 0;

/* continue if blocked mid-apdu */
	if (sock->is_apdu_eagain)
		goto retry_send;

	STATE(is_rate_limited) = FALSE;
	if (sock->is_nonblocking && sock->is_controlled_odata)
	{
		size_t total_tpdu_length = 0;

		for (unsigned i = 0; i < count; i++)
			total_tpdu_length += sock->iphdr_len + pgm_pkt_offset (is_one_apdu, pgmcc_family) + vector[i]->len;

		if (!pgm_rate_check2 (&sock->rate_control,
				      &sock->odata_rate_control,
				      total_tpdu_length - sock->iphdr_len,	/* includes 1 × IP header len */
				      sock->is_nonblocking))
		{
			sock->blocklen = total_tpdu_length;
			pgm_mutex_unlock (&sock->source_mutex);
			pgm_rwlock_reader_unlock (&sock->lock);
			return PGM_IO_STATUS_RATE_LIMITED;
		}
		STATE(is_rate_limited) = TRUE;
	}

	if (is_one_apdu)
	{
		STATE(apdu_length)	= 0;
		STATE(first_sqn)	= pgm_txw_next_lead(sock->window);
		for (unsigned i = 0; i < count; i++)
		{
			if (PGM_UNLIKELY(vector[i]->len > sock->max_tsdu_fragment)) {
				pgm_mutex_unlock (&sock->source_mutex);
				pgm_rwlock_reader_unlock (&sock->lock);
				return PGM_IO_STATUS_ERROR;
			}
			STATE(apdu_length) += vector[i]->len;
		}
		if (PGM_UNLIKELY(STATE(apdu_length) > sock->max_apdu)) {
			pgm_mutex_unlock (&sock->source_mutex);
			pgm_rwlock_reader_unlock (&sock->lock);
			return PGM_IO_STATUS_ERROR;
		}
	}

	for (STATE(vector_index) = 0; STATE(vector_index) < count; STATE(vector_index)++)
	{
		size_t		tpdu_length;
		ssize_t		sent;

		STATE(tsdu_length) = vector[STATE(vector_index)]->len;
		
		STATE(skb) = pgm_skb_get(vector[STATE(vector_index)]);
		STATE(skb)->sock = sock;
		STATE(skb)->tstamp = pgm_time_update_now();

		STATE(skb)->pgm_header = (struct pgm_header*)STATE(skb)->head;
		STATE(skb)->pgm_data   = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
		memcpy (STATE(skb)->pgm_header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
		STATE(skb)->pgm_header->pgm_sport	= sock->tsi.sport;
		STATE(skb)->pgm_header->pgm_dport	= sock->dport;
		STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
		STATE(skb)->pgm_header->pgm_options	= is_one_apdu ? PGM_OPT_PRESENT : 0;
		STATE(skb)->pgm_header->pgm_tsdu_length = htons ((uint16_t)STATE(tsdu_length));

/* ODATA */
		STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(sock->window));
		STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(sock->window));

		if (is_one_apdu)
		{
			struct pgm_opt_header	*opt_header;
			struct pgm_opt_length	*opt_len;

/* OPT_LENGTH */
			opt_len					= (struct pgm_opt_length*)(STATE(skb)->pgm_data + 1);
			opt_len->opt_type			= PGM_OPT_LENGTH;
			opt_len->opt_length			= sizeof(struct pgm_opt_length);
			opt_len->opt_total_length		= htons ((uint16_t)(sizeof(struct pgm_opt_length) +
										sizeof(struct pgm_opt_header) +
										sizeof(struct pgm_opt_fragment)));
/* OPT_FRAGMENT */
			opt_header				= (struct pgm_opt_header*)(opt_len + 1);
			opt_header->opt_type			= PGM_OPT_FRAGMENT | PGM_OPT_END;
			opt_header->opt_length			= sizeof(struct pgm_opt_header) +
								  sizeof(struct pgm_opt_fragment);
			STATE(skb)->pgm_opt_fragment			= (struct pgm_opt_fragment*)(opt_header + 1);
			STATE(skb)->pgm_opt_fragment->opt_reserved	= 0;
			STATE(skb)->pgm_opt_fragment->opt_sqn		= htonl (STATE(first_sqn));
			STATE(skb)->pgm_opt_fragment->opt_frag_off	= htonl ((uint32_t)STATE(data_bytes_offset));
			STATE(skb)->pgm_opt_fragment->opt_frag_len	= htonl ((uint32_t)STATE(apdu_length));

			pgm_assert (STATE(skb)->data == (STATE(skb)->pgm_opt_fragment + 1));
		}
		else
		{
			pgm_assert (STATE(skb)->data == (STATE(skb)->pgm_data + 1));
		}

/* TODO: the assembly checksum & copy routine is faster than memcpy & pgm_cksum on >= opteron hardware */
		STATE(skb)->pgm_header->pgm_checksum	= 0;
		pgm_assert ((char*)STATE(skb)->data > (char*)STATE(skb)->pgm_header);
		const size_t header_length		= (char*)STATE(skb)->data - (char*)STATE(skb)->pgm_header;
		const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, (uint16_t)header_length, 0);
		STATE(unfolded_odata)			= pgm_csum_partial ((char*)STATE(skb)->data, (uint16_t)STATE(tsdu_length), 0);
		STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), (uint16_t)header_length));

/* add to transmit window, skb::data set to payload */
		pgm_spinlock_lock (&sock->txw_spinlock);
		pgm_txw_add (sock->window, STATE(skb));
		pgm_spinlock_unlock (&sock->txw_spinlock);
retry_send:
		pgm_assert ((char*)STATE(skb)->tail > (char*)STATE(skb)->head);
		tpdu_length = (char*)STATE(skb)->tail - (char*)STATE(skb)->head;
		sent = pgm_sendto (sock,
				   !STATE(is_rate_limited),	/* rate limited on blocking */
			   	   &sock->odata_rate_control,
				   FALSE,			/* regular socket */
				   STATE(skb)->head,
				   tpdu_length,
				   (struct sockaddr*)&sock->send_gsr.gsr_group,
				   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
		if (sent < 0) {
			save_errno = pgm_get_last_sock_error();
			if (PGM_LIKELY(PGM_SOCK_EAGAIN == save_errno || PGM_SOCK_ENOBUFS == save_errno))
			{
				sock->is_apdu_eagain = TRUE;
				sock->blocklen = tpdu_length + sock->iphdr_len;
				goto blocked;
			}
/* fall through silently on other errors */
		}

/* save unfolded odata for retransmissions */
		pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

		if (PGM_LIKELY((size_t)sent == tpdu_length)) {
			bytes_sent += tpdu_length + sock->iphdr_len;	/* as counted at IP layer */
			packets_sent++;					/* IP packets */
			data_bytes_sent += STATE(tsdu_length);
		}

		pgm_free_skb (STATE(skb));
		STATE(data_bytes_offset) += STATE(tsdu_length);

/* check for end of transmission group */
		if (sock->use_proactive_parity) {
			const uint32_t odata_sqn   = ntohl (STATE(skb)->pgm_data->data_sqn);
			const uint32_t tg_sqn_mask = 0xffffffff << sock->tg_sqn_shift;
			if (!((odata_sqn + 1) & ~tg_sqn_mask))
				pgm_schedule_proactive_nak (sock, odata_sqn & tg_sqn_mask);
		}

	}
#ifdef TRANSPORT_DEBUG
	if (is_one_apdu)
	{
		pgm_assert( STATE(data_bytes_offset) == STATE(apdu_length) );
	}
#endif

	sock->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (sock, STATE(skb)->tstamp);

	pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)bytes_sent);
	sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
	sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	if (bytes_written)
		*bytes_written = data_bytes_sent;
	pgm_mutex_unlock (&sock->source_mutex);
	pgm_rwlock_reader_unlock (&sock->lock);
	return PGM_IO_STATUS_NORMAL;

blocked:
	if (bytes_sent) {
		reset_heartbeat_spm (sock, STATE(skb)->tstamp);
		pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)bytes_sent);
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
		sock->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	}
	pgm_mutex_unlock (&sock->source_mutex);
	pgm_rwlock_reader_unlock (&sock->lock);
	if (PGM_SOCK_ENOBUFS == save_errno)
		return PGM_IO_STATUS_RATE_LIMITED;
	if (sock->use_pgmcc)
		pgm_notify_clear (&sock->ack_notify);
	return PGM_IO_STATUS_WOULD_BLOCK;
}

/* cleanup resuming send state helper 
 */
#undef STATE

/* send repair packet.
 *
 * on success, TRUE is returned.  on error, FALSE is returned.
 */

static
bool
send_rdata (
	pgm_sock_t*	      restrict sock,
	struct pgm_sk_buff_t* restrict skb
	)
{
	size_t			 tpdu_length;
	struct pgm_header	*header;
	struct pgm_data		*rdata;
	ssize_t			 sent;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);
	pgm_assert ((char*)skb->tail > (char*)skb->head);

	tpdu_length = (char*)skb->tail - (char*)skb->head;

/* rate check including rdata specific limits */
	if (sock->is_controlled_rdata &&
	    !pgm_rate_check2 (&sock->rate_control,		/* total rate limit */
			      &sock->rdata_rate_control,	/* repair data limit */
			      tpdu_length,			/* excludes IP header len */
			      sock->is_nonblocking))
	{
		sock->blocklen = tpdu_length + sock->iphdr_len;
		return FALSE;
	}

/* update previous odata/rdata contents */
	header				= skb->pgm_header;
	rdata				= skb->pgm_data;
	header->pgm_type		= PGM_RDATA;
/* RDATA */
        rdata->data_trail		= htonl (pgm_txw_trail(sock->window));

        header->pgm_checksum		= 0;
	const size_t header_length	= tpdu_length - ntohs(header->pgm_tsdu_length);
	const uint32_t unfolded_header	= pgm_csum_partial (header, (uint16_t)header_length, 0);
	const uint32_t unfolded_odata	= pgm_txw_get_unfolded_checksum (skb);
	header->pgm_checksum		= pgm_csum_fold (pgm_csum_block_add (unfolded_header, unfolded_odata, (uint16_t)header_length));

/* congestion control */
	if (sock->use_pgmcc &&
	    sock->tokens < pgm_fp8 (1))
	{
//		pgm_trace (PGM_LOG_ROLE_CONGESTION_CONTROL,_("Token limit reached."));
		sock->blocklen = tpdu_length + sock->iphdr_len;
		return FALSE;
	}

	sent = pgm_sendto (sock,
			   FALSE,			/* already rate limited */
			   &sock->rdata_rate_control,
			   TRUE,			/* with router alert */
			   header,
			   tpdu_length,
			   (struct sockaddr*)&sock->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	if (sent < 0) {
		const int save_errno = pgm_get_last_sock_error();
		if (PGM_LIKELY(PGM_SOCK_EAGAIN == save_errno || PGM_SOCK_ENOBUFS == save_errno))
		{
			sock->blocklen = tpdu_length + sock->iphdr_len;
			return FALSE;
		}
/* fall through silently on other errors */
	}

	const pgm_time_t now = pgm_time_update_now();

	if (sock->use_pgmcc) {
		sock->tokens -= pgm_fp8 (1);
		sock->ack_expiry = now + sock->ack_expiry_ivl;
	}

/* re-set spm timer: we are already in the timer thread, no need to prod timers
 */
	pgm_mutex_lock (&sock->timer_mutex);
	sock->spm_heartbeat_state = 1;
	sock->next_heartbeat_spm = now + sock->spm_heartbeat_interval[sock->spm_heartbeat_state++];
	pgm_mutex_unlock (&sock->timer_mutex);

	pgm_txw_inc_retransmit_count (skb);
	sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_BYTES_RETRANSMITTED] += ntohs(header->pgm_tsdu_length);
	sock->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_MSGS_RETRANSMITTED]++;	/* impossible to determine APDU count */
	pgm_atomic_add32 (&sock->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], (uint32_t)(tpdu_length + sock->iphdr_len));
	return TRUE;
}

/* eof */
