/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM transport.
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

#ifndef __PGM_TRANSPORT_H__
#define __PGM_TRANSPORT_H__

#ifdef CONFIG_HAVE_POLL
#	include <poll.h>
#endif
#ifdef CONFIG_HAVE_EPOLL
#	include <sys/epoll.h>
#endif

#include <glib.h>

#ifndef __PGM_GSI_H__
#   include <pgm/gsi.h>
#endif

#ifndef __PGM_TSI_H__
#   include <pgm/tsi.h>
#endif

typedef struct pgm_transport_t pgm_transport_t;

#ifndef __PGM_IF_H__
#   include <pgm/if.h>
#endif

#ifndef __PGM_SOCKADDR_H__
#   include <pgm/sockaddr.h>
#endif

#ifndef __PGM_TIME_H__
#   include <pgm/time.h>
#endif

#ifndef __PGM_NOTIFY_H__
#   include <pgm/notify.h>
#endif

#ifndef __PGM_SKBUFF_H__
#   include <pgm/skbuff.h>
#endif


#define PGM_TRANSPORT_ERROR	pgm_transport_error_quark ()


/* Performance Counters */

typedef enum {
/* source side */
    PGM_PC_SOURCE_DATA_BYTES_SENT,
    PGM_PC_SOURCE_DATA_MSGS_SENT,	    /* msgs = packets not APDUs */
/*  PGM_PC_SOURCE_BYTES_BUFFERED, */	    /* tx window contents in bytes */
/*  PGM_PC_SOURCE_MSGS_BUFFERED, */
    PGM_PC_SOURCE_BYTES_SENT,
/*  PGM_PC_SOURCE_RAW_NAKS_RECEIVED, */
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
    PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED,	    /* serial numbers */
    PGM_PC_SOURCE_PARITY_NAKS_IGNORED,
    PGM_PC_SOURCE_SELECTIVE_NAKS_IGNORED,
/*  PGM_PC_SOURCE_ACK_ERRORS, */
/*  PGM_PC_SOURCE_PGMCC_ACKER, */
    PGM_PC_SOURCE_TRANSMISSION_CURRENT_RATE,
/*  PGM_PC_SOURCE_ACK_PACKETS_RECEIVED, */
    PGM_PC_SOURCE_PARITY_NNAK_PACKETS_RECEIVED,
    PGM_PC_SOURCE_SELECTIVE_NNAK_PACKETS_RECEIVED,
    PGM_PC_SOURCE_PARITY_NNAKS_RECEIVED,
    PGM_PC_SOURCE_SELECTIVE_NNAKS_RECEIVED,
    PGM_PC_SOURCE_NNAK_ERRORS,

/* marker */
    PGM_PC_SOURCE_MAX
} pgm_pc_source_e;

typedef enum {
/* receiver side */
    PGM_PC_RECEIVER_DATA_BYTES_RECEIVED,
    PGM_PC_RECEIVER_DATA_MSGS_RECEIVED,
    PGM_PC_RECEIVER_NAK_FAILURES,
    PGM_PC_RECEIVER_BYTES_RECEIVED,
/*  PGM_PC_RECEIVER_CKSUM_ERRORS, */	    /* inherently same as source */
    PGM_PC_RECEIVER_MALFORMED_SPMS,
    PGM_PC_RECEIVER_MALFORMED_ODATA,
    PGM_PC_RECEIVER_MALFORMED_RDATA,
    PGM_PC_RECEIVER_MALFORMED_NCFS,
    PGM_PC_RECEIVER_PACKETS_DISCARDED,
    PGM_PC_RECEIVER_LOSSES,
/*  PGM_PC_RECEIVER_BYTES_DELIVERED_TO_APP, */
/*  PGM_PC_RECEIVER_MSGS_DELIVERED_TO_APP, */
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
/*  PGM_PC_RECEIVER_NAKS_FAILED_GEN_EXPIRED */
    PGM_PC_RECEIVER_NAK_FAILURES_DELIVERED,
    PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED,
    PGM_PC_RECEIVER_NAK_ERRORS,
/*  PGM_PC_RECEIVER_LAST_ACTIVITY, */
/*  PGM_PC_RECEIVER_NAK_SVC_TIME_MIN, */
    PGM_PC_RECEIVER_NAK_SVC_TIME_MEAN,
/*  PGM_PC_RECEIVER_NAK_SVC_TIME_MAX, */
/*  PGM_PC_RECEIVER_NAK_FAIL_TIME_MIN, */
    PGM_PC_RECEIVER_NAK_FAIL_TIME_MEAN,
/*  PGM_PC_RECEIVER_NAK_FAIL_TIME_MAX, */
/*  PGM_PC_RECEIVER_TRANSMIT_MIN, */
    PGM_PC_RECEIVER_TRANSMIT_MEAN,
/*  PGM_PC_RECEIVER_TRANSMIT_MAX, */
/*  PGM_PC_RECEIVER_ACKS_SENT, */

/* marker */
    PGM_PC_RECEIVER_MAX
} pgm_pc_receiver_e;

#ifndef __PGM_MSGV_H__
#   include <pgm/msgv.h>
#endif

typedef enum
{
	/* Derived from errno */
	PGM_TRANSPORT_ERROR_INVAL,
	PGM_TRANSPORT_ERROR_MFILE,
	PGM_TRANSPORT_ERROR_NFILE,
	PGM_TRANSPORT_ERROR_NODEV,
	PGM_TRANSPORT_ERROR_NOMEM,
	PGM_TRANSPORT_ERROR_FAULT,
	PGM_TRANSPORT_ERROR_PERM,
	PGM_TRANSPORT_ERROR_NOPROTOOPT,
	/* Derived from eai_errno */
	PGM_TRANSPORT_ERROR_ADDRFAMILY,
	PGM_TRANSPORT_ERROR_AGAIN,
	PGM_TRANSPORT_ERROR_BADFLAGS,
	PGM_TRANSPORT_ERROR_FAIL,
	PGM_TRANSPORT_ERROR_FAMILY,
	PGM_TRANSPORT_ERROR_MEMORY,
	PGM_TRANSPORT_ERROR_NODATA,
	PGM_TRANSPORT_ERROR_NONAME,
	PGM_TRANSPORT_ERROR_SERVICE,
	PGM_TRANSPORT_ERROR_SOCKTYPE,
	PGM_TRANSPORT_ERROR_FAILED
} PGMTransportError;

struct pgm_sqn_list_t {
    guint	    len;
    guint32         sqn[63];	/* list of sequence numbers */
};

typedef struct pgm_sqn_list_t pgm_sqn_list_t;

struct pgm_peer_t {
    gint		ref_count;		    /* atomic integer */

    pgm_tsi_t           tsi;
    struct sockaddr_storage	group_nla;
    struct sockaddr_storage	nla, local_nla;	    /* nla = advertised, local_nla = from packet */
    struct sockaddr_storage	redirect_nla;	    /* from dlr */
    pgm_time_t		spmr_expiry;

    GStaticMutex	mutex;

    gpointer            rxw;		/* pgm_rxw_t */
    pgm_transport_t*    transport;
    GList		link_;

    gpointer		rs;
    gboolean		use_proactive_parity;	    /* indicating availability from this source */
    gboolean		use_ondemand_parity;
    guint		rs_n;
    guint		rs_k;
    guint32		tg_sqn_shift;		    /* log2 (rs_k) */

    guint32		spm_sqn;
    pgm_time_t		expiry;

    pgm_time_t		last_packet;
    guint32		cumulative_stats[PGM_PC_RECEIVER_MAX];
    guint32		snap_stats[PGM_PC_RECEIVER_MAX];

    guint32		min_fail_time;
    guint32		max_fail_time;
};

typedef struct pgm_peer_t pgm_peer_t;

struct pgm_transport_t {
    pgm_tsi_t           tsi;
    guint16		dport;
    guint16		udp_encap_ucast_port;
    guint16		udp_encap_mcast_port;

    GStaticMutex	mutex;
    GThread		*timer_thread;
    GMainLoop		*timer_loop;
    GMainContext	*timer_context;
    guint               timer_id;
    gboolean		is_bound;
    gboolean		is_open;
    gboolean            has_lost_data;
    gboolean		will_close_on_failure;

    gboolean		can_send_data;			/* and SPMs */
    gboolean		can_send_nak;
    gboolean		can_recv;
    gboolean		is_edge_triggered_recv;

    GCond		*thread_cond;
    GMutex		*thread_mutex;

    struct group_source_req send_gsr;			/* multicast */
    struct sockaddr_storage send_addr;			/* unicast nla */
    GStaticMutex	send_mutex;
    int			send_sock;
    GStaticMutex	send_with_router_alert_mutex;
    int			send_with_router_alert_sock;
    struct group_source_req recv_gsr[IP_MAX_MEMBERSHIPS];	/* sa_family = 0 terminated */
    guint		recv_gsr_len;
    int			recv_sock;

    guint16		max_tpdu;
    guint16		max_tsdu;		    /* excluding optional varpkt_len word */
    guint16		max_tsdu_fragment;
    gsize		iphdr_len;
    gboolean		use_multicast_loop;    /* and reuseaddr for UDP encapsulation */
    guint		hops;
    guint		txw_sqns, txw_secs, txw_max_rte;
    guint		rxw_sqns, rxw_secs, rxw_max_rte;
    int			sndbuf, rcvbuf;		    /* setsockopt (SO_SNDBUF/SO_RCVBUF) */

    GStaticRWLock	txw_lock;
    gpointer            txw;		   	    /* pgm_txw_t */
    gpointer		rate_control;		    /* rate_t */

    gboolean		is_apdu_eagain;		    /* writer-lock on txw_lock exists
						       as send would block */

    struct {
	guint		    data_pkt_offset;
	gsize		    data_bytes_offset;
	guint32		    first_sqn;
	struct pgm_sk_buff_t* skb;			/* references external buffer */
	gsize		    tsdu_length;
	guint32		    unfolded_odata;
	gsize		    apdu_length;
	guint		    vector_index;
	gsize		    vector_offset;
	gboolean	    is_rate_limited;
    } pkt_dontwait_state;

    guint32		spm_sqn;
    guint		spm_ambient_interval;	    /* microseconds */
    guint*		spm_heartbeat_interval;	    /* zero terminated, zero lead-pad */
    guint		spm_heartbeat_state;	    /* indexof spm_heartbeat_interval */
    guint		peer_expiry;		    /* from absence of SPMs */
    guint		spmr_expiry;		    /* waiting for peer SPMRs */

    GRand*		rand_;			    /* for calculating nak_rb_ivl from nak_bo_ivl */
    guint		nak_data_retries, nak_ncf_retries;
    pgm_time_t		nak_bo_ivl, nak_rpt_ivl, nak_rdata_ivl;
    pgm_time_t		next_heartbeat_spm, next_ambient_spm;

    gboolean		use_proactive_parity;
    gboolean		use_ondemand_parity;
    gboolean		use_varpkt_len;
    guint		rs_n;
    guint		rs_k;
    guint		rs_proactive_h;		    /* 0 <= proactive-h <= ( n - k ) */
    guint		tg_sqn_shift;
    struct pgm_sk_buff_t* rx_buffer;

    GStaticRWLock	peers_lock;
    GHashTable*		peers_hashtable;	    /* fast lookup */
    GList*		peers_list;		    /* easy iteration */
    GSList*		peers_waiting;		    /* rxw: have or lost data */
    GStaticMutex	waiting_mutex;
    pgm_notify_t	waiting_notify;		    /* timer to rx */
    gboolean		is_waiting_read;

    pgm_notify_t	rdata_notify;		    /* rx to timer */
    GIOChannel*		rdata_channel;
    guint		rdata_id;

    pgm_time_t		next_poll;
    pgm_notify_t	timer_notify;		    /* any to timer */
    GIOChannel*		notify_channel;
    guint               notify_id;
    pgm_notify_t	timer_shutdown;
    GIOChannel*		shutdown_channel;
    guint               shutdown_id;

    guint32		cumulative_stats[PGM_PC_SOURCE_MAX];
    guint32		snap_stats[PGM_PC_SOURCE_MAX];
    pgm_time_t		snap_time;
};


/* global variables */
extern GStaticRWLock pgm_transport_list_lock;
extern GSList* pgm_transport_list;


G_BEGIN_DECLS

int pgm_init (void);
gboolean pgm_supported (void) G_GNUC_WARN_UNUSED_RESULT;
int pgm_shutdown (void);

void pgm_drop_superuser (void);

GQuark pgm_transport_error_quark (void);
PGMTransportError pgm_transport_error_from_errno (gint);
PGMTransportError pgm_transport_error_from_eai_errno (gint);
gboolean pgm_transport_create (pgm_transport_t**, struct pgm_transport_info_t*, GError**) G_GNUC_WARN_UNUSED_RESULT;
gboolean pgm_transport_bind (pgm_transport_t*, GError**) G_GNUC_WARN_UNUSED_RESULT;
gboolean pgm_transport_destroy (pgm_transport_t*, gboolean);
gboolean pgm_transport_set_max_tpdu (pgm_transport_t*, guint16);
gboolean pgm_transport_set_multicast_loop (pgm_transport_t*, gboolean);
gboolean pgm_transport_set_hops (pgm_transport_t*, gint);
gboolean pgm_transport_set_sndbuf (pgm_transport_t*, int);
gboolean pgm_transport_set_rcvbuf (pgm_transport_t*, int);
gboolean pgm_transport_set_fec (pgm_transport_t*, guint, gboolean, gboolean, guint, guint);
gboolean pgm_transport_set_send_only (pgm_transport_t*, gboolean);
gboolean pgm_transport_set_recv_only (pgm_transport_t*, gboolean);
gboolean pgm_transport_set_close_on_failure (pgm_transport_t*, gboolean);

gsize pgm_transport_pkt_offset (gboolean) G_GNUC_WARN_UNUSED_RESULT;
static inline gsize pgm_transport_max_tsdu (pgm_transport_t* transport, gboolean can_fragment)
{
    gsize max_tsdu = can_fragment ? transport->max_tsdu_fragment : transport->max_tsdu;
    if (transport->use_varpkt_len)
	max_tsdu -= sizeof (guint16);
    return max_tsdu;
}
int pgm_transport_select_info (pgm_transport_t*, fd_set*, fd_set*, int*);
#ifdef CONFIG_HAVE_POLL
int pgm_transport_poll_info (pgm_transport_t*, struct pollfd*, int*, int);
#endif
#ifdef CONFIG_HAVE_EPOLL
int pgm_transport_epoll_ctl (pgm_transport_t*, int, int, int);
#endif

int pgm_transport_join_group (pgm_transport_t*, struct group_req*, gsize);
int pgm_transport_leave_group (pgm_transport_t*, struct group_req*, gsize);
int pgm_transport_block_source (pgm_transport_t*, struct group_source_req*, gsize);
int pgm_transport_unblock_source (pgm_transport_t*, struct group_source_req*, gsize);
int pgm_transport_join_source_group (pgm_transport_t*, struct group_source_req*, gsize);
int pgm_transport_leave_source_group (pgm_transport_t*, struct group_source_req*, gsize);
int pgm_transport_msfilter (pgm_transport_t*, struct group_filter*, gsize);

G_END_DECLS

#endif /* __PGM_TRANSPORT_H__ */
