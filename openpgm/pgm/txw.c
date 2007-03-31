/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * A basic transmit window 
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

	int		length;
	int		sequence_number;
	struct timeval	expiry;
	struct timeval	last_retransmit;
};

struct txw {
	GPtrArray*	pdata;
	GTrashStack*	trash;

	int		max_tpdu;		/* maximum packet size */

	int		lead, next_lead;
	int		trail;
	int		offset;
	
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
	int	tpdu_length,
	int	preallocate_size,
	int	txw_sqns,		/* transmit window size in sequence numbers */
	int	txw_secs,		/* size in seconds */
	int	txw_max_rte		/* max bandwidth */
	)
{
	if (txw_debug)
	printf ("txw: init (tpdu %i pre-alloc %i txw_sqns %i txw_secs %i txw_max_rte %i).\n",
		tpdu_length, preallocate_size, txw_sqns, txw_secs, txw_max_rte);

	struct txw* t = g_slice_alloc0 (sizeof(struct txw));
	t->pdata = g_ptr_array_new ();

	t->max_tpdu = tpdu_length;

	for (int i = 0; i < preallocate_size; i++)
	{
		gpointer packet = g_slice_alloc (t->max_tpdu);
		g_trash_stack_push (&t->trash, packet);
	}

/* calculate transmit window parameters */
	if (txw_sqns)
	{
	}
	else if (txw_secs && txw_max_rte)
	{
		txw_sqns = (txw_secs * txw_max_rte) / t->max_tpdu;
	}

	if (txw_debug)
	printf ("txw: %i packets.\n", txw_sqns);
	g_ptr_array_set_size (t->pdata, txw_sqns);

	return (gpointer)t;
}

int
txw_shutdown (
	gpointer	ptr
	)
{
	if (txw_debug)
	puts ("txw: shutdown.");

	if (!ptr) {
		puts ("txw: invalid txw.");
		return -1;
	}
	struct txw* t = (struct txw*)ptr;

	if (t->pdata)
	{
		g_ptr_array_foreach (t->pdata, _list_iterator, &t->max_tpdu);
		g_ptr_array_free (t->pdata, TRUE);
		t->pdata = NULL;
	}

	if (t->trash)
	{
		gpointer *p = NULL;

/* gcc recommends parentheses around assignment used as truth value */
		while ( (p = g_trash_stack_pop (&t->trash)) )
			g_slice_free1 (t->max_tpdu, t->trash);

/* empty trash stack = NULL */
	}

	return 0;
}

static void
_list_iterator (
	gpointer	data,
	gpointer	user_data
	)
{
/* iteration on empty array sized on init() */
	if (!data) return;

	struct txw_packet *tp = (struct txw_packet*)data;
	int length = tp->length;

	g_slice_free1 ( length, tp->data );
	g_slice_free1 ( sizeof(struct txw_packet), data );
}

gpointer
txw_alloc (
	gpointer	ptr
	)
{
	if (!ptr) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	return t->trash ? g_trash_stack_pop (&t->trash) : g_slice_alloc (t->max_tpdu);
}

int
txw_next_lead (
	gpointer	ptr
	)
{
	if (!ptr) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	return t->next_lead;
}

int
txw_lead (
	gpointer	ptr
	)
{
	if (!ptr) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	return t->lead;
}

int
txw_trail (
	gpointer	ptr
	)
{
	if (!ptr) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}
	struct txw* t = (struct txw*)ptr;

	return t->trail;
}

int
txw_push (
	gpointer	ptr,
	gpointer	packet,
	int		length
	)
{
	if (!ptr) {
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
	struct txw_packet* tp = g_slice_alloc (sizeof(struct txw_packet));

	tp->data		= packet;
	tp->length		= length;
	tp->sequence_number	= t->next_lead;

/* wrap offset helper */
	if (tp->sequence_number == ( TXW_LENGTH(t) + t->offset ))
	{
if (txw_debug > 1)
puts ("txw: wrap offset.");
		t->offset += TXW_LENGTH(t);
	}

	int offset = tp->sequence_number - t->offset;

if (txw_debug > 2)
printf ("txw: add packet offset %i\n", offset);
	g_ptr_array_index (t->pdata, offset) = tp;

	t->lead			= tp->sequence_number;
	t->next_lead		= TXW_INC_SQN(t->lead);

if (txw_debug > 2)
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
	int		length
	)
{
	if (!ptr) {
		puts ("txw: invalid parameter, major internal error.");
		exit (-1);
	}

	gpointer copy = g_slice_alloc (length);
	memcpy (copy, packet, length);

	return txw_push (ptr, copy, length);
}

int
txw_get (
	gpointer	ptr,
	int		sequence_number,
	gpointer*	packet,
	int*		length
	)
{
	if (!ptr) {
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

	int offset = TXW_PACKET_OFFSET(t, sequence_number);
	struct txw_packet* tp = g_ptr_array_index (t->pdata, offset);
	*packet = tp->data;
	*length	= tp->length;

	return 0;
}

int
txw_pop (
	gpointer	ptr
	)
{
	if (!ptr) {
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

	int offset = TXW_PACKET_OFFSET(t, t->trail);
	struct txw_packet* tp = g_ptr_array_index (t->pdata, offset);
	g_slice_free1 (tp->length, tp->data);
	g_slice_free1 (sizeof(struct txw), tp);

	t->trail = TXW_INC_SQN(t->trail);

	return 0;
}

/* eof */
