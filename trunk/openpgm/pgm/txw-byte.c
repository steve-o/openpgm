/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * A basic transmit window: byte array implementation.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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


#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>


#include <glib.h>

#include "txw.h"


struct txw_packet {
	gpointer	data;

	guint		length;
	guint32		sequence_number;
	struct timeval	expiry;
	struct timeval	last_retransmit;
};

struct txw {
	GByteArray*	pdata;
	GTrashStack*	trash_data;		/* max_tpdu */

	guint		max_tpdu;		/* maximum packet size */

	guint32		lead, next_lead;
	guint32		trail;
	guint		offset;
	
};

#define TXW_LENGTH(t)	( (t)->pdata->len )
#define TXW_SQNS(t)	( (1 + (t)->lead) - (t)->trail )

#define ABS_IN_TXW(t,x) \
	( (x) >= (t)->trail && (x) <= (t)->lead )

#define IN_TXW(t,x) \
	( (x) >= (t)->trail && (x) <= ((t)->trail + ((UINT32_MAX/2) - 1)) )

#define TXW_PACKET_OFFSET(t,x) \
	( \
		(x) >= (t)->offset ? \
			( (x) - (t)->offset ) : \
			( (x) - ( (t)->offset - TXW_LENGTH(t) ) ) \
	)

#define TXW_INC_SQN(x) \
	( (x) == UINT32_MAX ? 0 : ( (x) + 1 ) )



/* globals */
int txw_debug = 1;

static void _list_iterator (gpointer, gpointer);


gpointer
txw_init (
	guint	tpdu_length,
	guint32	preallocate_size,
	guint32	txw_sqns,		/* transmit window size in sequence numbers */
	guint	txw_secs,		/* size in seconds */
	guint	txw_max_rte		/* max bandwidth */
	)
{
	if (G_UNLIKELY(txw_debug))
	printf ("txw: init (tpdu %i pre-alloc %i txw_sqns %i txw_secs %i txw_max_rte %i).\n",
		tpdu_length, preallocate_size, txw_sqns, txw_secs, txw_max_rte);

	struct txw* t = g_slice_alloc0 (sizeof(struct txw));
	t->pdata = g_byte_array_new ();

	t->max_tpdu = tpdu_length;

	for (guint32 i = 0; i < preallocate_size; i++)
	{
		gpointer data   = g_slice_alloc (t->max_tpdu);
		g_trash_stack_push (&t->trash_data, data);
	}

/* calculate transmit window parameters */
	if (txw_sqns)
	{
	}
	else if (txw_secs && txw_max_rte)
	{
		txw_sqns = (txw_secs * txw_max_rte) / t->max_tpdu;
	}

	if (G_UNLIKELY(txw_debug))
	printf ("txw: %i packets.\n", txw_sqns);
	g_byte_array_set_size (t->pdata, sizeof(struct txw_packet) * txw_sqns);

	return (gpointer)t;
}

int
txw_shutdown (
	gpointer	ptr
	)
{
	if (G_UNLIKELY(txw_debug))
	puts ("txw: shutdown.");

	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid txw.");
		return -1;
	}
	struct txw* t = (struct txw*)ptr;

	if (G_LIKELY(t->pdata))
	{
		for (int i = 0; i < t->pdata->len / sizeof(struct txw_packet); i++)
		{
			struct txw_packet *tp = &t->pdata->data[sizeof(struct txw_packet) * i];
			if (tp->data) {
				g_slice_free1 (t->max_tpdu, tp->data);
				tp->data = NULL;
			}
		}
		g_byte_array_free (t->pdata, TRUE);
		t->pdata = NULL;
	}

	if (t->trash_data)
	{
		gpointer *p = NULL;

/* gcc recommends parentheses around assignment used as truth value */
		while ( (p = g_trash_stack_pop (&t->trash_data)) )
			g_slice_free1 (t->max_tpdu, t->trash_data);

/* empty trash stack = NULL */
	}

	return 0;
}

/* alloc for the payload per packet */
gpointer
txw_alloc (
	gpointer	ptr
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	return t->trash_data ? g_trash_stack_pop (&t->trash_data) : g_slice_alloc (t->max_tpdu);
}

guint32
txw_next_lead (
	gpointer	ptr
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	return t->next_lead;
}

guint32
txw_lead (
	gpointer	ptr
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	return t->lead;
}

guint32
txw_trail (
	gpointer	ptr
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	return t->trail;
}

int
txw_in_window (
	gpointer	ptr,
	guint32		sequence_number
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	return ABS_IN_TXW(t, sequence_number);
}

int
txw_push (
	gpointer	ptr,
	gpointer	packet,
	guint		length
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

/* check for full window */
	if (TXW_LENGTH(t) == TXW_SQNS(t)) {
		puts ("txw: full :o");

/* transmit window advancement scheme dependent action here */
		txw_pop (ptr);
	}

/* add to window */
	guint32 sequence_number	= t->next_lead;
/* wrap offset helper */
	if (sequence_number == ( TXW_LENGTH(t) + t->offset ))
	{
if (G_UNLIKELY(txw_debug > 1))
puts ("txw: wrap offset.");
		t->offset += TXW_LENGTH(t);
	}

	guint offset = sequence_number - t->offset;

if (G_UNLIKELY(txw_debug > 2))
printf ("txw: add packet offset %i\n", offset);

	struct txw_packet* tp = &t->pdata->data[sizeof(struct txw_packet) * offset];
	tp->data		= packet;
	tp->length		= length;
	tp->sequence_number	= sequence_number;

	t->lead			= tp->sequence_number;
	t->next_lead		= TXW_INC_SQN(t->lead);

if (G_UNLIKELY(txw_debug > 2))
{
	if (TXW_LENGTH(t) == TXW_SQNS(t)) puts ("txw: now full.");
	printf ("txw: next lead# %i\n", t->next_lead);
}

	return 0;
}

int
txw_push_copy (
	gpointer	ptr,
	gpointer	packet,
	guint		length
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	gpointer copy = txw_alloc (ptr);
	memcpy (copy, packet, length);

	return txw_push (ptr, copy, length);
}

int
txw_get (
	gpointer	ptr,
	guint32		sequence_number,
	gpointer*	packet,
	guint*		length
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

/* check if sequence number is in window */
	if (!ABS_IN_TXW(t, sequence_number))
	{
		printf ("txw: %i not in window.\n", sequence_number);
		return -1;
	}

	guint offset = TXW_PACKET_OFFSET(t, sequence_number);
	struct txw_packet* tp = &t->pdata->data[sizeof(struct txw_packet) * offset];
	*packet = tp->data;
	*length	= tp->length;

	return 0;
}

int
txw_pop (
	gpointer	ptr
	)
{
	if (G_UNLIKELY(!ptr)) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

/* check if window is not empty */
	if (!TXW_SQNS(t))
	{
		puts ("txw: empty.");
		return -1;
	}

	guint offset = TXW_PACKET_OFFSET(t, t->trail);
	struct txw_packet* tp = &t->pdata->data[sizeof(struct txw_packet) * offset];

//	g_slice_free1 (tp->length, tp->data);
	g_trash_stack_push (&t->trash_data, tp->data);
	tp->data = NULL;

	t->trail = TXW_INC_SQN(t->trail);

	return 0;
}

/* eof */
