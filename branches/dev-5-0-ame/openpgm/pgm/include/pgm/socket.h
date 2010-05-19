/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM socket.
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

#ifndef __PGM_TRANSPORT_H__
#define __PGM_TRANSPORT_H__

#ifdef CONFIG_HAVE_POLL
#	include <poll.h>
#endif
#ifdef CONFIG_HAVE_EPOLL
#	include <sys/epoll.h>
#endif
#ifndef _WIN32
#	include <sys/select.h>
#	include <sys/socket.h>
#endif
#include <pgm/types.h>

typedef struct pgm_sock_t pgm_sock_t;

#include <pgm/error.h>
#include <pgm/gsi.h>
#include <pgm/tsi.h>
#include <pgm/if.h>
#include <pgm/txw.h>
#include <pgm/thread.h>
#include <pgm/notify.h>
#include <pgm/time.h>
#include <pgm/rand.h>
#include <pgm/hashtable.h>
#include <pgm/list.h>
#include <pgm/slist.h>
#include <pgm/rate_control.h>

PGM_BEGIN_DECLS

#ifndef IP_MAX_MEMBERSHIPS
#	define IP_MAX_MEMBERSHIPS	20
#endif

/* IO status */
enum {
	PGM_IO_STATUS_ERROR,		/* an error occurred */
	PGM_IO_STATUS_NORMAL,		/* success */
	PGM_IO_STATUS_RESET,		/* session reset */
	PGM_IO_STATUS_FIN,		/* session finished */
	PGM_IO_STATUS_EOF,		/* socket closed */
	PGM_IO_STATUS_WOULD_BLOCK,	/* resource temporarily unavailable */
	PGM_IO_STATUS_RATE_LIMITED,	/* would-block on rate limit, check timer */
	PGM_IO_STATUS_TIMER_PENDING	/* would-block with pending timer */
};


/* Performance Counters */
enum {
	PGM_PC_SOURCE_DATA_BYTES_SENT,
	PGM_PC_SOURCE_DATA_MSGS_SENT,	    		/* msgs = packets not APDUs */
/*	PGM_PC_SOURCE_BYTES_BUFFERED, */	    	/* tx window contents in bytes */
/*	PGM_PC_SOURCE_MSGS_BUFFERED, */
	PGM_PC_SOURCE_BYTES_SENT,
/*	PGM_PC_SOURCE_RAW_NAKS_RECEIVED, */
	PGM_PC_SOURCE_CKSUM_ERRORS,
	PGM_PC_SOURCE_MALFORMED_NAKS,
	PGM_PC_SOURCE_PACKETS_DISCARDED,
	PGM_PC_SOURCE_PARITY_BYTES_RETRANSMITTED,
	PGM_PC_SOURCE_SELECTIVE_BYTES_RETRANSMITTED,
	PGM_PC_SOURCE_PARITY_MSGS_RETRANSMITTED,
	PGM_PC_SOURCE_SELECTIVE_MSGS_RETRANSMITTED,
	PGM_PC_SOURCE_PARITY_NAK_PACKETS_RECEIVED,
	PGM_PC_SOURCE_SELECTIVE_NAK_PACKETS_RECEIVED,   /* total packets */
	PGM_PC_SOURCE_PARITY_NAKS_RECEIVED,
	PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED,	    	/* serial numbers */
	PGM_PC_SOURCE_PARITY_NAKS_IGNORED,
	PGM_PC_SOURCE_SELECTIVE_NAKS_IGNORED,
/*	PGM_PC_SOURCE_ACK_ERRORS, */
/*	PGM_PC_SOURCE_PGMCC_ACKER, */
	PGM_PC_SOURCE_TRANSMISSION_CURRENT_RATE,
/*	PGM_PC_SOURCE_ACK_PACKETS_RECEIVED, */
	PGM_PC_SOURCE_PARITY_NNAK_PACKETS_RECEIVED,
	PGM_PC_SOURCE_SELECTIVE_NNAK_PACKETS_RECEIVED,
	PGM_PC_SOURCE_PARITY_NNAKS_RECEIVED,
	PGM_PC_SOURCE_SELECTIVE_NNAKS_RECEIVED,
	PGM_PC_SOURCE_NNAK_ERRORS,

/* marker */
	PGM_PC_SOURCE_MAX
};

struct pgm_sock_t {
	pgm_tsi_t           		tsi;
	uint16_t			dport;
	uint16_t			udp_encap_ucast_port;
	uint16_t			udp_encap_mcast_port;
	uint32_t			rand_node_id;			/* node identifier */

	pgm_rwlock_t			lock;				/* running / destroyed */
	pgm_mutex_t			receiver_mutex;			/* receiver API */
	pgm_mutex_t			source_mutex;			/* source API */
	pgm_spinlock_t			txw_spinlock;			/* transmit window */
	pgm_mutex_t			send_mutex;			/* non-router alert socket */
	pgm_mutex_t			timer_mutex;			/* next timer expiration */

	bool				is_bound;
	bool				is_destroyed;
	bool	            		is_reset;
	bool				is_abort_on_reset;

	bool				can_send_data;			/* and SPMs */
	bool				can_send_nak;			/* muted receiver */
	bool				can_recv_data;			/* send-only */
	bool				is_edge_triggered_recv;
	bool				is_nonblocking;

	struct group_source_req		send_gsr;			/* multicast */
	struct sockaddr_storage		send_addr;			/* unicast nla */
	int				send_sock;
	int				send_with_router_alert_sock;
	struct group_source_req 	recv_gsr[IP_MAX_MEMBERSHIPS];	/* sa_family = 0 terminated */
	unsigned			recv_gsr_len;
	int				recv_sock;

	size_t				max_apdu;
	uint16_t			max_tpdu;
	uint16_t			max_tsdu;		    /* excluding optional varpkt_len word */
	uint16_t			max_tsdu_fragment;
	size_t				iphdr_len;
	bool				use_multicast_loop;    	    /* and reuseaddr for UDP encapsulation */
	unsigned			hops;
	unsigned			txw_sqns, txw_secs;
	unsigned			rxw_sqns, rxw_secs;
	ssize_t				txw_max_rte, rxw_max_rte;
	size_t				sndbuf, rcvbuf;		    /* setsockopt (SO_SNDBUF/SO_RCVBUF) */

	pgm_txw_t* restrict    		window;
	pgm_rate_t			rate_control;
	bool				is_controlled_spm;
	bool				is_controlled_odata;
	bool				is_controlled_rdata;

	bool				use_cr;			/* congestion reports */
	bool				use_pgmcc;		/* congestion control */
	bool				is_pending_crqst;
	pgm_time_t			next_crqst;
	pgm_time_t			mrtt;			/* multicast round-trip time */
	pgm_time_t			crqst_ivl;
	pgm_time_t			acker_ivl;
	struct sockaddr_storage		acker_nla;
/*
 * ignore_cong    - previous congestion lead sqn
 * cc_token
 * cc_window
 *   w_s16        - weight for lossrate computation, 0.992
 *   ss_threshold - threshold for exponential window opening
 * dupacks        - number of duplicate acks
 * ack_bitmask    - S/R most recent ACKs for ODATA
 *
 * do_ack          - toggle ack generation
 * rtt_cong_filter - ignore multiple cong.event per rtt
 * dupack          - threshold for dup acks
 */

	pgm_notify_t			rdata_notify;

	pgm_hash_t			last_hash_key;
	void* restrict			last_hash_value;
	unsigned			last_commit;
	size_t				blocklen;		    /* length of buffer blocked */
	bool				is_apdu_eagain;		    /* writer-lock on window_lock exists as send would block */
	bool				is_spm_eagain;		    /* writer-lock in receiver */

	struct {
		size_t			    	data_pkt_offset;
		size_t		   		data_bytes_offset;
		uint32_t	    		first_sqn;
		struct pgm_sk_buff_t*		skb;		/* references external buffer */
		size_t				tsdu_length;
		uint32_t			unfolded_odata;
		size_t				apdu_length;
		unsigned			vector_index;
		size_t				vector_offset;
		bool				is_rate_limited;
	} pkt_dontwait_state;

	uint32_t			spm_sqn;
	unsigned			spm_ambient_interval;	    /* microseconds */
	unsigned* restrict		spm_heartbeat_interval;     /* zero terminated, zero lead-pad */
	unsigned			spm_heartbeat_state;	    /* indexof spm_heartbeat_interval */
	unsigned			spm_heartbeat_len;
	unsigned			peer_expiry;		    /* from absence of SPMs */
	unsigned			spmr_expiry;		    /* waiting for peer SPMRs */

	pgm_rand_t			rand_;			    /* for calculating nak_rb_ivl from nak_bo_ivl */
	unsigned			nak_data_retries, nak_ncf_retries;
	pgm_time_t			nak_bo_ivl, nak_rpt_ivl, nak_rdata_ivl;
	pgm_time_t			next_heartbeat_spm, next_ambient_spm;

	bool				use_proactive_parity;
	bool				use_ondemand_parity;
	bool				use_varpkt_len;
	uint8_t				rs_n;
	uint8_t				rs_k;
	uint8_t				rs_proactive_h;		    /* 0 <= proactive-h <= ( n - k ) */
	uint8_t				tg_sqn_shift;
	struct pgm_sk_buff_t* restrict	rx_buffer;

	pgm_rwlock_t			peers_lock;
	pgm_hashtable_t* restrict	peers_hashtable;	    /* fast lookup */
	pgm_list_t*      restrict	peers_list;		    /* easy iteration */
	pgm_slist_t*     restrict	peers_pending;		    /* rxw: have or lost data */
	pgm_notify_t			pending_notify;		    /* timer to rx */
	bool				is_pending_read;
	pgm_time_t			next_poll;

	uint32_t			cumulative_stats[PGM_PC_SOURCE_MAX];
	uint32_t			snap_stats[PGM_PC_SOURCE_MAX];
	pgm_time_t			snap_time;
};


/* global variables */
extern pgm_rwlock_t pgm_sock_list_lock;
extern pgm_slist_t* pgm_sock_list;

bool pgm_socket (pgm_sock_t**restrict, struct pgm_sock_info_t*restrict, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;
bool pgm_bind (pgm_sock_t*restrict, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;
bool pgm_close (pgm_sock_t*, bool);
bool pgm_setsockopt (pgm_sock_t*const, const int, const int, const void*, const socklen_t);

int pgm_select_info (pgm_sock_t*const restrict, fd_set*const restrict, fd_set*const restrict, int*const restrict);
#ifdef CONFIG_HAVE_POLL
int pgm_poll_info (pgm_sock_t*const restrict, struct pollfd*const restrict, int*const restrict, const int);
#endif
#ifdef CONFIG_HAVE_EPOLL
int pgm_epoll_ctl (pgm_sock_t*const, const int, const int, const int);
#endif

PGM_END_DECLS

#endif /* __PGM_TRANSPORT_H__ */
