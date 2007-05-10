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

#include <glib.h>

#include "backtrace.h"
#include "rxwi.h"

#if 1
#define g_trace(...)	while (0)
#else
#define g_trace(...)	g_debug(__VA_ARGS__)
#endif


struct tests {
	double (*test_func)(int, int);
	char*	name;
};


/* globals */
int on_send_nak (gpointer, guint, guint32, pgm_pkt_state*, gdouble, guint, gpointer);
int on_wait_ncf (gpointer, guint, guint32, pgm_pkt_state*, gdouble, guint, gpointer);
int on_wait_data (gpointer, guint, guint32, pgm_pkt_state*, gdouble, guint, gpointer);

double test_basic_rxw (int, int);
double test_jump (int, int);
double test_reverse (int, int);
double test_fill (int, int);


static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
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

//	g_thread_init (NULL);

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
	signal (SIGHUP, SIG_IGN);

	int test_size[] = { 100000, 200000, 100000, 200000, 0 };
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
                        for (int c = 1; c <= test_count; c++)
                        {
                                printf (",%s@%ib/%i", p->name, *p2, c);
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
                                for (int c = 1; c <= test_count; c++)
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

int
on_pgm_data (
	gpointer	data,
	guint		length,
	gpointer	param
	)
{
	g_trace ("on_pgm_data()");
	return 0;
}

int
backoff_state_foreach (
	struct rxw*	r
	)
{
	g_return_val_if_fail (r != NULL, -1);

	GList* list = r->backoff_queue->tail;

	if (!list) return 0;

	while (list)
	{
		GList* next_list_el = list->prev;
		struct rxw_packet* rp = (struct rxw_packet*)list->data;

		rxw_pkt_state_unlink (r, rp);

		/* -- pretend to send nak here -- */

/* nak sent, await ncf */
		g_queue_push_head_link (r->wait_ncf_queue, &rp->link_);

		list = next_list_el;
	}

	return 0;
}

double
test_basic_rxw (
	int	count,
	int	size_per_entry
	)
{
	struct timeval start, now;
	gpointer rxw;
	int i;

	rxw = rxw_init (size_per_entry, count, count, 0, 0, on_pgm_data, NULL);
//	rxw = rxw_init (size_per_entry, 0, count, 0, 0, on_pgm_data, NULL);
	rxw_window_update(rxw, 1, 0);

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? rxw_alloc(rxw) : NULL;

		rxw_push (rxw, entry, size_per_entry, i, 0);
		backoff_state_foreach (rxw);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	rxw_shutdown (rxw);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_jump (
	int	count,
	int	size_per_entry
	)
{
	struct timeval start, now;
	gpointer rxw;
	int i, j;

	rxw = rxw_init (size_per_entry, 2 * count, 2 * count, 0, 0, on_pgm_data, NULL);
	rxw_window_update(rxw, 1, 0);

	gettimeofday(&start, NULL);
	for (i = j = 0; i < count; i++, j+=2)
	{
		char *entry = size_per_entry ? rxw_alloc(rxw) : NULL;

		rxw_push (rxw, entry, size_per_entry, j, 0);
		backoff_state_foreach (rxw);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	rxw_shutdown (rxw);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_reverse (
	int	count,
	int	size_per_entry
	)
{
	struct timeval start, now;
	gpointer rxw;
	int i, j;

	rxw = rxw_init (size_per_entry, count, count, 0, 0, on_pgm_data, NULL);
//	rxw = rxw_init (size_per_entry, 0, count, 0, 0, on_pgm_data, NULL);
	rxw_window_update(rxw, 1, 0);

	gettimeofday(&start, NULL);
	for (i = 0, j = count; i < count; i++)
	{
		char *entry = size_per_entry ? rxw_alloc(rxw) : NULL;

		if (i > 0)
			rxw_push (rxw, entry, size_per_entry, --j, 0);
		else
			rxw_push (rxw, entry, size_per_entry, i, 0);

		backoff_state_foreach (rxw);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	rxw_shutdown (rxw);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

double
test_fill (
	int	count,
	int	size_per_entry
	)
{
	struct timeval start, now;
	gpointer rxw;
	int i;

	rxw = rxw_init (size_per_entry, count+1, count+1, 0, 0, on_pgm_data, NULL);
//	rxw = rxw_init (size_per_entry, 0, count+1, 0, 0, on_pgm_data, NULL);
	rxw_window_update(rxw, 1, 0);

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? rxw_alloc(rxw) : NULL;

		rxw_push (rxw, entry, size_per_entry, i+1, 0);

// immediately send naks
		backoff_state_foreach (rxw);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	rxw_shutdown (rxw);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

/* eof */
