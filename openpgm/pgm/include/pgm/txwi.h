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

#ifndef __PGM_REED_SOLOMON_H__
#	include <pgm/reed_solomon.h>
#endif

#ifndef __PGM_ATOMIC_H__
#	include <pgm/atomic.h>
#endif


G_BEGIN_DECLS

struct pgm_txw_state_t {
	guint32		unfolded_checksum;	/* first 32-bit word must be checksum */

	unsigned	waiting_retransmit:1;	/* in retransmit queue */
	unsigned	retransmit_count:15;
	unsigned	nak_elimination_count:16;

#if 0
        struct timeval  expiry;			/* Advance with time */
        struct timeval  last_retransmit;	/* NAK elimination */
#endif
	guint8		pkt_cnt_requested;	/* # parity packets to send */
	guint8		pkt_cnt_sent;		/* # parity packets already sent */
};

typedef struct pgm_txw_state_t pgm_txw_state_t;

/* must be smaller than PGM skbuff control buffer */
#ifndef G_STATIC_ASSERT
#	define G_PASTE_ARGS(identifier1,identifier2) identifier1 ## identifier2
#	define G_PASTE(identifier1,identifier2) G_PASTE_ARGS (identifier1, identifier2)
#	define G_STATIC_ASSERT(expr) typedef struct { char Compile_Time_Assertion[(expr) ? 1 : -1]; } G_PASTE (_GStaticAssert_, __LINE__)
#endif

G_STATIC_ASSERT(sizeof(struct pgm_txw_state_t) <= sizeof(((struct pgm_sk_buff_t*)0)->cb));

struct pgm_txw_t {
	const pgm_tsi_t*	tsi;

/* option: lockless atomics */
        guint32			lead;
        guint32			trail;

        GQueue			retransmit_queue;

	rs_t			rs;
	guint			tg_sqn_shift;
	struct pgm_sk_buff_t*	parity_buffer;
	unsigned		is_fec_enabled:1;

	guint32			size;			/* window content size in bytes */
	guint32			alloc;			/* length of pdata[] */
	struct pgm_sk_buff_t*	pdata[];
};

typedef struct pgm_txw_t pgm_txw_t;


PGM_GNUC_INTERNAL pgm_txw_t* pgm_txw_create (const pgm_tsi_t* const, const guint16, const guint32, const guint, const guint, const gboolean, const guint, const guint) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_txw_shutdown (pgm_txw_t* const);
PGM_GNUC_INTERNAL void pgm_txw_add (pgm_txw_t* const, struct pgm_sk_buff_t* const);
PGM_GNUC_INTERNAL struct pgm_sk_buff_t* pgm_txw_peek (pgm_txw_t* const, const guint32) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL gboolean pgm_txw_retransmit_push (pgm_txw_t* const, const guint32, const gboolean, const guint) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL struct pgm_sk_buff_t* pgm_txw_retransmit_try_peek (pgm_txw_t* const) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_txw_retransmit_remove_head (pgm_txw_t* const);

static inline guint32 pgm_txw_max_length (const pgm_txw_t* const window)
{
	g_assert (window);
	return window->alloc;
}

static inline guint32 pgm_txw_length (const pgm_txw_t* const window)
{
	g_assert (window);
	return ( 1 + window->lead ) - window->trail;
}

static inline guint32 pgm_txw_size (const pgm_txw_t* const window)
{
	g_assert (window);
	return window->size;
}

static inline gboolean pgm_txw_is_empty (const pgm_txw_t* const window)
{
	g_assert (window);
	return pgm_txw_length (window) == 0;
}

static inline gboolean pgm_txw_is_full (const pgm_txw_t* const window)
{
	g_assert (window);
	return pgm_txw_length (window) == pgm_txw_max_length (window);
}

static inline guint32 pgm_txw_lead (const pgm_txw_t* const window)
{
	g_assert (window);
	return window->lead;
}

static inline guint32 pgm_txw_lead_atomic (const pgm_txw_t* const window)
{
	g_assert (window);
	return pgm_atomic_int32_get (&window->lead);
}

static inline guint32 pgm_txw_next_lead (const pgm_txw_t* const window)
{
	g_assert (window);
	return (guint32)(pgm_txw_lead (window) + 1);
}

static inline guint32 pgm_txw_trail (const pgm_txw_t* const window)
{
	g_assert (window);
	return window->trail;
}

static inline guint32 pgm_txw_trail_atomic (const pgm_txw_t* const window)
{
	g_assert (window);
	return pgm_atomic_int32_get (&window->trail);
}

static inline guint32 pgm_txw_get_unfolded_checksum (struct pgm_sk_buff_t* skb)
{
	pgm_txw_state_t* state = (pgm_txw_state_t*)&skb->cb;
	return state->unfolded_checksum;
}

static inline void pgm_txw_set_unfolded_checksum (struct pgm_sk_buff_t* skb, const guint32 csum)
{
	pgm_txw_state_t* state = (pgm_txw_state_t*)&skb->cb;
	state->unfolded_checksum = csum;
}

static inline void pgm_txw_inc_retransmit_count (struct pgm_sk_buff_t* skb)
{
	pgm_txw_state_t* state = (pgm_txw_state_t*)&skb->cb;
	state->retransmit_count++;
}

static inline gboolean pgm_txw_retransmit_is_empty (pgm_txw_t* const window)
{
	g_assert (window);
	return g_queue_is_empty (&window->retransmit_queue);
}

G_END_DECLS

#endif /* __PGM_TXW_H__ */
