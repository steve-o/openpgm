/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM source transport.
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
#include <stdlib.h>
#include <glib.h>
#include <check.h>

#include <pgm/source.h>
#include <pgm/txwi.h>
#include <pgm/skbuff.h>


/* mock state */

#define PGM_NETWORK		""
#define PGM_PORT		7500
#define PGM_MAX_TPDU		1500
#define PGM_TXW_SQNS		32
#define PGM_RXW_SQNS		32
#define PGM_HOPS		16
#define PGM_SPM_AMBIENT		( pgm_secs(30) )
#define PGM_SPM_HEARTBEAT_INIT	{ pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(1300), pgm_secs(7), pgm_secs(16), pgm_secs(25), pgm_secs(30) }
#define PGM_PEER_EXPIRY		( pgm_secs(300) )
#define PGM_SPMR_EXPIRY		( pgm_msecs(250) )
#define PGM_NAK_BO_IVL		( pgm_msecs(50) )
#define PGM_NAK_RPT_IVL		( pgm_secs(2) )
#define PGM_NAK_RDATA_IVL	( pgm_secs(2) )
#define PGM_NAK_DATA_RETRIES	5
#define PGM_NAK_NCF_RETRIES	2

static pgm_transport_t* g_transport = NULL;


static
void
mock_setup (void)
{
	if (!g_thread_supported ()) g_thread_init (NULL);
}

static
void
mock_teardown (void)
{
}

static
struct pgm_transport_t*
generate_transport (void)
{
	struct pgm_transport_t* transport = g_malloc0 (sizeof(struct pgm_transport_t));
	return transport;
}

static
void
mock_pgm_txw_add (
	pgm_txw_t* const		window,
	struct pgm_sk_buff_t* const	skb
	)
{
}

static
struct pgm_sk_buff_t*
mock_pgm_txw_peek (
	pgm_txw_t* const		window,
	const guint32			sequence
	)
{
	return NULL;
}

static
gboolean
mock_pgm_txw_retransmit_push (
	pgm_txw_t* const		window,
	const guint32			sequence,
	const gboolean			is_parity,
	const guint			tg_sqn_shift
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_txw_retransmit_try_peek (
	pgm_txw_t* const		window,
	struct pgm_sk_buff_t** 		skb,
	guint32* const			unfolded_checksum,
	gboolean* const			is_parity,
	guint* const			rs_h
	)
{
	return FALSE;
}

static
void
mock_pgm_txw_retransmit_remove_head (
	pgm_txw_t* const		window
	)
{
}

static
void
mock__pgm_rs_encode (
	gpointer			rs,
	const void**			src,
	guint				offset,
	void*				dst,
	gsize				len
	)
{
}

static
gboolean
mock__pgm_rate_check (
	gpointer			bucket,
	const guint			data_size,
	const int			flags
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_spmr (
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_nak (
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_nnak (
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
guint32
mock_pgm_compat_csum_partial (
	const void*			addr,
	guint				len,
	guint32				csum
	)
{
	return 0x0;
}

static
guint32
mock_pgm_compat_csum_partial_copy (
	const void*			src,
	void*				dst,
	guint				len,
	guint32				csum
	)
{
	return 0x0;
}

static
guint32
mock_pgm_csum_block_add (
	guint32				csum,
	guint32				csum2,
	guint				offset
	)
{
	return 0x0;
}

static
guint16
mock_pgm_csum_fold (
	guint32				csum
	)
{
	return 0x0;
}

static
gssize
mock__pgm_sendto (
	pgm_transport_t*		transport,
	gboolean			use_rate_limit,
	gboolean			use_router_alert,
	const void*			buf,
	gsize				len,
	int				flags,
	const struct sockaddr*		to,
	gsize				tolen
	)
{
	return len;
}

/** time module */
static
pgm_time_t
mock_pgm_time_update_now (void)
{
	return 0x1;
}

/** transport module */
static
gsize
mock_pgm_transport_pkt_offset (
	const gboolean			can_fragment
	)
{
	return can_fragment ? ( sizeof(struct pgm_header)
			      + sizeof(struct pgm_data)
			      + sizeof(struct pgm_opt_length)
	                      + sizeof(struct pgm_opt_header)
			      + sizeof(struct pgm_opt_fragment) )
			    : ( sizeof(struct pgm_header) + sizeof(struct pgm_data) );
}


/* mock functions for external references */

#define pgm_txw_add			mock_pgm_txw_add
#define pgm_txw_peek			mock_pgm_txw_peek
#define pgm_txw_retransmit_push		mock_pgm_txw_retransmit_push
#define pgm_txw_retransmit_try_peek	mock_pgm_txw_retransmit_try_peek
#define pgm_txw_retransmit_remove_head	mock_pgm_txw_retransmit_remove_head
#define _pgm_rs_encode			mock__pgm_rs_encode
#define _pgm_rate_check			mock__pgm_rate_check
#define pgm_verify_spmr			mock_pgm_verify_spmr
#define pgm_verify_nak			mock_pgm_verify_nak
#define pgm_verify_nnak			mock_pgm_verify_nnak
#define pgm_compat_csum_partial		mock_pgm_compat_csum_partial
#define pgm_compat_csum_partial_copy	mock_pgm_compat_csum_partial_copy
#define pgm_csum_block_add		mock_pgm_csum_block_add
#define pgm_csum_fold			mock_pgm_csum_fold
#define _pgm_sendto			mock__pgm_sendto
#define pgm_time_update_now		mock_pgm_time_update_now
#define pgm_transport_pkt_offset	mock_pgm_transport_pkt_offset


#define SOURCE_DEBUG
#include "source.c"


/* target:
 *	gssize
 *	pgm_transport_send (
 *		pgm_transport_t*	transport,
 *		gconstpointer		apdu,
 *		gsize			apdu_length,
 *		int			flags
 *		)
 */

START_TEST (test_send_pass_001)
{
/* pre-condition on setup */
	fail_unless (NULL != g_transport);

	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	const gsize apdu_length = 100;
	fail_unless ((gssize)apdu_length == pgm_transport_send (g_transport, buffer, apdu_length, 0));
}
END_TEST

START_TEST (test_send_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	const gsize apdu_length = 100;
	fail_unless (-1 == pgm_transport_send (NULL, buffer, apdu_length, 0));
}
END_TEST

/* target:
 *	gssize
 *	pgm_transport_sendv (
 *		pgm_transport_t*	transport,
 *		const struct pgmiovec*	vector,
 *		guint			count,
 *		int			flags,
 *		gboolean		is_one_apdu
 *		)
 */

START_TEST (test_sendv_pass_001)
{
/* pre-condition on setup */
	fail_unless (NULL != g_transport);

	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	const gsize tsdu_length = 100;
	struct pgm_iovec vector[] = { { .iov_base = buffer, .iov_len = tsdu_length } };
	fail_unless ((gssize)tsdu_length == pgm_transport_sendv (g_transport, vector, 1, 0, TRUE));
}
END_TEST

START_TEST (test_sendv_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	const gsize tsdu_length = 100;
	struct pgm_iovec vector[] = { { .iov_base = buffer, .iov_len = tsdu_length } };
	fail_unless (-1 == pgm_transport_sendv (NULL, vector, 1, 0, TRUE));
}
END_TEST

/* target:
 *	gssize
 *	pgm_transport_send_skbv (
 *		pgm_transport_t*	transport,
 *		struct pgm_sk_buff_t*,
 *		guint,
 *		int,
 *		gboolean
 *		)
 */

START_TEST (test_send_skbv_pass_001)
{
/* pre-condition on setup */
	fail_unless (NULL != g_transport);

	struct pgm_sk_buff_t* skb = pgm_alloc_skb (g_transport->max_tpdu);
/* reserve PGM header */
	pgm_skb_put (skb, pgm_transport_pkt_offset (TRUE));
	const gsize tsdu_length = 100;
	fail_unless ((gssize)tsdu_length == pgm_transport_send_skbv (g_transport, skb, 1, 0, TRUE));
}
END_TEST

START_TEST (test_send_skbv_fail_001)
{
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (g_transport->max_tpdu);
/* reserve PGM header */
	pgm_skb_put (skb, pgm_transport_pkt_offset (TRUE));
	const gsize tsdu_length = 100;
	fail_unless (-1 == pgm_transport_send_skbv (NULL, skb, 1, 0, TRUE));
}
END_TEST

/* target:
 *	gboolean
 *	_pgm_send_spm_unlocked (
 *		pgm_transport_t*	transport
 *		)
 */

START_TEST (test_send_spm_unlocked_pass_001)
{
	fail ();
}
END_TEST

START_TEST (test_send_spm_unlocked_fail_001)
{
	_pgm_send_spm_unlocked (NULL);
	fail ();
}
END_TEST

/* target:
 *	gboolean
 *	_pgm_on_nak_notify (
 *		GIOChannel*		source,
 *		GIOCondition		condition,
 *		gpointer		data
 *		)
 */

START_TEST (test_on_nak_notify_pass_001)
{
	fail ();
}
END_TEST
	
START_TEST (test_on_nak_notify_fail_001)
{
	fail ();
}
END_TEST
	
/* target:
 *	gboolean
 *	_pgm_on_spmr (
 *		pgm_transport_t*	transport,
 *		pgm_peer_t*		peer,
 *		struct pgm_sk_buff_t*	skb
 *	)
 */

START_TEST (test_on_spmr_pass_001)
{
	fail ();
}
END_TEST

START_TEST (test_on_spmr_fail_001)
{
	_pgm_on_spmr (NULL, NULL, NULL);
	fail ();
}
END_TEST

/* target:
 *	gboolean
 *	_pgm_on_nak (
 *		pgm_transport_t*	transport,
 *		struct pgm_sk_buff_t*	skb
 *	)
 */

START_TEST (test_on_nak_pass_001)
{
	fail ();
}
END_TEST

START_TEST (test_on_nak_fail_001)
{
	_pgm_on_nak (NULL, NULL);
	fail ();
}
END_TEST

/* target:
 *	int
 *	_pgm_on_nnak (
 *		pgm_transport_t*	transport,
 *		struct pgm_sk_buff_t*	skb
 *	)
 */

START_TEST (test_on_nnak_pass_001)
{
	fail ();
}
END_TEST

START_TEST (test_on_nnak_fail_001)
{
	_pgm_on_nnak (NULL, NULL);
	fail ();
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_ambient_spm (
 *		pgm_transport_t*	transport,
 *		guint			interval
 *		)
 */

START_TEST (test_set_ambient_spm_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_ambient_spm (transport, 1000));
}
END_TEST

START_TEST (test_set_ambient_spm_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_ambient_spm (NULL, 1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_heartbeat_spm (
 *		pgm_transport_t*	transport,
 *		const guint*		intervals,
		const int		count
 *		)
 */

START_TEST (test_set_heartbeat_spm_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	const guint intervals[] = { 1, 2, 3, 4, 5 };
	fail_unless (TRUE == pgm_transport_set_heartbeat_spm (transport, intervals, G_N_ELEMENTS(intervals)));
}
END_TEST

START_TEST (test_set_heartbeat_spm_fail_001)
{
	const guint intervals[] = { 1, 2, 3, 4, 5 };
	fail_unless (FALSE == pgm_transport_set_heartbeat_spm (NULL, intervals, G_N_ELEMENTS(intervals)));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_txw_sqns (
 *		pgm_transport_t*	transport,
 *		const guint		sqns
 *	)
 */

START_TEST (test_set_txw_sqns_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_txw_sqns (transport, 100));
}
END_TEST

START_TEST (test_set_txw_sqns_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_txw_sqns (NULL, 100));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_txw_secs (
 *		pgm_transport_t*	transport,
 *		const guint		secs
 *	)
 */

START_TEST (test_set_txw_secs_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_txw_secs (transport, 10));
}
END_TEST

START_TEST (test_set_txw_secs_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_txw_secs (NULL, 10));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_txw_max_rte (
 *		pgm_transport_t*	transport,
 *		const guint		rate
 *	)
 */

START_TEST (test_set_txw_max_rte_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_txw_max_rte (transport, 100*1000));
}
END_TEST

START_TEST (test_set_txw_max_rte_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_txw_max_rte (NULL, 100*1000));
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_send = tcase_create ("send");
	suite_add_tcase (s, tc_send);
	tcase_add_checked_fixture (tc_send, mock_setup, mock_teardown);
	tcase_add_test (tc_send, test_send_pass_001);
	tcase_add_test (tc_send, test_send_fail_001);

	TCase* tc_sendv = tcase_create ("sendv");
	suite_add_tcase (s, tc_sendv);
	tcase_add_checked_fixture (tc_sendv, mock_setup, mock_teardown);
	tcase_add_test (tc_sendv, test_sendv_pass_001);
	tcase_add_test (tc_sendv, test_sendv_fail_001);

	TCase* tc_send_skbv = tcase_create ("send-skbv");
	suite_add_tcase (s, tc_send_skbv);
	tcase_add_checked_fixture (tc_send_skbv, mock_setup, mock_teardown);
	tcase_add_test (tc_send_skbv, test_send_skbv_pass_001);
	tcase_add_test (tc_send_skbv, test_send_skbv_fail_001);

	TCase* tc_send_spm_unlocked = tcase_create ("send-spm-unlocked");
	suite_add_tcase (s, tc_send_spm_unlocked);
	tcase_add_checked_fixture (tc_send_spm_unlocked, mock_setup, mock_teardown);
	tcase_add_test (tc_send_spm_unlocked, test_send_spm_unlocked_pass_001);
	tcase_add_test (tc_send_spm_unlocked, test_send_spm_unlocked_fail_001);

	TCase* tc_on_nak_notify = tcase_create ("on-nak-notify");
	suite_add_tcase (s, tc_on_nak_notify);
	tcase_add_checked_fixture (tc_on_nak_notify, mock_setup, mock_teardown);
	tcase_add_test (tc_on_nak_notify, test_on_nak_notify_pass_001);
	tcase_add_test (tc_on_nak_notify, test_on_nak_notify_fail_001);

	TCase* tc_on_spmr = tcase_create ("on-spmr");
	suite_add_tcase (s, tc_on_spmr);
	tcase_add_checked_fixture (tc_on_spmr, mock_setup, mock_teardown);
	tcase_add_test (tc_on_spmr, test_on_spmr_pass_001);
	tcase_add_test (tc_on_spmr, test_on_spmr_fail_001);

	TCase* tc_on_nak = tcase_create ("on-nak");
	suite_add_tcase (s, tc_on_nak);
	tcase_add_checked_fixture (tc_on_nak, mock_setup, mock_teardown);
	tcase_add_test (tc_on_nak, test_on_nak_pass_001);
	tcase_add_test (tc_on_nak, test_on_nak_fail_001);

	TCase* tc_on_nnak = tcase_create ("on-nnak");
	suite_add_tcase (s, tc_on_nnak);
	tcase_add_checked_fixture (tc_on_nnak, mock_setup, mock_teardown);
	tcase_add_test (tc_on_nnak, test_on_nnak_pass_001);
	tcase_add_test (tc_on_nnak, test_on_nnak_fail_001);

	TCase* tc_set_ambient_spm = tcase_create ("set-ambient-spm");
	suite_add_tcase (s, tc_set_ambient_spm);
	tcase_add_checked_fixture (tc_set_ambient_spm, mock_setup, mock_teardown);
	tcase_add_test (tc_set_ambient_spm, test_set_ambient_spm_pass_001);
	tcase_add_test (tc_set_ambient_spm, test_set_ambient_spm_fail_001);

	TCase* tc_set_heartbeat_spm = tcase_create ("set-heartbeat-spm");
	suite_add_tcase (s, tc_set_heartbeat_spm);
	tcase_add_checked_fixture (tc_set_heartbeat_spm, mock_setup, mock_teardown);
	tcase_add_test (tc_set_heartbeat_spm, test_set_heartbeat_spm_pass_001);
	tcase_add_test (tc_set_heartbeat_spm, test_set_heartbeat_spm_fail_001);

	TCase* tc_set_txw_sqns = tcase_create ("set-txw-sqns");
	suite_add_tcase (s, tc_set_txw_sqns);
	tcase_add_checked_fixture (tc_set_txw_sqns, mock_setup, mock_teardown);
	tcase_add_test (tc_set_txw_sqns, test_set_txw_sqns_pass_001);
	tcase_add_test (tc_set_txw_sqns, test_set_txw_sqns_fail_001);

	TCase* tc_set_txw_secs = tcase_create ("set-txw-secs");
	suite_add_tcase (s, tc_set_txw_secs);
	tcase_add_checked_fixture (tc_set_txw_secs, mock_setup, mock_teardown);
	tcase_add_test (tc_set_txw_secs, test_set_txw_secs_pass_001);
	tcase_add_test (tc_set_txw_secs, test_set_txw_secs_fail_001);

	TCase* tc_set_txw_max_rte = tcase_create ("set-txw-max-rte");
	suite_add_tcase (s, tc_set_txw_max_rte);
	tcase_add_checked_fixture (tc_set_txw_max_rte, mock_setup, mock_teardown);
	tcase_add_test (tc_set_txw_max_rte, test_set_txw_max_rte_pass_001);
	tcase_add_test (tc_set_txw_max_rte, test_set_txw_max_rte_fail_001);
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
