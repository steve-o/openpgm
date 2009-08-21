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

#include <glib.h>

struct pgm_sk_buff_t;

#ifndef __PGM_TRANSPORT_H__
#	include <pgm/transport.h>
#endif

#ifndef __PGM_PACKET_H__
#	include <pgm/packet.h>
#endif


struct pgm_sk_buff_t {
	GList			link_;

	pgm_transport_t*	transport;
	pgm_time_t		tstamp;
	pgm_tsi_t		tsi;

	guint32			sequence;

	char			cb[48];		/* control buffer */

	guint16			len;		/* actual data */
	unsigned		zero_padded:1;

	struct pgm_header*	pgm_header;
	struct pgm_opt_fragment* pgm_opt_fragment;
#define of_apdu_first_sqn	pgm_opt_fragment->opt_sqn
#define of_frag_offset		pgm_opt_fragment->opt_frag_off
#define of_apdu_len		pgm_opt_fragment->opt_frag_len
	struct pgm_data*	pgm_data;

	gpointer		head,
				data,
				tail,
				end;
	guint			truesize;
	gint			users;		/* atomic */
};

static inline void pgm_skb_over_panic (struct pgm_sk_buff_t* skb, guint16 len) G_GNUC_NORETURN;
static inline void pgm_skb_over_panic (struct pgm_sk_buff_t* skb, guint16 len)
{
	g_error ("skput:over: %d put:%d",
		    skb->len, len);
}

static inline void pgm_skb_under_panic (struct pgm_sk_buff_t* skb, guint16 len) G_GNUC_NORETURN;
static inline void pgm_skb_under_panic (struct pgm_sk_buff_t* skb, guint16 len)
{
	g_error ("skput:under: %d put:%d",
		    skb->len, len);
}

static inline struct pgm_sk_buff_t* pgm_alloc_skb (guint16 size)
{
	struct pgm_sk_buff_t* skb;

	skb = (struct pgm_sk_buff_t*)g_slice_alloc (size + sizeof(struct pgm_sk_buff_t));
	memset (skb, 0, sizeof(struct pgm_sk_buff_t));
	skb->truesize = size + sizeof(struct pgm_sk_buff_t);
	g_atomic_int_set (&skb->users, 1);
	skb->head = skb + 1;
	skb->data = skb->tail = skb->head;
	skb->end  = (guint8*)skb->data + size;
	return skb;
}

/* increase reference count */
static inline struct pgm_sk_buff_t* pgm_skb_get (struct pgm_sk_buff_t* skb)
{
	g_atomic_int_inc (&skb->users);
	return skb;
}

static inline void pgm_free_skb (struct pgm_sk_buff_t* skb)
{
	if (g_atomic_int_dec_and_test (&skb->users))
		g_slice_free1 (skb->truesize, skb);
}

/* add data */
static inline gpointer pgm_skb_put (struct pgm_sk_buff_t* skb, guint16 len)
{
	gpointer tmp = skb->tail;
	skb->tail = (guint8*)skb->tail + len;
	skb->len  += len;
	if (G_UNLIKELY(skb->tail > skb->end))
		pgm_skb_over_panic (skb, len);
	return tmp;
}

static inline gpointer __pgm_skb_pull (struct pgm_sk_buff_t *skb, guint16 len)
{
	skb->len -= len;
	return skb->data = (guint8*)skb->data + len;
}

/* remove data from start of buffer */
static inline gpointer pgm_skb_pull (struct pgm_sk_buff_t* skb, guint16 len)
{
	return G_UNLIKELY(len > skb->len) ? NULL : __pgm_skb_pull (skb, len);
}

static inline gint pgm_skb_headroom (const struct pgm_sk_buff_t* skb)
{
	return (guint8*)skb->data - (guint8*)skb->head;
}

static inline gint pgm_skb_tailroom (const struct pgm_sk_buff_t* skb)
{
	return (guint8*)skb->end - (guint8*)skb->tail;
}

/* reserve space to add data */
static inline void pgm_skb_reserve (struct pgm_sk_buff_t* skb, guint16 len)
{
	skb->data = (guint8*)skb->data + len;
	skb->tail = (guint8*)skb->tail + len;
	if (G_UNLIKELY(skb->tail > skb->end))
		pgm_skb_over_panic (skb, len);
	if (G_UNLIKELY(skb->data < skb->head))
		pgm_skb_under_panic (skb, len);
}

static inline struct pgm_sk_buff_t* pgm_skb_copy (const struct pgm_sk_buff_t* const skb)
{
	struct pgm_sk_buff_t* newskb;
	newskb = (struct pgm_sk_buff_t*)g_slice_alloc (skb->truesize);
	memcpy (newskb, skb, G_STRUCT_OFFSET(struct pgm_sk_buff_t, pgm_header));
	newskb->zero_padded = 0;
	newskb->truesize = skb->truesize;
	g_atomic_int_set (&newskb->users, 1);
	newskb->head = newskb + 1;
	newskb->end  = (guint8*)newskb->head + ((guint8*)skb->end - (guint8*)skb->head);
	newskb->data = (guint8*)newskb->head + ((guint8*)skb->data - (guint8*)skb->head);
	newskb->tail = (guint8*)newskb->head + ((guint8*)skb->tail - (guint8*)skb->head);
	newskb->pgm_header = skb->pgm_header ? (gpointer)((guint8*)newskb->head + ((guint8*)skb->pgm_header - (guint8*)skb->head)) : skb->pgm_header;
	newskb->pgm_opt_fragment = skb->pgm_opt_fragment ? (gpointer)((guint8*)newskb->head + ((guint8*)skb->pgm_opt_fragment - (guint8*)skb->head)) : skb->pgm_opt_fragment;
	newskb->pgm_data = skb->pgm_data ? (gpointer)((guint8*)newskb->head + ((guint8*)skb->pgm_data - (guint8*)skb->head)) : skb->pgm_data;
	memcpy (newskb->head, skb->head, (guint8*)skb->end - (guint8*)skb->head);
	return newskb;
}

static inline void pgm_skb_zero_pad (struct pgm_sk_buff_t* const skb, const guint16 len)
{
	if (skb->zero_padded)
		return;

	memset (skb->tail, 0, MIN(pgm_skb_tailroom(skb), len));
	skb->zero_padded = 1;
}

/* PGM skbuff for data, in-state skbuffs will return FALSE.
 */
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
	g_return_val_if_fail ((const guint8*)skb->head > (const guint8*)&skb->users, FALSE);
	g_return_val_if_fail (skb->data, FALSE);
	g_return_val_if_fail ((const guint8*)skb->data >= (const guint8*)skb->head, FALSE);
	g_return_val_if_fail (skb->tail, FALSE);
	g_return_val_if_fail ((const guint8*)skb->tail >= (const guint8*)skb->data, FALSE);
	g_return_val_if_fail (skb->len == (guint8*)skb->tail - (const guint8*)skb->data, FALSE);
	g_return_val_if_fail (skb->end, FALSE);
	g_return_val_if_fail ((const guint8*)skb->end >= (const guint8*)skb->tail, FALSE);
/* pgm_header */
	if (skb->pgm_header) {
		g_return_val_if_fail ((const guint8*)skb->pgm_header >= (const guint8*)skb->head, FALSE);
		g_return_val_if_fail ((const guint8*)skb->pgm_header + sizeof(struct pgm_header) <= (const guint8*)skb->tail, FALSE);
		g_return_val_if_fail (skb->pgm_data, FALSE);
		g_return_val_if_fail ((const guint8*)skb->pgm_data >= (const guint8*)skb->pgm_header + sizeof(struct pgm_header), FALSE);
		g_return_val_if_fail ((const guint8*)skb->pgm_data <= (const guint8*)skb->tail, FALSE);
		if (skb->pgm_opt_fragment) {
			g_return_val_if_fail ((const guint8*)skb->pgm_opt_fragment > (const guint8*)skb->pgm_data, FALSE);
			g_return_val_if_fail ((const guint8*)skb->pgm_opt_fragment + sizeof(struct pgm_opt_fragment) < (const guint8*)skb->tail, FALSE);
/* of_apdu_first_sqn can be any value */
/* of_frag_offset */
			g_return_val_if_fail (g_ntohl (skb->of_frag_offset) < g_ntohl (skb->of_apdu_len), FALSE);
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
	g_return_val_if_fail (skb->truesize == (guint)((const guint8*)skb->end - (const guint8*)skb), FALSE);
/* users */
	g_return_val_if_fail (g_atomic_int_get (&skb->users) > 0, FALSE);
	return TRUE;
}

#endif /* __PGM_SKBUFF_H__ */
