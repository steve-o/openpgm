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
generate_apdu (
	const pgm_tsi_t*	sender,
	const gsize		len
	)
{
}

/** receive window module */
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
	pgm_rxw_t* rxw = g_malloc0 (sizeof(pgm_rxw_t));
	return rxw;
}

static
void
mock_pgm_rxw_destroy (
	pgm_rxw_t* const	window
	)
{
}

static
int
mock_pgm_rxw_add (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb,
	const pgm_time_t		nak_rb_expiry
	)
{
	return PGM_RXW_APPENDED;
}

static
guint
mock_pgm_rxw_update (
	pgm_rxw_t* const	window,
	const guint32		txw_lead,
	const guint32		txw_trail,
	const pgm_time_t	nak_rb_expiry
	)
{
	return 0;
}

static
gssize
mock_pgm_rxw_readv (
	pgm_rxw_t* const	window,
	pgm_msgv_t**		pmsg,
	const guint		msg_len
	)
{
	return 100;
}

static
void
mock_pgm_rxw_state (
	pgm_rxw_t* const	window,
	struct pgm_sk_buff_t*	skb,
	pgm_pkt_state_e		new_state
	)
{
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
int
mock_pgm_rxw_confirm (
	pgm_rxw_t* const	window,
	const guint32		sequence,
	const pgm_time_t	nak_rdata_expiry,
	const pgm_time_t	nak_rb_expiry
	)
{
	return PGM_RXW_DUPLICATE;
}

/** packet module */
static
gboolean
mock_pgm_parse_raw (
	struct pgm_sk_buff_t* const	skb,
	struct sockaddr* const		dst,
	GError**			error
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_parse_udp_encap (
	struct pgm_sk_buff_t* const	skb,
	GError**			error
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_spm (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_nak (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_ncf (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
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
mock__pgm_on_nak (
	pgm_transport_t* const		transport,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock__pgm_on_nnak (
	pgm_transport_t* const		transport,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock__pgm_on_spmr (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		peer,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

/** reed-solomon module */
static
void
mock__pgm_rs_create (
	gpointer*		rs_,
	guint			n,
	guint			k
	)
{
}

static
void
mock__pgm_rs_destroy (
	gpointer		rs
	)
{
}

/** net module */
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

/** checksum module */
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
guint16
mock_pgm_csum_fold (
	guint32				csum
	)
{
	return 0x0;
}

/** time module */
static pgm_time_t mock_pgm_time_now = 0x1;

static
pgm_time_t
mock_pgm_time_update_now (void)
{
	return 0x1;
}


/* mock functions for external references */

#define pgm_rxw_create		mock_pgm_rxw_create
#define pgm_rxw_destroy		mock_pgm_rxw_destroy
#define pgm_rxw_add		mock_pgm_rxw_add
#define pgm_rxw_update		mock_pgm_rxw_update
#define pgm_rxw_readv		mock_pgm_rxw_readv
#define pgm_rxw_state		mock_pgm_rxw_state
#define pgm_rxw_lost		mock_pgm_rxw_lost
#define pgm_rxw_confirm		mock_pgm_rxw_confirm
#define pgm_parse_raw		mock_pgm_parse_raw
#define pgm_parse_udp_encap	mock_pgm_parse_udp_encap
#define pgm_verify_spm		mock_pgm_verify_spm
#define pgm_verify_nak		mock_pgm_verify_nak
#define pgm_verify_ncf		mock_pgm_verify_ncf
#define pgm_transport_poll_info	mock_pgm_transport_poll_info
#define _pgm_on_nak		mock__pgm_on_nak
#define _pgm_on_nnak		mock__pgm_on_nnak
#define _pgm_on_spmr		mock__pgm_on_spmr
#define _pgm_rs_create		mock__pgm_rs_create
#define _pgm_rs_destroy		mock__pgm_rs_destroy
#define _pgm_sendto		mock__pgm_sendto
#define pgm_compat_csum_partial	mock_pgm_compat_csum_partial
#define pgm_csum_fold		mock_pgm_csum_fold
#define pgm_time_now		mock_pgm_time_now
#define pgm_time_update_now	mock_pgm_time_update_now

#define RECEIVER_DEBUG
#include "receiver.c"


/* target:
 *	gssize
 *	pgm_transport_recv (
 *		pgm_transport_t*	transport,
 *		gpointer		data,
 *		gsize			len,
 *		int			flags
 *		)
 */

START_TEST (test_recv_pass_001)
{
/* pre-condition on setup */
	fail_unless (NULL != g_transport);

	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	const pgm_tsi_t sender = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const gsize apdu_length = 100;
	generate_apdu (&sender, apdu_length);
	fail_unless ((gssize)apdu_length == pgm_transport_recv (g_transport, buffer, sizeof(buffer), 0));
}
END_TEST

START_TEST (test_recv_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	fail_unless (-1 == pgm_transport_recv (NULL, buffer, sizeof(buffer), 0));
}
END_TEST

/* target:
 *	gssize
 *	pgm_transport_recvfrom (
 *		pgm_transport_t*	transport,
 *		gpointer		data,
 *		gsize			len,
 *		int			flags,
 *		pgm_tsi_t*		from
 *		)
 */

START_TEST (test_recvfrom_pass_001)
{
/* pre-condition on setup */
	fail_unless (NULL != g_transport);

	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	pgm_tsi_t tsi;
	const pgm_tsi_t sender = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const gsize apdu_length = 100;
	generate_apdu (&sender, apdu_length);
	fail_unless ((gssize)apdu_length == pgm_transport_recvfrom (g_transport, buffer, sizeof(buffer), 0, &tsi));
	fail_unless (TRUE == pgm_tsi_equal (&tsi, &sender));
}
END_TEST

START_TEST (test_recvfrom_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	pgm_tsi_t tsi;
	fail_unless (-1 == pgm_transport_recvfrom (NULL, buffer, sizeof(buffer), 0, &tsi));
}
END_TEST

/* target:
 *	gssize
 *	pgm_transport_recvmsg (
 *		pgm_transport_t*	transport,
 *		pgm_msgv_t*		msgv,
 *		int			flags
 *		)
 */

START_TEST (test_recvmsg_pass_001)
{
/* pre-condition on setup */
	fail_unless (NULL != g_transport);

	pgm_msgv_t msgv;
	pgm_tsi_t tsi;
	const pgm_tsi_t sender = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const gsize apdu_length = 100;
	generate_apdu (&sender, apdu_length);
	fail_unless ((gssize)apdu_length == pgm_transport_recvmsg (g_transport, &msgv, 0));
	fail_unless (msgv.msgv_len > 0);
	fail_unless (apdu_length == msgv.msgv_skb[0]->len);
}
END_TEST

START_TEST (test_recvmsg_fail_001)
{
	pgm_msgv_t msgv;
	fail_unless (-1 == pgm_transport_recvmsg (NULL, &msgv, 0));
}
END_TEST

/* target:
 *	gssize
 *	pgm_transport_recvmsgv (
 *		pgm_transport_t*	transport,
 *		pgm_msgv_t*		msgv,
 *		guint			msgv_length,
 *		int			flags
 *		)
 */

START_TEST (test_recvmsgv_pass_001)
{
/* pre-condition on setup */
	fail_unless (NULL != g_transport);

	pgm_msgv_t msgv[1];
	pgm_tsi_t tsi;
	const pgm_tsi_t sender = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const gsize apdu_length = 100;
	generate_apdu (&sender, apdu_length);
	fail_unless ((gssize)apdu_length == pgm_transport_recvmsgv (g_transport, msgv, G_N_ELEMENTS(msgv), 0));
	fail_unless (msgv[0].msgv_len > 0);
	fail_unless (apdu_length == msgv[0].msgv_skb[0]->len);
}
END_TEST

START_TEST (test_recvmsgv_fail_001)
{
	pgm_msgv_t msgv[1];
	fail_unless (-1 == pgm_transport_recvmsgv (NULL, msgv, G_N_ELEMENTS(msgv), 0));
}
END_TEST

/* target:
 *	void
 *	_pgm_peer_unref (
 *		pgm_peer_t*		peer
 *		)
 */

START_TEST (test_peer_unref_pass_001)
{
	fail ();
}
END_TEST

START_TEST (test_peer_unref_fail_001)
{
	_pgm_peer_unref (NULL);
	fail ();
}
END_TEST

/* target:
 *	void
 *	_pgm_check_peer_nak_state (
 *		pgm_transport_t*	transport
 *		)
 */

START_TEST (test_check_peer_nak_state_pass_001)
{
/* pre-condition on setup */
	fail_unless (NULL != g_transport);

	fail ();
}
END_TEST

START_TEST (test_check_peer_nak_state_fail_001)
{
	_pgm_check_peer_nak_state (NULL);
	fail ();
}
END_TEST

/* target:
 *	pgm_time_t
 *	_pgm_min_nak_expiry (
 *		pgm_time_t		expiration,
 *		pgm_transport_t*	transport
 *		)
 */

START_TEST (test_min_nak_expiry_pass_001)
{
/* pre-condition on setup */
	fail_unless (NULL != g_transport);

	fail ();
}
END_TEST

START_TEST (test_min_nak_expiry_fail_001)
{
	const pgm_time_t expiration = 0;
	_pgm_min_nak_expiry (expiration, NULL);
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
	fail_unless (TRUE == pgm_transport_set_peer_expiry (transport, 100*1000));
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
	fail_unless (TRUE == pgm_transport_set_spmr_expiry (transport, 100*1000));
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

	TCase* tc_recv = tcase_create ("recv");
	suite_add_tcase (s, tc_recv);
	tcase_add_checked_fixture (tc_recv, mock_setup, mock_teardown);
	tcase_add_test (tc_recv, test_recv_pass_001);
	tcase_add_test (tc_recv, test_recv_fail_001);

	TCase* tc_recvfrom = tcase_create ("recvfrom");
	suite_add_tcase (s, tc_recvfrom);
	tcase_add_checked_fixture (tc_recvfrom, mock_setup, mock_teardown);
	tcase_add_test (tc_recvfrom, test_recvfrom_pass_001);
	tcase_add_test (tc_recvfrom, test_recvfrom_fail_001);

	TCase* tc_recvmsg = tcase_create ("recvmsg");
	suite_add_tcase (s, tc_recvmsg);
	tcase_add_checked_fixture (tc_recvmsg, mock_setup, mock_teardown);
	tcase_add_test (tc_recvmsg, test_recvmsg_pass_001);
	tcase_add_test (tc_recvmsg, test_recvmsg_fail_001);

	TCase* tc_recvmsgv = tcase_create ("recvmsgv");
	suite_add_tcase (s, tc_recvmsgv);
	tcase_add_checked_fixture (tc_recvmsgv, mock_setup, mock_teardown);
	tcase_add_test (tc_recvmsgv, test_recvmsgv_pass_001);
	tcase_add_test (tc_recvmsgv, test_recvmsgv_fail_001);

	TCase* tc_peer_unref = tcase_create ("peer_unref");
	suite_add_tcase (s, tc_peer_unref);
	tcase_add_checked_fixture (tc_peer_unref, mock_setup, mock_teardown);
	tcase_add_test (tc_peer_unref, test_peer_unref_pass_001);
	tcase_add_test (tc_peer_unref, test_peer_unref_fail_001);

	TCase* tc_check_peer_nak_state = tcase_create ("check-peer-nak-state");
	suite_add_tcase (s, tc_check_peer_nak_state);
	tcase_add_checked_fixture (tc_check_peer_nak_state, mock_setup, mock_teardown);
	tcase_add_test (tc_check_peer_nak_state, test_check_peer_nak_state_pass_001);
	tcase_add_test (tc_check_peer_nak_state, test_check_peer_nak_state_fail_001);

	TCase* tc_min_nak_expiry = tcase_create ("min-nak-expiry");
	suite_add_tcase (s, tc_min_nak_expiry);
	tcase_add_checked_fixture (tc_min_nak_expiry, mock_setup, mock_teardown);
	tcase_add_test (tc_min_nak_expiry, test_min_nak_expiry_pass_001);
	tcase_add_test (tc_min_nak_expiry, test_min_nak_expiry_fail_001);

	TCase* tc_set_rxw_sqns = tcase_create ("set-rxw_sqns");
	suite_add_tcase (s, tc_set_rxw_sqns);
	tcase_add_checked_fixture (tc_set_rxw_sqns, mock_setup, mock_teardown);
	tcase_add_test (tc_set_rxw_sqns, test_set_rxw_sqns_pass_001);
	tcase_add_test (tc_set_rxw_sqns, test_set_rxw_sqns_fail_001);

	TCase* tc_set_rxw_secs = tcase_create ("set-rxw-secs");
	suite_add_tcase (s, tc_set_rxw_secs);
	tcase_add_checked_fixture (tc_set_rxw_secs, mock_setup, mock_teardown);
	tcase_add_test (tc_set_rxw_secs, test_set_rxw_secs_pass_001);
	tcase_add_test (tc_set_rxw_secs, test_set_rxw_secs_fail_001);

	TCase* tc_set_rxw_max_rte = tcase_create ("set-rxw-max-rte");
	suite_add_tcase (s, tc_set_rxw_max_rte);
	tcase_add_checked_fixture (tc_set_rxw_max_rte, mock_setup, mock_teardown);
	tcase_add_test (tc_set_rxw_max_rte, test_set_rxw_max_rte_pass_001);
	tcase_add_test (tc_set_rxw_max_rte, test_set_rxw_max_rte_fail_001);

	TCase* tc_set_peer_expiry = tcase_create ("set-peer-expiry");
	suite_add_tcase (s, tc_set_peer_expiry);
	tcase_add_checked_fixture (tc_set_peer_expiry, mock_setup, mock_teardown);
	tcase_add_test (tc_set_peer_expiry, test_set_peer_expiry_pass_001);
	tcase_add_test (tc_set_peer_expiry, test_set_peer_expiry_fail_001);

	TCase* tc_set_spmr_expiry = tcase_create ("set-spmr-expiry");
	suite_add_tcase (s, tc_set_spmr_expiry);
	tcase_add_checked_fixture (tc_set_spmr_expiry, mock_setup, mock_teardown);
	tcase_add_test (tc_set_spmr_expiry, test_set_spmr_expiry_pass_001);
	tcase_add_test (tc_set_spmr_expiry, test_set_spmr_expiry_fail_001);

	TCase* tc_set_nak_bo_ivl = tcase_create ("set-nak-bo-ivl");
	suite_add_tcase (s, tc_set_nak_bo_ivl);
	tcase_add_checked_fixture (tc_set_nak_bo_ivl, mock_setup, mock_teardown);
	tcase_add_test (tc_set_nak_bo_ivl, test_set_nak_bo_ivl_pass_001);
	tcase_add_test (tc_set_nak_bo_ivl, test_set_nak_bo_ivl_fail_001);

	TCase* tc_set_nak_rpt_ivl = tcase_create ("set-nak-rpt-ivl");
	suite_add_tcase (s, tc_set_nak_rpt_ivl);
	tcase_add_checked_fixture (tc_set_nak_rpt_ivl, mock_setup, mock_teardown);
	tcase_add_test (tc_set_nak_rpt_ivl, test_set_nak_rpt_ivl_pass_001);
	tcase_add_test (tc_set_nak_rpt_ivl, test_set_nak_rpt_ivl_fail_001);

	TCase* tc_set_nak_rdata_ivl = tcase_create ("set-nak-rdata-ivl");
	suite_add_tcase (s, tc_set_nak_rdata_ivl);
	tcase_add_checked_fixture (tc_set_nak_rdata_ivl, mock_setup, mock_teardown);
	tcase_add_test (tc_set_nak_rdata_ivl, test_set_nak_rdata_ivl_pass_001);
	tcase_add_test (tc_set_nak_rdata_ivl, test_set_nak_rdata_ivl_fail_001);

	TCase* tc_set_nak_data_retries = tcase_create ("set-nak-data-retries");
	suite_add_tcase (s, tc_set_nak_data_retries);
	tcase_add_checked_fixture (tc_set_nak_data_retries, mock_setup, mock_teardown);
	tcase_add_test (tc_set_nak_data_retries, test_set_nak_data_retries_pass_001);
	tcase_add_test (tc_set_nak_data_retries, test_set_nak_data_retries_fail_001);

	TCase* tc_set_nak_ncf_retries = tcase_create ("set-nak-ncf-retries");
	suite_add_tcase (s, tc_set_nak_ncf_retries);
	tcase_add_checked_fixture (tc_set_nak_ncf_retries, mock_setup, mock_teardown);
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
