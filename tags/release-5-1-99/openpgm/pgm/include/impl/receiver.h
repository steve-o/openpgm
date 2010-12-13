/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_RECEIVER_H__
#define __PGM_IMPL_RECEIVER_H__

typedef struct pgm_peer_t pgm_peer_t;

#ifndef _WIN32
#	include <sys/socket.h>
#endif
#include <impl/framework.h>
#include <impl/rxw.h>

PGM_BEGIN_DECLS

/* Performance Counters */

enum {
	PGM_PC_RECEIVER_DATA_BYTES_RECEIVED,
	PGM_PC_RECEIVER_DATA_MSGS_RECEIVED,
	PGM_PC_RECEIVER_NAK_FAILURES,
	PGM_PC_RECEIVER_BYTES_RECEIVED,
/*	PGM_PC_RECEIVER_CKSUM_ERRORS, */		/* inherently same as source */
	PGM_PC_RECEIVER_MALFORMED_SPMS,
	PGM_PC_RECEIVER_MALFORMED_ODATA,
	PGM_PC_RECEIVER_MALFORMED_RDATA,
	PGM_PC_RECEIVER_MALFORMED_NCFS,
	PGM_PC_RECEIVER_PACKETS_DISCARDED,
	PGM_PC_RECEIVER_LOSSES,
/*	PGM_PC_RECEIVER_BYTES_DELIVERED_TO_APP, */
/*	PGM_PC_RECEIVER_MSGS_DELIVERED_TO_APP, */
	PGM_PC_RECEIVER_DUP_SPMS,
	PGM_PC_RECEIVER_DUP_DATAS,
	PGM_PC_RECEIVER_PARITY_NAK_PACKETS_SENT,
	PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT,
	PGM_PC_RECEIVER_PARITY_NAKS_SENT,
	PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT,
	PGM_PC_RECEIVER_PARITY_NAKS_RETRANSMITTED,
	PGM_PC_RECEIVER_SELECTIVE_NAKS_RETRANSMITTED,
	PGM_PC_RECEIVER_PARITY_NAKS_FAILED,
	PGM_PC_RECEIVER_SELECTIVE_NAKS_FAILED,
	PGM_PC_RECEIVER_NAKS_FAILED_RXW_ADVANCED,
	PGM_PC_RECEIVER_NAKS_FAILED_NCF_RETRIES_EXCEEDED,
	PGM_PC_RECEIVER_NAKS_FAILED_DATA_RETRIES_EXCEEDED,
/*	PGM_PC_RECEIVER_NAKS_FAILED_GEN_EXPIRED */
	PGM_PC_RECEIVER_NAK_FAILURES_DELIVERED,
	PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED,
	PGM_PC_RECEIVER_NAK_ERRORS,
/*	PGM_PC_RECEIVER_LAST_ACTIVITY, */
/*	PGM_PC_RECEIVER_NAK_SVC_TIME_MIN, */
	PGM_PC_RECEIVER_NAK_SVC_TIME_MEAN,
/*	PGM_PC_RECEIVER_NAK_SVC_TIME_MAX, */
/*	PGM_PC_RECEIVER_NAK_FAIL_TIME_MIN, */
	PGM_PC_RECEIVER_NAK_FAIL_TIME_MEAN,
/*	PGM_PC_RECEIVER_NAK_FAIL_TIME_MAX, */
/*	PGM_PC_RECEIVER_TRANSMIT_MIN, */
	PGM_PC_RECEIVER_TRANSMIT_MEAN,
/*	PGM_PC_RECEIVER_TRANSMIT_MAX, */
	PGM_PC_RECEIVER_ACKS_SENT, 

/* marker */
	PGM_PC_RECEIVER_MAX
};

struct pgm_peer_t {
	volatile uint32_t		ref_count;		    /* atomic integer */

	pgm_tsi_t			tsi;
	struct sockaddr_storage		group_nla;
	struct sockaddr_storage		nla, local_nla;		/* nla = advertised, local_nla = from packet */
	struct sockaddr_storage		poll_nla;		/* from parent to direct poll-response */
	struct sockaddr_storage		redirect_nla;		/* from dlr */
	pgm_time_t			polr_expiry;
	pgm_time_t			spmr_expiry;
	pgm_time_t			spmr_tstamp;

	pgm_rxw_t*      restrict      	window;
	pgm_list_t			peers_link;
	pgm_slist_t			pending_link;

	unsigned			is_fec_enabled:1;
	unsigned			has_proactive_parity:1;	    /* indicating availability from this source */
	unsigned			has_ondemand_parity:1;

	uint32_t			spm_sqn;
	pgm_time_t			expiry;

	pgm_time_t			ack_rb_expiry;			/* 0 = no ACK pending */
	pgm_time_t			ack_last_tstamp;		/* in source time reference */
	pgm_list_t			ack_link;

	uint32_t			last_poll_sqn;
	uint16_t			last_poll_round;
	pgm_time_t			last_packet;
	pgm_time_t			last_data_tstamp;		/* local timestamp of ack_last_tstamp */
	unsigned			last_commit;
	uint32_t			lost_count;
	uint32_t			last_cumulative_losses;
	volatile uint32_t		cumulative_stats[PGM_PC_RECEIVER_MAX];
	uint32_t			snap_stats[PGM_PC_RECEIVER_MAX];

	uint32_t			min_fail_time;
	uint32_t			max_fail_time;
};

PGM_GNUC_INTERNAL pgm_peer_t* pgm_new_peer (pgm_sock_t*const restrict, const pgm_tsi_t*const restrict, const struct sockaddr*const restrict, const socklen_t, const struct sockaddr*const restrict, const socklen_t, const pgm_time_t);
PGM_GNUC_INTERNAL void pgm_peer_unref (pgm_peer_t*);
PGM_GNUC_INTERNAL int pgm_flush_peers_pending (pgm_sock_t*const restrict, struct pgm_msgv_t**restrict, const struct pgm_msgv_t*const, size_t*const restrict, unsigned*const restrict);
PGM_GNUC_INTERNAL bool pgm_peer_has_pending (pgm_peer_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_peer_set_pending (pgm_sock_t*const restrict, pgm_peer_t*const restrict);
PGM_GNUC_INTERNAL bool pgm_check_peer_state (pgm_sock_t*const, const pgm_time_t);
PGM_GNUC_INTERNAL void pgm_set_reset_error (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_msgv_t*const restrict);
PGM_GNUC_INTERNAL pgm_time_t pgm_min_receiver_expiry (pgm_sock_t*, pgm_time_t) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_peer_nak (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_data (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_ncf (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_spm (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_poll (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;

PGM_END_DECLS

#endif /* __PGM_IMPL_RECEIVER_H__ */
