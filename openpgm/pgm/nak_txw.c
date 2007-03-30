/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test sequential packet retrieval from a basic transmit window 
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

#include "txw.h"


struct tests {
	double (*test_func)(int, int);
	char*	name;
};


/* globals */

double test_basic_txw (int, int);

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
	puts ("nak_txw");

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

/* setup signal handlers */
	signal(SIGHUP, SIG_IGN);

	int test_size[] = { 10, 20, 100, 200, 1000, 2000, 10000, 20000, 100000, 200000, 0 };
	int test_payload[] = { 9000, 1500, 0 };
	struct tests tests[] = {
			{ test_basic_txw, "basic txw" },
			{ NULL, NULL }
			};
	struct tests* p2;

	p2 = tests;
	do {
		int *p3 = test_payload;

		do {
			printf ("%s@%i bytes\n", p2->name, *p3);

			int *p = test_size; do { printf ("%i,", *p++); } while (*p); p = test_size;
			putchar ('\n');

			do {
				p2->test_func (*p, *p3);
				double result = p2->test_func (*p, *p3);
				printf ("%g,", result);
				fflush (stdout);
			} while (*(++p));
			putchar ('\n');

		} while (*(++p3));

	} while ((++p2)->name);
		
/* with payload */

	puts ("finished.");
	return 0;
}

double
test_basic_txw (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	gpointer txw;
	int i;

	txw = txw_init (size_per_entry, 0, count, 0, 0);

/* fill window up */
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? g_slice_alloc(size_per_entry) : NULL;
		txw_push (txw, entry, size_per_entry);
	}

/* iterate through entire window requesting packet data */
	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *packet;
		int length;

		txw_get (txw, i, &packet, &length);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	txw_shutdown (txw);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

/* eof */
