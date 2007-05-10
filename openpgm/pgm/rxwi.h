/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic receive window.
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

#ifndef __PGM_RXW_H__
#define __PGM_RXW_H__

G_BEGIN_DECLS


typedef enum
{
    PGM_PKT_BACK_OFF_STATE,
    PGM_PKT_WAIT_NCF_STATE,
    PGM_PKT_WAIT_DATA_STATE,

    PGM_PKT_HAVE_DATA_STATE,

    PGM_PKT_LOST_DATA_STATE,	    /* if recovery fails, but packet has not yet been commited */

    PGM_PKT_ERROR_STATE
} pgm_pkt_state;

/* callback for commiting contiguous pgm packets */
typedef int (*rxw_callback)(gpointer, guint, gpointer);

struct rxw_packet {
        gpointer        data;

        guint           length;
        guint32         sequence_number;

	guint64		nak_rb_expiry;
	guint64		nak_rpt_expiry;
	guint64		nak_rdata_expiry;
        GList           link_;
        pgm_pkt_state   state;
        guint           ncf_retry_count;
        guint           data_retry_count;
};

struct rxw {
        GPtrArray*      pdata;
        GTrashStack*    trash_packet;           /* sizeof(rxw_packet) */
        GTrashStack*    trash_data;             /* max_tpdu */

        GQueue*         backoff_queue;
        GQueue*         wait_ncf_queue;
        GQueue*         wait_data_queue;
	guint		lost_count;

        guint           max_tpdu;               /* maximum packet size */

        guint32         lead, trail;
        guint32         rxw_trail, rxw_trail_init;
        gboolean        rxw_constrained;
        gboolean        window_defined;

        rxw_callback    on_data;
        gpointer        param;
};


struct rxw* rxw_init (guint, guint32, guint32, guint, guint, rxw_callback, gpointer);
int rxw_shutdown (struct rxw*);

int rxw_push (struct rxw*, gpointer, guint, guint32, guint32);

/* from state checking */
int rxw_mark_lost (struct rxw*, guint32);

/* from SPM */
int rxw_window_update (struct rxw*, guint32, guint32);

/* from NCF */
int rxw_ncf (struct rxw*, guint32, guint64);


static inline guint rxw_len (struct rxw* r)
{
    return r->pdata->len;
}

static inline guint32 rxw_sqns (struct rxw* r)
{
    return ( 1 + r->lead ) - r->trail;
}

static inline gboolean rxw_empty (struct rxw* r)
{
    return rxw_sqns (r) == 0;
}

static inline gboolean rxw_full (struct rxw* r)
{
    return rxw_len (r) == rxw_sqns (r);
}

static inline gpointer rxw_alloc (struct rxw* r)
{
    return r->trash_data ? g_trash_stack_pop (&r->trash_data) : g_slice_alloc (r->max_tpdu);
}

static inline int rxw_push_copy (struct rxw* r, gpointer packet_, guint len, guint32 sn, guint32 trail)
{
    gpointer packet = rxw_alloc (r);
    memcpy (packet, packet_, len);
    return rxw_push (r, packet, len, sn, trail);
}

int rxw_pkt_state_unlink (struct rxw*, struct rxw_packet*);

G_END_DECLS

#endif /* __PGM_RXW_H__ */
