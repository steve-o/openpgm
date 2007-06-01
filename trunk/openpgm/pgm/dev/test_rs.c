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
#include <netinet/ip.h>

#include <glib.h>

#include "pgm/reed_solomon.h"
#include "pgm/timer.h"
#include "pgm/packet.h"


/* globals */

static int g_max_tpdu = 1500;



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

/* data block for CCSDS(255,223) */
	guint8 data8[223];
	for (int i = 0; i < G_N_ELEMENTS(data8); i++)
		data8[i] = i % 255;

/* parity block: must be set to 0 */
	guint16 parity[32];
	memset (parity, 0, sizeof(parity));

/* FEC engine:
 *
 * symbol size:			8 bits
 * primitive polynomial:	1 + x^2 + x^3 + x^4 + x^8
 * first consecutive root:	?
 * primitive element for roots:	?
 * number of roots:		32 (2t roots)
 *
 *
 * poly = (1*x^0) + (0*x^1) + (1*x^2) + (1*x^3) + (1*x^4) + (0*x^5) + (0*x^6) + (0*x^7) + (1*x^8)
 *      =  1         0         1         1         1         0         0         0         1
 *      = 101110001
 *      = 0x171
 */

	struct rs_control *rs = init_rs (8, 0x171, 0, 1, G_N_ELEMENTS(parity));

	guint64 start, end, elapsed;

	puts (	"\n"
		"encoding\n"
		"--------\n" );

/* test encoding */
	start = time_update_now();

	encode_rs8 (rs, data8, sizeof(data8), parity, 0);

	end = time_update_now();
	elapsed = end - start;
	printf ("encoding time %lu us\n", elapsed);

	puts (	"\n"
		"errors\n"
		"------\n" );

/* corrupt packets */
	int corrupt = 3;
	for (int i = 0; i < corrupt; i++)
	{
		data8[g_random_int_range (0, sizeof(data8) - 1)] = g_random_int_range (0, 255);
	}
	printf ("corrupted %i symbols (bytes)\n", corrupt);

/* test decoding */

	start = time_update_now();
	int numerr = decode_rs8 (rs, data8, parity, sizeof(data8), NULL, 0, NULL, 0, NULL);
	end = time_update_now();
	elapsed = end - start;
	printf ("decoding time %lu us\n", elapsed);

	printf ("numerr = %i\n", numerr);

/* test erasures if errors didn't fail */
	if (numerr > 0)
	{
		puts (	"\n"
			"erasures\n"
			"--------\n" );
		int erasures = G_N_ELEMENTS(parity);
		int eras_pos[erasures];
		for (int i = 0; i < erasures; i++)
		{
			data8[i] = 0;
			eras_pos[i] = i;
		}
		printf ("erased %i symbols\n", erasures);

/* test decoding part 2 */

		start = time_update_now();
		int numerr = decode_rs8 (rs, data8, parity, sizeof(data8), NULL, erasures, eras_pos, 0, NULL);
		end = time_update_now();
		elapsed = end - start;
		printf ("decoding time %lu us\n", elapsed);

		printf ("numerr = %i\n", numerr);
	}

/* clean up */
	free_rs (rs);
	time_destroy();	

	puts ("\n\nfinished.");
	return 0;
}

/* eof */
