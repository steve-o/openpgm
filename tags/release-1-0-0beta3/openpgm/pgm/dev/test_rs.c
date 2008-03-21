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

static int g_max_tpdu = 1400;	/* minus PGM and FEC overhead */



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

	pgm_time_init();

/* Reed-Solomon code */
	int n = 255;		/* [ k+1 .. 255 ] */
	int k = 64;		/* [ 2 .. 128 ] */

	const gchar source[] = "Ayumi Lee (Hangul: 이 아유미, Japanese name: Ito Ayumi 伊藤亜由美, born August 25, 1984, Tottori Prefecture, Japan[1]), from former Korean girl-group Sugar, was raised in Japan for much of her younger life and is the reason for her Japanese accent, although she is now fluent in both languages. Although Ayumi's name is commonly believed to be Japanese in origin, she has explained that her name is hanja-based.";
	int source_len = strlen (source);		/* chars: g_utf8_strlen() */
	int block_len = 0;
	int source_offset = 0;
	guint8* packet_block[k];
	for (int i = 0; i < k; i++) {
		packet_block[i] = g_malloc0 (g_max_tpdu);

/* fill with source text */
		int packet_offset = 0;
		do {
			int copy_len = MIN( source_len - source_offset, g_max_tpdu - packet_offset );
/* adjust for unicode borders */
			gchar* p = source + source_offset + copy_len;
			if (*p)
			{
				int new_copy_len = g_utf8_find_prev_char (source + source_offset, p + 1) - (source + source_offset);
				if (new_copy_len != copy_len) {
					printf ("ERROR: shuffle on packet border not implemented.\n");
				}
			}
			memcpy (packet_block[i] + packet_offset, source + source_offset, copy_len);
			packet_offset += copy_len;
			source_offset += copy_len;
			if (source_offset >= source_len) source_offset = 0;
			block_len += copy_len;
		} while (packet_offset < g_max_tpdu);
	}

/* parity packet */
	guint8* parity = g_slice_alloc0 (g_max_tpdu);

/* Start Reed-Solomon engine
 *
 * symbol size:			8 bits
 * primitive polynomial:	1 + x^2 + x^3 + x^4 + x^8
 *
 * poly = (1*x^0) + (0*x^1) + (1*x^2) + (1*x^3) + (1*x^4) + (0*x^5) + (0*x^6) + (0*x^7) + (1*x^8)
 *      =  1         0         1         1         1         0         0         0         1
 *      = 101110001
 *      = 0x171 / 0x11D (reversed)
 */

	void* rs;
	pgm_rs_create (&rs, n, k);

/* select random packet to erase:
 *   erasure_index is offset in FEC block of parity packet [k .. n-1]
 *   erasure is offset of erased packet [0 .. k-1]
 */
	int erasure_index = k;
	int erasure = g_random_int_range (0, k);

	pgm_time_t start, end, elapsed;

	printf ("\n"
		"encoding\n"
		"--------\n"
		"\n"
		"GF(2⁸), RS (%i,%i)\n", n, k );

/* test encoding */
	start = pgm_time_update_now();

	pgm_rs_encode (rs, packet_block, erasure_index, parity, g_max_tpdu);

	end = pgm_time_update_now();
	elapsed = end - start;
	printf ("encoding time %" G_GUINT64_FORMAT " us\n", elapsed);

/* test erasure decoding (no errors) */
	puts (	"\n"
		"decoding\n"
		"--------\n" );
	printf ("erased %i packet at %i\n", 1, erasure);

	int offsets[ k ];
	for (int i = 0; i < k; i++)
		offsets[i] = i;
	offsets[erasure] = erasure_index;
	g_slice_free1 (g_max_tpdu, packet_block[erasure]);
	packet_block[erasure] = parity;		/* place parity packet into original data block */

/* test decoding */
	start = pgm_time_update_now();

	int retval = pgm_rs_decode_parity_inline (rs, packet_block, offsets, g_max_tpdu);

	end = pgm_time_update_now();
	elapsed = end - start;
	printf ("decoding time %" G_GUINT64_FORMAT " us\n", elapsed);

/* display final string */
	gchar* final = g_malloc ( (k * g_max_tpdu) + 1 );
	final[0] = 0;
	for (int i = 0; i < k; i++)
		strncat (final, packet_block[i], g_max_tpdu);
	final[ k * g_max_tpdu ] = 0;
	printf ("decoded string:\n[%.15s...]%i cf. %i\n", final, strlen(final), block_len);

	if (strlen(final) == block_len) {
		puts ("Test success.");
	} else {
		puts ("Test failed.");
	}
	g_free (final);

/* clean up */
	for (int i = 0; i < k; i++) {
		g_slice_free1 (g_max_tpdu, packet_block[i]);
		packet_block[i] = NULL;
	}
	
	pgm_rs_destroy (rs);
	pgm_time_destroy();	

	puts ("\n\nfinished.");
	return 0;
}

/* eof */
