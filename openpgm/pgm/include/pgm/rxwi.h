/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic receive window.
 *
 * Copyright (c) 2006-2008 Miru Limited.
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

#include <glib.h>

#ifndef __PGM_TIMER_H__
#   include "pgm/timer.h"
#endif

#ifndef __PGM_MSGV_H__
#   include "pgm/msgv.h"
#endif


G_BEGIN_DECLS

typedef enum
{
    PGM_PKT_BACK_OFF_STATE,
    PGM_PKT_WAIT_NCF_STATE,
    PGM_PKT_WAIT_DATA_STATE,

    PGM_PKT_HAVE_DATA_STATE,

    PGM_PKT_LOST_DATA_STATE,	    /* if recovery fails, but packet has not yet been commited */

    PGM_PKT_ERROR_STATE
} pgm_pkt_state_e;

typedef enum
{
    PGM_RXW_OK = 0,
    PGM_RXW_CREATED_PLACEHOLDER,
    PGM_RXW_FILLED_PLACEHOLDER,
    PGM_RXW_ADVANCED_WINDOW,
    PGM_RXW_NOT_IN_TXW,
    PGM_RXW_WINDOW_UNDEFINED,
    PGM_RXW_DUPLICATE,
    PGM_RXW_APDU_LOST,
    PGM_RXW_MALFORMED_APDU,
    PGM_RXW_UNKNOWN
} pgm_rxw_returns_e;

const char* pgm_rxw_state_string (pgm_pkt_state_e);

/* callback for commiting contiguous pgm packets */
typedef int (*pgm_rxw_commitfn_t)(guint32, gpointer, guint, gpointer);

struct pgm_rxw_packet_t {
	gpointer        data;
	guint           length;
	guint32         sequence_number;

	guint32		apdu_first_sqn;
/*	guint32		frag_offset;	*/
	guint32		apdu_len;

	pgm_time_t	t0;
	pgm_time_t	nak_rb_expiry;
	pgm_time_t	nak_rpt_expiry;
	pgm_time_t	nak_rdata_expiry;

        GList           link_;
        pgm_pkt_state_e state;

	guint		nak_transmit_count;
        guint           ncf_retry_count;
        guint           data_retry_count;
};

typedef struct pgm_rxw_packet_t pgm_rxw_packet_t;

struct pgm_rxw_t {
        GPtrArray*      pdata;
	GTrashStack**	trash_data;		/* owned by transport */
	GTrashStack**	trash_packet;
	GStaticMutex*	trash_mutex;

	GSList		waiting_link;
	gboolean	waiting;

        GQueue*         backoff_queue;
        GQueue*         wait_ncf_queue;
        GQueue*         wait_data_queue;
	guint		lost_count;
	guint		fragment_count;

        guint           max_tpdu;               /* maximum packet size */

        guint32         lead, trail;
        guint32         rxw_trail, rxw_trail_init;
        gboolean        rxw_constrained;
        gboolean        window_defined;

	gint		min_fill_time;
	gint		max_fill_time;
	gint		min_nak_transmit_count;
	gint		max_nak_transmit_count;
};

typedef struct pgm_rxw_t pgm_rxw_t;


pgm_rxw_t* pgm_rxw_init (guint, guint32, guint32, guint, guint, GTrashStack**, GTrashStack**, GStaticMutex*);
int pgm_rxw_shutdown (pgm_rxw_t*);

int pgm_rxw_push_fragment (pgm_rxw_t*, gpointer, guint, guint32, guint32, guint32, guint32, guint32, pgm_time_t);

int pgm_rxw_readv (pgm_rxw_t*, pgm_msgv_t**, int, struct iovec**, int);

/* from state checking */
int pgm_rxw_mark_lost (pgm_rxw_t*, guint32);

/* from SPM */
int pgm_rxw_window_update (pgm_rxw_t*, guint32, guint32, pgm_time_t);

/* from NCF */
int pgm_rxw_ncf (pgm_rxw_t*, guint32, pgm_time_t, pgm_time_t);


static inline guint pgm_rxw_len (pgm_rxw_t* r)
{
    return r->pdata->len;
}

static inline guint32 pgm_rxw_sqns (pgm_rxw_t* r)
{
    return ( 1 + r->lead ) - r->trail;
}

static inline gboolean pgm_rxw_empty (pgm_rxw_t* r)
{
    return pgm_rxw_sqns (r) == 0;
}

static inline gboolean pgm_rxw_full (pgm_rxw_t* r)
{
    return pgm_rxw_len (r) == pgm_rxw_sqns (r);
}

static inline gpointer pgm_rxw_alloc (pgm_rxw_t* r)
{
    gpointer p;
    g_static_mutex_lock (r->trash_mutex);
    if (g_trash_stack_height(r->trash_data)) {
	p = g_trash_stack_pop (r->trash_data);
    } else {
	p = g_slice_alloc (r->max_tpdu);
    }
    g_static_mutex_unlock (r->trash_mutex);
    return p;
}

static inline void pgm_rxw_data_unref (GTrashStack** trash, GStaticMutex* mutex, gpointer data)
{
    g_static_mutex_lock (mutex);
    g_trash_stack_push (trash, data);
    g_static_mutex_unlock (mutex);
}

static inline int pgm_rxw_push (pgm_rxw_t* r, gpointer packet, guint len, guint32 sqn, guint32 trail, pgm_time_t nak_rb_expiry)
{
    return pgm_rxw_push_fragment (r, packet, len, sqn, trail, 0, 0, 0, nak_rb_expiry);
}

static inline int pgm_rxw_push_fragment_copy (pgm_rxw_t* r, gpointer packet_, guint len, guint32 sqn, guint32 trail, guint32 apdu_first_sqn, guint32 fragment_offset, guint32 apdu_len, pgm_time_t nak_rb_expiry)
{
    gpointer packet = pgm_rxw_alloc (r);
    memcpy (packet, packet_, len);
    return pgm_rxw_push_fragment (r, packet, len, sqn, trail, apdu_first_sqn, fragment_offset, apdu_len, nak_rb_expiry);
}

static inline int pgm_rxw_push_copy (pgm_rxw_t* r, gpointer packet_, guint len, guint32 sqn, guint32 trail, pgm_time_t nak_rb_expiry)
{
    gpointer packet = pgm_rxw_alloc (r);
    memcpy (packet, packet_, len);
    return pgm_rxw_push (r, packet, len, sqn, trail, nak_rb_expiry);
}

int pgm_rxw_pkt_state_unlink (pgm_rxw_t*, pgm_rxw_packet_t*);

G_END_DECLS

#endif /* __PGM_RXW_H__ */
