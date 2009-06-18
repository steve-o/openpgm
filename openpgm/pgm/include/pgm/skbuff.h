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

#ifndef __PGM_TRANSPORT_H__
#	include <pgm/transport.h>
#endif


struct pgm_sk_buff_t {
	GList			link_;

	pgm_transport_t*	transport;
	pgm_time_t		tstamp;
	struct sockaddr_storage	src;
	struct sockaddr_storage dst;
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

static inline void pgm_skb_zero_pad (struct pgm_sk_buff_t* skb, guint16 len)
{
	if (skb->zero_padded)
		return;

	memset (skb->tail, 0, MIN(pgm_skb_tailroom(skb), len));
	skb->zero_padded = 1;
}

#endif /* __PGM_SKBUFF_H__ */
