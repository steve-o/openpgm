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

G_BEGIN_DECLS

struct txw_packet {
        gpointer        data;

        guint           length;
        guint32         sequence_number;
        struct timeval  expiry;
        struct timeval  last_retransmit;
};

struct txw {
        GPtrArray*      pdata;
        GTrashStack*    trash_packet;           /* sizeof(txw_packet) */
        GTrashStack*    trash_data;             /* max_tpdu */

        guint           max_tpdu;               /* maximum packet size */

        guint32         lead;
        guint32         trail;

};


struct txw* txw_init (guint, guint32, guint32, guint, guint);
int txw_shutdown (struct txw*);

int txw_push (struct txw*, gpointer, guint);
int txw_peek (struct txw*, guint32, gpointer*, guint*);

static inline guint txw_len (struct txw* t)
{
    return t->pdata->len;
}

static inline guint32 txw_sqns (struct txw* t)
{
    return ( 1 + t->lead ) - t->trail;
}

static inline gboolean txw_empty (struct txw* t)
{
    return txw_sqns (t) == 0;
}

static inline gboolean txw_full (struct txw* t)
{
    return txw_len (t) == txw_sqns (t);
}

static inline gpointer txw_alloc (struct txw* t)
{
    return t->trash_data ? g_trash_stack_pop (&t->trash_data) : g_slice_alloc (t->max_tpdu);
}

static inline guint32 txw_next_lead (struct txw* t)
{
    return (guint32)(t->lead + 1);
}

static inline guint32 txw_lead (struct txw* t)
{
    return t->lead;
}

static inline guint32 txw_trail (struct txw* t)
{
    return t->trail;
}

static inline int txw_push_copy (struct txw* t, gpointer packet_, guint len)
{
    gpointer packet = txw_alloc (t);
    memcpy (packet, packet_, len);
    return txw_push (t, packet, len);
}


G_END_DECLS

#endif /* __PGM_TXW_H__ */
