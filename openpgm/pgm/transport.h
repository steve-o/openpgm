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
#   include "pgm.h"
#endif

struct pgm_transport;

struct tsi {            /* transport session identifier */
    guint8      gsi[6];	    /* global session ID */
    guint16     sport;	    /* source port: a random number to help detect session re-starts */
};

struct pgm_peer {
    struct tsi		tsi;
    struct sockaddr_storage	nla;

    GMutex*		mutex;

    struct rxw*	    	rxw;
    int			spm_sqn;
    struct pgm_transport*   transport;
};

struct pgm_transport {
    struct tsi		tsi;

    GMutex*		mutex;
    gboolean		bound;

    struct sock_mreq	send_smr;			/* multicast & unicast nla */
    int			send_sock;
    int			send_with_router_alert_sock;
    struct sock_mreq	recv_smr[IP_MAX_MEMBERSHIPS];	/* sa_family = 0 terminated */
    int			recv_sock;
    GIOChannel*		recv_channel;

    guint16		max_tpdu;
    gint		hops;
    guint		spm_ambient_interval;	    /* microseconds */
    guint*		spm_heartbeat_interval;	    /* zero terminated */
    guint		txw_preallocate, txw_sqns, txw_secs, txw_max_rte;
    guint		rxw_preallocate, rxw_sqns, rxw_secs, rxw_max_rte;
    int			sndbuf, rcvbuf;

    struct txw*		txw;
    int			spm_sqn;
    gchar*		spm_packet;
    int			spm_len;

    GHashTable*		peers;
};


G_BEGIN_DECLS

int pgm_init (void);

gchar* pgm_print_tsi (struct tsi*);

int pgm_transport_create (struct pgm_transport**, guint8*, struct sock_mreq*, int, struct sock_mreq*);
int pgm_transport_bind (struct pgm_transport*);
int pgm_transport_create_watch (struct pgm_transport*, GSource*);
int pgm_transport_add_watch (struct pgm_transport*);
int pgm_transport_destroy (struct pgm_transport*, gboolean);

int pgm_transport_set_max_tpdu (struct pgm_transport*, guint16);
int pgm_transport_set_hops (struct pgm_transport*, gint);
int pgm_transport_set_ambient_spm (struct pgm_transport*, guint);
int pgm_transport_set_heartbeat_spm (struct pgm_transport*, guint*, int);

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

static inline gpointer pgm_alloc (struct pgm_transport*  transport)
{
    return ((gchar*)txw_alloc (transport->txw)) + sizeof(struct pgm_header) + sizeof(struct pgm_data);
}

int pgm_write (struct pgm_transport*, const gchar*, gsize);

static inline int pgm_write_copy (struct pgm_transport* transport, const gchar* buf, gsize count)
{
    gchar *pkt = pgm_alloc (transport);
    memcpy (pkt, buf, count);
    return pgm_write (transport, pkt, count);
}

/* TODO: contexts, hooks */

G_END_DECLS

#endif /* __PGM_TRANSPORT_H__ */
