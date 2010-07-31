/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM source transport.
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
#include <pgm/i18n.h>
#include <pgm/framework.h>
#include "pgm/source.h"
#include "pgm/sqn_list.h"
#include "pgm/packet_parse.h"
#include "pgm/net.h"


//#define SOURCE_DEBUG

#ifndef SOURCE_DEBUG
#	define PGM_DISABLE_ASSERT
#endif

#if !defined(ENOBUFS) && defined(WSAENOBUFS)
#	define ENOBUFS	WSAENOBUFS
#endif


/* locals */
static inline bool peer_is_source (const pgm_peer_t*) PGM_GNUC_CONST;
static inline bool peer_is_peer (const pgm_peer_t*) PGM_GNUC_CONST;
static void reset_heartbeat_spm (pgm_transport_t*const, const pgm_time_t);
static bool send_ncf (pgm_transport_t*const restrict, const struct sockaddr*const restrict, const struct sockaddr*const restrict, const uint32_t, const bool);
static bool send_ncf_list (pgm_transport_t*const restrict, const struct sockaddr*const restrict, const struct sockaddr*restrict, struct pgm_sqn_list_t*const restrict, const bool);
static int send_odata (pgm_transport_t*const restrict, struct pgm_sk_buff_t*const restrict, size_t*restrict);
static int send_odata_copy (pgm_transport_t*const restrict, const void*restrict, const uint16_t, size_t*restrict);
static int send_odatav (pgm_transport_t*const restrict, const struct pgm_iovec*const restrict, const unsigned, size_t*restrict);
static bool send_rdata (pgm_transport_t*const restrict, struct pgm_sk_buff_t*const restrict);


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

/* Linux 2.6 limited to millisecond resolution with conventional timers, however RDTSC
 * and future high-resolution timers allow nanosecond resolution.  Current ethernet technology
 * is limited to microseconds at best so we'll sit there for a bit.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_ambient_spm (
	pgm_transport_t* const	transport,
	const unsigned		spm_ambient_interval	/* in microseconds */
	)
{
	pgm_return_val_if_fail (NULL != transport, FALSE);
	pgm_return_val_if_fail (spm_ambient_interval > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
	transport->spm_ambient_interval = spm_ambient_interval;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* an array of intervals appropriately tuned till ambient period is reached.
 *
 * array is zero leaded for ambient state, and zero terminated for easy detection.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_heartbeat_spm (
	pgm_transport_t* const restrict	transport,
	const unsigned*  const restrict spm_heartbeat_interval,
	const unsigned			len
	)
{
	pgm_return_val_if_fail (NULL != transport, FALSE);
	pgm_return_val_if_fail (len > 0, FALSE);
	for (unsigned i = 0; i < len; i++)
		pgm_return_val_if_fail (spm_heartbeat_interval[i] > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
	if (transport->spm_heartbeat_interval)
		pgm_free (transport->spm_heartbeat_interval);
	transport->spm_heartbeat_interval = pgm_new (unsigned,len + 1);
	memcpy (&transport->spm_heartbeat_interval[1], spm_heartbeat_interval, sizeof(unsigned) * len);
	transport->spm_heartbeat_interval[0] = 0;
	transport->spm_heartbeat_len = len;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < txw_sqns < one less than half sequence space
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_txw_sqns (
	pgm_transport_t* const	transport,
	const unsigned		sqns
	)
{
	pgm_return_val_if_fail (NULL != transport, FALSE);
	pgm_return_val_if_fail (sqns < ((UINT32_MAX/2)-1), FALSE);
	pgm_return_val_if_fail (sqns > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
	transport->txw_sqns = sqns;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < secs < ( txw_sqns / txw_max_rte )
 *
 * can only be enforced upon bind.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

bool
pgm_transport_set_txw_secs (
	pgm_transport_t* const	transport,
	const unsigned		secs
	)
{
	pgm_return_val_if_fail (NULL != transport, FALSE);
	pgm_return_val_if_fail (secs > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
	transport->txw_secs = secs;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < txw_max_rte < interface capacity
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
pgm_transport_set_txw_max_rte (
	pgm_transport_t* const	transport,
	const unsigned		max_rte
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
		return FALSE;
	}
	transport->txw_max_rte = max_rte;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* prototype of function to send pro-active parity NAKs.
 */
static
bool
pgm_schedule_proactive_nak (
	pgm_transport_t*	transport,
	uint32_t		nak_tg_sqn	/* transmission group (shifted) */
	)
{
	pgm_return_val_if_fail (NULL != transport, FALSE);
	const bool status = pgm_txw_retransmit_push (transport->window,
						   nak_tg_sqn | transport->rs_proactive_h,
						   TRUE /* is_parity */,
						   transport->tg_sqn_shift);
	return status;
}

/* a deferred request for RDATA, now processing in the timer thread, we check the transmit
 * window to see if the packet exists and forward on, maintaining a lock until the queue is
 * empty.
 *
 * returns TRUE on success, returns FALSE if operation would block.
 */

bool
pgm_on_deferred_nak (
	pgm_transport_t* const	transport
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);

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
	pgm_spinlock_lock (&transport->txw_spinlock);
	struct pgm_sk_buff_t* skb = pgm_txw_retransmit_try_peek (transport->window);
	if (skb) {
		skb = pgm_skb_get (skb);
		pgm_spinlock_unlock (&transport->txw_spinlock);
		if (!send_rdata (transport, skb)) {
			pgm_free_skb (skb);
			pgm_notify_send (&transport->rdata_notify);
			return FALSE;
		}
		pgm_free_skb (skb);
/* now remove sequence number from retransmit queue, re-enabling NAK processing for this sequence number */
		pgm_txw_retransmit_remove_head (transport->window);
	} else
		pgm_spinlock_unlock (&transport->txw_spinlock);
	return TRUE;
}

/* SPMR indicates if multicast to cancel own SPMR, or unicast to send SPM.
 *
 * rate limited to 1/IHB_MIN per TSI (13.4).
 *
 * if SPMR was valid, returns TRUE, if invalid returns FALSE.
 */

bool
pgm_on_spmr (
	pgm_transport_t*      const restrict transport,
	pgm_peer_t*	      const restrict peer,	/* maybe NULL if transport is source */
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_spmr (transport:%p peer:%p skb:%p)",
		(void*)transport, (void*)peer, (void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_spmr (skb))) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed SPMR rejected."));
		return FALSE;
	}

	if (peer_is_source (peer)) {
		const bool send_status = pgm_send_spm (transport, 0);
		if (PGM_UNLIKELY(!send_status)) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Failed to send SPM on SPM-Request."));
		}
	} else {
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("Suppressing SPMR due to peer multicast SPMR."));
		reset_spmr_timer (peer);
	}
	return TRUE;
}

/* NAK requesting RDATA transmission for a sending transport, only valid if
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

bool
pgm_on_nak (
	pgm_transport_t*      const restrict transport,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_nak (transport:%p skb:%p)",
		(const void*)transport, (const void*)skb);

	const bool is_parity = skb->pgm_header->pgm_options & PGM_OPT_PARITY;
	if (is_parity) {
		transport->cumulative_stats[PGM_PC_SOURCE_PARITY_NAKS_RECEIVED]++;
		if (!transport->use_ondemand_parity) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Parity NAK rejected as on-demand parity is not enabled."));
			transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
			return FALSE;
		}
	} else
		transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED]++;

	if (PGM_UNLIKELY(!pgm_verify_nak (skb))) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed NAK rejected."));
		transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
		return FALSE;
	}

	const struct pgm_nak*  nak  = (struct pgm_nak*) skb->data;
	const struct pgm_nak6* nak6 = (struct pgm_nak6*)skb->data;
		
/* NAK_SRC_NLA contains our transport unicast NLA */
	struct sockaddr_storage nak_src_nla;
	pgm_nla_to_sockaddr (&nak->nak_src_nla_afi, (struct sockaddr*)&nak_src_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nak_src_nla, (struct sockaddr*)&transport->send_addr) != 0))
	{
		char saddr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&nak_src_nla, saddr, sizeof(saddr));
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("NAK rejected for unmatched NLA: %s"), saddr);
		transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
		return FALSE;
	}

/* NAK_GRP_NLA containers our transport multicast group */ 
	struct sockaddr_storage nak_grp_nla;
	pgm_nla_to_sockaddr ((AF_INET6 == nak_src_nla.ss_family) ? &nak6->nak6_grp_nla_afi : &nak->nak_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nak_grp_nla, (struct sockaddr*)&transport->send_gsr.gsr_group) != 0))
	{
		char sgroup[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&nak_src_nla, sgroup, sizeof(sgroup));
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("NAK rejected as targeted for different multicast group: %s"), sgroup);
		transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
		return FALSE;
	}

/* create queue object */
	struct pgm_sqn_list_t sqn_list;
	sqn_list.sqn[0] = ntohl (nak->nak_sqn);
	sqn_list.len = 1;

	pgm_debug ("nak_sqn %" PRIu32, sqn_list.sqn[0]);

/* check NAK list */
	const uint32_t* nak_list = NULL;
	uint_fast8_t nak_list_len = 0;
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (AF_INET6 == nak_src_nla.ss_family) ?
							(const struct pgm_opt_length*)(nak6 + 1) :
							(const struct pgm_opt_length*)(nak  + 1);
		if (PGM_UNLIKELY(opt_len->opt_type != PGM_OPT_LENGTH)) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed NAK rejected."));
			transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
			return FALSE;
		}
		if (PGM_UNLIKELY(opt_len->opt_length != sizeof(struct pgm_opt_length))) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed NAK rejected."));
			transport->cumulative_stats[PGM_PC_SOURCE_MALFORMED_NAKS]++;
			return FALSE;
		}
/* TODO: check for > 16 options & past packet end */
		const struct pgm_opt_header* opt_header = (const struct pgm_opt_header*)opt_len;
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
	if (PGM_UNLIKELY(nak_list_len > 63)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Malformed NAK rejected on too long sequence list."));
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
		send_ncf_list (transport, (struct sockaddr*)&nak_src_nla, (struct sockaddr*)&nak_grp_nla, &sqn_list, is_parity);
	else
		send_ncf (transport, (struct sockaddr*)&nak_src_nla, (struct sockaddr*)&nak_grp_nla, sqn_list.sqn[0], is_parity);

/* queue retransmit requests */
	for (uint_fast8_t i = 0; i < sqn_list.len; i++) {
		const bool push_status = pgm_txw_retransmit_push (transport->window, sqn_list.sqn[i], is_parity, transport->tg_sqn_shift);
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

bool
pgm_on_nnak (
	pgm_transport_t*      const restrict transport,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_nnak (transport:%p skb:%p)",
		(void*)transport, (void*)skb);

	transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAK_PACKETS_RECEIVED]++;

	if (PGM_UNLIKELY(!pgm_verify_nnak (skb))) {
		transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
		return FALSE;
	}

	const struct pgm_nak*  nnak  = (struct pgm_nak*) skb->data;
	const struct pgm_nak6* nnak6 = (struct pgm_nak6*)skb->data;
		
/* NAK_SRC_NLA contains our transport unicast NLA */
	struct sockaddr_storage nnak_src_nla;
	pgm_nla_to_sockaddr (&nnak->nak_src_nla_afi, (struct sockaddr*)&nnak_src_nla);

	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nnak_src_nla, (struct sockaddr*)&transport->send_addr) != 0))
	{
		transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
		return FALSE;
	}

/* NAK_GRP_NLA containers our transport multicast group */ 
	struct sockaddr_storage nnak_grp_nla;
	pgm_nla_to_sockaddr ((AF_INET6 == nnak_src_nla.ss_family) ? &nnak6->nak6_grp_nla_afi : &nnak->nak_grp_nla_afi, (struct sockaddr*)&nnak_grp_nla);
	if (PGM_UNLIKELY(pgm_sockaddr_cmp ((struct sockaddr*)&nnak_grp_nla, (struct sockaddr*)&transport->send_gsr.gsr_group) != 0))
	{
		transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
		return FALSE;
	}

/* check NNAK list */
	uint_fast8_t nnak_list_len = 0;
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (AF_INET6 == nnak_src_nla.ss_family) ?
							(const struct pgm_opt_length*)(nnak6 + 1) :
							(const struct pgm_opt_length*)(nnak + 1);
		if (PGM_UNLIKELY(opt_len->opt_type != PGM_OPT_LENGTH)) {
			transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
			return FALSE;
		}
		if (PGM_UNLIKELY(opt_len->opt_length != sizeof(struct pgm_opt_length))) {
			transport->cumulative_stats[PGM_PC_SOURCE_NNAK_ERRORS]++;
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

	transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_NNAKS_RECEIVED] += 1 + nnak_list_len;
	return TRUE;
}

/* ACK, sent upstream by one selected ACKER for congestion control feedback.
 *
 * if ACK is valid, returns TRUE.  on error, FALSE is returned.
 */

bool
pgm_on_ack (
	pgm_transport_t*      const restrict transport,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_on_ack (transport:%p skb:%p)",
		(const void*)transport, (const void*)skb);

	if (PGM_UNLIKELY(!pgm_verify_ack (skb))) {
		return FALSE;
	}

	if (!transport->use_pgmcc)
		return FALSE;

	const struct pgm_ack* ack  = (struct pgm_ack*)skb->data;

/* reset ACK expiration */
	transport->next_crqst = 0;

/* multicast round-trip time */
	const uint32_t txw_lead = pgm_txw_lead_atomic (transport->window);
	const uint32_t mrtt = transport->mrtt + (txw_lead - ntohl(ack->ack_rx_max));
	return TRUE;
}

/* ambient/heartbeat SPM's
 *
 * heartbeat: ihb_tmr decaying between ihb_min and ihb_max 2x after last packet
 *
 * on success, TRUE is returned, if operation would block, FALSE is returned.
 */

bool
pgm_send_spm (
	pgm_transport_t* const	transport,
	const int		flags
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != transport->window);

	pgm_debug ("pgm_send_spm (transport:%p flags:%d)",
		(const void*)transport, flags);

	size_t tpdu_length = sizeof(struct pgm_header);
	if (AF_INET == transport->send_gsr.gsr_group.ss_family)
		tpdu_length += sizeof(struct pgm_spm);
	else
		tpdu_length += sizeof(struct pgm_spm6);
	if (transport->use_proactive_parity ||
	    transport->use_ondemand_parity ||
	    transport->is_pending_crqst ||
	    PGM_OPT_FIN == flags)
	{
		tpdu_length += sizeof(struct pgm_opt_length);
/* forward error correction */
		if (transport->use_proactive_parity ||
		    transport->use_ondemand_parity)
			tpdu_length += sizeof(struct pgm_opt_header) +
				       sizeof(struct pgm_opt_parity_prm);
/* congestion report request */
		if (transport->is_pending_crqst)
			tpdu_length += sizeof(struct pgm_opt_header) +
				       sizeof(struct pgm_opt_crqst);
/* end of session */
		if (PGM_OPT_FIN == flags)
			tpdu_length += sizeof(struct pgm_opt_header) +
				       sizeof(struct pgm_opt_fin);
	}
	char buf[ tpdu_length ];
	if (PGM_UNLIKELY(pgm_mem_gc_friendly))
		memset (buf, 0, tpdu_length);
	struct pgm_header* header = (struct pgm_header*)buf;
	struct pgm_spm*  spm  = (struct pgm_spm *)(header + 1);
	struct pgm_spm6* spm6 = (struct pgm_spm6*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = transport->tsi.sport;
	header->pgm_dport       = transport->dport;
	header->pgm_type        = PGM_SPM;
	header->pgm_options     = 0;
	header->pgm_tsdu_length = 0;

/* SPM */
	spm->spm_sqn		= htonl (transport->spm_sqn);
	spm->spm_trail		= htonl (pgm_txw_trail_atomic (transport->window));
	spm->spm_lead		= htonl (pgm_txw_lead_atomic (transport->window));
	spm->spm_reserved	= 0;
/* our nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&transport->send_addr, (char*)&spm->spm_nla_afi);

/* PGM options */
	if (transport->use_proactive_parity ||
	    transport->use_ondemand_parity ||
	    transport->is_pending_crqst ||
	    PGM_OPT_FIN == flags)
	{
		struct pgm_opt_length* opt_len;
		struct pgm_opt_header *opt_header, *last_opt_header;
		uint16_t opt_total_length;

		if (AF_INET == transport->send_gsr.gsr_group.ss_family)
			opt_header = (struct pgm_opt_length*)(spm + 1);
		else
			opt_header = (struct pgm_opt_length*)(spm6 + 1);
		header->pgm_options |= PGM_OPT_PRESENT;
		opt_len			= opt_header;
		opt_len->opt_type	= PGM_OPT_LENGTH;
		opt_len->opt_length	= sizeof(struct pgm_opt_length);
		opt_total_length	= sizeof(struct pgm_opt_length);
		last_opt_header = opt_header = opt_len + 1;

/* OPT_PARITY_PRM */
		if (transport->use_proactive_parity ||
		    transport->use_ondemand_parity)
		{
			header->pgm_options |= PGM_OPT_NETWORK;
			opt_total_length += sizeof(struct pgm_opt_header) +
					    sizeof(struct pgm_opt_parity_prm);
			opt_header->opt_type	= PGM_OPT_PARITY_PRM;
			opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_parity_prm);
			struct pgm_opt_parity_prm* opt_parity_prm = (struct pgm_opt_parity_prm*)(opt_header + 1);
			opt_parity_prm->opt_reserved = (transport->use_proactive_parity ? PGM_PARITY_PRM_PRO : 0) |
						       (transport->use_ondemand_parity ? PGM_PARITY_PRM_OND : 0);
			opt_parity_prm->parity_prm_tgs = htonl (transport->rs_k);
			last_opt_header = opt_header;
			opt_header = opt_parity_prm + 1;
		}

/* OPT_CRQST */
		if (transport->is_pending_crqst)
		{
			header->pgm_options |= PGM_OPT_NETWORK;
			opt_total_length += sizeof(struct pgm_opt_header) +
					    sizeof(struct pgm_opt_crqst);
			opt_header->opt_type	= PGM_OPT_CRQST;
			opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_crqst);
			struct pgm_opt_crqst* opt_crqst = (struct pgm_opt_crqst*)(opt_header + 1);
/* request receiver worst path report, OPT_CR_RX_WP */
			opt_crqst->opt_reserved = PGM_OPT_CRQST_RXP;
			transport->is_pending_crqst = FALSE;
			last_opt_header = opt_header;
			opt_header = opt_crqst + 1;
		}

/* OPT_FIN */
		if (PGM_OPT_FIN == flags)
		{
			opt_total_length += sizeof(struct pgm_opt_header) +
					    sizeof(struct pgm_opt_fin);
			opt_header->opt_type	= PGM_OPT_FIN;
			opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_fin);
			struct pgm_opt_fin* opt_fin = (struct pgm_opt_fin*)(opt_header + 1);
			opt_fin->opt_reserved = 0;
			last_opt_header = opt_header;
			opt_header = opt_fin + 1;
		}

		last_opt_header->opt_type |= PGM_OPT_END;
		opt_len->opt_total_length = htons (opt_total_length);
	}

/* checksum optional for SPMs */
	header->pgm_checksum = 0;
	header->pgm_checksum = pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

	const ssize_t sent = pgm_sendto (transport,
					 flags != PGM_OPT_SYN && transport->is_controlled_spm,	/* rate limited */
					 TRUE,		/* with router alert */
					 buf,
					 tpdu_length,
					 (struct sockaddr*)&transport->send_gsr.gsr_group,
					 pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno)) {
		transport->blocklen = tpdu_length;
		return FALSE;
	}
/* advance SPM sequence only on successful transmission */
	transport->spm_sqn++;
	pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], tpdu_length);
	return TRUE;
}

/* send a NAK confirm (NCF) message with provided sequence number list.
 *
 * on success, TRUE is returned, returns FALSE if operation would block.
 */

static
bool
send_ncf (
	pgm_transport_t*       const restrict transport,
	const struct sockaddr* const restrict nak_src_nla,
	const struct sockaddr* const restrict nak_grp_nla,
	const uint32_t			      sequence,
	const bool			      is_parity		/* send parity NCF */
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != nak_src_nla);
	pgm_assert (NULL != nak_grp_nla);
	pgm_assert (nak_src_nla->sa_family == nak_grp_nla->sa_family);

#ifdef SOURCE_DEBUG
	char saddr[INET6_ADDRSTRLEN], gaddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (nak_src_nla, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (nak_grp_nla, gaddr, sizeof(gaddr));
	pgm_debug ("send_ncf (transport:%p nak-src-nla:%s nak-grp-nla:%s sequence:%" PRIu32" is-parity:%s)",
		(void*)transport,
		saddr,
		gaddr,
		sequence,
		is_parity ? "TRUE": "FALSE"
		);
#endif

	size_t tpdu_length = sizeof(struct pgm_header);
	tpdu_length += (AF_INET == nak_src_nla->sa_family) ? sizeof(struct pgm_nak) : sizeof(struct pgm_nak6);
	char buf[ tpdu_length ];
	struct pgm_header* header = (struct pgm_header*)buf;
	struct pgm_nak*  ncf  = (struct pgm_nak *)(header + 1);
	struct pgm_nak6* ncf6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport	= transport->tsi.sport;
	header->pgm_dport	= transport->dport;
	header->pgm_type        = PGM_NCF;
        header->pgm_options     = is_parity ? PGM_OPT_PARITY : 0;
        header->pgm_tsdu_length = 0;

/* NCF */
	ncf->nak_sqn		= htonl (sequence);

/* source nla */
	pgm_sockaddr_to_nla (nak_src_nla, (char*)&ncf->nak_src_nla_afi);

/* group nla */
	pgm_sockaddr_to_nla (nak_grp_nla, (AF_INET6 == nak_src_nla->sa_family) ? (char*)&ncf6->nak6_grp_nla_afi : (char*)&ncf->nak_grp_nla_afi );
        header->pgm_checksum = 0;
        header->pgm_checksum = pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

	const ssize_t sent = pgm_sendto (transport,
					FALSE,			/* not rate limited */
					TRUE,			/* with router alert */
					buf,
					tpdu_length,
					(struct sockaddr*)&transport->send_gsr.gsr_group,
					pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno))
		return FALSE;
	pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], tpdu_length);
	return TRUE;
}

/* A NCF packet with a OPT_NAK_LIST option extension
 *
 * on success, TRUE is returned.  on error, FALSE is returned.
 */

static
bool
send_ncf_list (
	pgm_transport_t*       const restrict transport,
	const struct sockaddr* const restrict nak_src_nla,
	const struct sockaddr* const restrict nak_grp_nla,
	struct pgm_sqn_list_t* const restrict sqn_list,		/* will change to network-order */
	const bool			      is_parity		/* send parity NCF */
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
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
	pgm_debug ("send_ncf_list (transport:%p nak-src-nla:%s nak-grp-nla:%s sqn-list:[%s] is-parity:%s)",
		(void*)transport,
		saddr,
		gaddr,
		list,
		is_parity ? "TRUE": "FALSE"
		);
#endif

	size_t tpdu_length = sizeof(struct pgm_header) +
			    sizeof(struct pgm_opt_length) +		/* includes header */
			    sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list) +
			    ( (sqn_list->len-1) * sizeof(uint32_t) );
	tpdu_length += (AF_INET == nak_src_nla->sa_family) ? sizeof(struct pgm_nak) : sizeof(struct pgm_nak6);
	char buf[ tpdu_length ];
	struct pgm_header* header = (struct pgm_header*)buf;
	struct pgm_nak*  ncf  = (struct pgm_nak *)(header + 1);
	struct pgm_nak6* ncf6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport	= transport->tsi.sport;
	header->pgm_dport	= transport->dport;
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
	struct pgm_opt_length* opt_len = (AF_INET6 == nak_src_nla->sa_family) ? (struct pgm_opt_length*)(ncf6 + 1) : (struct pgm_opt_length*)(ncf + 1);
	opt_len->opt_type	= PGM_OPT_LENGTH;
	opt_len->opt_length	= sizeof(struct pgm_opt_length);
	opt_len->opt_total_length = htons (	sizeof(struct pgm_opt_length) +
						sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_nak_list) +
						( (sqn_list->len-1) * sizeof(uint32_t) ) );
	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
	opt_header->opt_type	= PGM_OPT_NAK_LIST | PGM_OPT_END;
	opt_header->opt_length	= sizeof(struct pgm_opt_header) +
				  sizeof(struct pgm_opt_nak_list) +
				  ( (sqn_list->len-1) * sizeof(uint32_t) );
	struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
	opt_nak_list->opt_reserved = 0;
/* to network-order */
	for (uint_fast8_t i = 1; i < sqn_list->len; i++)
		opt_nak_list->opt_sqn[i-1] = htonl (sqn_list->sqn[i]);

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

	const ssize_t sent = pgm_sendto (transport,
					FALSE,			/* not rate limited */
					TRUE,			/* with router alert */
					buf,
					tpdu_length,
					(struct sockaddr*)&transport->send_gsr.gsr_group,
					pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno))
		return FALSE;
	pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], tpdu_length);
	return TRUE;
}

/* cancel any pending heartbeat SPM and schedule a new one
 */

static
void
reset_heartbeat_spm (
	pgm_transport_t*	transport,
	const pgm_time_t	now
	)
{
	pgm_mutex_lock (&transport->timer_mutex);
	const pgm_time_t next_poll = transport->next_poll;
	const pgm_time_t spm_heartbeat_interval = transport->spm_heartbeat_interval[ transport->spm_heartbeat_state = 1 ];
	transport->next_heartbeat_spm = now + spm_heartbeat_interval;
	if (pgm_time_after( next_poll, transport->next_heartbeat_spm ))
	{
		transport->next_poll = transport->next_heartbeat_spm;
		if (!transport->is_pending_read) {
			pgm_notify_send (&transport->pending_notify);
			transport->is_pending_read = TRUE;
		}
	}
	pgm_mutex_unlock (&transport->timer_mutex);
}

/* state helper for resuming sends
 */
#define STATE(x)	(transport->pkt_dontwait_state.x)

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
 * reliable transports role.
 */

static
int
send_odata (
	pgm_transport_t*      const restrict transport,
	struct pgm_sk_buff_t* const restrict skb,
	size_t*			    restrict bytes_written
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);
	pgm_assert (skb->len <= transport->max_tsdu);

	pgm_debug ("send_odata (transport:%p skb:%p bytes-written:%p)",
		(void*)transport, (void*)skb, (void*)bytes_written);

	const uint16_t tsdu_length = skb->len;
	const size_t   tpdu_length = tsdu_length + pgm_transport_pkt_offset2 (FALSE, transport->use_pgmcc);

/* continue if send would block */
	if (transport->is_apdu_eagain)
		goto retry_send;

/* add PGM header to skbuff */
	STATE(skb) = pgm_skb_get(skb);
	STATE(skb)->transport = transport;
	STATE(skb)->tstamp = pgm_time_update_now();

	STATE(skb)->pgm_header = (struct pgm_header*)STATE(skb)->head;
	STATE(skb)->pgm_data   = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
	memcpy (STATE(skb)->pgm_header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	STATE(skb)->pgm_header->pgm_sport	= transport->tsi.sport;
	STATE(skb)->pgm_header->pgm_dport	= transport->dport;
	STATE(skb)->pgm_header->pgm_type        = PGM_ODATA;
        STATE(skb)->pgm_header->pgm_options     = transport->use_pgmcc ? PGM_OPT_PRESENT : 0;
        STATE(skb)->pgm_header->pgm_tsdu_length = htons (tsdu_length);

/* ODATA */
        STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(transport->window));
        STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(transport->window));

        STATE(skb)->pgm_header->pgm_checksum    = 0;
	void* data = STATE(skb)->pgm_data + 1;
	if (transport->use_pgmcc) {
		struct pgm_opt_length* opt_len = data;
		opt_len->opt_type	= PGM_OPT_LENGTH;
		opt_len->opt_length	= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length = htons (	sizeof(struct pgm_opt_length) +
							sizeof(struct pgm_opt_header) +
							(AF_INET6 == transport->acker_nla.ss_family) ? sizeof(struct pgm_opt6_cc_data) : sizeof(struct pgm_opt_cc_data) );
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type	= PGM_OPT_PGMCC_DATA | PGM_OPT_END;
		opt_header->opt_length	= sizeof(struct pgm_opt_header) + (AF_INET6 == transport->acker_nla.ss_family) ? sizeof(struct pgm_opt6_cc_data) : sizeof(struct pgm_opt_cc_data);
		struct pgm_opt_cc_data*  cc_data  = (struct pgm_opt_cc_data*)(opt_header + 1);
		struct pgm_opt6_cc_data* cc_data6 = (struct pgm_opt6_cc_data*)(opt_header + 1);

		cc_data->opt_tstamp = pgm_to_msecs (STATE(skb)->tstamp);
/* acker nla */
		pgm_sockaddr_to_nla ((struct sockaddr*)&transport->acker_nla, (char*)&cc_data->opt_nla_afi);
		if (AF_INET6 == transport->acker_nla.ss_family)
			data = (char*)cc_data6 + sizeof(struct pgm_opt6_cc_data);
		else
			data = (char*)cc_data  + sizeof(struct pgm_opt_cc_data);
	}
	const size_t pgm_header_len		= (char*)data - (char*)STATE(skb)->pgm_header;
	const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, pgm_header_len, 0);
	STATE(unfolded_odata)			= pgm_csum_partial (data, tsdu_length, 0);
        STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), pgm_header_len));

/* add to transmit window, skb::data set to payload */
	pgm_spinlock_lock (&transport->txw_spinlock);
	pgm_txw_add (transport->window, STATE(skb));
	pgm_spinlock_unlock (&transport->txw_spinlock);

/* the transmit window MUST check the user count to ensure it does not 
 * attempt to send a repair-data packet based on in transit original data.
 */

	ssize_t sent;
retry_send:
	sent = pgm_sendto (transport,
			   transport->is_controlled_odata,	/* rate limited */
			   FALSE,				/* regular socket */
			   STATE(skb)->head,
			   tpdu_length,
			   (struct sockaddr*)&transport->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno)) {
		transport->is_apdu_eagain = TRUE;
		transport->blocklen = tpdu_length;
		return EAGAIN == errno ? PGM_IO_STATUS_WOULD_BLOCK : PGM_IO_STATUS_RATE_LIMITED;
	}

/* save unfolded odata for retransmissions */
	pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

	transport->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (transport, STATE(skb)->tstamp);

	if (PGM_LIKELY((size_t)sent == tpdu_length)) {
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += tsdu_length;
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  ++;
		pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], tpdu_length + transport->iphdr_len);
	}

/* check for end of transmission group */
	if (transport->use_proactive_parity) {
		const uint32_t odata_sqn = ntohl (STATE(skb)->pgm_data->data_sqn);
		const uint32_t tg_sqn_mask = 0xffffffff << transport->tg_sqn_shift;
		if (!((odata_sqn + 1) & ~tg_sqn_mask))
			pgm_schedule_proactive_nak (transport, odata_sqn & tg_sqn_mask);
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
	pgm_transport_t* const restrict	transport,
	const void*	       restrict	tsdu,
	const uint16_t			tsdu_length,
	size_t*		       restrict	bytes_written
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (tsdu_length <= transport->max_tsdu);
	if (PGM_LIKELY(tsdu_length)) pgm_assert (NULL != tsdu);

	pgm_debug ("send_odata_copy (transport:%p tsdu:%p tsdu_length:%u bytes-written:%p)",
		(void*)transport, tsdu, tsdu_length, (void*)bytes_written);

	const size_t tpdu_length = tsdu_length + pgm_transport_pkt_offset2 (FALSE, transport->use_pgmcc);

/* continue if blocked mid-apdu */
	if (transport->is_apdu_eagain)
		goto retry_send;

	STATE(skb) = pgm_alloc_skb (transport->max_tpdu);
	STATE(skb)->transport = transport;
	STATE(skb)->tstamp = pgm_time_update_now();
	pgm_skb_reserve (STATE(skb), pgm_transport_pkt_offset2  (FALSE, transport->use_pgmcc));
	pgm_skb_put (STATE(skb), tsdu_length);

	STATE(skb)->pgm_header	= (struct pgm_header*)STATE(skb)->head;
	STATE(skb)->pgm_data	= (struct pgm_data*)(STATE(skb)->pgm_header + 1);
	memcpy (STATE(skb)->pgm_header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	STATE(skb)->pgm_header->pgm_sport	= transport->tsi.sport;
	STATE(skb)->pgm_header->pgm_dport	= transport->dport;
	STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
	STATE(skb)->pgm_header->pgm_options	= 0;
	STATE(skb)->pgm_header->pgm_tsdu_length = htons (tsdu_length);

/* ODATA */
	STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(transport->window));
	STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(transport->window));

	STATE(skb)->pgm_header->pgm_checksum	= 0;
	const size_t pgm_header_len		= (char*)(STATE(skb)->pgm_data + 1) - (char*)STATE(skb)->pgm_header;
	const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, pgm_header_len, 0);
	STATE(unfolded_odata)			= pgm_csum_partial_copy (tsdu, (char*)(STATE(skb)->pgm_data + 1), tsdu_length, 0);
	STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), pgm_header_len));

/* add to transmit window, skb::data set to payload */
	pgm_spinlock_lock (&transport->txw_spinlock);
	pgm_txw_add (transport->window, STATE(skb));
	pgm_spinlock_unlock (&transport->txw_spinlock);

	ssize_t sent;
retry_send:
	sent = pgm_sendto (transport,
			   transport->is_controlled_odata,	/* rate limited */
			   FALSE,			/* regular socket */
			   STATE(skb)->head,
			   tpdu_length,
			   (struct sockaddr*)&transport->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno)) {
		transport->is_apdu_eagain = TRUE;
		transport->blocklen = tpdu_length;
		return EAGAIN == errno ? PGM_IO_STATUS_WOULD_BLOCK : PGM_IO_STATUS_RATE_LIMITED;
	}

/* save unfolded odata for retransmissions */
	pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

	transport->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (transport, STATE(skb)->tstamp);

	if (PGM_LIKELY((size_t)sent == tpdu_length)) {
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += tsdu_length;
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  ++;
		pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], tpdu_length + transport->iphdr_len);
	}

/* check for end of transmission group */
	if (transport->use_proactive_parity) {
		const uint32_t odata_sqn = ntohl (STATE(skb)->pgm_data->data_sqn);
		const uint32_t tg_sqn_mask = 0xffffffff << transport->tg_sqn_shift;
		if (!((odata_sqn + 1) & ~tg_sqn_mask))
			pgm_schedule_proactive_nak (transport, odata_sqn & tg_sqn_mask);
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
	pgm_transport_t*	const restrict transport,
	const struct pgm_iovec* const restrict vector,
	const unsigned			       count,		/* number of items in vector */
	size_t*		 	      restrict bytes_written
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (count <= PGM_MAX_FRAGMENTS);
	if (PGM_LIKELY(count)) pgm_assert (NULL != vector);

	pgm_debug ("send_odatav (transport:%p vector:%p count:%u bytes-written:%p)",
		(const void*)transport, (const void*)vector, count, (const void*)bytes_written);

	if (PGM_UNLIKELY(0 == count))
		return send_odata_copy (transport, NULL, 0, bytes_written);

/* continue if blocked on send */
	if (transport->is_apdu_eagain)
		goto retry_send;

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
	pgm_return_val_if_fail (STATE(tsdu_length) <= transport->max_tsdu, PGM_IO_STATUS_ERROR);

	STATE(skb) = pgm_alloc_skb (transport->max_tpdu);
	STATE(skb)->transport = transport;
	STATE(skb)->tstamp = pgm_time_update_now();
	pgm_skb_reserve (STATE(skb), pgm_transport_pkt_offset2  (FALSE, transport->use_pgmcc));
	pgm_skb_put (STATE(skb), STATE(tsdu_length));

	STATE(skb)->pgm_header  = (struct pgm_header*)STATE(skb)->data;
	STATE(skb)->pgm_data    = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
	memcpy (STATE(skb)->pgm_header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	STATE(skb)->pgm_header->pgm_sport	= transport->tsi.sport;
	STATE(skb)->pgm_header->pgm_dport	= transport->dport;
	STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
	STATE(skb)->pgm_header->pgm_options	= 0;
	STATE(skb)->pgm_header->pgm_tsdu_length = htons (STATE(tsdu_length));

/* ODATA */
	STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(transport->window));
	STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(transport->window));

	STATE(skb)->pgm_header->pgm_checksum	= 0;
	const size_t pgm_header_len		= (char*)(STATE(skb)->pgm_data + 1) - (char*)STATE(skb)->pgm_header;
	const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, pgm_header_len, 0);

/* unroll first iteration to make friendly branch prediction */
	char*	dst		= (char*)(STATE(skb)->pgm_data + 1);
	STATE(unfolded_odata)	= pgm_csum_partial_copy ((const char*)vector[0].iov_base, dst, vector[0].iov_len, 0);

/* iterate over one or more vector elements to perform scatter/gather checksum & copy */
	for (unsigned i = 1; i < count; i++) {
		dst += vector[i-1].iov_len;
		const uint32_t unfolded_element = pgm_csum_partial_copy ((const char*)vector[i].iov_base, dst, vector[i].iov_len, 0);
		STATE(unfolded_odata) = pgm_csum_block_add (STATE(unfolded_odata), unfolded_element, vector[i-1].iov_len);
	}

	STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), pgm_header_len));

/* add to transmit window, skb::data set to payload */
	pgm_spinlock_lock (&transport->txw_spinlock);
	pgm_txw_add (transport->window, STATE(skb));
	pgm_spinlock_unlock (&transport->txw_spinlock);

	ssize_t sent;
	size_t  tpdu_length;
retry_send:
	pgm_assert ((char*)STATE(skb)->tail > (char*)STATE(skb)->head);
	tpdu_length = (char*)STATE(skb)->tail - (char*)STATE(skb)->head;
	sent = pgm_sendto (transport,
			   transport->is_controlled_odata,	/* rate limited */
			   FALSE,			/* regular socket */
			   STATE(skb)->head,
			   tpdu_length,
			   (struct sockaddr*)&transport->send_gsr.gsr_group,
			   pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno)) {
		transport->is_apdu_eagain = TRUE;
		transport->blocklen = tpdu_length;
		return EAGAIN == errno ? PGM_IO_STATUS_WOULD_BLOCK : PGM_IO_STATUS_RATE_LIMITED;
	}

/* save unfolded odata for retransmissions */
	pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

	transport->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (transport, STATE(skb)->tstamp);

	if (PGM_LIKELY((size_t)sent == STATE(skb)->len)) {
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += STATE(tsdu_length);
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  ++;
		pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], tpdu_length + transport->iphdr_len);
	}

/* check for end of transmission group */
	if (transport->use_proactive_parity) {
		const uint32_t odata_sqn   = ntohl (STATE(skb)->pgm_data->data_sqn);
		const uint32_t tg_sqn_mask = 0xffffffff << transport->tg_sqn_shift;
		if (!((odata_sqn + 1) & ~tg_sqn_mask))
			pgm_schedule_proactive_nak (transport, odata_sqn & tg_sqn_mask);
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
	pgm_transport_t* const restrict	transport,
	const void*	       restrict	apdu,
	const size_t			apdu_length,
	size_t*		       restrict	bytes_written
	)
{
	size_t   bytes_sent	 = 0;		/* counted at IP layer */
	unsigned packets_sent	 = 0;		/* IP packets */
	size_t   data_bytes_sent = 0;

	pgm_assert (NULL != transport);
	pgm_assert (NULL != apdu);

/* continue if blocked mid-apdu */
	if (transport->is_apdu_eagain)
		goto retry_send;

/* if non-blocking calculate total wire size and check rate limit */
	STATE(is_rate_limited) = FALSE;
	if (transport->is_nonblocking && transport->is_controlled_odata)
	{
		const size_t header_length = pgm_transport_pkt_offset2  (TRUE, transport->use_pgmcc);
		size_t tpdu_length = 0;
		size_t offset_	  = 0;
		do {
			const uint16_t tsdu_length = MIN( pgm_transport_max_tsdu (transport, TRUE), apdu_length - offset_ );
			tpdu_length += transport->iphdr_len + header_length + tsdu_length;
			offset_ += tsdu_length;
		} while (offset_ < apdu_length);

/* calculation includes one iphdr length already */
		if (!pgm_rate_check (&transport->rate_control,
				     tpdu_length - transport->iphdr_len,
				     transport->is_nonblocking))
		{
			transport->blocklen = tpdu_length;
			return PGM_IO_STATUS_RATE_LIMITED;
		}
		STATE(is_rate_limited) = TRUE;
	}

	STATE(data_bytes_offset)	= 0;
	STATE(first_sqn)		= pgm_txw_next_lead(transport->window);

	do {
/* retrieve packet storage from transmit window */
		size_t header_length = pgm_transport_pkt_offset2  (TRUE, transport->use_pgmcc);
		STATE(tsdu_length) = MIN( pgm_transport_max_tsdu (transport, TRUE), apdu_length - STATE(data_bytes_offset) );

		STATE(skb) = pgm_alloc_skb (transport->max_tpdu);
		STATE(skb)->transport = transport;
		STATE(skb)->tstamp = pgm_time_update_now();
		pgm_skb_reserve (STATE(skb), header_length);
		pgm_skb_put (STATE(skb), STATE(tsdu_length));

		STATE(skb)->pgm_header  = (struct pgm_header*)STATE(skb)->head;
		STATE(skb)->pgm_data    = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
		memcpy (STATE(skb)->pgm_header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
		STATE(skb)->pgm_header->pgm_sport	= transport->tsi.sport;
		STATE(skb)->pgm_header->pgm_dport	= transport->dport;
		STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
		STATE(skb)->pgm_header->pgm_options	= PGM_OPT_PRESENT;
		STATE(skb)->pgm_header->pgm_tsdu_length = htons (STATE(tsdu_length));

/* ODATA */
		STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(transport->window));
		STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(transport->window));

/* OPT_LENGTH */
		struct pgm_opt_length* opt_len		= (struct pgm_opt_length*)(STATE(skb)->pgm_data + 1);
		opt_len->opt_type			= PGM_OPT_LENGTH;
		opt_len->opt_length			= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length		= htons (	sizeof(struct pgm_opt_length) +
									sizeof(struct pgm_opt_header) +
									sizeof(struct pgm_opt_fragment) );
/* OPT_FRAGMENT */
		struct pgm_opt_header* opt_header	= (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type			= PGM_OPT_FRAGMENT | PGM_OPT_END;
		opt_header->opt_length			= sizeof(struct pgm_opt_header) +
						  	  sizeof(struct pgm_opt_fragment);
		STATE(skb)->pgm_opt_fragment			= (struct pgm_opt_fragment*)(opt_header + 1);
		STATE(skb)->pgm_opt_fragment->opt_reserved	= 0;
		STATE(skb)->pgm_opt_fragment->opt_sqn		= htonl (STATE(first_sqn));
		STATE(skb)->pgm_opt_fragment->opt_frag_off	= htonl (STATE(data_bytes_offset));
		STATE(skb)->pgm_opt_fragment->opt_frag_len	= htonl (apdu_length);

/* TODO: the assembly checksum & copy routine is faster than memcpy & pgm_cksum on >= opteron hardware */
		STATE(skb)->pgm_header->pgm_checksum	= 0;
		const size_t pgm_header_len		= (char*)(STATE(skb)->pgm_opt_fragment + 1) - (char*)STATE(skb)->pgm_header;
		const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, pgm_header_len, 0);
		STATE(unfolded_odata)			= pgm_csum_partial_copy ((const char*)apdu + STATE(data_bytes_offset), STATE(skb)->pgm_opt_fragment + 1, STATE(tsdu_length), 0);
		STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), pgm_header_len));

/* add to transmit window, skb::data set to payload */
		pgm_spinlock_lock (&transport->txw_spinlock);
		pgm_txw_add (transport->window, STATE(skb));
		pgm_spinlock_unlock (&transport->txw_spinlock);

		ssize_t sent;
		size_t  tpdu_length;
retry_send:
		pgm_assert ((char*)STATE(skb)->tail > (char*)STATE(skb)->head);
		tpdu_length = (char*)STATE(skb)->tail - (char*)STATE(skb)->head;
		sent = pgm_sendto (transport,
				   !STATE(is_rate_limited),	/* rate limit on blocking */
				   FALSE,				/* regular socket */
				   STATE(skb)->head,
				   tpdu_length,
				   (struct sockaddr*)&transport->send_gsr.gsr_group,
				   pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
		if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno)) {
			transport->is_apdu_eagain = TRUE;
			transport->blocklen = tpdu_length;
			goto blocked;
		}

/* save unfolded odata for retransmissions */
		pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

		if (PGM_LIKELY((size_t)sent == tpdu_length)) {
			bytes_sent += tpdu_length + transport->iphdr_len;	/* as counted at IP layer */
			packets_sent++;							/* IP packets */
			data_bytes_sent += STATE(tsdu_length);
		}

		STATE(data_bytes_offset) += STATE(tsdu_length);

/* check for end of transmission group */
		if (transport->use_proactive_parity) {
			const uint32_t odata_sqn = ntohl (STATE(skb)->pgm_data->data_sqn);
			const uint32_t tg_sqn_mask = 0xffffffff << transport->tg_sqn_shift;
			if (!((odata_sqn + 1) & ~tg_sqn_mask))
				pgm_schedule_proactive_nak (transport, odata_sqn & tg_sqn_mask);
		}

	} while ( STATE(data_bytes_offset)  < apdu_length);
	pgm_assert( STATE(data_bytes_offset) == apdu_length );

	transport->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (transport, STATE(skb)->tstamp);

	pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], bytes_sent);
	transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
	transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	if (bytes_written)
		*bytes_written = apdu_length;
	return PGM_IO_STATUS_NORMAL;

blocked:
	if (bytes_sent) {
		reset_heartbeat_spm (transport, STATE(skb)->tstamp);
		pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], bytes_sent);
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	}
	return EAGAIN == errno ? PGM_IO_STATUS_WOULD_BLOCK : PGM_IO_STATUS_RATE_LIMITED;
}

/* Send one APDU, whether it fits within one TPDU or more.
 *
 * on success, returns PGM_IO_STATUS_NORMAL, on block for non-blocking sockets
 * returns PGM_IO_STATUS_WOULD_BLOCK, returns PGM_IO_STATUS_RATE_LIMITED if
 * packet size exceeds the current rate limit.
 */
int
pgm_send (
	pgm_transport_t* const restrict transport,
	const void*	       restrict	apdu,
	const size_t			apdu_length,
	size_t*	       	       restrict	bytes_written
	)
{
	pgm_debug ("pgm_send (transport:%p apdu:%p apdu-length:%zu bytes-written:%p)",
		(void*)transport, apdu, apdu_length, (void*)bytes_written);

/* parameters */
	pgm_return_val_if_fail (NULL != transport, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(apdu_length)) pgm_return_val_if_fail (NULL != apdu, PGM_IO_STATUS_ERROR);

/* shutdown */
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&transport->lock)))
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);

/* state */
	if (PGM_UNLIKELY(!transport->is_bound ||
	    transport->is_destroyed ||
	    apdu_length > transport->max_apdu))
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	}

/* source */
	pgm_mutex_lock (&transport->source_mutex);

/* pass on non-fragment calls */
	if (apdu_length <= transport->max_tsdu)
	{
		const int status = send_odata_copy (transport, apdu, apdu_length, bytes_written);
		pgm_mutex_unlock (&transport->source_mutex);
		pgm_rwlock_reader_unlock (&transport->lock);
		return status;
	}

	const int status = send_apdu (transport, apdu, apdu_length, bytes_written);
	pgm_mutex_unlock (&transport->source_mutex);
	pgm_rwlock_reader_unlock (&transport->lock);
	return status;
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
	pgm_transport_t*	const restrict transport,
	const struct pgm_iovec* const restrict vector,
	const unsigned			       count,		/* number of items in vector */
	const bool			       is_one_apdu,	/* true  = vector = apdu, false = vector::iov_base = apdu */
        size_t*                       restrict bytes_written
	)
{
	pgm_debug ("pgm_sendv (transport:%p vector:%p count:%u is-one-apdu:%s bytes-written:%p)",
		(const void*)transport,
		(const void*)vector,
		count,
		is_one_apdu ? "TRUE" : "FALSE",
		(const void*)bytes_written);

	pgm_return_val_if_fail (NULL != transport, PGM_IO_STATUS_ERROR);
	pgm_return_val_if_fail (count <= PGM_MAX_FRAGMENTS, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(count)) pgm_return_val_if_fail (NULL != vector, PGM_IO_STATUS_ERROR);
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&transport->lock)))
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	if (PGM_UNLIKELY(!transport->is_bound ||
	    transport->is_destroyed))
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	}

	pgm_mutex_lock (&transport->source_mutex);

/* pass on zero length as cannot count vector lengths */
	if (PGM_UNLIKELY(0 == count))
	{
		const int status = send_odata_copy (transport, NULL, count, bytes_written);
		pgm_mutex_unlock (&transport->source_mutex);
		pgm_rwlock_reader_unlock (&transport->lock);
		return status;
	}

	size_t   bytes_sent	 = 0;
	unsigned packets_sent	 = 0;
	size_t   data_bytes_sent = 0;

/* continue if blocked mid-apdu */
	if (transport->is_apdu_eagain) {
		if (is_one_apdu) {
			if (STATE(apdu_length) <= transport->max_tsdu)
			{
				const int status = send_odatav (transport, vector, count, bytes_written);
				pgm_mutex_unlock (&transport->source_mutex);
				pgm_rwlock_reader_unlock (&transport->lock);
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
		    vector[i].iov_len > transport->max_apdu)
		{
			pgm_mutex_unlock (&transport->source_mutex);
			pgm_rwlock_reader_unlock (&transport->lock);
			pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
		}
		STATE(apdu_length) += vector[i].iov_len;
	}

/* pass on non-fragment calls */
	if (is_one_apdu) {
		if (STATE(apdu_length) <= transport->max_tsdu) {
			const int status = send_odatav (transport, vector, count, bytes_written);
			pgm_mutex_unlock (&transport->source_mutex);
			pgm_rwlock_reader_unlock (&transport->lock);
			return status;
		} else if (STATE(apdu_length) > transport->max_apdu) {
			pgm_mutex_unlock (&transport->source_mutex);
			pgm_rwlock_reader_unlock (&transport->lock);
			pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
		}
	}

/* if non-blocking calculate total wire size and check rate limit */
	STATE(is_rate_limited) = FALSE;
	if (transport->is_nonblocking && transport->is_controlled_odata)
        {
		const size_t header_length = pgm_transport_pkt_offset2  (TRUE, transport->use_pgmcc);
                size_t tpdu_length = 0;
		size_t offset_	   = 0;
		do {
			const uint16_t tsdu_length = MIN( pgm_transport_max_tsdu (transport, TRUE), STATE(apdu_length) - offset_ );
			tpdu_length += transport->iphdr_len + header_length + tsdu_length;
			offset_     += tsdu_length;
		} while (offset_ < STATE(apdu_length));

/* calculation includes one iphdr length already */
                if (!pgm_rate_check (&transport->rate_control,
				     tpdu_length - transport->iphdr_len,
				     transport->is_nonblocking))
		{
			transport->blocklen = tpdu_length;
			pgm_mutex_unlock (&transport->source_mutex);
			pgm_rwlock_reader_unlock (&transport->lock);
			return PGM_IO_STATUS_RATE_LIMITED;
		}
		STATE(is_rate_limited) = TRUE;
        }

/* non-fragmented packets can be forwarded onto basic send() */
	if (!is_one_apdu)
	{
		for (STATE(data_pkt_offset) = 0; STATE(data_pkt_offset) < count; STATE(data_pkt_offset)++)
		{
			size_t wrote_bytes;
			int status;
retry_send:
			status = send_apdu (transport,
					    vector[STATE(data_pkt_offset)].iov_base,
					    vector[STATE(data_pkt_offset)].iov_len,
					    &wrote_bytes);
			switch (status) {
			case PGM_IO_STATUS_NORMAL:
				break;
			case PGM_IO_STATUS_WOULD_BLOCK:
			case PGM_IO_STATUS_RATE_LIMITED:
				transport->is_apdu_eagain = TRUE;
				pgm_mutex_unlock (&transport->source_mutex);
				pgm_rwlock_reader_unlock (&transport->lock);
				return status;
			case PGM_IO_STATUS_ERROR:
				pgm_mutex_unlock (&transport->source_mutex);
				pgm_rwlock_reader_unlock (&transport->lock);
				return status;
			default:
				pgm_assert_not_reached();
			}
			data_bytes_sent += wrote_bytes;
		}

		transport->is_apdu_eagain = FALSE;
		if (bytes_written)
			*bytes_written = data_bytes_sent;
		pgm_mutex_unlock (&transport->source_mutex);
		pgm_rwlock_reader_unlock (&transport->lock);
		return PGM_IO_STATUS_NORMAL;
	}

	STATE(data_bytes_offset)	= 0;
	STATE(vector_index)		= 0;
	STATE(vector_offset)		= 0;

	STATE(first_sqn)		= pgm_txw_next_lead(transport->window);

	do {
/* retrieve packet storage from transmit window */
		size_t header_length = pgm_transport_pkt_offset2  (TRUE, transport->use_pgmcc);
		STATE(tsdu_length) = MIN( pgm_transport_max_tsdu (transport, TRUE), STATE(apdu_length) - STATE(data_bytes_offset) );
		STATE(skb) = pgm_alloc_skb (transport->max_tpdu);
		STATE(skb)->transport = transport;
		STATE(skb)->tstamp = pgm_time_update_now();
		pgm_skb_reserve (STATE(skb), header_length);
		pgm_skb_put (STATE(skb), STATE(tsdu_length));

		STATE(skb)->pgm_header  = (struct pgm_header*)STATE(skb)->head;
		STATE(skb)->pgm_data    = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
		memcpy (STATE(skb)->pgm_header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
		STATE(skb)->pgm_header->pgm_sport	= transport->tsi.sport;
		STATE(skb)->pgm_header->pgm_dport	= transport->dport;
		STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
		STATE(skb)->pgm_header->pgm_options	= PGM_OPT_PRESENT;
		STATE(skb)->pgm_header->pgm_tsdu_length = htons (STATE(tsdu_length));

/* ODATA */
		STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(transport->window));
		STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(transport->window));

/* OPT_LENGTH */
		struct pgm_opt_length* opt_len		= (struct pgm_opt_length*)(STATE(skb)->pgm_data + 1);
		opt_len->opt_type			= PGM_OPT_LENGTH;
		opt_len->opt_length			= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length		= htons (	sizeof(struct pgm_opt_length) +
									sizeof(struct pgm_opt_header) +
									sizeof(struct pgm_opt_fragment) );
/* OPT_FRAGMENT */
		struct pgm_opt_header* opt_header	= (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type			= PGM_OPT_FRAGMENT | PGM_OPT_END;
		opt_header->opt_length			= sizeof(struct pgm_opt_header) +
							  sizeof(struct pgm_opt_fragment);
		STATE(skb)->pgm_opt_fragment			= (struct pgm_opt_fragment*)(opt_header + 1);
		STATE(skb)->pgm_opt_fragment->opt_reserved	= 0;
		STATE(skb)->pgm_opt_fragment->opt_sqn		= htonl (STATE(first_sqn));
		STATE(skb)->pgm_opt_fragment->opt_frag_off	= htonl (STATE(data_bytes_offset));
		STATE(skb)->pgm_opt_fragment->opt_frag_len	= htonl (STATE(apdu_length));

/* checksum & copy */
		STATE(skb)->pgm_header->pgm_checksum	= 0;
		const size_t pgm_header_len		= (char*)(STATE(skb)->pgm_opt_fragment + 1) - (char*)STATE(skb)->pgm_header;
		const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, pgm_header_len, 0);

/* iterate over one or more vector elements to perform scatter/gather checksum & copy
 *
 * STATE(vector_index)	- index into application scatter/gather vector
 * STATE(vector_offset) - current offset into current vector element
 * STATE(unfolded_odata)- checksum accumulator
 */
		const char* src	= (const char*)vector[STATE(vector_index)].iov_base + STATE(vector_offset);
		char* dst		= (char*)(STATE(skb)->pgm_opt_fragment + 1);
		size_t src_length	= vector[STATE(vector_index)].iov_len - STATE(vector_offset);
		size_t dst_length	= 0;
		size_t copy_length	= MIN( STATE(tsdu_length), src_length );
		STATE(unfolded_odata)	= pgm_csum_partial_copy (src, dst, copy_length, 0);

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

/* transport packet complete */
			if (dst_length == STATE(tsdu_length))
				break;

			src		= (const char*)vector[STATE(vector_index)].iov_base + STATE(vector_offset);
			dst	       += copy_length;
			src_length	= vector[STATE(vector_index)].iov_len - STATE(vector_offset);
			copy_length	= MIN( STATE(tsdu_length) - dst_length, src_length );
			const uint32_t unfolded_element = pgm_csum_partial_copy (src, dst, copy_length, 0);
			STATE(unfolded_odata) = pgm_csum_block_add (STATE(unfolded_odata), unfolded_element, dst_length);
		}

		STATE(skb)->pgm_header->pgm_checksum = pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), pgm_header_len));

/* add to transmit window, skb::data set to payload */
		pgm_spinlock_lock (&transport->txw_spinlock);
		pgm_txw_add (transport->window, STATE(skb));
		pgm_spinlock_unlock (&transport->txw_spinlock);

		ssize_t sent;
		size_t  tpdu_length;
retry_one_apdu_send:
		tpdu_length = (char*)STATE(skb)->tail - (char*)STATE(skb)->head;
		sent = pgm_sendto (transport,
				   !STATE(is_rate_limited),	/* rate limited on blocking */
				   FALSE,				/* regular socket */
				   STATE(skb)->head,
				   tpdu_length,
				   (struct sockaddr*)&transport->send_gsr.gsr_group,
				   pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
		if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno)) {
			transport->is_apdu_eagain = TRUE;
			transport->blocklen = tpdu_length;
			goto blocked;
		}

/* save unfolded odata for retransmissions */
		pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

		if (PGM_LIKELY((size_t)sent == tpdu_length)) {
			bytes_sent += tpdu_length + transport->iphdr_len;	/* as counted at IP layer */
			packets_sent++;							/* IP packets */
			data_bytes_sent += STATE(tsdu_length);
		}

		STATE(data_bytes_offset) += STATE(tsdu_length);

/* check for end of transmission group */
		if (transport->use_proactive_parity) {
			const uint32_t odata_sqn = ntohl (STATE(skb)->pgm_data->data_sqn);
			const uint32_t tg_sqn_mask = 0xffffffff << transport->tg_sqn_shift;
			if (!((odata_sqn + 1) & ~tg_sqn_mask))
				pgm_schedule_proactive_nak (transport, odata_sqn & tg_sqn_mask);
		}

	} while ( STATE(data_bytes_offset)  < STATE(apdu_length) );
	pgm_assert( STATE(data_bytes_offset) == STATE(apdu_length) );

	transport->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (transport, STATE(skb)->tstamp);

	pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], bytes_sent);
	transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
	transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	if (bytes_written)
		*bytes_written = STATE(apdu_length);
	pgm_mutex_unlock (&transport->source_mutex);
	pgm_rwlock_reader_unlock (&transport->lock);
	return PGM_IO_STATUS_NORMAL;

blocked:
	if (bytes_sent) {
		reset_heartbeat_spm (transport, STATE(skb)->tstamp);
		pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], bytes_sent);
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	}
	pgm_mutex_unlock (&transport->source_mutex);
	pgm_rwlock_reader_unlock (&transport->lock);
	return EAGAIN == errno ? PGM_IO_STATUS_WOULD_BLOCK : PGM_IO_STATUS_RATE_LIMITED;
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
	pgm_transport_t*       const restrict transport,
	struct pgm_sk_buff_t** const restrict vector,		/* array of skb pointers vs. array of skbs */
	const unsigned			      count,
	const bool			      is_one_apdu,	/* true: vector = apdu, false: vector::iov_base = apdu */
	size_t*		 	     restrict bytes_written
	)
{
	pgm_debug ("pgm_send_skbv (transport:%p vector:%p count:%u is-one-apdu:%s bytes-written:%p)",
		(const void*)transport,
		(const void*)vector,
		count,
		is_one_apdu ? "TRUE" : "FALSE",
		(const void*)bytes_written);

	pgm_return_val_if_fail (NULL != transport, PGM_IO_STATUS_ERROR);
	pgm_return_val_if_fail (count <= PGM_MAX_FRAGMENTS, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(count)) pgm_return_val_if_fail (NULL != vector, PGM_IO_STATUS_ERROR);
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&transport->lock)))
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	if (PGM_UNLIKELY(!transport->is_bound ||
	    transport->is_destroyed))
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	}

	pgm_mutex_lock (&transport->source_mutex);

/* pass on zero length as cannot count vector lengths */
	if (PGM_UNLIKELY(0 == count))
	{
		const int status = send_odata_copy (transport, NULL, count, bytes_written);
		pgm_mutex_unlock (&transport->source_mutex);
		pgm_rwlock_reader_unlock (&transport->lock);
		return status;
	}
	else if (1 == count)
	{
		const int status = send_odata (transport, vector[0], bytes_written);
		pgm_mutex_unlock (&transport->source_mutex);
		pgm_rwlock_reader_unlock (&transport->lock);
		return status;
	}

	size_t   bytes_sent	 = 0;
	unsigned packets_sent	 = 0;
	size_t   data_bytes_sent = 0;

/* continue if blocked mid-apdu */
	if (transport->is_apdu_eagain)
		goto retry_send;

	STATE(is_rate_limited) = FALSE;
	if (transport->is_nonblocking && transport->is_controlled_odata)
	{
		size_t total_tpdu_length = 0;
		for (unsigned i = 0; i < count; i++)
			total_tpdu_length += transport->iphdr_len + pgm_transport_pkt_offset2  (is_one_apdu, transport->use_pgmcc) + vector[i]->len;

/* calculation includes one iphdr length already */
		if (!pgm_rate_check (&transport->rate_control,
				     total_tpdu_length - transport->iphdr_len,
				     transport->is_nonblocking))
		{
			transport->blocklen = total_tpdu_length;
			pgm_mutex_unlock (&transport->source_mutex);
			pgm_rwlock_reader_unlock (&transport->lock);
			return PGM_IO_STATUS_RATE_LIMITED;
		}
		STATE(is_rate_limited) = TRUE;
	}

	if (is_one_apdu)
	{
		STATE(apdu_length)	= 0;
		STATE(first_sqn)	= pgm_txw_next_lead(transport->window);
		for (unsigned i = 0; i < count; i++)
		{
			if (PGM_UNLIKELY(vector[i]->len > transport->max_tsdu_fragment)) {
				pgm_mutex_unlock (&transport->source_mutex);
				pgm_rwlock_reader_unlock (&transport->lock);
				return PGM_IO_STATUS_ERROR;
			}
			STATE(apdu_length) += vector[i]->len;
		}
		if (PGM_UNLIKELY(STATE(apdu_length) > transport->max_apdu)) {
			pgm_mutex_unlock (&transport->source_mutex);
			pgm_rwlock_reader_unlock (&transport->lock);
			return PGM_IO_STATUS_ERROR;
		}
	}

	for (STATE(vector_index) = 0; STATE(vector_index) < count; STATE(vector_index)++)
	{
		STATE(tsdu_length) = vector[STATE(vector_index)]->len;
		
		STATE(skb) = pgm_skb_get(vector[STATE(vector_index)]);
		STATE(skb)->transport = transport;
		STATE(skb)->tstamp = pgm_time_update_now();

		STATE(skb)->pgm_header = (struct pgm_header*)STATE(skb)->head;
		STATE(skb)->pgm_data   = (struct pgm_data*)(STATE(skb)->pgm_header + 1);
		memcpy (STATE(skb)->pgm_header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
		STATE(skb)->pgm_header->pgm_sport	= transport->tsi.sport;
		STATE(skb)->pgm_header->pgm_dport	= transport->dport;
		STATE(skb)->pgm_header->pgm_type	= PGM_ODATA;
		STATE(skb)->pgm_header->pgm_options	= is_one_apdu ? PGM_OPT_PRESENT : 0;
		STATE(skb)->pgm_header->pgm_tsdu_length = htons (STATE(tsdu_length));

/* ODATA */
		STATE(skb)->pgm_data->data_sqn		= htonl (pgm_txw_next_lead(transport->window));
		STATE(skb)->pgm_data->data_trail	= htonl (pgm_txw_trail(transport->window));

		if (is_one_apdu)
		{
/* OPT_LENGTH */
			struct pgm_opt_length* opt_len		= (struct pgm_opt_length*)(STATE(skb)->pgm_data + 1);
			opt_len->opt_type			= PGM_OPT_LENGTH;
			opt_len->opt_length			= sizeof(struct pgm_opt_length);
			opt_len->opt_total_length		= htons (	sizeof(struct pgm_opt_length) +
										sizeof(struct pgm_opt_header) +
										sizeof(struct pgm_opt_fragment) );
/* OPT_FRAGMENT */
			struct pgm_opt_header* opt_header	= (struct pgm_opt_header*)(opt_len + 1);
			opt_header->opt_type			= PGM_OPT_FRAGMENT | PGM_OPT_END;
			opt_header->opt_length			= sizeof(struct pgm_opt_header) +
								  sizeof(struct pgm_opt_fragment);
			STATE(skb)->pgm_opt_fragment			= (struct pgm_opt_fragment*)(opt_header + 1);
			STATE(skb)->pgm_opt_fragment->opt_reserved	= 0;
			STATE(skb)->pgm_opt_fragment->opt_sqn		= htonl (STATE(first_sqn));
			STATE(skb)->pgm_opt_fragment->opt_frag_off	= htonl (STATE(data_bytes_offset));
			STATE(skb)->pgm_opt_fragment->opt_frag_len	= htonl (STATE(apdu_length));

			pgm_assert (STATE(skb)->data == (STATE(skb)->pgm_opt_fragment + 1));
		}
		else
		{
			pgm_assert (STATE(skb)->data == (STATE(skb)->pgm_data + 1));
		}

/* TODO: the assembly checksum & copy routine is faster than memcpy & pgm_cksum on >= opteron hardware */
		STATE(skb)->pgm_header->pgm_checksum	= 0;
		pgm_assert ((char*)STATE(skb)->data > (char*)STATE(skb)->pgm_header);
		const size_t pgm_header_len		= (char*)STATE(skb)->data - (char*)STATE(skb)->pgm_header;
		const uint32_t unfolded_header		= pgm_csum_partial (STATE(skb)->pgm_header, pgm_header_len, 0);
		STATE(unfolded_odata)			= pgm_csum_partial ((char*)STATE(skb)->data, STATE(tsdu_length), 0);
		STATE(skb)->pgm_header->pgm_checksum	= pgm_csum_fold (pgm_csum_block_add (unfolded_header, STATE(unfolded_odata), pgm_header_len));

/* add to transmit window, skb::data set to payload */
		pgm_spinlock_lock (&transport->txw_spinlock);
		pgm_txw_add (transport->window, STATE(skb));
		pgm_spinlock_unlock (&transport->txw_spinlock);
		ssize_t sent;
		size_t  tpdu_length;
retry_send:
		pgm_assert ((char*)STATE(skb)->tail > (char*)STATE(skb)->head);
		tpdu_length = (char*)STATE(skb)->tail - (char*)STATE(skb)->head;
		sent = pgm_sendto (transport,
				   !STATE(is_rate_limited),	/* rate limited on blocking */
				    FALSE,				/* regular socket */
				    STATE(skb)->head,
				    tpdu_length,
				    (struct sockaddr*)&transport->send_gsr.gsr_group,
				    pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
		if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno)) {
			transport->is_apdu_eagain = TRUE;
			transport->blocklen = tpdu_length;
			goto blocked;
		}

/* save unfolded odata for retransmissions */
		pgm_txw_set_unfolded_checksum (STATE(skb), STATE(unfolded_odata));

		if (PGM_LIKELY((size_t)sent == tpdu_length)) {
			bytes_sent += tpdu_length + transport->iphdr_len;	/* as counted at IP layer */
			packets_sent++;							/* IP packets */
			data_bytes_sent += STATE(tsdu_length);
		}

		pgm_free_skb (STATE(skb));
		STATE(data_bytes_offset) += STATE(tsdu_length);

/* check for end of transmission group */
		if (transport->use_proactive_parity) {
			const uint32_t odata_sqn   = ntohl (STATE(skb)->pgm_data->data_sqn);
			const uint32_t tg_sqn_mask = 0xffffffff << transport->tg_sqn_shift;
			if (!((odata_sqn + 1) & ~tg_sqn_mask))
				pgm_schedule_proactive_nak (transport, odata_sqn & tg_sqn_mask);
		}

	}
#ifdef TRANSPORT_DEBUG
	if (is_one_apdu)
	{
		pgm_assert( STATE(data_bytes_offset) == STATE(apdu_length) );
	}
#endif

	transport->is_apdu_eagain = FALSE;
	reset_heartbeat_spm (transport, STATE(skb)->tstamp);

	pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], bytes_sent);
	transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
	transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	if (bytes_written)
		*bytes_written = data_bytes_sent;
	pgm_mutex_unlock (&transport->source_mutex);
	pgm_rwlock_reader_unlock (&transport->lock);
	return PGM_IO_STATUS_NORMAL;

blocked:
	if (bytes_sent) {
		reset_heartbeat_spm (transport, STATE(skb)->tstamp);
		pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], bytes_sent);
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_MSGS_SENT]  += packets_sent;
		transport->cumulative_stats[PGM_PC_SOURCE_DATA_BYTES_SENT] += data_bytes_sent;
	}
	pgm_mutex_unlock (&transport->source_mutex);
	pgm_rwlock_reader_unlock (&transport->lock);
	return EAGAIN == errno ? PGM_IO_STATUS_WOULD_BLOCK : PGM_IO_STATUS_RATE_LIMITED;
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
	pgm_transport_t*      restrict transport,
	struct pgm_sk_buff_t* restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);
	pgm_assert ((char*)skb->tail > (char*)skb->head);

	const size_t tpdu_length = (char*)skb->tail - (char*)skb->head;

/* update previous odata/rdata contents */
	struct pgm_header* header	= skb->pgm_header;
	struct pgm_data* rdata		= skb->pgm_data;
	header->pgm_type		= PGM_RDATA;
/* RDATA */
        rdata->data_trail		= htonl (pgm_txw_trail(transport->window));

        header->pgm_checksum		= 0;
	const size_t pgm_header_len	= tpdu_length - ntohs(header->pgm_tsdu_length);
	uint32_t unfolded_header	= pgm_csum_partial (header, pgm_header_len, 0);
	uint32_t unfolded_odata		= pgm_txw_get_unfolded_checksum (skb);
	header->pgm_checksum		= pgm_csum_fold (pgm_csum_block_add (unfolded_header, unfolded_odata, pgm_header_len));

	const ssize_t sent = pgm_sendto (transport,
					 transport->is_controlled_rdata,	/* rate limited */
					 TRUE,				/* with router alert */
					 header,
					 tpdu_length,
					 (struct sockaddr*)&transport->send_gsr.gsr_group,
					 pgm_sockaddr_len((struct sockaddr*)&transport->send_gsr.gsr_group));
/* re-save unfolded payload for further retransmissions */
	pgm_txw_set_unfolded_checksum (skb, unfolded_odata);

	if (sent < 0 && EAGAIN == errno) {
		transport->blocklen = tpdu_length;
		return FALSE;
	}

/* re-set spm timer: we are already in the timer thread, no need to prod timers
 */
	pgm_mutex_lock (&transport->timer_mutex);
	transport->spm_heartbeat_state = 1;
	transport->next_heartbeat_spm = pgm_time_update_now() + transport->spm_heartbeat_interval[transport->spm_heartbeat_state++];
	pgm_mutex_unlock (&transport->timer_mutex);

	pgm_txw_inc_retransmit_count (skb);
	transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_BYTES_RETRANSMITTED] += ntohs(header->pgm_tsdu_length);
	transport->cumulative_stats[PGM_PC_SOURCE_SELECTIVE_MSGS_RETRANSMITTED]++;	/* impossible to determine APDU count */
	pgm_atomic_add32 (&transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT], tpdu_length + transport->iphdr_len);
	return TRUE;
}

/* eof */
