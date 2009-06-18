/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test alloc/wraping on a basic receive window 
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <glib.h>

#include "pgm/backtrace.h"
#include "pgm/rxwi.h"
#include "pgm/skbuff.h"

#if 1
#define g_trace(...)	while (0)
#else
#define g_trace(...)	g_debug(__VA_ARGS__)
#endif


struct tests {
	double	      (*test_func)(int, int);
	const char*	name;
};


/* globals */
int on_send_nak (gpointer, guint, guint32, pgm_pkt_state_e*, gdouble, guint, gpointer);
int on_wait_ncf (gpointer, guint, guint32, pgm_pkt_state_e*, gdouble, guint, gpointer);
int on_wait_data (gpointer, guint, guint32, pgm_pkt_state_e*, gdouble, guint, gpointer);

double test_basic_rxw (int, int);
double test_jump (int, int);
double test_reverse (int, int);
double test_fill (int, int);


G_GNUC_NORETURN static void
usage (
	const char*	bin
	)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	exit (1);
}

int
main (
	int		argc,
	char*		argv[]
	)
{
	puts ("basic_rxw");

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "h")) != -1)
	{
		switch (c) {

		case 'h':
		case '?': usage (binary_name);
		}
	}

	pgm_time_init();
//	g_thread_init (NULL);

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
	signal (SIGHUP, SIG_IGN);

	int test_size[] = { 10000, 20000, 20000, 10000, 0 };
//	int test_size[] = { 10, 0 };
	int test_payload[] = { /*9000,*/ 1500, 0 };
	struct tests tests[] = {
			{ test_basic_rxw, "basic rxw" },
			{ test_jump, "sequence numbers in jumps" },
			{ test_reverse, "sequence numbers in reverse " },
			{ test_fill, "basic fill " },
			{ NULL, NULL }
			};

/* print header */
	struct tests* p;
	int* p2;
	int* p3;

	int test_count = 3;

        p = tests;
        do {
                p2 = test_payload;
                do {
                        for (int test = 1; test <= test_count; test++)
                        {
                                printf (",%s@%ib/%i", p->name, *p2, test);
                        }
                } while (*(++p2));
        } while ((++p)->name);
        putchar ('\n');

/* each row is one payload size */
        p3 = test_size;
        do {
                printf ("%i", *p3);

                p = tests;
                do {
                        p2 = test_payload;
                        do {
                                for (int test = 1; test <= test_count; test++)
                                {
                                        printf (",%g", p->test_func (*p3, *p2) );
                                }
                        } while (*(++p2));
                } while ((++p)->name);

                putchar ('\n');
        } while (*(++p3));

	puts ("finished.");
	return 0;
}

static int
on_pgm_data (
	G_GNUC_UNUSED gpointer data,
	G_GNUC_UNUSED guint length,
	G_GNUC_UNUSED gpointer param
	)
{
	g_trace ("on_pgm_data()");
	return 0;
}

static int
flush_rxw (
	pgm_rxw_t*	r
	)
{
	g_return_val_if_fail (r != NULL, -1);

	pgm_msgv_t msgv[ IOV_MAX ];
	struct pgm_iovec iov[ IOV_MAX ];

	int bytes_read = 0;
	do {
		pgm_msgv_t* pmsgv = msgv;
		struct pgm_iovec* piov = iov;
		bytes_read = pgm_rxw_readv (r, &pmsgv, G_N_ELEMENTS(msgv), &piov, G_N_ELEMENTS(iov), 0);
	} while (bytes_read > 0);

	return 0;
}

static int
backoff_state_foreach (
	pgm_rxw_t*	r
	)
{
	g_return_val_if_fail (r != NULL, -1);

	GList* list = r->backoff_queue.tail;

	if (!list) return 0;

	while (list)
	{
		GList* next_list_el = list->prev;
		struct pgm_sk_buff_t* skb = (struct pgm_sk_buff_t*)list;
		pgm_rxw_packet_t* pkt = (pgm_rxw_packet_t*)&skb->cb;

		pgm_rxw_pkt_state_unlink (r, skb);

		/* -- pretend to send nak here -- */

/* nak sent, await ncf */
		pkt->state = PGM_PKT_WAIT_NCF_STATE;
		g_queue_push_head_link (&r->wait_ncf_queue, &skb->link_);

		list = next_list_el;
	}

	return 0;
}

double
test_basic_rxw (
	int		count,
	int		size_per_entry
	)
{
	struct timeval start, now;
	gpointer rxw;
	int i;

	rxw = pgm_rxw_init (NULL, size_per_entry, count, 0, 0);
	g_assert (rxw);
	pgm_rxw_window_update(rxw, 0 /* trail */, -1 /* lead */, 1, 0, pgm_time_update_now());

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		struct pgm_sk_buff_t* skb = pgm_alloc_skb (size_per_entry);
		const int header_size = pgm_transport_pkt_offset(FALSE);
		pgm_skb_reserve (skb, header_size);
		pgm_skb_put (skb, size_per_entry);

		skb->sequence = i;
		pgm_rxw_push (rxw, skb, pgm_time_now);
		backoff_state_foreach (rxw);
		flush_rxw (rxw);
	}
	gettimeofday(&now, NULL);

	pgm_rxw_shutdown (rxw);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );
	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_jump (
	int		count,
	int		size_per_entry
	)
{
	struct timeval start, now;
	gpointer rxw;
	int i, j;

	rxw = pgm_rxw_init (NULL, size_per_entry, 2 * count, 0, 0);
	g_assert (rxw);
	pgm_rxw_window_update(rxw, 0 /* trail */, -1 /* lead */, 1, 0, pgm_time_update_now());

	gettimeofday(&start, NULL);
	for (i = j = 0; i < count; i++, j+=2)
	{
		struct pgm_sk_buff_t* skb = pgm_alloc_skb (size_per_entry);
		const int header_size = pgm_transport_pkt_offset(FALSE);
		pgm_skb_reserve (skb, header_size);
		pgm_skb_put (skb, size_per_entry);

		skb->sequence = j;
		pgm_rxw_push (rxw, skb, pgm_time_now);
		backoff_state_foreach (rxw);
		flush_rxw (rxw);
	}
	gettimeofday(&now, NULL);

	pgm_rxw_shutdown (rxw);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );
	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_reverse (
	int		count,
	int		size_per_entry
	)
{
	struct timeval start, now;
	gpointer rxw;
	int i, j;

	rxw = pgm_rxw_init (NULL, size_per_entry, count, 0, 0);
	g_assert (rxw);
	pgm_rxw_window_update(rxw, 0 /* trail */, -1 /* lead */, 1, 0, pgm_time_update_now());

	gettimeofday(&start, NULL);
	for (i = 0, j = count; i < count; i++)
	{
		struct pgm_sk_buff_t* skb = pgm_alloc_skb (size_per_entry);
		const int header_size = pgm_transport_pkt_offset(FALSE);
		pgm_skb_reserve (skb, header_size);
		pgm_skb_put (skb, size_per_entry);

		if (i > 0)	skb->sequence = --j;
		else		skb->sequence =   i;

		pgm_rxw_push (rxw, skb, pgm_time_now);
		backoff_state_foreach (rxw);
		flush_rxw (rxw);
	}
	gettimeofday(&now, NULL);

	pgm_rxw_shutdown (rxw);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );
	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_fill (
	int		count,
	int		size_per_entry
	)
{
	struct timeval start, now;
	gpointer rxw;
	int i;

	rxw = pgm_rxw_init (NULL, size_per_entry, count+1, 0, 0);
	g_assert (rxw);
	pgm_rxw_window_update(rxw, 0 /* trail */, -1 /* lead */, 1, 0, pgm_time_update_now());

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		struct pgm_sk_buff_t* skb = pgm_alloc_skb (size_per_entry);
		const int header_size = pgm_transport_pkt_offset(FALSE);
		pgm_skb_reserve (skb, header_size);
		pgm_skb_put (skb, size_per_entry);

		skb->sequence = i + 1;
		pgm_rxw_push (rxw, skb, pgm_time_now);
		backoff_state_foreach (rxw);
		flush_rxw (rxw);
	}
	gettimeofday(&now, NULL);

	pgm_rxw_shutdown (rxw);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );
	return (secs * 1000.0 * 1000.0) / (double)count;
}

/* eof */
