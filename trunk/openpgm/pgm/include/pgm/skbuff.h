/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * PGM socket buffers
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
#ifndef __PGM_SKBUFF_H__
#define __PGM_SKBUFF_H__

#include <string.h>

struct pgm_sk_buff_t;

#include <pgm/types.h>
#include <pgm/atomic.h>
#include <pgm/mem.h>
#include <pgm/list.h>
#include <pgm/time.h>
#include <pgm/packet.h>
#include <pgm/tsi.h>
#include <pgm/socket.h>

PGM_BEGIN_DECLS

struct pgm_sk_buff_t {
	pgm_list_t			link_;

	pgm_sock_t* restrict		sock;
	pgm_time_t			tstamp;
	pgm_tsi_t			tsi;

	uint32_t			sequence;
	uint32_t			__padding;	/* push alignment of pgm_sk_buff_t::cb to 8 bytes */

	char				cb[48];		/* control buffer */

	uint16_t			len;		/* actual data */
	unsigned			zero_padded:1;
	unsigned			__padding2:31;	/* fix bit field */

	struct pgm_header*		pgm_header;
	struct pgm_opt_fragment* 	pgm_opt_fragment;
#define of_apdu_first_sqn		pgm_opt_fragment->opt_sqn
#define of_frag_offset			pgm_opt_fragment->opt_frag_off
#define of_apdu_len			pgm_opt_fragment->opt_frag_len
	struct pgm_opt_pgmcc_data*	pgm_opt_pgmcc_data;
	struct pgm_data*		pgm_data;

	void			       *head,		/* all may-alias */
				       *data,
				       *tail,
				       *end;
	uint32_t			truesize;
	volatile uint32_t		users;		/* atomic */
};

void pgm_skb_over_panic (const struct pgm_sk_buff_t*const, const uint16_t) PGM_GNUC_NORETURN;
void pgm_skb_under_panic (const struct pgm_sk_buff_t*const, const uint16_t) PGM_GNUC_NORETURN;
bool pgm_skb_is_valid (const struct pgm_sk_buff_t*const) PGM_GNUC_PURE PGM_GNUC_WARN_UNUSED_RESULT;

/* attribute __pure__ only valid for platforms with atomic ops.
 * attribute __malloc__ not used as only part of the memory should be aliased.
 * attribute __alloc_size__ does not allow headroom.
 */
static inline struct pgm_sk_buff_t* pgm_alloc_skb (const uint16_t) PGM_GNUC_WARN_UNUSED_RESULT;

static inline
struct pgm_sk_buff_t*
pgm_alloc_skb (
	const uint16_t		size
	)
{
	struct pgm_sk_buff_t* skb;

	skb = (struct pgm_sk_buff_t*)pgm_malloc (size + sizeof(struct pgm_sk_buff_t));
/* Requires fast FSB to test
	pgm_prefetchw (skb);
 */
	if (PGM_UNLIKELY(pgm_mem_gc_friendly)) {
		memset (skb, 0, size + sizeof(struct pgm_sk_buff_t));
		skb->zero_padded = 1;
	} else {
		memset (skb, 0, sizeof(struct pgm_sk_buff_t));
	}
	skb->truesize = size + sizeof(struct pgm_sk_buff_t);
	pgm_atomic_write32 (&skb->users, 1);
	skb->head = skb + 1;
	skb->data = skb->tail = skb->head;
	skb->end  = (char*)skb->data + size;
	return skb;
}

/* increase reference count */
static inline
struct pgm_sk_buff_t*
pgm_skb_get (
	struct pgm_sk_buff_t*const skb
	)
{
	pgm_atomic_inc32 (&skb->users);
	return skb;
}

static inline
void
pgm_free_skb (
	struct pgm_sk_buff_t*const skb
	)
{
	if (pgm_atomic_exchange_and_add32 (&skb->users, (uint32_t)-1) == 1)
		pgm_free (skb);
}

/* add data */
static inline
void*
pgm_skb_put (
	struct pgm_sk_buff_t* const skb,
	const uint16_t		len
	)
{
	void* tmp = skb->tail;
	skb->tail = (char*)skb->tail + len;
	skb->len  += len;
	if (PGM_UNLIKELY(skb->tail > skb->end))
		pgm_skb_over_panic (skb, len);
	return tmp;
}

static inline
void*
__pgm_skb_pull (
	struct pgm_sk_buff_t*const skb,
	const uint16_t		len
	)
{
	skb->len -= len;
	return skb->data = (char*)skb->data + len;
}

/* remove data from start of buffer */
static inline
void*
pgm_skb_pull (
	struct pgm_sk_buff_t*const skb,
	const uint16_t		len
	)
{
	return PGM_UNLIKELY(len > skb->len) ? NULL : __pgm_skb_pull (skb, len);
}

static inline uint16_t pgm_skb_headroom (const struct pgm_sk_buff_t*const) PGM_GNUC_PURE PGM_GNUC_WARN_UNUSED_RESULT;
static inline uint16_t pgm_skb_tailroom (const struct pgm_sk_buff_t*const) PGM_GNUC_PURE PGM_GNUC_WARN_UNUSED_RESULT;

static inline
uint16_t
pgm_skb_headroom (
	const struct pgm_sk_buff_t*const skb
	)
{
	return (uint16_t)((char*)skb->data - (char*)skb->head);
}

static inline
uint16_t
pgm_skb_tailroom (
	const struct pgm_sk_buff_t*const skb
	)
{
	return (uint16_t)((char*)skb->end - (char*)skb->tail);
}

/* reserve space to add data */
static inline
void
pgm_skb_reserve (
	struct pgm_sk_buff_t*const skb,
	const uint16_t		len
	)
{
	skb->data = (char*)skb->data + len;
	skb->tail = (char*)skb->tail + len;
	if (PGM_UNLIKELY(skb->tail > skb->end))
		pgm_skb_over_panic (skb, len);
	if (PGM_UNLIKELY(skb->data < skb->head))
		pgm_skb_under_panic (skb, len);
}

static inline struct pgm_sk_buff_t* pgm_skb_copy (const struct pgm_sk_buff_t* const) PGM_GNUC_WARN_UNUSED_RESULT;

static inline
struct pgm_sk_buff_t*
pgm_skb_copy (
	const struct pgm_sk_buff_t* const skb
	)
{
	struct pgm_sk_buff_t* newskb;
	newskb = (struct pgm_sk_buff_t*)pgm_malloc (skb->truesize);
	memcpy (newskb, skb, PGM_OFFSETOF(struct pgm_sk_buff_t, pgm_header));
	newskb->zero_padded = 0;
	newskb->truesize = skb->truesize;
	pgm_atomic_write32 (&newskb->users, 1);
	newskb->head = newskb + 1;
	newskb->end  = (char*)newskb->head + ((char*)skb->end  - (char*)skb->head);
	newskb->data = (char*)newskb->head + ((char*)skb->data - (char*)skb->head);
	newskb->tail = (char*)newskb->head + ((char*)skb->tail - (char*)skb->head);
	newskb->pgm_header = skb->pgm_header ? (struct pgm_header*)((char*)newskb->head + ((char*)skb->pgm_header - (char*)skb->head)) : skb->pgm_header;
	newskb->pgm_opt_fragment = skb->pgm_opt_fragment ? (struct pgm_opt_fragment*)((char*)newskb->head + ((char*)skb->pgm_opt_fragment - (char*)skb->head)) : skb->pgm_opt_fragment;
	newskb->pgm_data = skb->pgm_data ? (struct pgm_data*)((char*)newskb->head + ((char*)skb->pgm_data - (char*)skb->head)) : skb->pgm_data;
	memcpy (newskb->head, skb->head, (char*)skb->end - (char*)skb->head);
	return newskb;
}

static inline
void
pgm_skb_zero_pad (
	struct pgm_sk_buff_t* const	skb,
	const uint16_t			len
	)
{
/* C89 version */
	const uint16_t tailroom = MIN(pgm_skb_tailroom (skb), len);
	if (skb->zero_padded)
		return;

	if (tailroom > 0)
		memset (skb->tail, 0, tailroom);
	skb->zero_padded = 1;
}

PGM_END_DECLS

#endif /* __PGM_SKBUFF_H__ */
