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

typedef struct pgm_txw_state_t pgm_txw_state_t;
typedef struct pgm_txw_t pgm_txw_t;

#ifndef __PGM_QUEUE_H__
#	include <pgm/queue.h>
#endif

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

/* must be smaller than PGM skbuff control buffer */
struct pgm_txw_state_t {
	uint32_t	unfolded_checksum;	/* first 32-bit word must be checksum */

	unsigned	waiting_retransmit:1;	/* in retransmit queue */
	unsigned	retransmit_count:15;
	unsigned	nak_elimination_count:16;

#if 0
        struct timeval  expiry;			/* Advance with time */
        struct timeval  last_retransmit;	/* NAK elimination */
#endif
	uint8_t		pkt_cnt_requested;	/* # parity packets to send */
	uint8_t		pkt_cnt_sent;		/* # parity packets already sent */
};

struct pgm_txw_t {
	const pgm_tsi_t*	tsi;

/* option: lockless atomics */
        volatile uint32_t	lead;
        volatile uint32_t	trail;

        pgm_queue_t		retransmit_queue;

	pgm_rs_t		rs;
	uint8_t			tg_sqn_shift;
	struct pgm_sk_buff_t*	parity_buffer;
	unsigned		is_fec_enabled:1;

	size_t			size;			/* window content size in bytes */
	unsigned		alloc;			/* length of pdata[] */
	struct pgm_sk_buff_t*	pdata[];
};

PGM_GNUC_INTERNAL pgm_txw_t* pgm_txw_create (const pgm_tsi_t* const, const uint16_t, const uint32_t, const unsigned, const ssize_t, const bool, const uint8_t, const uint8_t) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_txw_shutdown (pgm_txw_t* const);
PGM_GNUC_INTERNAL void pgm_txw_add (pgm_txw_t* const, struct pgm_sk_buff_t* const);
PGM_GNUC_INTERNAL struct pgm_sk_buff_t* pgm_txw_peek (pgm_txw_t* const, const uint32_t) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_txw_retransmit_push (pgm_txw_t* const, const uint32_t, const bool, const uint8_t) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL struct pgm_sk_buff_t* pgm_txw_retransmit_try_peek (pgm_txw_t* const) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_txw_retransmit_remove_head (pgm_txw_t* const);
PGM_GNUC_INTERNAL uint32_t pgm_txw_get_unfolded_checksum (struct pgm_sk_buff_t*);
PGM_GNUC_INTERNAL void pgm_txw_set_unfolded_checksum (struct pgm_sk_buff_t*, const uint32_t);
PGM_GNUC_INTERNAL void pgm_txw_inc_retransmit_count (struct pgm_sk_buff_t*);
PGM_GNUC_INTERNAL bool pgm_txw_retransmit_is_empty (pgm_txw_t* const);

static inline size_t pgm_txw_max_length (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return window->alloc;
}

static inline uint32_t pgm_txw_length (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return ( 1 + window->lead ) - window->trail;
}

static inline size_t pgm_txw_size (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return window->size;
}

static inline bool pgm_txw_is_empty (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return (0 == pgm_txw_length (window));
}

static inline bool pgm_txw_is_full (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return (pgm_txw_length (window) == pgm_txw_max_length (window));
}

static inline uint32_t pgm_txw_lead (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return window->lead;
}

static inline uint32_t pgm_txw_lead_atomic (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return pgm_atomic_int32_get ((const volatile int32_t*)&window->lead);
}

static inline uint32_t pgm_txw_next_lead (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return (uint32_t)(pgm_txw_lead (window) + 1);
}

static inline uint32_t pgm_txw_trail (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return window->trail;
}

static inline uint32_t pgm_txw_trail_atomic (const pgm_txw_t* const window)
{
	pgm_assert (window);
	return pgm_atomic_int32_get ((const volatile gint32*)&window->trail);
}


G_END_DECLS

#endif /* __PGM_TXW_H__ */
