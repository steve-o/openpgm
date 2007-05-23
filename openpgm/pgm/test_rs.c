/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test Reed Solomon Forward Error Correction (FEC).
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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "reed_solomon.h"
#include "timer.h"


/* globals */

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
	puts ("test_rs");

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

	time_init();

/* data block */
	guint8 data8[512];
	for (int i = 0; i < G_N_ELEMENTS(data8); i++)
		data8[i] = i % 255;

/* FEC engine */
	struct rs_control *rs = init_rs (10, 0x409, 0, 1, 6);

/* parity block */
	guint16 parity[6];
	memset (parity, 0, sizeof(parity));

	guint64 start, end, elapsed;


/* test encoding */
	start = time_update_now();

	encode_rs8 (rs, data8, 512, parity, 0);

	end = time_update_now();
	elapsed = end - start;
	printf ("encoding time %lu us\n", elapsed);


/* corrupt packets */
	int corrupt = 3;
	for (int i = 0; i < corrupt; i++)
	{
		data8[g_random_int_range (0, sizeof(data8) - 1)] = g_random_int_range (0, 255);
	}
	printf ("corrupted %i packets\n", corrupt);

/* test decoding */

	start = time_update_now();

	int numerr = decode_rs8 (rs, data8, parity, 512, NULL, 0, NULL, 0, NULL);

	end = time_update_now();
	elapsed = end - start;
	printf ("decoding time %lu us\n", elapsed);

	printf ("\nnumerr = %i\n", numerr);

/* clean up */
	free_rs (rs);
	time_destroy();	

	puts ("finished.");
	return 0;
}

/* eof */
