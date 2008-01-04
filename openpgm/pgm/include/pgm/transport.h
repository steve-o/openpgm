/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM transport.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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

#include <glib.h>

#ifndef __PGM_GSI_H__
#   include "gsi.h"
#endif

#ifndef __PGM_SOCKADDR_H__
#   include "sockaddr.h"
#endif

#ifndef __PGM_TIMER_H__
#   include "timer.h"
#endif


typedef struct pgm_transport_t pgm_transport_t;

struct pgm_tsi_t {            /* transport session identifier */
    pgm_gsi_t   gsi;	    /* global session identifier */
    guint16     sport;	    /* source port: a random number to help detect session re-starts */
};

typedef struct pgm_tsi_t pgm_tsi_t;

struct pgm_sqn_list_t {
    gint	    len;
    guint32         sqn[63];	/* list of sequence numbers */
};

typedef struct pgm_sqn_list_t pgm_sqn_list_t;

struct pgm_peer_t {
    gint		ref_count;

    pgm_tsi_t           tsi;
    struct sockaddr_storage	nla, local_nla;	    /* nla = advertised, local_nla = from packet */
    struct sockaddr_storage	redirect_nla;	    /* from dlr */
    pgm_time_t		spmr_expiry;

    GStaticMutex	mutex;

    gpointer            rxw;		/* pgm_rxw_t */
    pgm_transport_t*    transport;

    int			spm_sqn;
    pgm_time_t		expiry;
};

typedef struct pgm_peer_t pgm_peer_t;
typedef struct pgm_event_t pgm_event_t;

struct pgm_event_t {
    gpointer		data;
    guint		len;
    struct pgm_peer_t*	peer;
};

struct pgm_transport_t {
    pgm_tsi_t           tsi;
    guint16		dport;

    guint16		udp_encap_port;

    GStaticMutex	mutex;
    GThread		*rx_thread, *timer_thread;
    GMainLoop		*rx_loop, *timer_loop;
    GMainContext	*rx_context, *timer_context;
    gboolean		bound;

    GCond		*thread_cond;
    GMutex		*thread_mutex;

    struct pgm_sock_mreq send_smr;			/* multicast & unicast nla */
    GStaticMutex	send_mutex;
    int			send_sock;
    GStaticMutex	send_with_router_alert_mutex;
    int			send_with_router_alert_sock;
    struct pgm_sock_mreq recv_smr[IP_MAX_MEMBERSHIPS];	/* sa_family = 0 terminated */
    int			recv_sock;
    GIOChannel*		recv_channel;

    guint16		max_tpdu;
    gint		hops;
    guint		txw_preallocate, txw_sqns, txw_secs, txw_max_rte;
    guint		rxw_preallocate, rxw_sqns, rxw_secs, rxw_max_rte;
    int			sndbuf, rcvbuf;

    GStaticRWLock	txw_lock;
    gpointer            txw;		   	    /* pgm_txw_t */
    gpointer		rate_control;		    /* rate_t */

    int			spm_sqn;
    guint		spm_ambient_interval;	    /* microseconds */
    guint*		spm_heartbeat_interval;	    /* zero terminated, zero lead-pad */
    guint		spm_heartbeat_state;	    /* indexof spm_heartbeat_interval */
    gchar*		spm_packet;
    int			spm_len;

    guint		peer_expiry;		    /* from absence of SPMs */
    guint		spmr_expiry;		    /* waiting for peer SPMRs */

    guint		nak_data_retries, nak_ncf_retries;
    guint		nak_rb_ivl, nak_rpt_ivl, nak_rdata_ivl;
    pgm_time_t		next_heartbeat_spm, next_ambient_spm;
    pgm_time_t		next_spmr_expiry;

    gboolean		proactive_parity;
    gboolean		ondemand_parity;
    guint		fec_n;
    guint		fec_k;
    guint		fec_padding;

    GStaticRWLock	peers_lock;
    GHashTable*		peers;

    GAsyncQueue*	commit_queue;
    int			commit_pipe[2];
    GTrashStack*	trash_event;		    /* sizeof(struct pgm_event) */
    guint		event_preallocate;
    GStaticMutex	event_mutex;

    GAsyncQueue*	rdata_queue;
    int			rdata_pipe[2];
    GTrashStack*	trash_rdata;
    GIOChannel*		rdata_channel;
    GStaticMutex	rdata_mutex;

    pgm_time_t		next_poll;
    int			timer_pipe[2];
    GIOChannel*		timer_channel;
};

typedef int (*pgm_eventfn_t)(gpointer, guint, gpointer);


G_BEGIN_DECLS

int pgm_init (void);

int pgm_event_unref (pgm_transport_t*, pgm_event_t*);

gchar* pgm_print_tsi (const pgm_tsi_t*);

int pgm_transport_create (pgm_transport_t**, pgm_gsi_t*, guint16, struct pgm_sock_mreq*, int, struct pgm_sock_mreq*);
int pgm_transport_bind (pgm_transport_t*);
GSource* pgm_transport_create_watch (pgm_transport_t*);
int pgm_transport_add_watch_full (pgm_transport_t*, gint, pgm_eventfn_t, gpointer, GDestroyNotify);
int pgm_transport_add_watch (pgm_transport_t*, pgm_eventfn_t, gpointer);
int pgm_transport_destroy (pgm_transport_t*, gboolean);

int pgm_transport_set_max_tpdu (pgm_transport_t*, guint16);
int pgm_transport_set_hops (pgm_transport_t*, gint);
int pgm_transport_set_ambient_spm (pgm_transport_t*, guint);
int pgm_transport_set_heartbeat_spm (pgm_transport_t*, guint*, int);

int pgm_transport_set_peer_expiry (pgm_transport_t*, guint);
int pgm_transport_set_spmr_expiry (pgm_transport_t*, guint);

int pgm_transport_set_txw_preallocate (pgm_transport_t*, guint);
int pgm_transport_set_txw_sqns (pgm_transport_t*, guint);
int pgm_transport_set_txw_secs (pgm_transport_t*, guint);
int pgm_transport_set_txw_max_rte (pgm_transport_t*, guint);

int pgm_transport_set_rxw_preallocate (pgm_transport_t*, guint);
int pgm_transport_set_rxw_sqns (pgm_transport_t*, guint);
int pgm_transport_set_rxw_secs (pgm_transport_t*, guint);
int pgm_transport_set_rxw_max_rte (pgm_transport_t*, guint);

int pgm_transport_set_sndbuf (pgm_transport_t*, int);
int pgm_transport_set_rcvbuf (pgm_transport_t*, int);
int pgm_transport_set_event_preallocate (pgm_transport_t*, guint);

int pgm_transport_set_nak_rb_ivl (pgm_transport_t*, guint);
int pgm_transport_set_nak_rpt_ivl (pgm_transport_t*, guint);
int pgm_transport_set_nak_rdata_ivl (pgm_transport_t*, guint);
int pgm_transport_set_nak_data_retries (pgm_transport_t*, guint);
int pgm_transport_set_nak_ncf_retries (pgm_transport_t*, guint);

gpointer pgm_alloc (pgm_transport_t*);
int pgm_write_unlocked (pgm_transport_t*, const gchar*, gsize);
static inline int pgm_write (pgm_transport_t* transport, const gchar* buf, gsize count)
{
    g_static_rw_lock_writer_lock (&transport->txw_lock);
    int retval = pgm_write_unlocked (transport, buf, count);

/* unlocked in pgm_write_unlocked()
 *   g_static_rw_lock_writer_unlock (&transport->txw_lock);
 */

    return retval;
}

int pgm_write_copy (pgm_transport_t*, const gchar*, gsize);
int pgm_write_copy_fragment_unlocked (pgm_transport_t*, const gchar*, gsize);
static inline int pgm_write_copy_fragment (pgm_transport_t* transport, const gchar* buf, gsize count)
{
    g_static_rw_lock_writer_lock (&transport->txw_lock);
    int retval = pgm_write_copy_fragment_unlocked (transport, buf, count);

/* unlocked in pgm_write_unlocked()
 *   g_static_rw_lock_writer_unlock (&transport->txw_lock);
 */

    return retval;
}

int pgm_write_copy_ex (pgm_transport_t*, const gchar*, gsize);

/* TODO: contexts, hooks */

int pgm_transport_set_fec (pgm_transport_t*, gboolean, gboolean, guint, guint);

G_END_DECLS

#endif /* __PGM_TRANSPORT_H__ */
