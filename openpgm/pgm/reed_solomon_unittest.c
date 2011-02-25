/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for Reed-Solomon forward error correction based on Vandermonde matrices.
 *
 * Copyright (c) 2009 Miru Limited.
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
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


/* mock state */


/* mock functions for external references */

size_t
pgm_transport_pkt_offset2 (
        const bool                      can_fragment,
        const bool                      use_pgmcc
        )
{
        return 0;
}

#define REED_SOLOMON_DEBUG
#include "reed_solomon.c"

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	void
 *	pgm_rs_create (
 *		pgm_rs_t*		rs,
 *		const uint8_t		n,
 *		const uint8_t		k
 *	)
 */

START_TEST (test_create_pass_001)
{
	pgm_rs_t rs;
	pgm_rs_create (&rs, 255, 16);
}
END_TEST

START_TEST (test_create_fail_001)
{
	pgm_rs_create (NULL, 255, 16);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_rs_destroy (
 *		pgm_rs_t*		rs,
 *	)
 */

START_TEST (test_destroy_pass_001)
{
	pgm_rs_t rs;
	pgm_rs_create (&rs, 255, 16);
	pgm_rs_destroy (&rs);
}
END_TEST

START_TEST (test_destroy_fail_001)
{
	pgm_rs_destroy (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_rs_encode (
 *		pgm_rs_t*		rs,
 *		const pgm_gf8_t**	src,
 *		const uint8_t		offset,
 *		pgm_gf8_t*		dst,
 *		const uint16_t		len
 *	)
 */

START_TEST (test_encode_pass_001)
{
	const gchar source[] = "i am not a string";
	const guint16 source_len = strlen (source);
	pgm_rs_t rs;
	const guint8 k = source_len;
	const guint8 parity_index = k;
	const guint16 packet_len = 100;
	pgm_gf8_t* source_packets[k];
	pgm_gf8_t* parity_packet = g_malloc0 (packet_len);
	pgm_rs_create (&rs, 255, k);
	for (unsigned i = 0; i < k; i++) {
		source_packets[i] = g_malloc0 (packet_len);
		source_packets[i][0] = source[i];
		g_message ("packet#%2.2d: 0x%2.2x '%c'", i, source[i], source[i]);
	}
	pgm_rs_encode (&rs, (const pgm_gf8_t**)source_packets, parity_index, parity_packet, packet_len);
	g_message ("parity-packet: %2.2x", parity_packet[0]);
	pgm_rs_destroy (&rs);
}
END_TEST

START_TEST (test_encode_fail_001)
{
	pgm_rs_encode (NULL, NULL, 0, NULL, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_rs_decode_parity_inline (
 *		pgm_rs_t*		rs,
 *		pgm_gf8_t**		block,
 *		const uint8_t*		offsets,
 *		const uint16_t		len
 *	)
 */

START_TEST (test_decode_parity_inline_pass_001)
{
	const gchar source[] = "i am not a string";
	const guint16 source_len = strlen (source);
	pgm_rs_t rs;
	const guint8 k = source_len;
	const guint8 parity_index = k;
	const guint16 packet_len = 100;
	pgm_gf8_t* source_packets[k];
	pgm_gf8_t* parity_packet = g_malloc0 (packet_len);
	pgm_rs_create (&rs, 255, k);
	for (unsigned i = 0; i < k; i++) {
		source_packets[i] = g_malloc0 (packet_len);
		source_packets[i][0] = source[i];
	}
	pgm_rs_encode (&rs, (const pgm_gf8_t**)source_packets, parity_index, parity_packet, packet_len);
/* simulate error occuring at index #3 */
	const guint erased_index = 3;
	source_packets[erased_index][0] = 'X';
	for (unsigned i = 0; i < k; i++) {
		g_message ("damaged-packet#%2.2d: 0x%2.2x '%c'",
			   i, source_packets[i][0], source_packets[i][0]);
	}
	guint8 offsets[k];
	for (unsigned i = 0; i < k; i++)
		offsets[i] = i;
/* erased offset now points to parity packet at k */
	offsets[erased_index] = parity_index;
/* move parity inline */
	g_free (source_packets[erased_index]);
	source_packets[erased_index] = parity_packet;
	pgm_rs_decode_parity_inline (&rs, source_packets, offsets, packet_len);
	pgm_rs_destroy (&rs);
	for (unsigned i = 0; i < k; i++) {
		g_message ("repaired-packet#%2.2d: 0x%2.2x '%c'",
			   i, source_packets[i][0], source_packets[i][0]);
	}
}
END_TEST

START_TEST (test_decode_parity_inline_fail_001)
{
	pgm_rs_decode_parity_inline (NULL, NULL, NULL, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_rs_decode_parity_appended (
 *		pgm_rs_t*		rs,
 *		pgm_gf8_t*		block,
 *		const uint8_t*		offsets,
 *		const uint16_t		len
 *	)
 */

START_TEST (test_decode_parity_appended_pass_001)
{
	const gchar source[] = "i am not a string";
	const guint16 source_len = strlen (source);
	pgm_rs_t rs;
	const guint8 k = source_len;
	const guint8 parity_index = k;
	const guint16 packet_len = 100;
	pgm_gf8_t* source_packets[k+1];	/* include 1 appended parity packet */
	pgm_gf8_t* parity_packet = g_malloc0 (packet_len);
	pgm_rs_create (&rs, 255, k);
	for (unsigned i = 0; i < k; i++) {
		source_packets[i] = g_malloc0 (packet_len);
		source_packets[i][0] = source[i];
	}
	pgm_rs_encode (&rs, (const pgm_gf8_t**)source_packets, parity_index, parity_packet, packet_len);
/* simulate error occuring at index #3 */
	const guint erased_index = 3;
	source_packets[erased_index][0] = 'X';
	for (unsigned i = 0; i < k; i++) {
		g_message ("damaged-packet#%2.2d: 0x%2.2x '%c'",
			   i, source_packets[i][0], source_packets[i][0]);
	}
	guint8 offsets[k];
	for (unsigned i = 0; i < k; i++)
		offsets[i] = i;
/* erased offset now points to parity packet at k */
	offsets[erased_index] = parity_index;
/* erase damaged packet */
	memset (source_packets[erased_index], 0, packet_len);
/* append parity to source packet block */
	source_packets[parity_index] = parity_packet;
	pgm_rs_decode_parity_appended (&rs, source_packets, offsets, packet_len);
	pgm_rs_destroy (&rs);
	for (unsigned i = 0; i < k; i++) {
		g_message ("repaired-packet#%2.2d: 0x%2.2x '%c'",
			   i, source_packets[i][0], source_packets[i][0]);
	}
}
END_TEST

START_TEST (test_decode_parity_appended_fail_001)
{
	pgm_rs_decode_parity_appended (NULL, NULL, NULL, 0);
	fail ("reached");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_create = tcase_create ("create");
	suite_add_tcase (s, tc_create);
	tcase_add_test (tc_create, test_create_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_create, test_create_fail_001, SIGABRT);
#endif

	TCase* tc_destroy = tcase_create ("destroy");
	suite_add_tcase (s, tc_destroy);
	tcase_add_test (tc_destroy, test_destroy_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_destroy, test_destroy_fail_001, SIGABRT);
#endif

	TCase* tc_encode = tcase_create ("encode");
	suite_add_tcase (s, tc_encode);
	tcase_add_test (tc_encode, test_encode_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_encode, test_encode_fail_001, SIGABRT);
#endif

	TCase* tc_decode_parity_inline = tcase_create ("decode-parity-inline");
	suite_add_tcase (s, tc_decode_parity_inline);
	tcase_add_test (tc_decode_parity_inline, test_decode_parity_inline_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_decode_parity_inline, test_decode_parity_inline_fail_001, SIGABRT);
#endif

	TCase* tc_decode_parity_appended = tcase_create ("decode-parity-appended");
	suite_add_tcase (s, tc_decode_parity_appended);
	tcase_add_test (tc_decode_parity_appended, test_decode_parity_appended_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_decode_parity_appended, test_decode_parity_appended_fail_001, SIGABRT);
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
