/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for receive window.
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


#include <stdio.h>
#include <stdlib.h>
#include <check.h>

#include <pgm/transport.h>
#include <pgm/timer.h>


/* mock global */

static pgm_time_t mock_pgm_time_now = 0x1;

/* mock functions for external references */

gchar*
mock_pgm_print_tsi (
	const pgm_tsi_t*	tsi
	)
{
	const guint8* gsi = (const guint8*)tsi;
	g_return_val_if_fail (tsi != NULL, NULL);
	static char buf[PGM_TSISTRLEN];
	snprintf (buf, sizeof(buf), "%i.%i.%i.%i.%i.%i.%i",
		  gsi[0], gsi[1], gsi[2], gsi[3], gsi[4], gsi[5], g_ntohs (tsi->sport));
	return buf;
}

static
int
mock_pgm_rs_decode_parity_appended (
	gpointer		rs,
	void**			block,
	guint*			offsets,
	gsize			len
	)
{
	return 0;
}

#define pgm_time_now	mock_pgm_time_now
#define pgm_print_tsi	mock_pgm_print_tsi
#define pgm_rs_decode_parity_appended	mock_pgm_rs_decode_parity_appended

#define RXW_DEBUG
#include "rxwi.c"


/* generate valid skb, data pointer pointing to PGM payload
 */
static
struct pgm_sk_buff_t*
generate_valid_skb (void)
{
	const pgm_tsi_t tsi = { { 200, 202, 203, 204, 205, 206 }, 2000 };
	const guint16 tsdu_length = 1000;
	const guint16 header_length = sizeof(struct pgm_header) + sizeof(struct pgm_data);
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (1500);
	memcpy (&skb->tsi, &tsi, sizeof(tsi));
/* fake but valid transport and timestamp */
	skb->transport = (pgm_transport_t*)0x1;
	skb->tstamp = pgm_time_now;
/* header */
	pgm_skb_reserve (skb, header_length);
	memset (skb->head, 0, header_length);
	skb->pgm_header = (struct pgm_header*)skb->head;
	skb->pgm_data   = (struct pgm_data*)(skb->pgm_header + 1);
	skb->pgm_header->pgm_type = PGM_ODATA;
	skb->pgm_header->pgm_tsdu_length = g_htons (tsdu_length);
/* DATA */
	pgm_skb_put (skb, tsdu_length);
	return skb;
}

/* target:
 *	pgm_rxw_t*
 *	pgm_rxw_init (
 *		const pgm_tsi_t*	tsi,
 *		const guint16		tpdu_size,
 *		const guint32		sqns,
 *		const guint		secs,
 *		const guint		max_rte
 *		)
 */

/* vanilla sequence count window */
START_TEST (test_init_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_init (&tsi, 1500, 100, 0, 0));
}
END_TEST

/* vanilla time based window */
START_TEST (test_init_pass_002)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_init (&tsi, 1500, 0, 60, 800000));
}
END_TEST

/* jumbo frame */
START_TEST (test_init_pass_003)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_init (&tsi, 9000, 0, 60, 800000));
}
END_TEST

/* max frame */
START_TEST (test_init_pass_004)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_init (&tsi, UINT16_MAX, 0, 60, 800000));
}
END_TEST

/* invalid tsi pointer */
START_TEST (test_init_fail_001)
{
	pgm_rxw_init (NULL, 1500, 100, 0, 0);
	fail ();
}
END_TEST

/* invalid tpdu size */
START_TEST (test_init_fail_002)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_init (&tsi, 0, 100, 0, 0));
}
END_TEST

START_TEST (test_init_fail_003)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_init (&tsi, 0, 0, 60, 800000);
	fail ();
}
END_TEST

/* no specified sequence count or time value */
START_TEST (test_init_fail_004)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_init (&tsi, 0, 0, 0, 800000);
	fail ();
}
END_TEST

/* no specified rate */
START_TEST (test_init_fail_005)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_init (&tsi, 0, 0, 60, 0);
	fail ();
}
END_TEST

/* all invalid */
START_TEST (test_init_fail_006)
{
	pgm_rxw_init (NULL, 0, 0, 0, 0);
	fail ();
}
END_TEST

/* target:
 *	void
 *	pgm_rxw_shutdown (
 *		pgm_rxw_t* const	window
 *		)
 */

START_TEST (test_shutdown_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	pgm_rxw_shutdown (window);
}
END_TEST

START_TEST (test_shutdown_fail_001)
{
	pgm_rxw_shutdown (NULL);
	fail ();
}
END_TEST

/* target:
 *	void
 *	pgm_rxw_add (
 *		pgm_rxw_t* const		window,
 *		struct pgm_sk_buff_t* const	skb,
 *		const pgm_time_t		nak_rb_expiry
 *		)
 * failures raise assert errors and stop process execution.
 */

START_TEST (test_add_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	const pgm_time_t nak_rb_expiry = 1;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, nak_rb_expiry));
	pgm_rxw_shutdown (window);
}
END_TEST

/* null skb */
START_TEST (test_add_fail_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	const pgm_time_t nak_rb_expiry = 1;
	pgm_rxw_add (window, NULL, nak_rb_expiry);
	fail ();
}
END_TEST

/* null window */
START_TEST (test_add_fail_002)
{
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	const pgm_time_t nak_rb_expiry = 1;
	pgm_rxw_add (NULL, skb, nak_rb_expiry);
	fail ();
}
END_TEST

/* null skb content */
START_TEST (test_add_fail_003)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	char buffer[1500];
	memset (buffer, 0, sizeof(buffer));
	const pgm_time_t nak_rb_expiry = 1;
	pgm_rxw_add (window, (struct pgm_sk_buff_t*)buffer, nak_rb_expiry);
	fail ();
}
END_TEST

/* 0 nak_rb_expiry */
START_TEST (test_add_fail_004)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_rxw_add (window, skb, 0);
	fail ();
}
END_TEST

/* target:
 *	struct pgm_sk_buff_t*
 *	pgm_rxw_peek (
 *		pgm_rxw_t* const	window,
 *		const guint32		sequence
 *		)
 */

START_TEST (test_peek_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	fail_unless (NULL == pgm_rxw_peek (window, 0));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t nak_rb_expiry = 1;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, nak_rb_expiry));
	fail_unless (skb == pgm_rxw_peek (window, 0));
	fail_unless (NULL == pgm_rxw_peek (window, 1));
	fail_unless (NULL == pgm_rxw_peek (window, -1));
	pgm_rxw_shutdown (window);
}
END_TEST

/* null window */
START_TEST (test_peek_fail_001)
{
	pgm_rxw_peek (NULL, 0);
	fail ();
}
END_TEST

/** inline function tests **/
/* pgm_rxw_max_length () 
 */
START_TEST (test_max_length_pass_001)
{
	const guint window_length = 100;
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, window_length, 0, 0);
	fail_if (NULL == window);
	fail_unless (window_length == pgm_rxw_max_length (window));
	pgm_rxw_shutdown (window);
}
END_TEST

START_TEST (test_max_length_fail_001)
{
	pgm_rxw_max_length (NULL);
	fail ();
}
END_TEST

/* pgm_rxw_length () 
 */
START_TEST (test_length_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	fail_unless (0 == pgm_rxw_length (window));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t nak_rb_expiry = 1;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, nak_rb_expiry));
	fail_unless (1 == pgm_rxw_length (window));
	pgm_rxw_shutdown (window);
}
END_TEST

START_TEST (test_length_fail_001)
{
	pgm_rxw_length (NULL);
	fail ();
}
END_TEST

/* pgm_rxw_size () 
 */
START_TEST (test_size_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	fail_unless (0 == pgm_rxw_size (window));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t nak_rb_expiry = 1;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, nak_rb_expiry));
	fail_unless (1000 == pgm_rxw_size (window));
	pgm_rxw_shutdown (window);
}
END_TEST

START_TEST (test_size_fail_001)
{
	pgm_rxw_size (NULL);
	fail ();
}
END_TEST

/* pgm_rxw_is_empty
 */
START_TEST (test_is_empty_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	fail_unless (pgm_rxw_is_empty (window));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t nak_rb_expiry = 1;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, nak_rb_expiry));
	fail_if (pgm_rxw_is_empty (window));
	pgm_rxw_shutdown (window);
}
END_TEST

START_TEST (test_is_empty_fail_001)
{
	pgm_rxw_is_empty (NULL);
	fail ();
}
END_TEST

/* pgm_rxw_is_full
 */
START_TEST (test_is_full_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_init (&tsi, 1500, 1, 0, 0);
	fail_if (NULL == window);
	fail_if (pgm_rxw_is_full (window));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t nak_rb_expiry = 1;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, nak_rb_expiry));
	fail_unless (pgm_rxw_is_full (window));
	pgm_rxw_shutdown (window);
}
END_TEST

START_TEST (test_is_full_fail_001)
{
	pgm_rxw_is_full (NULL);
	fail ();
}
END_TEST

static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_init = tcase_create ("init");
	suite_add_tcase (s, tc_init);
	tcase_add_test (tc_init, test_init_pass_001);
	tcase_add_test (tc_init, test_init_pass_002);
	tcase_add_test (tc_init, test_init_pass_003);
	tcase_add_test (tc_init, test_init_pass_004);
	tcase_add_test_raise_signal (tc_init, test_init_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_init, test_init_fail_002, SIGABRT);
	tcase_add_test_raise_signal (tc_init, test_init_fail_003, SIGABRT);
	tcase_add_test_raise_signal (tc_init, test_init_fail_004, SIGABRT);

	TCase* tc_shutdown = tcase_create ("shutdown");
	suite_add_tcase (s, tc_shutdown);
	tcase_add_test (tc_shutdown, test_shutdown_pass_001);
	tcase_add_test_raise_signal (tc_shutdown, test_shutdown_fail_001, SIGABRT);

	TCase* tc_add = tcase_create ("add");
	suite_add_tcase (s, tc_add);
	tcase_add_test (tc_add, test_add_pass_001);
	tcase_add_test_raise_signal (tc_add, test_add_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_add, test_add_fail_002, SIGABRT);
	tcase_add_test_raise_signal (tc_add, test_add_fail_003, SIGABRT);

	TCase* tc_peek = tcase_create ("peek");
	suite_add_tcase (s, tc_peek);
	tcase_add_test (tc_peek, test_peek_pass_001);
	tcase_add_test_raise_signal (tc_peek, test_peek_fail_001, SIGABRT);

	TCase* tc_max_length = tcase_create ("max-length");
	suite_add_tcase (s, tc_max_length);
	tcase_add_test (tc_max_length, test_max_length_pass_001);
	tcase_add_test_raise_signal (tc_max_length, test_max_length_fail_001, SIGABRT);

	TCase* tc_length = tcase_create ("length");
	suite_add_tcase (s, tc_length);
	tcase_add_test (tc_length, test_length_pass_001);
	tcase_add_test_raise_signal (tc_length, test_length_fail_001, SIGABRT);

	TCase* tc_size = tcase_create ("size");
	suite_add_tcase (s, tc_size);
	tcase_add_test (tc_size, test_size_pass_001);
	tcase_add_test_raise_signal (tc_size, test_size_fail_001, SIGABRT);

	TCase* tc_is_empty = tcase_create ("is-empty");
	suite_add_tcase (s, tc_is_empty);
	tcase_add_test (tc_is_empty, test_is_empty_pass_001);
	tcase_add_test_raise_signal (tc_is_empty, test_is_empty_fail_001, SIGABRT);

	TCase* tc_is_full = tcase_create ("is-full");
	suite_add_tcase (s, tc_is_full);
	tcase_add_test (tc_is_full, test_is_full_pass_001);
	tcase_add_test_raise_signal (tc_is_full, test_is_full_fail_001, SIGABRT);

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
