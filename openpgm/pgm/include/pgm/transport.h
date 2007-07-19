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

#ifndef __PGM_SOCKADDR_H__
#   include "sockaddr.h"
#endif

#ifndef __PGM_RXW_H__
#   include "rxwi.h"
#endif

#ifndef __PGM_TXW_H__
#   include "txwi.h"
#endif

#ifndef __PGM_PACKET__H
#   include "packet.h"
#endif

struct pgm_transport;

struct tsi {            /* transport session identifier */
    guint8      gsi[6];	    /* global session ID */
    guint16     sport;	    /* source port: a random number to help detect session re-starts */
};

struct pgm_peer {
    gint		ref_count;

    struct tsi		tsi;
    struct sockaddr_storage	nla, local_nla;	    /* nla = advertised, local_nla = from packet */
    struct sockaddr_storage	redirect_nla;	    /* from dlr */
    pgm_time_t		spmr_expiry;

    GStaticMutex	mutex;

    struct rxw*	    	rxw;
    struct pgm_transport*   transport;

    int			spm_sqn;
    pgm_time_t		expiry;

#define next_nak_rb_expiry(p)	    ( ((struct rxw_packet*)((struct pgm_peer*)(p))->rxw->backoff_queue->tail)->nak_rb_expiry )
#define next_nak_rpt_expiry(p)	    ( ((struct rxw_packet*)((struct pgm_peer*)(p))->rxw->wait_ncf_queue->tail)->nak_rpt_expiry )
#define next_nak_rdata_expiry(p)    ( ((struct rxw_packet*)((struct pgm_peer*)(p))->rxw->wait_data_queue->tail)->nak_rdata_expiry )
};

struct pgm_event {
    gpointer		data;
    guint		len;
    struct pgm_peer*	peer;
};

struct pgm_transport {
    struct tsi		tsi;
    guint16		dport;

    GStaticMutex	mutex;
    GThread		*rx_thread, *timer_thread;
    GMainLoop		*rx_loop, *timer_loop;
    GMainContext	*rx_context, *timer_context;
    gboolean		bound;

    struct sock_mreq	send_smr;			/* multicast & unicast nla */
    GStaticMutex	send_mutex;
    int			send_sock;
    GStaticMutex	send_with_router_alert_mutex;
    int			send_with_router_alert_sock;
    struct sock_mreq	recv_smr[IP_MAX_MEMBERSHIPS];	/* sa_family = 0 terminated */
    int			recv_sock;
    GIOChannel*		recv_channel;

    guint16		max_tpdu;
    gint		hops;
    guint		txw_preallocate, txw_sqns, txw_secs, txw_max_rte;
    guint		rxw_preallocate, rxw_sqns, rxw_secs, rxw_max_rte;
    int			sndbuf, rcvbuf;

    GStaticRWLock	txw_lock;
    struct txw*		txw;
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
    pgm_time_t		next_spm_expiry;
    pgm_time_t		next_spmr_expiry;

    gboolean		proactive_parity;
    gboolean		ondemand_parity;
    guint		default_tgsize;		    /* k */
    guint		default_h;		    /* 2t */

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
};

typedef int (*pgm_func)(gpointer, guint, gpointer);


G_BEGIN_DECLS

int pgm_init (void);

gchar* pgm_print_tsi (const struct tsi*);

int pgm_transport_create (struct pgm_transport**, guint8*, guint16, struct sock_mreq*, int, struct sock_mreq*);
int pgm_transport_bind (struct pgm_transport*);
GSource* pgm_transport_create_watch (struct pgm_transport*);
int pgm_transport_add_watch_full (struct pgm_transport*, gint, pgm_func, gpointer, GDestroyNotify);
int pgm_transport_add_watch (struct pgm_transport*, pgm_func, gpointer);
int pgm_transport_destroy (struct pgm_transport*, gboolean);

int pgm_transport_set_max_tpdu (struct pgm_transport*, guint16);
int pgm_transport_set_hops (struct pgm_transport*, gint);
int pgm_transport_set_ambient_spm (struct pgm_transport*, guint);
int pgm_transport_set_heartbeat_spm (struct pgm_transport*, guint*, int);

int pgm_transport_set_peer_expiry (struct pgm_transport*, guint);
int pgm_transport_set_spmr_expiry (struct pgm_transport*, guint);

int pgm_transport_set_txw_preallocate (struct pgm_transport*, guint);
int pgm_transport_set_txw_sqns (struct pgm_transport*, guint);
int pgm_transport_set_txw_secs (struct pgm_transport*, guint);
int pgm_transport_set_txw_max_rte (struct pgm_transport*, guint);

int pgm_transport_set_rxw_preallocate (struct pgm_transport*, guint);
int pgm_transport_set_rxw_sqns (struct pgm_transport*, guint);
int pgm_transport_set_rxw_secs (struct pgm_transport*, guint);
int pgm_transport_set_rxw_max_rte (struct pgm_transport*, guint);

int pgm_transport_set_sndbuf (struct pgm_transport*, int);
int pgm_transport_set_rcvbuf (struct pgm_transport*, int);
int pgm_transport_set_event_preallocate (struct pgm_transport*, guint);

int pgm_transport_set_nak_rb_ivl (struct pgm_transport*, guint);
int pgm_transport_set_nak_rpt_ivl (struct pgm_transport*, guint);
int pgm_transport_set_nak_rdata_ivl (struct pgm_transport*, guint);
int pgm_transport_set_nak_data_retries (struct pgm_transport*, guint);
int pgm_transport_set_nak_ncf_retries (struct pgm_transport*, guint);

static inline gpointer pgm_alloc (struct pgm_transport*  transport)
{
    g_static_rw_lock_writer_lock (&transport->txw_lock);
    gpointer ptr = ((gchar*)txw_alloc (transport->txw)) + sizeof(struct pgm_header) + sizeof(struct pgm_data);
    g_static_rw_lock_writer_unlock (&transport->txw_lock);
    return ptr;
}

int pgm_write_unlocked (struct pgm_transport*, const gchar*, gsize);
static inline int pgm_write (struct pgm_transport* transport, const gchar* buf, gsize count)
{
    g_static_rw_lock_writer_lock (&transport->txw_lock);
    int retval = pgm_write_unlocked (transport, buf, count);
//    g_static_rw_lock_writer_unlock (&transport->txw_lock);
    return retval;
}

static inline int pgm_write_copy (struct pgm_transport* transport, const gchar* buf, gsize count)
{
    g_static_rw_lock_writer_lock (&transport->txw_lock);
    gchar *pkt = ((gchar*)txw_alloc (transport->txw)) + sizeof(struct pgm_header) + sizeof(struct pgm_data);
    memcpy (pkt, buf, count);
    int retval = pgm_write_unlocked (transport, pkt, count);
//    g_static_rw_lock_writer_unlock (&transport->txw_lock);
    return retval;
}

int pgm_write_copy_fragment_unlocked (struct pgm_transport*, const gchar*, gsize);
static inline int pgm_write_copy_fragment (struct pgm_transport* transport, const gchar* buf, gsize count)
{
    g_static_rw_lock_writer_lock (&transport->txw_lock);
    int retval = pgm_write_copy_fragment_unlocked (transport, buf, count);
//    g_static_rw_lock_writer_unlock (&transport->txw_lock);
    return retval;
}

static inline int pgm_write_copy_ex (struct pgm_transport* transport, const gchar* buf, gsize count)
{
    if ( count <= ( transport->max_tpdu - (  sizeof(struct pgm_header) +
					    sizeof(struct pgm_data) ) ) )
    {
	return pgm_write_copy (transport, buf, count);
    }

    return pgm_write_copy_fragment (transport, buf, count);
}

/* TODO: contexts, hooks */

int pgm_transport_set_fec (struct pgm_transport*, gboolean, gboolean, guint, guint);

G_END_DECLS

#endif /* __PGM_TRANSPORT_H__ */
