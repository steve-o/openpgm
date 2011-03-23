/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM packet handling.
 *
 * Copyright (c) 2009-2010 Miru Limited.
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
#include <stdbool.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


/* mock state */

#define PACKET_DEBUG
#include "packet_test.c"


static
struct pgm_sk_buff_t*
generate_raw_pgm (void)
{
	const char source[] = "i am not a string";
	const guint source_len = sizeof(source);
	struct pgm_sk_buff_t* skb;
	GError* err = NULL;

	skb = pgm_alloc_skb (1500);
	skb->sock		= (pgm_sock_t*)0x1;
	skb->tstamp		= 0x1;
	skb->data		= skb->head;
	skb->len		= sizeof(struct pgm_ip) + sizeof(struct pgm_header) + sizeof(struct pgm_data) + source_len;
	skb->tail		= (guint8*)skb->data + skb->len;

/* add IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_hl		= sizeof(struct pgm_ip) / 4;
	iphdr->ip_v		= 4;
	iphdr->ip_tos		= 0;
	iphdr->ip_len		= g_htons (skb->len);
	iphdr->ip_id		= 0;
	iphdr->ip_off		= 0;
	iphdr->ip_ttl		= 16;
	iphdr->ip_p		= IPPROTO_PGM;
	iphdr->ip_sum		= 0;
	iphdr->ip_src.s_addr	= inet_addr ("127.0.0.1");
	iphdr->ip_dst.s_addr	= inet_addr ("127.0.0.2");

/* add PGM header */
	struct pgm_header* pgmhdr = (gpointer)(iphdr + 1);
	pgmhdr->pgm_sport	= g_htons ((guint16)1000);
	pgmhdr->pgm_dport	= g_htons ((guint16)7500);
	pgmhdr->pgm_type	= PGM_ODATA;
	pgmhdr->pgm_options	= 0;
	pgmhdr->pgm_gsi[0]	= 1;
	pgmhdr->pgm_gsi[1]	= 2;
	pgmhdr->pgm_gsi[2]	= 3;
	pgmhdr->pgm_gsi[3]	= 4;
	pgmhdr->pgm_gsi[4]	= 5;
	pgmhdr->pgm_gsi[5]	= 6;
	pgmhdr->pgm_tsdu_length = g_htons (source_len);

/* add ODATA header */
	struct pgm_data* datahdr = (gpointer)(pgmhdr + 1);
	datahdr->data_sqn	= g_htonl ((guint32)0);
	datahdr->data_trail	= g_htonl ((guint32)-1);

/* add payload */
	gpointer data = (gpointer)(datahdr + 1);
	memcpy (data, source, source_len);

/* finally PGM checksum */
	pgmhdr->pgm_checksum 	= 0;
	pgmhdr->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (pgmhdr, sizeof(struct pgm_header) + sizeof(struct pgm_data) + source_len, 0));

/* and IP checksum */
	iphdr->ip_sum		= pgm_inet_checksum (skb->head, skb->len, 0);

	return skb;
}


/* mock functions for external references */

size_t
pgm_pkt_offset (
        const bool                      can_fragment,
        const sa_family_t		pgmcc_family	/* 0 = disable */
        )
{
        return 0;
}

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	gboolean
 *	pgm_print_packet (
 *		gpointer			data,
 *		gsize				len
 *	)
 */

START_TEST (test_print_packet_pass_001)
{
	struct pgm_sk_buff_t* skb = generate_raw_pgm ();
	pgm_print_packet (skb->head, skb->len);
}
END_TEST

START_TEST (test_print_packet_fail_001)
{
	pgm_print_packet (NULL, 0);
	fail ("reached");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_print_packet = tcase_create ("print-packet");
	suite_add_tcase (s, tc_print_packet);
	tcase_add_test (tc_print_packet, test_print_packet_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_print_packet, test_print_packet_fail_001, SIGABRT);
#endif
	return s;
}

static
Suite*
make_master_suite (void)
{
	Suite* s = suite_create ("Master");
	return s;
}

int
main (void)
{
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
