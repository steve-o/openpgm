/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test alloc on a basic transmit window 
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

#include "txwi.h"


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
	puts ("basic_txw");

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

	int test_size[] = { 100000, 200000, 100000, 200000, 0 };
//	int test_size[] = { 10, 0 };
	int test_payload[] = { /*9000,*/ 1500, 0 };
	struct tests tests[] = {
			{ test_basic_txw, "basic txw" },
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
                                for (int c = 1; c <= 3; c++)
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

double
test_basic_txw (
		int count,
		int size_per_entry
		)
{
	struct timeval start, now;
	gpointer txw;
	int i;

	txw = txw_init (size_per_entry, count, count, 0, 0);

	gettimeofday(&start, NULL);
	for (i = 0; i < count; i++)
	{
		char *entry = size_per_entry ? txw_alloc(txw) : NULL;

		txw_push (txw, entry, size_per_entry);
	}
	gettimeofday(&now, NULL);

        double secs = (now.tv_sec - start.tv_sec) + ( (now.tv_usec - start.tv_usec) / 1000.0 / 1000.0 );

	txw_shutdown (txw);

	return (secs * 1000.0 * 1000.0) / (double)count;
}

/* eof */
