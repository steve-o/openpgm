/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic transmit window.
 *
 * Copyright (c) 2006-2010 Miru Limited.
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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_TXW_H__
#define __PGM_IMPL_TXW_H__

typedef struct pgm_txw_state_t pgm_txw_state_t;
typedef struct pgm_txw_t pgm_txw_t;

#include <impl/framework.h>

PGM_BEGIN_DECLS

/* must be smaller than PGM skbuff control buffer */
struct pgm_txw_state_t {
	uint32_t	unfolded_checksum;	/* first 32-bit word must be checksum */

	unsigned	waiting_retransmit:1;	/* in retransmit queue */
	unsigned	retransmit_count:15;
	unsigned	nak_elimination_count:16;

	uint8_t		pkt_cnt_requested;	/* # parity packets to send */
	uint8_t		pkt_cnt_sent;		/* # parity packets already sent */
};

struct pgm_txw_t {
	const pgm_tsi_t* restrict	tsi;

/* option: lockless atomics */
        volatile uint32_t		lead;
        volatile uint32_t		trail;

        pgm_queue_t			retransmit_queue;

	pgm_rs_t			rs;
	uint8_t				tg_sqn_shift;
	struct pgm_sk_buff_t* restrict	parity_buffer;

/* Advance with data */
	pgm_time_t			adv_ivl_expiry;	
	unsigned			increment_window_naks;
	unsigned			adv_secs;		/* TXW_ADV_SECS */
	unsigned			adv_sqns;		/* TXW_ADV_SECS in sequences */

	unsigned			is_fec_enabled:1;
	unsigned			adv_mode:1;		/* 0 = advance by time, 1 = advance by data */

	size_t				size;			/* window content size in bytes */
	unsigned			alloc;			/* length of pdata[] */
/* C90 and older */
	struct pgm_sk_buff_t*		pdata[1];
};

PGM_GNUC_INTERNAL pgm_txw_t* pgm_txw_create (const pgm_tsi_t*const, const uint16_t, const uint32_t, const unsigned, const ssize_t, const bool, const uint8_t, const uint8_t) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_txw_shutdown (pgm_txw_t*const);
PGM_GNUC_INTERNAL void pgm_txw_add (pgm_txw_t*const restrict, struct pgm_sk_buff_t*const restrict);
PGM_GNUC_INTERNAL struct pgm_sk_buff_t* pgm_txw_peek (const pgm_txw_t*const, const uint32_t) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_txw_retransmit_push (pgm_txw_t*const, const uint32_t, const bool, const uint8_t) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL struct pgm_sk_buff_t* pgm_txw_retransmit_try_peek (pgm_txw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_txw_retransmit_remove_head (pgm_txw_t*const);
PGM_GNUC_INTERNAL uint32_t pgm_txw_get_unfolded_checksum (const struct pgm_sk_buff_t*const) PGM_GNUC_PURE;
PGM_GNUC_INTERNAL void pgm_txw_set_unfolded_checksum (struct pgm_sk_buff_t*const, const uint32_t);
PGM_GNUC_INTERNAL void pgm_txw_inc_retransmit_count (struct pgm_sk_buff_t*const);
PGM_GNUC_INTERNAL bool pgm_txw_retransmit_is_empty (const pgm_txw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;

/* declare for GCC attributes */
static inline size_t pgm_txw_max_length (const pgm_txw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint32_t pgm_txw_length (const pgm_txw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline size_t pgm_txw_size (const pgm_txw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline bool pgm_txw_is_empty (const pgm_txw_t* const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline bool pgm_txw_is_full (const pgm_txw_t* const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint32_t pgm_txw_lead (const pgm_txw_t* const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint32_t pgm_txw_lead_atomic (const pgm_txw_t* const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint32_t pgm_txw_next_lead (const pgm_txw_t* const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint32_t pgm_txw_trail (const pgm_txw_t* const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint32_t pgm_txw_trail_atomic (const pgm_txw_t* const) PGM_GNUC_WARN_UNUSED_RESULT;

static inline
size_t
pgm_txw_max_length (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return window->alloc;
}

static inline
uint32_t
pgm_txw_length (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return ( 1 + window->lead ) - window->trail;
}

static inline
size_t
pgm_txw_size (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return window->size;
}

static inline
bool
pgm_txw_is_empty (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return (0 == pgm_txw_length (window));
}

static inline
bool
pgm_txw_is_full (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return (pgm_txw_length (window) == pgm_txw_max_length (window));
}

static inline
uint32_t
pgm_txw_lead (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return window->lead;
}

/* atomics may rely on global variables and so cannot be defined __pure__ */
static inline
uint32_t
pgm_txw_lead_atomic (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return pgm_atomic_read32 (&window->lead);
}

static inline
uint32_t
pgm_txw_next_lead (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return (uint32_t)(pgm_txw_lead (window) + 1);
}

static inline
uint32_t
pgm_txw_trail (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return window->trail;
}

static inline
uint32_t
pgm_txw_trail_atomic (
	const pgm_txw_t*const window
	)
{
	pgm_assert (NULL != window);
	return pgm_atomic_read32 (&window->trail);
}

PGM_END_DECLS

#endif /* __PGM_IMPL_TXW_H__ */
