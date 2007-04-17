/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic receive window.
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



/* callback for commiting contigious pgm packets */
typedef int (*rxw_callback)(gpointer, guint, gpointer);

/* callback for processing naks */
typedef int (*rxw_state_callback)(gpointer, guint, guint32, pgm_pkt_state*, gdouble, guint, gpointer);



gpointer rxw_init (guint, guint32, guint32, guint, guint, rxw_callback, gpointer);
int rxw_shutdown (gpointer);

gpointer rxw_alloc (gpointer);
int rxw_push (gpointer, gpointer, guint, guint32, guint32);

/* for NAK re/generation */
int rxw_state_foreach (gpointer, pgm_pkt_state, rxw_state_callback, gpointer);

/* from SPM */
int rxw_window_update (gpointer, guint32, guint32);

/* from NCF */
int rxw_ncf (gpointer, guint32);


G_END_DECLS

#endif /* __PGM_RXW_H__ */
