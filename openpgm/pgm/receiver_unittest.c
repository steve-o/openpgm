/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM receiver transport.
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

#include <pgm/receiver.h>
#include <pgm/rxwi.h>

#define pgm_histogram_add	mock_pgm_histogram_add
#include <pgm/histogram.h>


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

static
void
mock_setup (void)
{
	if (!g_thread_supported ()) g_thread_init (NULL);
}

static
struct pgm_transport_t*
generate_transport (void)
{
	struct pgm_transport_t* transport = g_malloc0 (sizeof(struct pgm_transport_t));
	return transport;
}

static
pgm_peer_t*
generate_peer (void)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_peer_t* peer = g_malloc0 (sizeof(pgm_peer_t));
	peer->window = g_malloc0 (sizeof(pgm_rxw_t));
	g_atomic_int_inc (&peer->ref_count);
	return peer;
}

/** transport module */
static
int
mock_pgm_transport_poll_info (
	pgm_transport_t* const	transport,
	struct pollfd*		fds,
	int*			n_fds,
	int			events
	)
{
}

static
gboolean
mock_pgm_on_nak (
	pgm_transport_t* const		transport,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_on_nnak (
	pgm_transport_t* const		transport,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_on_spmr (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		peer,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

/** net module */
static
gssize
mock_pgm_sendto (
	pgm_transport_t*		transport,
	gboolean			use_rate_limit,
	gboolean			use_router_alert,
	const void*			buf,
	gsize				len,
	const struct sockaddr*		to,
	gsize				tolen
	)
{
	return len;
}

/** time module */
static pgm_time_t mock_pgm_time_now = 0x1;

static
pgm_time_t
mock_pgm_time_update_now (void)
{
	return mock_pgm_time_now;
}

/* packet module */
static
gboolean
mock_pgm_verify_spm (
	const struct pgm_sk_buff_t* const       skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_nak (
	const struct pgm_sk_buff_t* const       skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_ncf (
	const struct pgm_sk_buff_t* const       skb
	)
{
	return TRUE;
}

/* receive window module */
static
pgm_rxw_t*
mock_pgm_rxw_create (
	const pgm_tsi_t*	tsi,
	const guint16		tpdu_size,
	const guint32		sqns,
	const guint		secs,
	const guint		max_rte
	)
{
	return g_malloc0 (sizeof(pgm_rxw_t));
}

static
void
mock_pgm_rxw_destroy (
	pgm_rxw_t* const	window
	)
{
	g_assert (NULL != window);
	g_free (window);
}

static
int
mock_pgm_rxw_confirm (
	pgm_rxw_t* const	window,
	const guint32		sequence,
	const pgm_time_t	now,
	const pgm_time_t	nak_rdata_expiry,
	const pgm_time_t	nak_rb_expiry
	)
{
	return PGM_RXW_DUPLICATE;
}

static
void
mock_pgm_rxw_lost (
	pgm_rxw_t* const	window,
	const guint32		sequence
	)
{
}

static
void
mock_pgm_rxw_state (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb,
	const pgm_pkt_state_e		new_state
	)
{
}

static
guint
mock_pgm_rxw_update (
	pgm_rxw_t* const		window,
	const guint32			txw_lead,
	const guint32			txw_trail,
	const pgm_time_t		now,
	const pgm_time_t		nak_rb_expiry
	)
{
	return 0;
}

static
void
mock_pgm_rxw_update_fec (
	pgm_rxw_t* const		window,
	const guint			rs_k
	)
{
}

static
int
mock_pgm_rxw_add (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb,
	const pgm_time_t		now,
	const pgm_time_t		nak_rb_expiry
	)
{
	return PGM_RXW_APPENDED;
}

static
gssize
mock_pgm_rxw_readv (
	pgm_rxw_t* const		window,
	pgm_msgv_t**			pmsg,
	const guint			pmsglen
	)
{
	return 0;
}

/* checksum module */
static
guint16
mock_pgm_csum_fold (
	guint32				csum
	)
{
	return 0x0;
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

#define pgm_verify_spm		mock_pgm_verify_spm
#define pgm_verify_nak		mock_pgm_verify_nak
#define pgm_verify_ncf		mock_pgm_verify_ncf
#define pgm_sendto		mock_pgm_sendto
#define pgm_time_now		mock_pgm_time_now
#define pgm_time_update_now	mock_pgm_time_update_now
#define pgm_rxw_destroy		mock_pgm_rxw_destroy
#define pgm_rxw_create		mock_pgm_rxw_create
#define pgm_rxw_update		mock_pgm_rxw_update
#define pgm_rxw_update_fec	mock_pgm_rxw_update_fec
#define pgm_rxw_confirm		mock_pgm_rxw_confirm
#define pgm_rxw_lost		mock_pgm_rxw_lost
#define pgm_rxw_state		mock_pgm_rxw_state
#define pgm_rxw_add		mock_pgm_rxw_add
#define pgm_rxw_readv		mock_pgm_rxw_readv
#define pgm_csum_fold		mock_pgm_csum_fold
#define pgm_compat_csum_partial	mock_pgm_compat_csum_partial
#define pgm_histogram_init	mock_pgm_histogram_init

#define RECEIVER_DEBUG
#include "receiver.c"


/* target:
 *	void
 *	pgm_peer_unref (
 *		pgm_peer_t*		peer
 *		)
 */

/* last ref */
START_TEST (test_peer_unref_pass_001)
{
	pgm_peer_t* peer = generate_peer();
	pgm_peer_unref (peer);
}
END_TEST

/* non-last ref */
START_TEST (test_peer_unref_pass_002)
{
	pgm_peer_t* peer = _pgm_peer_ref (generate_peer());
	pgm_peer_unref (peer);
}
END_TEST


START_TEST (test_peer_unref_fail_001)
{
	pgm_peer_unref (NULL);
	fail ();
}
END_TEST

/* target:
 *	void
 *	pgm_check_peer_nak_state (
 *		pgm_transport_t*	transport
 *		)
 */

START_TEST (test_check_peer_nak_state_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	transport->is_bound = TRUE;
	pgm_check_peer_nak_state (transport, mock_pgm_time_now);
}
END_TEST

START_TEST (test_check_peer_nak_state_fail_001)
{
	pgm_check_peer_nak_state (NULL, mock_pgm_time_now);
	fail ();
}
END_TEST

/* target:
 *	pgm_time_t
 *	pgm_min_nak_expiry (
 *		pgm_time_t		expiration,
 *		pgm_transport_t*	transport
 *		)
 */

START_TEST (test_min_nak_expiry_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	transport->is_bound = TRUE;
	const pgm_time_t expiration = pgm_secs(1);
	pgm_time_t next_expiration = pgm_min_nak_expiry (expiration, transport);
}
END_TEST

START_TEST (test_min_nak_expiry_fail_001)
{
	const pgm_time_t expiration = pgm_secs(1);
	pgm_min_nak_expiry (expiration, NULL);
	fail ();
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_rxw_sqns (
 *		pgm_transport_t*	transport,
 *		const guint		sqns
 *	)
 */

START_TEST (test_set_rxw_sqns_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_rxw_sqns (transport, 100));
}
END_TEST

START_TEST (test_set_rxw_sqns_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_rxw_sqns (NULL, 100));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_rxw_secs (
 *		pgm_transport_t*	transport,
 *		const guint		secs
 *	)
 */

START_TEST (test_set_rxw_secs_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_rxw_secs (transport, 10));
}
END_TEST

START_TEST (test_set_rxw_secs_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_rxw_secs (NULL, 10));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_rxw_max_rte (
 *		pgm_transport_t*	transport,
 *		const guint		rate
 *	)
 */

START_TEST (test_set_rxw_max_rte_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_rxw_max_rte (transport, 100*1000));
}
END_TEST

START_TEST (test_set_rxw_max_rte_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_rxw_max_rte (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_peer_expiry (
 *		pgm_transport_t*	transport,
 *		const guint		expiration_time
 *	)
 */

START_TEST (test_set_peer_expiry_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->spm_ambient_interval = pgm_secs(30);
	fail_unless (TRUE == pgm_transport_set_peer_expiry (transport, pgm_secs(100)));
}
END_TEST

START_TEST (test_set_peer_expiry_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_peer_expiry (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_spmr_expiry (
 *		pgm_transport_t*	transport,
 *		const guint		expiration_time
 *	)
 */

START_TEST (test_set_spmr_expiry_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->spm_ambient_interval = pgm_secs(30);
	fail_unless (TRUE == pgm_transport_set_spmr_expiry (transport, pgm_secs(10)));
}
END_TEST

START_TEST (test_set_spmr_expiry_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_spmr_expiry (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_bo_ivl (
 *		pgm_transport_t*	transport,
 *		const guint		interval
 *	)
 */

START_TEST (test_set_nak_bo_ivl_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_nak_bo_ivl (transport, 100*1000));
}
END_TEST

START_TEST (test_set_nak_bo_ivl_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_bo_ivl (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_rpt_ivl (
 *		pgm_transport_t*	transport,
 *		const guint		interval
 *	)
 */

START_TEST (test_set_nak_rpt_ivl_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_nak_rpt_ivl (transport, 100*1000));
}
END_TEST

START_TEST (test_set_nak_rpt_ivl_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_rpt_ivl (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_rdata_ivl (
 *		pgm_transport_t*	transport,
 *		const guint		interval
 *	)
 */

START_TEST (test_set_nak_rdata_ivl_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_nak_rdata_ivl (transport, 100*1000));
}
END_TEST

START_TEST (test_set_nak_rdata_ivl_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_rdata_ivl (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_data_retries (
 *		pgm_transport_t*	transport,
 *		const guint		cnt
 *	)
 */

START_TEST (test_set_nak_data_retries_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_nak_data_retries (transport, 100));
}
END_TEST

START_TEST (test_set_nak_data_retries_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_data_retries (NULL, 100));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_ncf_retries (
 *		pgm_transport_t*	transport,
 *		const guint		cnt
 *	)
 */

START_TEST (test_set_nak_ncf_retries_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_nak_ncf_retries (transport, 100));
}
END_TEST

START_TEST (test_set_nak_ncf_retries_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_ncf_retries (NULL, 100));
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_peer_unref = tcase_create ("peer_unref");
	suite_add_tcase (s, tc_peer_unref);
	tcase_add_checked_fixture (tc_peer_unref, mock_setup, NULL);
	tcase_add_test (tc_peer_unref, test_peer_unref_pass_001);
	tcase_add_test_raise_signal (tc_peer_unref, test_peer_unref_fail_001, SIGABRT);

	TCase* tc_check_peer_nak_state = tcase_create ("check-peer-nak-state");
	suite_add_tcase (s, tc_check_peer_nak_state);
	tcase_add_checked_fixture (tc_check_peer_nak_state, mock_setup, NULL);
	tcase_add_test (tc_check_peer_nak_state, test_check_peer_nak_state_pass_001);
	tcase_add_test_raise_signal (tc_check_peer_nak_state, test_check_peer_nak_state_fail_001, SIGABRT);

	TCase* tc_min_nak_expiry = tcase_create ("min-nak-expiry");
	suite_add_tcase (s, tc_min_nak_expiry);
	tcase_add_checked_fixture (tc_min_nak_expiry, mock_setup, NULL);
	tcase_add_test (tc_min_nak_expiry, test_min_nak_expiry_pass_001);
	tcase_add_test_raise_signal (tc_min_nak_expiry, test_min_nak_expiry_fail_001, SIGABRT);

	TCase* tc_set_rxw_sqns = tcase_create ("set-rxw_sqns");
	suite_add_tcase (s, tc_set_rxw_sqns);
	tcase_add_checked_fixture (tc_set_rxw_sqns, mock_setup, NULL);
	tcase_add_test (tc_set_rxw_sqns, test_set_rxw_sqns_pass_001);
	tcase_add_test (tc_set_rxw_sqns, test_set_rxw_sqns_fail_001);

	TCase* tc_set_rxw_secs = tcase_create ("set-rxw-secs");
	suite_add_tcase (s, tc_set_rxw_secs);
	tcase_add_checked_fixture (tc_set_rxw_secs, mock_setup, NULL);
	tcase_add_test (tc_set_rxw_secs, test_set_rxw_secs_pass_001);
	tcase_add_test (tc_set_rxw_secs, test_set_rxw_secs_fail_001);

	TCase* tc_set_rxw_max_rte = tcase_create ("set-rxw-max-rte");
	suite_add_tcase (s, tc_set_rxw_max_rte);
	tcase_add_checked_fixture (tc_set_rxw_max_rte, mock_setup, NULL);
	tcase_add_test (tc_set_rxw_max_rte, test_set_rxw_max_rte_pass_001);
	tcase_add_test (tc_set_rxw_max_rte, test_set_rxw_max_rte_fail_001);

	TCase* tc_set_peer_expiry = tcase_create ("set-peer-expiry");
	suite_add_tcase (s, tc_set_peer_expiry);
	tcase_add_checked_fixture (tc_set_peer_expiry, mock_setup, NULL);
	tcase_add_test (tc_set_peer_expiry, test_set_peer_expiry_pass_001);
	tcase_add_test (tc_set_peer_expiry, test_set_peer_expiry_fail_001);

	TCase* tc_set_spmr_expiry = tcase_create ("set-spmr-expiry");
	suite_add_tcase (s, tc_set_spmr_expiry);
	tcase_add_checked_fixture (tc_set_spmr_expiry, mock_setup, NULL);
	tcase_add_test (tc_set_spmr_expiry, test_set_spmr_expiry_pass_001);
	tcase_add_test (tc_set_spmr_expiry, test_set_spmr_expiry_fail_001);

	TCase* tc_set_nak_bo_ivl = tcase_create ("set-nak-bo-ivl");
	suite_add_tcase (s, tc_set_nak_bo_ivl);
	tcase_add_checked_fixture (tc_set_nak_bo_ivl, mock_setup, NULL);
	tcase_add_test (tc_set_nak_bo_ivl, test_set_nak_bo_ivl_pass_001);
	tcase_add_test (tc_set_nak_bo_ivl, test_set_nak_bo_ivl_fail_001);

	TCase* tc_set_nak_rpt_ivl = tcase_create ("set-nak-rpt-ivl");
	suite_add_tcase (s, tc_set_nak_rpt_ivl);
	tcase_add_checked_fixture (tc_set_nak_rpt_ivl, mock_setup, NULL);
	tcase_add_test (tc_set_nak_rpt_ivl, test_set_nak_rpt_ivl_pass_001);
	tcase_add_test (tc_set_nak_rpt_ivl, test_set_nak_rpt_ivl_fail_001);

	TCase* tc_set_nak_rdata_ivl = tcase_create ("set-nak-rdata-ivl");
	suite_add_tcase (s, tc_set_nak_rdata_ivl);
	tcase_add_checked_fixture (tc_set_nak_rdata_ivl, mock_setup, NULL);
	tcase_add_test (tc_set_nak_rdata_ivl, test_set_nak_rdata_ivl_pass_001);
	tcase_add_test (tc_set_nak_rdata_ivl, test_set_nak_rdata_ivl_fail_001);

	TCase* tc_set_nak_data_retries = tcase_create ("set-nak-data-retries");
	suite_add_tcase (s, tc_set_nak_data_retries);
	tcase_add_checked_fixture (tc_set_nak_data_retries, mock_setup, NULL);
	tcase_add_test (tc_set_nak_data_retries, test_set_nak_data_retries_pass_001);
	tcase_add_test (tc_set_nak_data_retries, test_set_nak_data_retries_fail_001);

	TCase* tc_set_nak_ncf_retries = tcase_create ("set-nak-ncf-retries");
	suite_add_tcase (s, tc_set_nak_ncf_retries);
	tcase_add_checked_fixture (tc_set_nak_ncf_retries, mock_setup, NULL);
	tcase_add_test (tc_set_nak_ncf_retries, test_set_nak_ncf_retries_pass_001);
	tcase_add_test (tc_set_nak_ncf_retries, test_set_nak_ncf_retries_fail_001);
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
