/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic transmit window.
 *
 * Copyright (c) 2006 Miru Limited.
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

#ifndef __PGM_TXW_H__
#define __PGM_TXW_H__

#include <glib.h>

#ifndef __PGM_SKBUFF_H__
#	include <pgm/skbuff.h>
#endif


G_BEGIN_DECLS

struct pgm_txw_packet_t {
	guint32		unfolded_checksum;	/* first 32-bit word must be checksum */
#if 0
        struct timeval  expiry;			/* Advance with time */
        struct timeval  last_retransmit;	/* NAK elimination */
#endif
	guint8		pkt_cnt_requested;	/* # parity packets to send */
	guint8		pkt_cnt_sent;		/* # parity packets already sent */
};

typedef struct pgm_txw_packet_t pgm_txw_packet_t;

struct pgm_txw_t {
	struct {				/* GPtrArray */
		gpointer*	pdata;
		guint		len;
		guint		alloc;
	} pdata;

        guint16         max_tpdu;               /* maximum packet size */

        guint32         lead;
        guint32         trail;

        GQueue		retransmit_queue;
	GStaticMutex	retransmit_mutex;

	guint32		bytes_in_window;
	guint32		packets_in_window;
};

typedef struct pgm_txw_t pgm_txw_t;


pgm_txw_t* pgm_txw_init (guint16, guint32, guint, guint);
int pgm_txw_shutdown (pgm_txw_t*);

int pgm_txw_push (pgm_txw_t*, struct pgm_sk_buff_t*);
int pgm_txw_peek (pgm_txw_t*, guint32, struct pgm_sk_buff_t**);

/* return type as defined in garray.h */
static inline guint pgm_txw_len (pgm_txw_t* t)
{
    return t->pdata.len;
}

static inline guint32 pgm_txw_sqns (pgm_txw_t* t)
{
    return ( 1 + t->lead ) - t->trail;
}

static inline gboolean pgm_txw_empty (pgm_txw_t* t)
{
    return pgm_txw_sqns (t) == 0;
}

static inline gboolean pgm_txw_full (pgm_txw_t* t)
{
    return pgm_txw_len (t) == pgm_txw_sqns (t);
}

static inline guint32 pgm_txw_next_lead (pgm_txw_t* t)
{
    return (guint32)(t->lead + 1);
}

static inline guint32 pgm_txw_lead (pgm_txw_t* t)
{
    return t->lead;
}

static inline guint32 pgm_txw_trail (pgm_txw_t* t)
{
    return t->trail;
}

int pgm_txw_retransmit_push (pgm_txw_t*, guint32, gboolean, guint);
int pgm_txw_retransmit_try_peek (pgm_txw_t*, struct pgm_sk_buff_t**, guint32*, gboolean*, guint*);
int pgm_txw_retransmit_try_pop (pgm_txw_t*, guint32*, gpointer*, guint16*, gboolean*, guint*, guint);
int pgm_txw_retransmit_pop (pgm_txw_t*, guint);

G_END_DECLS

#endif /* __PGM_TXW_H__ */
