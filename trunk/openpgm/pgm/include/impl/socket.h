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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_SOCKET_H__
#define __PGM_IMPL_SOCKET_H__

struct pgm_sock_t;

#include <impl/framework.h>
#include <impl/txw.h>
#include <impl/source.h>

PGM_BEGIN_DECLS

#ifndef IP_MAX_MEMBERSHIPS
#	define IP_MAX_MEMBERSHIPS	20
#endif

struct pgm_sock_t {
	sa_family_t			family;				/* communications domain */
	int				socket_type;
	int				protocol;
	pgm_tsi_t           		tsi;
	in_port_t			dport;
	in_port_t			udp_encap_ucast_port;
	in_port_t			udp_encap_mcast_port;
	uint32_t			rand_node_id;			/* node identifier */

	pgm_rwlock_t			lock;				/* running / destroyed */
	pgm_mutex_t			receiver_mutex;			/* receiver API */
	pgm_mutex_t			source_mutex;			/* source API */
	pgm_spinlock_t			txw_spinlock;			/* transmit window */
	pgm_mutex_t			send_mutex;			/* non-router alert socket */
	pgm_mutex_t			timer_mutex;			/* next timer expiration */

	bool				is_bound;
	bool				is_connected;
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
	SOCKET				send_sock;
	SOCKET				send_with_router_alert_sock;
	struct group_source_req 	recv_gsr[IP_MAX_MEMBERSHIPS];	/* sa_family = 0 terminated */
	unsigned			recv_gsr_len;
	SOCKET				recv_sock;

	size_t				max_apdu;
	uint16_t			max_tpdu;
	uint16_t			max_tsdu;		    /* excluding optional var_pktlen word */
	uint16_t			max_tsdu_fragment;
	size_t				iphdr_len;
	bool				use_multicast_loop;    	    /* and reuseaddr for UDP encapsulation */
	unsigned			hops;
	unsigned			txw_sqns, txw_secs;
	unsigned			rxw_sqns, rxw_secs;
	ssize_t				txw_max_rte, rxw_max_rte;
	ssize_t				odata_max_rte;
	ssize_t				rdata_max_rte;
	size_t				sndbuf, rcvbuf;		    /* setsockopt (SO_SNDBUF/SO_RCVBUF) */

	pgm_txw_t* restrict    		window;
	pgm_rate_t			rate_control;
	pgm_rate_t			odata_rate_control;
	pgm_rate_t			rdata_rate_control;
	pgm_time_t			adv_ivl;		/* advancing with data */
	unsigned			adv_mode;		/* 0 = time, 1 = data */
	bool				is_controlled_spm;
	bool				is_controlled_odata;
	bool				is_controlled_rdata;

	bool				use_cr;			/* congestion reports */
	bool				use_pgmcc;		/* congestion control */
	bool				is_pending_crqst;
	unsigned			ack_c;			/* constant C */
	unsigned			ack_c_p;		/* constant Cᵨ */
	uint32_t			ssthresh;		/* slow-start threshold */
	uint32_t			tokens;
	uint32_t			cwnd_size;		/* congestion window size */
	uint32_t			ack_rx_max;
	uint32_t			ack_bitmap;
	uint32_t			acks_after_loss;
	uint32_t			suspended_sqn;
	bool				is_congested;
	pgm_time_t			ack_expiry;
	pgm_time_t			ack_expiry_ivl;
	pgm_time_t			next_crqst;
	pgm_time_t			crqst_ivl;
	pgm_time_t			ack_bo_ivl;
	struct sockaddr_storage		acker_nla;
	uint64_t			acker_loss;

	pgm_notify_t			ack_notify;
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
	bool				use_var_pktlen;
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

size_t pgm_pkt_offset (bool, sa_family_t);

PGM_END_DECLS

#endif /* __PGM_IMPL_SOCKET_H__ */
