/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test partial checksum combining as used with RDATA acceleration.
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


#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "pgm/pgm.h"
#include "pgm/packet.h"
#include "pgm/checksum.h"


/* globals */

pgm_gsi_t g_gsi;
guint16 g_sport;
guint16 g_dport;


G_GNUC_NORETURN static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	exit (1);
}

void
verify_checksum (
	gpointer	pkt,
	gsize		pkt_length
	)
{
	struct pgm_header* pgm_header = (struct pgm_header*)pkt;
	gsize pgm_length = pkt_length;

	g_assert (pgm_header->pgm_checksum != 0);

	int sum = pgm_header->pgm_checksum;
	pgm_header->pgm_checksum = 0;
	int pgm_sum = pgm_csum_fold (pgm_csum_partial((const char*)pgm_header, pgm_length, 0));
	pgm_header->pgm_checksum = sum;
	g_assert (pgm_sum == sum);

//	g_message ("0x%4.4x === 0x%4.4x", pgm_sum, sum);
}

void
create_odata (
	gpointer	tsdu,
	gsize		tsdu_length,
	gpointer*	odata_pkt,
	gsize*		odata_pkt_length
	)
{
	gsize tpdu_length = pgm_transport_pkt_offset (FALSE) + tsdu_length;
	gpointer pkt = (guint8*)tsdu - pgm_transport_pkt_offset (FALSE);

	struct pgm_header *header = (struct pgm_header*)pkt;
	struct pgm_data *odata = (struct pgm_data*)(header + 1);
	memcpy (header->pgm_gsi, &g_gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = g_sport;
        header->pgm_dport       = g_dport;
        header->pgm_type        = PGM_ODATA;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = g_htons (tsdu_length);

/* ODATA */
        odata->data_sqn         = g_htonl (1000);
        odata->data_trail       = g_htonl (999);

        header->pgm_checksum    = 0;
        gsize pgm_header_len    = (guint8*)(odata + 1) - (guint8*)header;
        guint32 unfolded_header = pgm_csum_partial (header, pgm_header_len, 0);
	guint32 unfolded_odata  = pgm_csum_partial ((guint8*)(odata + 1), tsdu_length, 0);
	header->pgm_checksum    = pgm_csum_fold (pgm_csum_block_add (unfolded_header, unfolded_odata, pgm_header_len));

	verify_checksum (pkt, tpdu_length);

/* save unfolded odata for retransmissions */
        *(guint32*)(void*)&((struct pgm_header*)pkt)->pgm_sport = unfolded_odata;

	*odata_pkt = pkt;
	*odata_pkt_length = tpdu_length;
}

void
create_rdata (
	gpointer*	odata_pkt,
	gsize		odata_pkt_length
	)
{
/* update previous odata/rdata contents */
        struct pgm_header* header = (struct pgm_header*)odata_pkt;
        struct pgm_data* rdata    = (struct pgm_data*)(header + 1);
        header->pgm_type          = PGM_RDATA;

/* RDATA */
        rdata->data_trail       = g_htonl (9999);	/* random trail */

	guint32 unfolded_odata  = 0;
	unfolded_odata  = *(guint32*)(void*)&header->pgm_sport;
        header->pgm_sport       = g_sport;
        header->pgm_dport       = g_dport;

        header->pgm_checksum    = 0;

        gsize pgm_header_len    = odata_pkt_length - g_ntohs(header->pgm_tsdu_length);
        guint32 unfolded_header = pgm_csum_partial (header, pgm_header_len, 0);
	header->pgm_checksum    = pgm_csum_fold (pgm_csum_block_add (unfolded_header, unfolded_odata, pgm_header_len)); 

	verify_checksum (odata_pkt, odata_pkt_length);

/* re-save unfolded payload for further retransmissions */
	*(guint32*)(void*)&header->pgm_sport = unfolded_odata;
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	puts ("test_partial_csum");

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

	int e = pgm_create_md5_gsi (&g_gsi);
	g_assert (e == 0);

	g_sport = g_htons(7500);
	g_dport = g_htons(7501);

	gpointer tpdu = g_malloc (1500); /* max tpdu */

	for (gsize tsdu_length = 1; tsdu_length <= 1400; tsdu_length++)
	{
		g_message ("tsdu length %" G_GSIZE_FORMAT, tsdu_length);
		for (int i = 0; i < 100; i++)
		{
			gpointer tsdu;
	
/* fill payload with random data */
			tsdu = (guint8*)tpdu + pgm_transport_pkt_offset (FALSE);
			guint8* p = tsdu;
			for (gsize j = 0; j < tsdu_length; j++) {
				*p++ = g_random_int_range (0, G_MAXUINT8);
			}

			gpointer odata_pkt;
			gsize	 odata_pkt_length;

			create_odata(tsdu, tsdu_length, &odata_pkt, &odata_pkt_length);

			for (int k = 0; k < 100; k++) {
				create_rdata(odata_pkt, odata_pkt_length);
			}
		}
	}

	g_free (tpdu);

	puts ("finished.");
	return 0;
}

/* eof */
