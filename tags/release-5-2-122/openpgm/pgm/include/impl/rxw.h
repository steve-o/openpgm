/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic receive window.
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
#ifndef __PGM_IMPL_RXW_H__
#define __PGM_IMPL_RXW_H__

typedef struct pgm_rxw_state_t pgm_rxw_state_t;
typedef struct pgm_rxw_t pgm_rxw_t;

#include <impl/framework.h>

PGM_BEGIN_DECLS

enum
{
	PGM_PKT_STATE_ERROR = 0,
	PGM_PKT_STATE_BACK_OFF,	    /* PGM protocol recovery states */
	PGM_PKT_STATE_WAIT_NCF,
	PGM_PKT_STATE_WAIT_DATA,
	PGM_PKT_STATE_HAVE_DATA,	    /* data received waiting to commit to application layer */
	PGM_PKT_STATE_HAVE_PARITY,	    /* contains parity information not original data */
	PGM_PKT_STATE_COMMIT_DATA,	    /* commited data waiting for purging */
	PGM_PKT_STATE_LOST_DATA		    /* if recovery fails, but packet has not yet been commited */
};

enum
{
	PGM_RXW_OK = 0,
	PGM_RXW_INSERTED,
	PGM_RXW_APPENDED,
	PGM_RXW_UPDATED,
	PGM_RXW_MISSING,
	PGM_RXW_DUPLICATE,
	PGM_RXW_MALFORMED,
	PGM_RXW_BOUNDS,
	PGM_RXW_SLOW_CONSUMER,
	PGM_RXW_UNKNOWN
};

/* must be smaller than PGM skbuff control buffer */
struct pgm_rxw_state_t {
	pgm_time_t	timer_expiry;
        int		pkt_state;

	uint8_t		nak_transmit_count;	/* 8-bit for size constraints */
        uint8_t		ncf_retry_count;
        uint8_t		data_retry_count;

/* only valid on tg_sqn::pkt_sqn = 0 */
	unsigned	is_contiguous:1;	/* transmission group */
};

struct pgm_rxw_t {
	const pgm_tsi_t*	tsi;

        pgm_queue_t		ack_backoff_queue;
        pgm_queue_t		nak_backoff_queue;
        pgm_queue_t		wait_ncf_queue;
        pgm_queue_t		wait_data_queue;
/* window context counters */
	uint32_t		lost_count;		/* failed to repair */
	uint32_t		fragment_count;		/* incomplete apdu */
	uint32_t		parity_count;		/* parity for repairs */
	uint32_t		committed_count;	/* but still in window */

        uint16_t		max_tpdu;               /* maximum packet size */
        uint32_t		lead, trail;
        uint32_t		rxw_trail, rxw_trail_init;
	uint32_t		commit_lead;
        unsigned		is_constrained:1;
        unsigned		is_defined:1;
	unsigned		has_event:1;		/* edge triggered */
	unsigned		is_fec_available:1;
	pgm_rs_t		rs;
	uint32_t		tg_size;		/* transmission group size for parity recovery */
	uint8_t			tg_sqn_shift;

	uint32_t		bitmap;			/* receive status of last 32 packets */
	uint32_t		data_loss;		/* p */
	uint32_t		ack_c_p;		/* constant Cᵨ */

/* counters all guint32 */
	uint32_t		min_fill_time;		/* restricted from pgm_time_t */
	uint32_t		max_fill_time;
	uint32_t		min_nak_transmit_count;
	uint32_t		max_nak_transmit_count;
	uint32_t		cumulative_losses;
	uint32_t		bytes_delivered;
	uint32_t		msgs_delivered;

	size_t			size;			/* in bytes */
	unsigned		alloc;			/* in pkts */
/* C90 and older */
	struct pgm_sk_buff_t*   pdata[1];
};


PGM_GNUC_INTERNAL pgm_rxw_t* pgm_rxw_create (const pgm_tsi_t*const, const uint16_t, const unsigned, const unsigned, const ssize_t, const uint32_t) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_rxw_destroy (pgm_rxw_t*const);
PGM_GNUC_INTERNAL int pgm_rxw_add (pgm_rxw_t*const restrict, struct pgm_sk_buff_t*const restrict, const pgm_time_t, const pgm_time_t) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_rxw_add_ack (pgm_rxw_t*const restrict, struct pgm_sk_buff_t*const restrict, const pgm_time_t);
PGM_GNUC_INTERNAL void pgm_rxw_remove_ack (pgm_rxw_t*const restrict, struct pgm_sk_buff_t*const restrict);
PGM_GNUC_INTERNAL void pgm_rxw_remove_commit (pgm_rxw_t*const);
PGM_GNUC_INTERNAL ssize_t pgm_rxw_readv (pgm_rxw_t*const restrict, struct pgm_msgv_t** restrict, const unsigned) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL unsigned pgm_rxw_remove_trail (pgm_rxw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL unsigned pgm_rxw_update (pgm_rxw_t*const, const uint32_t, const uint32_t, const pgm_time_t, const pgm_time_t) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_rxw_update_fec (pgm_rxw_t*const, const uint8_t);
PGM_GNUC_INTERNAL int pgm_rxw_confirm (pgm_rxw_t*const, const uint32_t, const pgm_time_t, const pgm_time_t, const pgm_time_t) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_rxw_lost (pgm_rxw_t*const, const uint32_t);
PGM_GNUC_INTERNAL void pgm_rxw_state (pgm_rxw_t*const restrict, struct pgm_sk_buff_t*const restrict, const int);
PGM_GNUC_INTERNAL struct pgm_sk_buff_t* pgm_rxw_peek (pgm_rxw_t*const, const uint32_t) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL const char* pgm_pkt_state_string (const int) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL const char* pgm_rxw_returns_string (const int) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_rxw_dump (const pgm_rxw_t*const);

/* declare for GCC attributes */
static inline unsigned pgm_rxw_max_length (const pgm_rxw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint32_t pgm_rxw_length (const pgm_rxw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline size_t pgm_rxw_size (const pgm_rxw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline bool pgm_rxw_is_empty (const pgm_rxw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline bool pgm_rxw_is_full (const pgm_rxw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint32_t pgm_rxw_lead (const pgm_rxw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint32_t pgm_rxw_next_lead (const pgm_rxw_t*const) PGM_GNUC_WARN_UNUSED_RESULT;

static inline
unsigned
pgm_rxw_max_length (
	const pgm_rxw_t* const window
	)
{
	pgm_assert (NULL != window);
	return window->alloc;
}

static inline
uint32_t
pgm_rxw_length (
	const pgm_rxw_t* const window
	)
{
	pgm_assert (NULL != window);
	return ( 1 + window->lead ) - window->trail;
}

static inline
size_t
pgm_rxw_size (
	const pgm_rxw_t* const window
	)
{
	pgm_assert (NULL != window);
	return window->size;
}

static inline
bool
pgm_rxw_is_empty (
	const pgm_rxw_t* const window
	)
{
	pgm_assert (NULL != window);
	return pgm_rxw_length (window) == 0;
}

static inline
bool
pgm_rxw_is_full (
	const pgm_rxw_t* const window
	)
{
	pgm_assert (NULL != window);
	return pgm_rxw_length (window) == pgm_rxw_max_length (window);
}

static inline
uint32_t
pgm_rxw_lead (
	const pgm_rxw_t* const window
	)
{
	pgm_assert (NULL != window);
	return window->lead;
}

static inline
uint32_t
pgm_rxw_next_lead (
	const pgm_rxw_t* const window
	)
{
	pgm_assert (NULL != window);
	return (uint32_t)(pgm_rxw_lead (window) + 1);
}

PGM_END_DECLS

#endif /* __PGM_IMPL_RXW_H__ */
