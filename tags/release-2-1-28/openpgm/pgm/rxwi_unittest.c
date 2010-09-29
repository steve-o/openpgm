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
#include <pgm/time.h>
#include <pgm/reed_solomon.h>

#define pgm_histogram_add	mock_pgm_histogram_add
#include <pgm/histogram.h>


/* mock global */

static pgm_time_t mock_pgm_time_now = 0x1;

/* mock functions for external references */

/** reed-solomon module */
static
void
mock_pgm_rs_create (
	rs_t*			rs_,
	guint			n,
	guint			k
	)
{
}

static
void
mock_pgm_rs_destroy (
	rs_t*			rs
	)
{
}

static
void
mock_pgm_rs_decode_parity_appended (
	rs_t*			rs,
	void**			block,
	guint*			offsets,
	gsize			len
	)
{
// null
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


#define pgm_time_now			mock_pgm_time_now
#define pgm_rs_create			mock_pgm_rs_create
#define pgm_rs_destroy			mock_pgm_rs_destroy
#define pgm_rs_decode_parity_appended	mock_pgm_rs_decode_parity_appended
#define pgm_histogram_init		mock_pgm_histogram_init

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
 *	pgm_rxw_create (
 *		const pgm_tsi_t*	tsi,
 *		const guint16		tpdu_size,
 *		const guint32		sqns,
 *		const guint		secs,
 *		const guint		max_rte
 *		)
 */

/* vanilla sequence count window */
START_TEST (test_create_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_create (&tsi, 1500, 100, 0, 0), "create failed");
}
END_TEST

/* vanilla time based window */
START_TEST (test_create_pass_002)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_create (&tsi, 1500, 0, 60, 800000), "create failed");
}
END_TEST

/* jumbo frame */
START_TEST (test_create_pass_003)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_create (&tsi, 9000, 0, 60, 800000), "create failed");
}
END_TEST

/* max frame */
START_TEST (test_create_pass_004)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_create (&tsi, UINT16_MAX, 0, 60, 800000), "create failed");
}
END_TEST

/* invalid tsi pointer */
START_TEST (test_create_fail_001)
{
	pgm_rxw_t* window = pgm_rxw_create (NULL, 1500, 100, 0, 0);
	fail ("reached");
}
END_TEST

/* invalid tpdu size */
START_TEST (test_create_fail_002)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_rxw_create (&tsi, 0, 100, 0, 0), "create failed");
}
END_TEST

START_TEST (test_create_fail_003)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 0, 0, 60, 800000);
	fail ("reached");
}
END_TEST

/* no specified sequence count or time value */
START_TEST (test_create_fail_004)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 0, 0, 0, 800000);
	fail ("reached");
}
END_TEST

/* no specified rate */
START_TEST (test_create_fail_005)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 0, 0, 60, 0);
	fail ("reached");
}
END_TEST

/* all invalid */
START_TEST (test_create_fail_006)
{
	pgm_rxw_t* window = pgm_rxw_create (NULL, 0, 0, 0, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_rxw_destroy (
 *		pgm_rxw_t* const	window
 *		)
 */

START_TEST (test_destroy_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_destroy_fail_001)
{
	pgm_rxw_destroy (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	int
 *	pgm_rxw_add (
 *		pgm_rxw_t* const		window,
 *		struct pgm_sk_buff_t* const	skb,
 *		const pgm_time_t		now,
 *		const pgm_time_t		nak_rb_expiry
 *		)
 * failures raise assert errors and stop process execution.
 */

START_TEST (test_add_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	pgm_rxw_destroy (window);
}
END_TEST

/* missing + inserted */
START_TEST (test_add_pass_002)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
        pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
        fail_if (NULL == window, "create failed");
/* #1 */
        struct pgm_sk_buff_t* skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
        const pgm_time_t nak_rb_expiry = 2;
        fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
/* #2 with jump */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (2);
	fail_unless (PGM_RXW_MISSING == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not missing");
/* #3 to fill in gap */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (1);
	fail_unless (PGM_RXW_INSERTED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not inserted");
        pgm_rxw_destroy (window);
}
END_TEST

/* duplicate + append */
START_TEST (test_add_pass_003)
{
        pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
        pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
        fail_if (NULL == window, "create failed");
/* #1 */
        struct pgm_sk_buff_t* skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed");
        skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
        const pgm_time_t nak_rb_expiry = 2;
        fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
/* #2 repeat sequence  */
        skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed");
        skb->pgm_data->data_sqn = g_htonl (0);
        fail_unless (PGM_RXW_DUPLICATE == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not duplicate");
/* #3 append */
        skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed");
        skb->pgm_data->data_sqn = g_htonl (1);
        fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
        pgm_rxw_destroy (window);
}
END_TEST

/* malformed: tpdu too long */
START_TEST (test_add_pass_004)
{
        pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
        pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
        fail_if (NULL == window, "create failed");
        struct pgm_sk_buff_t* skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed"); 
	skb->pgm_header->pgm_tsdu_length = g_htons (65535);
        skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
        const pgm_time_t nak_rb_expiry = 2;
        fail_unless (PGM_RXW_MALFORMED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not malformed");
}
END_TEST

/* bounds + append */
START_TEST (test_add_pass_005)
{
        pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
        pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
        fail_if (NULL == window, "create failed");
/* #1 */
        struct pgm_sk_buff_t* skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed"); 
        skb->pgm_data->data_sqn = g_htonl (0);
	skb->pgm_data->data_trail = g_htonl (-10);
	const pgm_time_t now = 1;
        const pgm_time_t nak_rb_expiry = 2;
        fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
/* #2 jump backwards  */
        skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed");
        skb->pgm_data->data_sqn = g_htonl (-1);
	skb->pgm_data->data_trail = g_htonl (-10);
        fail_unless (PGM_RXW_BOUNDS == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not bounds");
/* #3 append */
        skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed");
        skb->pgm_data->data_sqn = g_htonl (1);
	skb->pgm_data->data_trail = g_htonl (-10);
        fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
/* #4 jump forward */
        skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed");
        skb->pgm_data->data_sqn = g_htonl (100 + (UINT32_MAX / 2));
	skb->pgm_data->data_trail = g_htonl (UINT32_MAX / 2);
        fail_unless (PGM_RXW_BOUNDS == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not bounds");
/* #5 append */
        skb = generate_valid_skb ();
        fail_if (NULL == skb, "generate_valid_skb failed");
        skb->pgm_data->data_sqn = g_htonl (2);
	skb->pgm_data->data_trail = g_htonl (-10);
        fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
        pgm_rxw_destroy (window);
}
END_TEST

/* null skb */
START_TEST (test_add_fail_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	int retval = pgm_rxw_add (window, NULL, now, nak_rb_expiry);
	fail ("reached");
}
END_TEST

/* null window */
START_TEST (test_add_fail_002)
{
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	int retval = pgm_rxw_add (NULL, skb, now, nak_rb_expiry);
	fail ("reached");
}
END_TEST

/* null skb content */
START_TEST (test_add_fail_003)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	char buffer[1500];
	memset (buffer, 0, sizeof(buffer));
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	int retval = pgm_rxw_add (window, (struct pgm_sk_buff_t*)buffer, now, nak_rb_expiry);
	fail ("reached");
}
END_TEST

/* 0 nak_rb_expiry */
START_TEST (test_add_fail_004)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	int retval = pgm_rxw_add (window, skb, now, 0);
	fail ("reached");
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
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (NULL == pgm_rxw_peek (window, 0));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (skb == pgm_rxw_peek (window, 0), "peek failed");
	fail_unless (NULL == pgm_rxw_peek (window, 1), "peek failed");
	fail_unless (NULL == pgm_rxw_peek (window, -1), "peek failed");
	pgm_rxw_destroy (window);
}
END_TEST

/* null window */
START_TEST (test_peek_fail_001)
{
	struct pgm_sk_buff_t* skb = pgm_rxw_peek (NULL, 0);
	fail ("reached");
}
END_TEST

/** inline function tests **/
/* pgm_rxw_max_length () 
 */
START_TEST (test_max_length_pass_001)
{
	const guint window_length = 100;
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, window_length, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (window_length == pgm_rxw_max_length (window), "max_length failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_max_length_fail_001)
{
	pgm_rxw_max_length (NULL);
	fail ("reached");
}
END_TEST

/* pgm_rxw_length () 
 */
START_TEST (test_length_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (0 == pgm_rxw_length (window), "length failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (1 == pgm_rxw_length (window), "length failed");
/* #2 */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (1);
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (2 == pgm_rxw_length (window), "length failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_length_fail_001)
{
	pgm_rxw_length (NULL);
	fail ("reached");
}
END_TEST

/* pgm_rxw_size () 
 */
START_TEST (test_size_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (0 == pgm_rxw_size (window), "size failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (1000 == pgm_rxw_size (window), "size failed");
/* #2 */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (1);
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (2000 == pgm_rxw_size (window), "size failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_size_fail_001)
{
	pgm_rxw_size (NULL);
	fail ("reached");
}
END_TEST

/* pgm_rxw_is_empty
 */
START_TEST (test_is_empty_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_unless (pgm_rxw_is_empty (window), "is_empty failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_if (pgm_rxw_is_empty (window), "is_empty failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_is_empty_fail_001)
{
	pgm_rxw_is_empty (NULL);
	fail ("reached");
}
END_TEST

/* pgm_rxw_is_full
 */
START_TEST (test_is_full_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 1, 0, 0);
	fail_if (NULL == window, "create failed");
	fail_if (pgm_rxw_is_full (window), "is_full failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (pgm_rxw_is_full (window), "is_full failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_is_full_fail_001)
{
	pgm_rxw_is_full (NULL);
	fail ("reached");
}
END_TEST

/* pgm_rxw_lead
 */
START_TEST (test_lead_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	guint32 lead = pgm_rxw_lead (window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (lead + 1 == pgm_rxw_lead (window), "lead failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_lead_fail_001)
{
	pgm_rxw_lead (NULL);
	fail ("reached");
}
END_TEST

/* pgm_rxw_next_lead
 */
START_TEST (test_next_lead_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	guint32 next_lead = pgm_rxw_next_lead (window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (next_lead == pgm_rxw_lead (window), "lead failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_next_lead_fail_001)
{
	pgm_rxw_next_lead (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	gssize
 *	pgm_rxw_readv (
 *		pgm_rxw_t* const	window,
 *		pgm_msgv_t**		pmsg,
 *		const guint		msg_len
 *		)
 */

START_TEST (test_readv_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_msgv_t msgv[2], *pmsg;
/* #1 empty */
	pmsg = msgv;
	fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
/* #2 single TPDU-APDU */
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	pmsg = msgv;
	fail_unless (1000 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	pmsg = msgv;
	fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
/* #3,4 two APDUs */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (1);
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (2);
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	pmsg = msgv;
	fail_unless (2000 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
/* #5,6 skip and repair APDU */
	pmsg = msgv;
	fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (4);
	fail_unless (PGM_RXW_MISSING == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not missing");
	pmsg = msgv;
	fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (3);
	fail_unless (PGM_RXW_INSERTED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not inserted");
	pmsg = msgv;
	fail_unless (2000 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	pmsg = msgv;
	fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	pgm_rxw_destroy (window);
}
END_TEST

/* zero-length */
START_TEST (test_readv_pass_002)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_msgv_t msgv[2], *pmsg;
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_header->pgm_tsdu_length = g_htons (0);
	skb->tail = (guint8*)skb->tail - skb->len;
	skb->len = 0;
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	pmsg = msgv;
	fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	pmsg = msgv;
	fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	pgm_rxw_destroy (window);
}
END_TEST

/* full window */
START_TEST (test_readv_pass_003)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_msgv_t msgv[1], *pmsg;
	struct pgm_sk_buff_t* skb;
	for (unsigned i = 0; i < 100; i++)
	{
		skb = generate_valid_skb ();
		fail_if (NULL == skb, "generate_valid_skb failed");
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
		fail_unless ((1 + i) == pgm_rxw_length (window), "length failed");
	}
	fail_unless (pgm_rxw_is_full (window), "is_full failed");
	fail_unless (_pgm_rxw_commit_is_empty (window), "commit_is_empty failed");
	for (unsigned i = 0; i < 100; i++)
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
		fail_unless ((1 + i) == _pgm_rxw_commit_length (window), "commit_length failed");
	}
	fail_unless (pgm_rxw_length (window) == _pgm_rxw_commit_length (window), "commit_length failed");
	pmsg = msgv;
	fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	pgm_rxw_destroy (window);
}
END_TEST

/* full + 1 window */
START_TEST (test_readv_pass_004)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_msgv_t msgv[1], *pmsg;
	struct pgm_sk_buff_t* skb;
	for (unsigned i = 0; i < 101; i++)
	{
		skb = generate_valid_skb ();
		fail_if (NULL == skb, "generate_valid_skb failed");
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
		fail_unless (MIN(100, 1 + i) == pgm_rxw_length (window), "length failed");
	}
	fail_unless (pgm_rxw_is_full (window), "is_full failed");
	fail_unless (_pgm_rxw_commit_is_empty (window), "commit_is_empty failed");
	for (unsigned i = 0; i < 100; i++)
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
		fail_unless ((1 + i) == _pgm_rxw_commit_length (window), "commit_length failed");
	}
	fail_unless (pgm_rxw_length (window) == _pgm_rxw_commit_length (window), "commit_length failed");
	pmsg = msgv;
	fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	pgm_rxw_destroy (window);
}
END_TEST

/* full - 2 lost last in window */
START_TEST (test_readv_pass_005)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_msgv_t msgv[1], *pmsg;
	struct pgm_sk_buff_t* skb;
	for (unsigned i = 0; i < 98; i++)
	{
		skb = generate_valid_skb ();
		fail_if (NULL == skb, "generate_valid_skb failed");
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
		fail_unless ((1 + i) == pgm_rxw_length (window), "length failed");
	}
	fail_if (pgm_rxw_is_full (window), "is_full failed");
	fail_unless (_pgm_rxw_commit_is_empty (window), "commit_is_empty failed");
	{
		unsigned i = 99;
		skb = generate_valid_skb ();
		fail_if (NULL == skb, "generate_valid_skb failed");
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_MISSING == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not missing");
		fail_unless ((1 + i) == pgm_rxw_length (window), "length failed");
	}
	fail_unless (pgm_rxw_is_full (window));
	fail_unless (_pgm_rxw_commit_is_empty (window));
	for (unsigned i = 0; i < 98; i++)
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
		fail_unless ((1 + i) == _pgm_rxw_commit_length (window), "commit_length failed");
	}
	fail_unless (pgm_rxw_length (window) == (2 + _pgm_rxw_commit_length (window)), "commit_length failed");
/* read end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	}
	pgm_rxw_destroy (window);
}
END_TEST

/* add full window, readv 1 skb, add 1 more */
START_TEST (test_readv_pass_006)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_msgv_t msgv[1], *pmsg;
	struct pgm_sk_buff_t* skb;
	for (unsigned i = 0; i < 100; i++)
	{
		skb = generate_valid_skb ();
		fail_if (NULL == skb, "generate_valid_skb failed");
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
		fail_unless (MIN(100, 1 + i) == pgm_rxw_length (window), "length failed");
	}
	fail_unless (pgm_rxw_is_full (window));
	fail_unless (_pgm_rxw_commit_is_empty (window));
/* read one skb */
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
		fail_unless (1 == _pgm_rxw_commit_length (window), "commit_length failed");
	}
/* add one more new skb */
	{
		unsigned i = 100;
		skb = generate_valid_skb ();
		fail_if (NULL == skb, "generate_valid_skb failed");
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_BOUNDS == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not bounds");
		fail_unless (MIN(100, 1 + i) == pgm_rxw_length (window), "length failed");
	}
/* read off 99 more skbs */
	for (unsigned i = 0; i < 99; i++)
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
		fail_unless ((2 + i) == _pgm_rxw_commit_length (window), "commit_length failed");
	}
/* read end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	}
	pgm_rxw_destroy (window);
}
END_TEST

/* NULL window */
START_TEST (test_readv_fail_001)
{
	pgm_msgv_t msgv[1], *pmsg = msgv;
	gssize len = pgm_rxw_readv (NULL, &pmsg, G_N_ELEMENTS(msgv));
	fail ("reached");
}
END_TEST

/* NULL pmsg */
START_TEST (test_readv_fail_002)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	pgm_msgv_t msgv[1], *pmsg = msgv;
	gssize len = pgm_rxw_readv (window, NULL, G_N_ELEMENTS(msgv));
	fail ("reached");
}
END_TEST

/* 0 msg-len */
START_TEST (test_readv_fail_003)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	pgm_msgv_t msgv[1], *pmsg = msgv;
	gssize len = pgm_rxw_readv (window, &pmsg, 0);
	fail ("reached");
}
END_TEST

/* target:
 *
 * 	void
 * 	pgm_rxw_remove_commit (
 * 		pgm_rxw_t* const	window
 * 		)
 */

/* full - 2 lost last in window */
START_TEST (test_remove_commit_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_msgv_t msgv[1], *pmsg;
	struct pgm_sk_buff_t* skb;
	for (unsigned i = 0; i < 98; i++)
	{
		skb = generate_valid_skb ();
		fail_if (NULL == skb, "generate_valid_skb failed");
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
		fail_unless ((1 + i) == pgm_rxw_length (window), "length failed");
	}
	fail_if (pgm_rxw_is_full (window));
	fail_unless (_pgm_rxw_commit_is_empty (window));
/* #98 is missing */
	{
		unsigned i = 99;
		skb = generate_valid_skb ();
		fail_if (NULL == skb, "generate_valid_skb failed");
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_MISSING == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
		fail_unless ((1 + i) == pgm_rxw_length (window), "length failed");
	}
	fail_unless (pgm_rxw_is_full (window), "is_full failed");
	fail_unless (_pgm_rxw_commit_is_empty (window), "commit_is_empty");
/* now mark #98 lost */
	pgm_rxw_lost (window, 98);
	for (unsigned i = 0; i < 98; i++)
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
		fail_unless ((1 + i) == _pgm_rxw_commit_length (window), "commit_length failed");
	}
	fail_unless (100 == pgm_rxw_length (window), "length failed");
	fail_unless ( 98 == _pgm_rxw_commit_length (window), "commit_length failed");
/* read end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	}
	fail_unless (100 == pgm_rxw_length (window), "length failed");
	fail_unless ( 98 == _pgm_rxw_commit_length (window), "commit_length failed");
	pgm_rxw_remove_commit (window);
/* read lost skb #98 */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	}
	pgm_rxw_remove_commit (window);
/* read valid skb #99 */
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	}
/* read end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	}
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_remove_commit_fail_001)
{
	pgm_rxw_remove_commit (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	guint
 *	pgm_rxw_remove_trail (
 *		pgm_rxw_t* const	window
 *		)
 */

START_TEST (test_remove_trail_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_msgv_t msgv[2], *pmsg;
	fail_unless (0 == pgm_rxw_remove_trail (window), "remove_trail failed");
/* #1,2 two APDUs */
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (1);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry));
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (2);
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry));
	fail_unless (1 == pgm_rxw_remove_trail (window), "remove_trail failed");
	fail_unless (1 == pgm_rxw_length (window), "length failed");
	fail_unless (1000 == pgm_rxw_size (window), "size failed");
	pmsg = msgv;
	fail_unless (1000 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	fail_unless (0 == pgm_rxw_remove_trail (window), "remove_trail failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_remove_trail_fail_001)
{
	guint count = pgm_rxw_remove_trail (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	guint32
 *	pgm_rxw_update (
 *		pgm_rxw_t* const	window,
 *		const guint32		txw_trail,
 *		const guint32		txw_lead,
 *		const pgm_time_t	now,
 *		const pgm_time_t	nak_rb_expiry
 *		)
 */

START_TEST (test_update_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	const pgm_time_t now = 1;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (0 == pgm_rxw_update (window, 100, 99, now, nak_rb_expiry), "update failed");
/* dupe */
	fail_unless (0 == pgm_rxw_update (window, 100, 99, now, nak_rb_expiry), "update failed");
/* #1 at 100 */
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (100);
	fail_unless (PGM_RXW_BOUNDS == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not bounds");
/* #2 at 101 */
	skb->pgm_data->data_sqn = g_htonl (101);
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	pgm_msgv_t msgv[1], *pmsg = msgv;
	fail_unless (1000 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
/* #3 at 102 */
	fail_unless (1 == pgm_rxw_update (window, 102, 99, now, nak_rb_expiry), "update failed");
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (102);
	fail_unless (PGM_RXW_INSERTED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not inserted");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_update_fail_001)
{
	guint count = pgm_rxw_update (NULL, 0, 0, 0, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	int
 *	pgm_rxw_confirm (
 *		pgm_rxw_t* const	window,
 *		const guint32		sequence,
 *		const pgm_time_t	now,
 *		const pgm_time_t	nak_rdata_expiry,
 *		const pgm_time_t	nak_rb_expiry
 *		)
 */

START_TEST (test_confirm_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	const pgm_time_t now = 1;
	const pgm_time_t nak_rdata_expiry = 2;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_BOUNDS == pgm_rxw_confirm (window, 0, now, nak_rdata_expiry, nak_rb_expiry), "confirm not bounds");
/* #1 at 100 */
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (100);
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (1 == pgm_rxw_length (window), "length failed");
	fail_unless (PGM_RXW_BOUNDS == pgm_rxw_confirm (window, 99, now, nak_rdata_expiry, nak_rb_expiry), "confirm not bounds");
	fail_unless (PGM_RXW_DUPLICATE == pgm_rxw_confirm (window, 100, now, nak_rdata_expiry, nak_rb_expiry), "confirm not duplicate");
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_confirm (window, 101, now, nak_rdata_expiry, nak_rb_expiry), "confirm not appended");
	fail_unless (2 == pgm_rxw_length (window));
	fail_unless (PGM_RXW_UPDATED == pgm_rxw_confirm (window, 101, now, nak_rdata_expiry, nak_rb_expiry), "confirm not updated");
/* #2 at 101 */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (101);
	fail_unless (PGM_RXW_INSERTED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not inserted");
	pgm_msgv_t msgv[2], *pmsg = msgv;
	fail_unless (2000 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	pgm_rxw_destroy (window);
}
END_TEST

/* constrained confirm */
START_TEST (test_confirm_pass_002)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_msgv_t msgv[1], *pmsg;
	struct pgm_sk_buff_t* skb;
	for (unsigned i = 0; i < 100; i++)
	{
		skb = generate_valid_skb ();
		fail_if (NULL == skb, "generate_valid_skb failed");
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
		fail_unless (MIN(100, 1 + i) == pgm_rxw_length (window), "length failed");
	}
	fail_unless (pgm_rxw_is_full (window), "is_full failed");
	fail_unless (_pgm_rxw_commit_is_empty (window), "is_empty failed");
/* read one skb */
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
		fail_unless (1 == _pgm_rxw_commit_length (window), "commit_length failed");
	}
/* confirm next sequence */
	const pgm_time_t now = 1;
	const pgm_time_t nak_rdata_expiry = 2;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_BOUNDS == pgm_rxw_confirm (window, 100, now, nak_rdata_expiry, nak_rb_expiry), "confirm not bounds");
/* read off 99 more skbs */
	for (unsigned i = 0; i < 99; i++)
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
		fail_unless ((2 + i) == _pgm_rxw_commit_length (window), "commit_length failed");
	}
/* read end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	}
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_confirm_fail_001)
{
	int retval = pgm_rxw_confirm (NULL, 0, 0, 0, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_rxw_lost (
 *		pgm_rxw_t* const	window,
 *		const guint32		sequence
 *		)
 */

START_TEST (test_lost_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	const pgm_time_t now = 1;
	const pgm_time_t nak_rdata_expiry = 2;
	const pgm_time_t nak_rb_expiry = 2;
/* #1 at 100 */
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (100);
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
	fail_unless (1 == pgm_rxw_length (window), "length failed");
	fail_unless (1000 == pgm_rxw_size (window), "size failed");
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_confirm (window, 101, now, nak_rdata_expiry, nak_rb_expiry), "confirm not appended");
	fail_unless (2 == pgm_rxw_length (window), "length failed");
	fail_unless (1000 == pgm_rxw_size (window), "size failed");
	pgm_rxw_lost (window, 101);
	fail_unless (2 == pgm_rxw_length (window), "length failed");
	fail_unless (1000 == pgm_rxw_size (window), "size failed");
/* #2 at 101 */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (101);
	fail_unless (PGM_RXW_INSERTED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not inserted");
	fail_unless (2 == pgm_rxw_length (window), "length failed");
	fail_unless (2000 == pgm_rxw_size (window), "size failed");
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_lost_fail_001)
{
	pgm_rxw_lost (NULL, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_rxw_state (
 *		pgm_rxw_t* const	window,
 *		struct pgm_sk_buff_t*	skb,
 *		pgm_pkt_state_e		new_state
 *		)
 */

START_TEST (test_state_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	const pgm_time_t now = 1;
	const pgm_time_t nak_rdata_expiry = 2;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (0 == pgm_rxw_update (window, 100, 99, now, nak_rb_expiry), "update failed");
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_confirm (window, 101, now, nak_rdata_expiry, nak_rb_expiry), "confirm not appended");
	struct pgm_sk_buff_t* skb = pgm_rxw_peek (window, 101);
	pgm_rxw_state (window, skb, PGM_PKT_WAIT_NCF_STATE);
	pgm_rxw_state (window, skb, PGM_PKT_WAIT_DATA_STATE);
	pgm_rxw_destroy (window);
}
END_TEST

START_TEST (test_state_fail_001)
{
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	pgm_rxw_state (NULL, skb, PGM_PKT_BACK_OFF_STATE);
	fail ("reached");
}
END_TEST

START_TEST (test_state_fail_002)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	pgm_rxw_state (window, NULL, PGM_PKT_BACK_OFF_STATE);
	fail ("reached");
}
END_TEST

START_TEST (test_state_fail_003)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	pgm_rxw_state (window, skb, -1);
	fail ("reached");
}
END_TEST

/* pgm_peer_has_pending
 */

START_TEST (test_has_pending_pass_001)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window, "create failed");
/* empty */
	fail_unless (0 == window->has_event, "unexpected event");
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (0);
	const pgm_time_t now = 1;
	const pgm_time_t nak_rdata_expiry = 2;
	const pgm_time_t nak_rb_expiry = 2;
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not appended");
/* 1 sequence */
	fail_unless (1 == window->has_event, "no event");
	window->has_event = 0;
/* jump */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (2);
	fail_unless (PGM_RXW_MISSING == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not missing");
	fail_unless (0 == window->has_event, "unexpected event");
/* loss */
	pgm_rxw_lost (window, 1);
	fail_unless (1 == window->has_event, "no event");
	window->has_event = 0;
/* insert */
	skb = generate_valid_skb ();
	fail_if (NULL == skb, "generate_valid_skb failed");
	skb->pgm_data->data_sqn = g_htonl (1);
	fail_unless (PGM_RXW_INSERTED == pgm_rxw_add (window, skb, now, nak_rb_expiry), "add not inserted");
	fail_unless (1 == window->has_event, "no event");
	window->has_event = 0;
/* confirm */
	fail_unless (PGM_RXW_APPENDED == pgm_rxw_confirm (window, 3, now, nak_rdata_expiry, nak_rb_expiry), "confirm not appended");
	fail_unless (0 == window->has_event, "unexpected event");
/* partial read */
	pgm_msgv_t msgv[2], *pmsg = msgv;
	fail_unless (2000 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	fail_unless (0 == window->has_event, "unexpected event");
/* finish read */
	pmsg = msgv;
	fail_unless (1000 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)), "readv failed");
	fail_unless (0 == window->has_event, "unexpected event");
	pgm_rxw_destroy (window);
}
END_TEST

static
Suite*
make_basic_test_suite (void)
{
	Suite* s;

	s = suite_create ("basic transmit window API");

	TCase* tc_create = tcase_create ("create");
	suite_add_tcase (s, tc_create);
	tcase_add_test (tc_create, test_create_pass_001);
	tcase_add_test (tc_create, test_create_pass_002);
	tcase_add_test (tc_create, test_create_pass_003);
	tcase_add_test (tc_create, test_create_pass_004);
	tcase_add_test_raise_signal (tc_create, test_create_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_create, test_create_fail_002, SIGABRT);
	tcase_add_test_raise_signal (tc_create, test_create_fail_003, SIGABRT);
	tcase_add_test_raise_signal (tc_create, test_create_fail_004, SIGABRT);

	TCase* tc_destroy = tcase_create ("destroy");
	suite_add_tcase (s, tc_destroy);
	tcase_add_test (tc_destroy, test_destroy_pass_001);
	tcase_add_test_raise_signal (tc_destroy, test_destroy_fail_001, SIGABRT);

	TCase* tc_add = tcase_create ("add");
	suite_add_tcase (s, tc_add);
	tcase_add_test (tc_add, test_add_pass_001);
	tcase_add_test (tc_add, test_add_pass_002);
	tcase_add_test (tc_add, test_add_pass_003);
	tcase_add_test (tc_add, test_add_pass_004);
	tcase_add_test (tc_add, test_add_pass_005);
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

	TCase* tc_lead = tcase_create ("lead");
	suite_add_tcase (s, tc_lead);
	tcase_add_test (tc_lead, test_lead_pass_001);
	tcase_add_test_raise_signal (tc_lead, test_lead_fail_001, SIGABRT);

	TCase* tc_next_lead = tcase_create ("next-lead");
	suite_add_tcase (s, tc_next_lead);
	tcase_add_test (tc_next_lead, test_next_lead_pass_001);
	tcase_add_test_raise_signal (tc_next_lead, test_next_lead_fail_001, SIGABRT);

	TCase* tc_readv = tcase_create ("readv");
	suite_add_tcase (s, tc_readv);
	tcase_add_test (tc_readv, test_readv_pass_001);
	tcase_add_test (tc_readv, test_readv_pass_002);
	tcase_add_test (tc_readv, test_readv_pass_003);
	tcase_add_test (tc_readv, test_readv_pass_004);
	tcase_add_test (tc_readv, test_readv_pass_005);
	tcase_add_test (tc_readv, test_readv_pass_006);
	tcase_add_test_raise_signal (tc_readv, test_readv_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_readv, test_readv_fail_002, SIGABRT);
	tcase_add_test_raise_signal (tc_readv, test_readv_fail_003, SIGABRT);

	TCase* tc_remove_commit = tcase_create ("remove-commit");
	suite_add_tcase (s, tc_remove_commit);
	tcase_add_test (tc_remove_commit, test_remove_commit_pass_001);
	tcase_add_test_raise_signal (tc_remove_commit, test_remove_commit_fail_001, SIGABRT);

	TCase* tc_remove_trail = tcase_create ("remove-trail");
	TCase* tc_update = tcase_create ("update");
	suite_add_tcase (s, tc_update);
	tcase_add_test (tc_update, test_update_pass_001);
	tcase_add_test_raise_signal (tc_update, test_update_fail_001, SIGABRT);

        TCase* tc_confirm = tcase_create ("confirm");
	suite_add_tcase (s, tc_confirm);
	tcase_add_test (tc_confirm, test_confirm_pass_001);
	tcase_add_test (tc_confirm, test_confirm_pass_002);
	tcase_add_test_raise_signal (tc_confirm, test_confirm_fail_001, SIGABRT);

        TCase* tc_lost = tcase_create ("lost");
	suite_add_tcase (s, tc_lost);
	tcase_add_test (tc_lost, test_lost_pass_001);
	tcase_add_test_raise_signal (tc_lost, test_lost_fail_001, SIGABRT);

        TCase* tc_state = tcase_create ("state");
	suite_add_tcase (s, tc_state);
	tcase_add_test (tc_state, test_state_pass_001);
	tcase_add_test_raise_signal (tc_state, test_state_fail_001, SIGABRT);

	return s;
}

/* read through lost packet */
START_TEST (test_readv_pass_007)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	pgm_msgv_t msgv[1], *pmsg;
	struct pgm_sk_buff_t* skb;
/* add #0 */
	{
		unsigned i = 0;
		skb = generate_valid_skb ();
		fail_if (NULL == skb);
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry));
		fail_unless ((1 + i) == pgm_rxw_length (window));
	}
/* add # 2 */
	{
		unsigned i = 2;
		skb = generate_valid_skb ();
		fail_if (NULL == skb);
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_MISSING == pgm_rxw_add (window, skb, now, nak_rb_expiry));
		fail_unless ((1 + i) == pgm_rxw_length (window));
	}
/* lose #1 */
	{
		pgm_rxw_lost (window, 1);
	}
	fail_unless (_pgm_rxw_commit_is_empty (window));
/* read #0 */
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
/* end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
	pgm_rxw_remove_commit (window);
/* read lost skb #1 */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
	pgm_rxw_remove_commit (window);
/* read #2 */
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
/* end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
	pgm_rxw_destroy (window);
}
END_TEST

/* read through loss extended window */
START_TEST (test_readv_pass_008)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	pgm_msgv_t msgv[1], *pmsg;
	struct pgm_sk_buff_t* skb;
/* add #0 */
	{
		unsigned i = 0;
		skb = generate_valid_skb ();
		fail_if (NULL == skb);
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry));
		fail_unless ((1 + i) == pgm_rxw_length (window));
	}
	fail_unless (_pgm_rxw_commit_is_empty (window));
/* read #0 */
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
	pgm_rxw_remove_commit (window);
/* end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
/* add #100 */
	{
		unsigned i = 100;
		skb = generate_valid_skb ();
		fail_if (NULL == skb);
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_MISSING == pgm_rxw_add (window, skb, now, nak_rb_expiry));
	}
/* lose #1-99 */
	{
		for (unsigned i = 1; i < 100; i++)
			pgm_rxw_lost (window, i);
	}
/* read #100 */
	{
		int i = 0;
		int bytes_read;
		pmsg = msgv;
		do {
			bytes_read = pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv));
			pgm_rxw_remove_commit (window);
			i++;
			if (i > 100) break;
		} while (-1 == bytes_read);
		fail_unless (100 == i);
	}
/* end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
	pgm_rxw_destroy (window);
}
END_TEST

/* read through long data-loss */
START_TEST (test_readv_pass_009)
{
	pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_rxw_t* window = pgm_rxw_create (&tsi, 1500, 100, 0, 0);
	fail_if (NULL == window);
	pgm_msgv_t msgv[1], *pmsg;
	struct pgm_sk_buff_t* skb;
/* add #0 */
	{
		unsigned i = 0;
		skb = generate_valid_skb ();
		fail_if (NULL == skb);
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_APPENDED == pgm_rxw_add (window, skb, now, nak_rb_expiry));
		fail_unless ((1 + i) == pgm_rxw_length (window));
	}
	fail_unless (_pgm_rxw_commit_is_empty (window));
/* read #0 */
	{
		pmsg = msgv;
		fail_unless (0 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
	pgm_rxw_remove_commit (window);
/* end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
/* add #2000 */
	{
		unsigned i = 2000;
		skb = generate_valid_skb ();
		fail_if (NULL == skb);
		skb->pgm_header->pgm_tsdu_length = g_htons (0);
		skb->tail = (guint8*)skb->tail - skb->len;
		skb->len = 0;
		skb->pgm_data->data_sqn = g_htonl (i);
		const pgm_time_t now = 1;
		const pgm_time_t nak_rb_expiry = 2;
		fail_unless (PGM_RXW_MISSING == pgm_rxw_add (window, skb, now, nak_rb_expiry));
	}
/* lose #1-1999 */
	{
		for (unsigned i = 1901; i < 2000; i++)
			pgm_rxw_lost (window, i);
	}
/* read #2000 */
	{
		int i = 0;
		int bytes_read;
		pmsg = msgv;
		do {
			bytes_read = pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv));
			pgm_rxw_remove_commit (window);
			i++;
			if (i > 100) break;
		} while (-1 == bytes_read);
		fail_unless (100 == i);
	}
/* end-of-window */
	{
		pmsg = msgv;
		fail_unless (-1 == pgm_rxw_readv (window, &pmsg, G_N_ELEMENTS(msgv)));
	}
	pgm_rxw_destroy (window);
}
END_TEST

/* a.k.a. unreliable delivery
 */

static
Suite*
make_best_effort_test_suite (void)
{
	Suite* s;

	s = suite_create ("Best effort delivery");

	TCase* tc_readv = tcase_create ("readv");
	suite_add_tcase (s, tc_readv);
	tcase_add_test (tc_readv, test_readv_pass_007);
	tcase_add_test (tc_readv, test_readv_pass_008);
	tcase_add_test (tc_readv, test_readv_pass_009);

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
	srunner_add_suite (sr, make_basic_test_suite ());
	srunner_add_suite (sr, make_best_effort_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
