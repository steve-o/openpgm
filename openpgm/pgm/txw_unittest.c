/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for transmit window.
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
#include <check.h>
#include <glib.h>

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


/* mock global */

#define pgm_histogram_add		mock_pgm_histogram_add
#define pgm_rs_create			mock_pgm_rs_create
#define pgm_rs_destroy			mock_pgm_rs_destroy
#define pgm_rs_encode			mock_pgm_rs_encode
#define pgm_compat_csum_partial		mock_pgm_compat_csum_partial
#define pgm_histogram_init		mock_pgm_histogram_init

#define TXW_DEBUG
#include "txw.c"


/** reed-solomon module */
void
mock_pgm_rs_create (
	pgm_rs_t*		rs,
	uint8_t			n,
	uint8_t			k
	)
{
}

void
mock_pgm_rs_destroy (
	pgm_rs_t*		rs
	)
{
}

void
mock_pgm_rs_encode(
	pgm_rs_t*		rs,
	const pgm_gf8_t**	src,
	const uint8_t		offset,
	pgm_gf8_t*		dst,
	const uint16_t		len
        )
{
}

/** checksum module */
uint32_t
mock_pgm_compat_csum_partial (
	const void*		addr,
	uint16_t		len,
	uint32_t		csum
	)
{
	return 0x0;
}

void
mock_pgm_histogram_init (
	pgm_histogram_t*	histogram
	)
{
}

void
mock_pgm_histogram_add (
	pgm_histogram_t*	histogram,
	int			value
	)
{
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

/* generate valid skb, data pointer pointing to PGM payload
 */
static
struct pgm_sk_buff_t*
generate_valid_skb (void)
{
	const guint16 tsdu_length = 1000;
	const guint16 header_length = sizeof(struct pgm_header) + sizeof(struct pgm_data);
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (1500);
/* fake but valid transport and timestamp */
	skb->sock = (pgm_sock_t*)0x1;
	skb->tstamp = 1;
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
 *	pgm_txw_t*
 *	pgm_txw_create (
 *		const pgm_tsi_t* const	tsi,
 *		const guint16		tpdu_size,
 *		const guint32		sqns,
 *		const guint		secs,
 *		const guint		max_rte,
 *		const gboolean		use_fec,
 *		const guint		rs_n,
 *		const guint		rs_k
 *		)
 */

/* vanilla sequence count window */
START_TEST (test_create_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0), "create failed");
}
END_TEST

/* vanilla time based window */
START_TEST (test_create_pass_002)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_txw_create (&tsi, 1500, 0, 60, 800000, FALSE, 0, 0), "create failed");
}
END_TEST

/* jumbo frame */
START_TEST (test_create_pass_003)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_txw_create (&tsi, 9000, 0, 60, 800000, FALSE, 0, 0), "create failed");
}
END_TEST

/* max frame */
START_TEST (test_create_pass_004)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_txw_create (&tsi, UINT16_MAX, 0, 60, 800000, FALSE, 0, 0), "create failed");
}
END_TEST

/* invalid tpdu size */
START_TEST (test_create_fail_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const pgm_txw_t* window = pgm_txw_create (&tsi, 0, 0, 60, 800000, FALSE, 0, 0);
	fail ("reached");
}
END_TEST

/* no specified sequence count or time value */
START_TEST (test_create_fail_002)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const pgm_txw_t* window = pgm_txw_create (&tsi, 0, 0, 0, 800000, FALSE, 0, 0);
	fail ("reached");
}
END_TEST

/* no specified rate */
START_TEST (test_create_fail_003)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const pgm_txw_t* window = pgm_txw_create (&tsi, 0, 0, 60, 0, FALSE, 0, 0);
	fail ("reached");
}
END_TEST

/* all invalid */
START_TEST (test_create_fail_004)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const pgm_txw_t* window = pgm_txw_create (NULL, 0, 0, 0, 0, FALSE, 0, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_txw_shutdown (
 *		pgm_txw_t* const	window
 *		)
 */

START_TEST (test_shutdown_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_shutdown_fail_001)
{
	pgm_txw_shutdown (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_txw_add (
 *		pgm_txw_t* const		window,
 *		struct pgm_sk_buff_t* const	skb
 *		)
 * failures raise assert errors and stop process execution.
 */

START_TEST (test_add_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	pgm_txw_shutdown (window);
}
END_TEST

/* null skb */
START_TEST (test_add_fail_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_txw_add (window, NULL);
	fail ("reached");
}
END_TEST

/* null window */
START_TEST (test_add_fail_002)
{
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (NULL, skb);
	fail ("reached");
}
END_TEST

/* null skb content */
START_TEST (test_add_fail_003)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	char buffer[1500];
	memset (buffer, 0, sizeof(buffer));
	pgm_txw_add (window, (struct pgm_sk_buff_t*)buffer);
	fail ("reached");
}
END_TEST

/* target:
 *	struct pgm_sk_buff_t*
 *	pgm_txw_peek (
 *		pgm_txw_t* const	window,
 *		const guint32		sequence
 *		)
 */

START_TEST (test_peek_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_unless (skb == pgm_txw_peek (window, window->trail), "peek failed");
	pgm_txw_shutdown (window);
}
END_TEST

/* null window */
START_TEST (test_peek_fail_001)
{
	const struct pgm_sk_buff_t* skb = pgm_txw_peek (NULL, 0);
	fail ("reached");
}
END_TEST

/* empty window */
START_TEST (test_peek_fail_002)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (NULL == pgm_txw_peek (window, window->trail), "peek failed");
	pgm_txw_shutdown (window);
}
END_TEST

/** inline function tests **/
/* pgm_txw_max_length () 
 */
START_TEST (test_max_length_pass_001)
{
	const guint window_length = 100;
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, window_length, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (window_length == pgm_txw_max_length (window), "max_length failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_max_length_fail_001)
{
	const size_t answer = pgm_txw_max_length (NULL);
	fail ("reached");
}
END_TEST

/* pgm_txw_length () 
 */
START_TEST (test_length_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (0 == pgm_txw_length (window), "length failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_unless (1 == pgm_txw_length (window), "length failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_length_fail_001)
{
	const uint32_t answer = pgm_txw_length (NULL);
	fail ("reached");
}
END_TEST

/* pgm_txw_size () 
 */
START_TEST (test_size_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (0 == pgm_txw_size (window), "size failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_unless (1000 == pgm_txw_size (window), "size failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_size_fail_001)
{
	const size_t answer = pgm_txw_size (NULL);
	fail ("reached");
}
END_TEST

/* pgm_txw_is_empty
 */
START_TEST (test_is_empty_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (pgm_txw_is_empty (window), "is_empty failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_if (pgm_txw_is_empty (window), "is_empty failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_is_empty_fail_001)
{
	const bool answer = pgm_txw_is_empty (NULL);
	fail ("reached");
}
END_TEST

/* pgm_txw_is_full
 */
START_TEST (test_is_full_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 1, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_if (pgm_txw_is_full (window), "is_full failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_unless (pgm_txw_is_full (window), "is_full failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_is_full_fail_001)
{
	const bool answer = pgm_txw_is_full (NULL);
	fail ("reached");
}
END_TEST

/* pgm_txw_lead
 */
START_TEST (test_lead_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	guint32 lead = pgm_txw_lead (window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_unless (lead + 1 == pgm_txw_lead (window), "lead failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_lead_fail_001)
{
	const uint32_t answer = pgm_txw_lead (NULL);
	fail ("reached");
}
END_TEST

/* pgm_txw_next_lead
 */
START_TEST (test_next_lead_pass_001)
{
	const guint window_length = 100;
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, window_length, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	guint32 next_lead = pgm_txw_next_lead (window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_unless (next_lead == pgm_txw_lead (window), "lead failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_next_lead_fail_001)
{
	const uint32_t answer = pgm_txw_next_lead (NULL);
	fail ("reached");
}
END_TEST

/* pgm_txw_trail
 */
START_TEST (test_trail_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 1, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
/* does not advance with adding skb */
	guint32 trail = pgm_txw_trail (window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_unless (trail == pgm_txw_trail (window), "trail failed");
/* does advance when filling up window */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_if (trail == pgm_txw_trail (window), "trail failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_trail_fail_001)
{
	const uint32_t answer = pgm_txw_trail (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	bool
 *	pgm_txw_retransmit_push (
 *		pgm_txw_t* const	window,
 *		const uint32_t		sequence,
 *		const bool		is_parity,
 *		const uint8_t		tg_sqn_shift
 *		)
 */

START_TEST (test_retransmit_push_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
/* empty window invalidates all requests */
	fail_unless (FALSE == pgm_txw_retransmit_push (window, window->trail, FALSE, 0), "retransmit_push failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
/* first request */
	fail_unless (TRUE == pgm_txw_retransmit_push (window, window->trail, FALSE, 0), "retransmit_push failed");
/* second request eliminated */
	fail_unless (FALSE == pgm_txw_retransmit_push (window, window->trail, FALSE, 0), "retransmit_push failed");
	pgm_txw_shutdown (window);
}
END_TEST

START_TEST (test_retransmit_push_fail_001)
{
	const bool answer = pgm_txw_retransmit_push (NULL, 0, FALSE, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	struct pgm_sk_buff_t*
 *	pgm_txw_retransmit_try_peek (
 *		pgm_txw_t* const	window
 *		)
 */

START_TEST (test_retransmit_try_peek_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_unless (1 == pgm_txw_retransmit_push (window, window->trail, FALSE, 0), "retransmit_push failed");
	fail_unless (NULL != pgm_txw_retransmit_try_peek (window), "retransmit_try_peek failed");
	pgm_txw_shutdown (window);
}
END_TEST

/* null window */
START_TEST (test_retransmit_try_peek_fail_001)
{
	const struct pgm_sk_buff_t* skb = pgm_txw_retransmit_try_peek (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_txw_retransmit_remove_head (
 *		pgm_txw_t* const	window
 *		)
 */

START_TEST (test_retransmit_remove_head_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	pgm_txw_add (window, skb);
	fail_unless (1 == pgm_txw_retransmit_push (window, window->trail, FALSE, 0), "retransmit_push failed");
	fail_unless (NULL != pgm_txw_retransmit_try_peek (window), "retransmit_try_peek failed");
	pgm_txw_retransmit_remove_head (window);
	pgm_txw_shutdown (window);
}
END_TEST

/* null window */
START_TEST (test_retransmit_remove_head_fail_001)
{
	pgm_txw_retransmit_remove_head (NULL);
	fail ("reached");
}
END_TEST

/* empty retransmit queue */
START_TEST (test_retransmit_remove_head_fail_002)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_txw_t* window = pgm_txw_create (&tsi, 0, 100, 0, 0, FALSE, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_txw_retransmit_remove_head (window);
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
	tcase_add_test (tc_create, test_create_pass_002);
	tcase_add_test (tc_create, test_create_pass_003);
	tcase_add_test (tc_create, test_create_pass_004);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_create, test_create_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_create, test_create_fail_002, SIGABRT);
	tcase_add_test_raise_signal (tc_create, test_create_fail_003, SIGABRT);
	tcase_add_test_raise_signal (tc_create, test_create_fail_004, SIGABRT);
#endif

	TCase* tc_shutdown = tcase_create ("shutdown");
	suite_add_tcase (s, tc_shutdown);
	tcase_add_test (tc_shutdown, test_shutdown_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_shutdown, test_shutdown_fail_001, SIGABRT);
#endif

	TCase* tc_add = tcase_create ("add");
	suite_add_tcase (s, tc_add);
	tcase_add_test (tc_add, test_add_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_add, test_add_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_add, test_add_fail_002, SIGABRT);
	tcase_add_test_raise_signal (tc_add, test_add_fail_003, SIGABRT);
#endif

	TCase* tc_peek = tcase_create ("peek");
	suite_add_tcase (s, tc_peek);
	tcase_add_test (tc_peek, test_peek_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_peek, test_peek_fail_001, SIGABRT);
#endif
/* logical not fatal errors */
	tcase_add_test (tc_peek, test_peek_fail_002);

	TCase* tc_max_length = tcase_create ("max-length");
	suite_add_tcase (s, tc_max_length);
	tcase_add_test (tc_max_length, test_max_length_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_max_length, test_max_length_fail_001, SIGABRT);
#endif

	TCase* tc_length = tcase_create ("length");
	suite_add_tcase (s, tc_length);
	tcase_add_test (tc_length, test_length_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_length, test_length_fail_001, SIGABRT);
#endif

	TCase* tc_size = tcase_create ("size");
	suite_add_tcase (s, tc_size);
	tcase_add_test (tc_size, test_size_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_size, test_size_fail_001, SIGABRT);
#endif

	TCase* tc_is_empty = tcase_create ("is-empty");
	suite_add_tcase (s, tc_is_empty);
	tcase_add_test (tc_is_empty, test_is_empty_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_is_empty, test_is_empty_fail_001, SIGABRT);
#endif
	TCase* tc_is_full = tcase_create ("is-full");
	suite_add_tcase (s, tc_is_full);
	tcase_add_test (tc_is_full, test_is_full_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_is_full, test_is_full_fail_001, SIGABRT);
#endif

	TCase* tc_lead = tcase_create ("lead");
	suite_add_tcase (s, tc_lead);
	tcase_add_test (tc_lead, test_lead_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_lead, test_lead_fail_001, SIGABRT);
#endif

	TCase* tc_next_lead = tcase_create ("next-lead");
	suite_add_tcase (s, tc_next_lead);
	tcase_add_test (tc_next_lead, test_next_lead_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_next_lead, test_next_lead_fail_001, SIGABRT);
#endif

	TCase* tc_trail = tcase_create ("trail");
	suite_add_tcase (s, tc_trail);
	tcase_add_test (tc_trail, test_trail_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_trail, test_trail_fail_001, SIGABRT);
#endif

	TCase* tc_retransmit_push = tcase_create ("retransmit-push");
	suite_add_tcase (s, tc_retransmit_push);
	tcase_add_test (tc_retransmit_push, test_retransmit_push_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_retransmit_push, test_retransmit_push_fail_001, SIGABRT);
#endif

	TCase* tc_retransmit_try_peek = tcase_create ("retransmit-try-peek");
	suite_add_tcase (s, tc_retransmit_try_peek);
	tcase_add_test (tc_retransmit_try_peek, test_retransmit_try_peek_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_retransmit_try_peek, test_retransmit_try_peek_fail_001, SIGABRT);
#endif

	TCase* tc_retransmit_remove_head = tcase_create ("retransmit-remove-head");
	suite_add_tcase (s, tc_retransmit_remove_head);
	tcase_add_test (tc_retransmit_remove_head, test_retransmit_remove_head_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_retransmit_remove_head, test_retransmit_remove_head_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_retransmit_remove_head, test_retransmit_remove_head_fail_002, SIGABRT);
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
