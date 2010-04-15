/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * PGM socket buffers
 *
 * Copyright (c) 2006-2009 Miru Limited.
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

#ifndef __PGM_SKBUFF_H__
#define __PGM_SKBUFF_H__

#include <stdlib.h>
#include <glib.h>

struct pgm_sk_buff_t;

#ifndef __PGM_TRANSPORT_H__
#	include <pgm/transport.h>
#endif

#ifndef __PGM_PACKET_H__
#	include <pgm/packet.h>
#endif

#ifndef __PGM_ATOMIC_H__
#	include <pgm/atomic.h>
#endif

#ifndef __PGM_MEM_H__
#	include <pgm/mem.h>
#endif

#ifndef __PGM_LIST_H__
#	include <pgm/list.h>
#endif

#ifndef __PGM_TIME_H__
#	include <pgm/time.h>
#endif

#ifndef __PGM_TSI_H__
#	include <pgm/tsi.h>
#endif


struct pgm_sk_buff_t {
	pgm_list_t		link_;

	pgm_transport_t*	transport;
	pgm_time_t		tstamp;
	pgm_tsi_t		tsi;

	uint32_t		sequence;
	uint32_t		__padding;	/* push alignment of pgm_sk_buff_t::cb to 8 bytes */

	char			cb[48];		/* control buffer */

	uint16_t		len;		/* actual data */
	unsigned		zero_padded:1;

	struct pgm_header*	pgm_header;
	struct pgm_opt_fragment* pgm_opt_fragment;
#define of_apdu_first_sqn	pgm_opt_fragment->opt_sqn
#define of_frag_offset		pgm_opt_fragment->opt_frag_off
#define of_apdu_len		pgm_opt_fragment->opt_frag_len
	struct pgm_data*	pgm_data;

	void		       *head,
			       *data,
			       *tail,
			       *end;
	uint32_t		truesize;
	volatile int32_t	users;		/* atomic */
};

static inline void pgm_skb_over_panic (struct pgm_sk_buff_t* skb, uint16_t len) G_GNUC_NORETURN;
static inline void pgm_skb_over_panic (struct pgm_sk_buff_t* skb, uint16_t len)
{
	pgm_fatal ("skput:over: %d put:%d",
		    skb->len, len);
	pgm_assert_not_reached();
}

static inline void pgm_skb_under_panic (struct pgm_sk_buff_t* skb, uint16_t len) G_GNUC_NORETURN;
static inline void pgm_skb_under_panic (struct pgm_sk_buff_t* skb, uint16_t len)
{
	pgm_fatal ("skput:under: %d put:%d",
		    skb->len, len);
	pgm_assert_not_reached();
}

static inline struct pgm_sk_buff_t* pgm_alloc_skb (uint16_t size)
{
	struct pgm_sk_buff_t* skb;

	skb = (struct pgm_sk_buff_t*)pgm_malloc (size + sizeof(struct pgm_sk_buff_t));
	if (G_UNLIKELY(pgm_mem_gc_friendly)) {
		memset (skb, 0, size + sizeof(struct pgm_sk_buff_t));
		skb->zero_padded = 1;
	} else {
		memset (skb, 0, sizeof(struct pgm_sk_buff_t));
	}
	skb->truesize = size + sizeof(struct pgm_sk_buff_t);
	pgm_atomic_int32_set (&skb->users, 1);
	skb->head = skb + 1;
	skb->data = skb->tail = skb->head;
	skb->end  = (char*)skb->data + size;
	return skb;
}

/* increase reference count */
static inline struct pgm_sk_buff_t* pgm_skb_get (struct pgm_sk_buff_t* skb)
{
	pgm_atomic_int32_inc (&skb->users);
	return skb;
}

static inline void pgm_free_skb (struct pgm_sk_buff_t* skb)
{
	if (pgm_atomic_int32_dec_and_test (&skb->users))
		pgm_free (skb);
}

/* add data */
static inline void* pgm_skb_put (struct pgm_sk_buff_t* skb, uint16_t len)
{
	void* tmp = skb->tail;
	skb->tail = (char*)skb->tail + len;
	skb->len  += len;
	if (G_UNLIKELY(skb->tail > skb->end))
		pgm_skb_over_panic (skb, len);
	return tmp;
}

static inline void* __pgm_skb_pull (struct pgm_sk_buff_t *skb, uint16_t len)
{
	skb->len -= len;
	return skb->data = (char*)skb->data + len;
}

/* remove data from start of buffer */
static inline void* pgm_skb_pull (struct pgm_sk_buff_t* skb, uint16_t len)
{
	return G_UNLIKELY(len > skb->len) ? NULL : __pgm_skb_pull (skb, len);
}

static inline uint16_t pgm_skb_headroom (const struct pgm_sk_buff_t* skb)
{
	return (char*)skb->data - (char*)skb->head;
}

static inline uint16_t pgm_skb_tailroom (const struct pgm_sk_buff_t* skb)
{
	return (char*)skb->end - (char*)skb->tail;
}

/* reserve space to add data */
static inline void pgm_skb_reserve (struct pgm_sk_buff_t* skb, uint16_t len)
{
	skb->data = (char*)skb->data + len;
	skb->tail = (char*)skb->tail + len;
	if (G_UNLIKELY(skb->tail > skb->end))
		pgm_skb_over_panic (skb, len);
	if (G_UNLIKELY(skb->data < skb->head))
		pgm_skb_under_panic (skb, len);
}

static inline struct pgm_sk_buff_t* pgm_skb_copy (const struct pgm_sk_buff_t* const skb)
{
	struct pgm_sk_buff_t* newskb;
	newskb = (struct pgm_sk_buff_t*)pgm_malloc (skb->truesize);
	memcpy (newskb, skb, G_STRUCT_OFFSET(struct pgm_sk_buff_t, pgm_header));
	newskb->zero_padded = 0;
	newskb->truesize = skb->truesize;
	pgm_atomic_int32_set (&newskb->users, 1);
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

static inline void pgm_skb_zero_pad (struct pgm_sk_buff_t* const skb, const uint16_t len)
{
	if (skb->zero_padded)
		return;

	const uint16_t tailroom = MIN(pgm_skb_tailroom (skb), len);
	if (tailroom > 0)
		memset (skb->tail, 0, tailroom);
	skb->zero_padded = 1;
}

/* PGM skbuff for data, in-state skbuffs will return FALSE.
 */
#ifndef SKB_DEBUG
static inline gboolean pgm_skb_is_valid (G_GNUC_UNUSED const struct pgm_sk_buff_t* const skb)
{
#else
static inline gboolean pgm_skb_is_valid (const struct pgm_sk_buff_t* const skb)
{
	g_return_val_if_fail (skb, FALSE);
/* link_ */
/* transport */
	g_return_val_if_fail (skb->transport, FALSE);
/* tstamp */
	g_return_val_if_fail (skb->tstamp > 0, FALSE);
/* tsi */
/* sequence can be any value */
/* cb can be any value */
/* len can be any value */
/* zero_padded can be any value */
/* gpointers */
	g_return_val_if_fail (skb->head, FALSE);
	g_return_val_if_fail ((const char*)skb->head > (const char*)&skb->users, FALSE);
	g_return_val_if_fail (skb->data, FALSE);
	g_return_val_if_fail ((const char*)skb->data >= (const char*)skb->head, FALSE);
	g_return_val_if_fail (skb->tail, FALSE);
	g_return_val_if_fail ((const char*)skb->tail >= (const char*)skb->data, FALSE);
	g_return_val_if_fail (skb->len == (char*)skb->tail - (const char*)skb->data, FALSE);
	g_return_val_if_fail (skb->end, FALSE);
	g_return_val_if_fail ((const char*)skb->end >= (const char*)skb->tail, FALSE);
/* pgm_header */
	if (skb->pgm_header) {
		g_return_val_if_fail ((const char*)skb->pgm_header >= (const char*)skb->head, FALSE);
		g_return_val_if_fail ((const char*)skb->pgm_header + sizeof(struct pgm_header) <= (const char*)skb->tail, FALSE);
		g_return_val_if_fail (skb->pgm_data, FALSE);
		g_return_val_if_fail ((const char*)skb->pgm_data >= (const char*)skb->pgm_header + sizeof(struct pgm_header), FALSE);
		g_return_val_if_fail ((const char*)skb->pgm_data <= (const char*)skb->tail, FALSE);
		if (skb->pgm_opt_fragment) {
			g_return_val_if_fail ((const char*)skb->pgm_opt_fragment > (const char*)skb->pgm_data, FALSE);
			g_return_val_if_fail ((const char*)skb->pgm_opt_fragment + sizeof(struct pgm_opt_fragment) < (const char*)skb->tail, FALSE);
/* of_apdu_first_sqn can be any value */
/* of_frag_offset */
			g_return_val_if_fail (ntohl (skb->of_frag_offset) < ntohl (skb->of_apdu_len), FALSE);
/* of_apdu_len can be any value */
		}
		g_return_val_if_fail (PGM_ODATA == skb->pgm_header->pgm_type || PGM_RDATA == skb->pgm_header->pgm_type, FALSE);
/* FEC broken */
		g_return_val_if_fail (0 == (skb->pgm_header->pgm_options & PGM_OPT_PARITY), FALSE);
		g_return_val_if_fail (0 == (skb->pgm_header->pgm_options & PGM_OPT_VAR_PKTLEN), FALSE);
	} else {
		g_return_val_if_fail (NULL == skb->pgm_data, FALSE);
		g_return_val_if_fail (NULL == skb->pgm_opt_fragment, FALSE);
	}
/* truesize */
	g_return_val_if_fail (skb->truesize >= sizeof(struct pgm_sk_buff_t*) + skb->len, FALSE);
	g_return_val_if_fail (skb->truesize == ((const char*)skb->end - (const char*)skb), FALSE);
/* users */
	g_return_val_if_fail (pgm_atomic_int32_get (&skb->users) > 0, FALSE);
#endif
	return TRUE;
}

#endif /* __PGM_SKBUFF_H__ */
