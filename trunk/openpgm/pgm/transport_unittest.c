/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM transport.
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

#include <pgm/transport.h>
#include <pgm/txwi.h>


/* mock state */

static int mock_ipproto_pgm = IPPROTO_PGM;

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
struct pgm_transport_info_t*
generate_asm_tinfo (void)
{
	const in_addr_t default_group = inet_addr("239.192.0.1");
	const pgm_gsi_t gsi = { 200, 202, 203, 204, 205, 206 };
	struct pgm_transport_info_t* tinfo = g_malloc0(sizeof(struct pgm_transport_info_t));
	tinfo->ti_recv_addrs_len = 1;
	tinfo->ti_recv_addrs = g_malloc0(sizeof(struct group_source_req));
	((struct sockaddr*)&tinfo->ti_recv_addrs[0].gsr_group)->sa_family = AF_INET;
	((struct sockaddr_in*)&tinfo->ti_recv_addrs[0].gsr_group)->sin_addr.s_addr = default_group;
	((struct sockaddr*)&tinfo->ti_recv_addrs[0].gsr_source)->sa_family = AF_INET;
	((struct sockaddr_in*)&tinfo->ti_recv_addrs[0].gsr_source)->sin_addr.s_addr = default_group;
	tinfo->ti_send_addrs_len = 1;
	tinfo->ti_send_addrs = g_malloc0(sizeof(struct group_source_req));
	((struct sockaddr*)&tinfo->ti_send_addrs[0].gsr_group)->sa_family = AF_INET;
	((struct sockaddr_in*)&tinfo->ti_send_addrs[0].gsr_group)->sin_addr.s_addr = default_group;
	((struct sockaddr*)&tinfo->ti_send_addrs[0].gsr_source)->sa_family = AF_INET;
	((struct sockaddr_in*)&tinfo->ti_send_addrs[0].gsr_source)->sin_addr.s_addr = default_group;
	return tinfo;
}

/** receiver module */
static
void
mock_pgm_peer_unref (
	pgm_peer_t*		peer
	)
{
}

/** source module */
static
gboolean
mock_pgm_on_nak_notify (
	GIOChannel*		source,
	GIOCondition		condition,
	gpointer		data
	)
{
	return TRUE;
}

static
int
mock_pgm_send_spm_unlocked (
	pgm_transport_t*	transport
	)
{
	return 0;
}

/** timer module */
static
gpointer
mock_pgm_timer_thread (
	gpointer		data
	)
{
	pgm_transport_t* transport = (pgm_transport_t*)data;
	transport->timer_context = g_main_context_new ();
	g_mutex_lock (transport->thread_mutex);
	transport->timer_loop = g_main_loop_new (transport->timer_context, FALSE);
	g_cond_signal (transport->thread_cond);
	g_mutex_unlock (transport->thread_mutex);
	g_main_loop_run (transport->timer_loop);
	g_main_loop_unref (transport->timer_loop);
	g_main_context_unref (transport->timer_context);
	return NULL;
}

static
int
mock_pgm_timer_add (
	pgm_transport_t*	transport
	)
{
	return 1;	/* GSource id */
}

/** transmit window module */
static
pgm_txw_t*
mock_pgm_txw_create (
	const pgm_tsi_t* const	tsi,
	const guint16		tpdu_size,
	const guint32		sqns,
	const guint		secs,
	const guint		max_rte,
	const gboolean		use_fec,
	const guint		rs_n,
	const guint		rs_k
	)
{
	pgm_txw_t* window = g_malloc0 (sizeof(pgm_txw_t));
	return window;
}

static
void
mock_pgm_txw_shutdown (
	pgm_txw_t* const	window
	)
{
	g_free (window);
}

/** rate control module */
static
void
mock_pgm_rate_create (
	gpointer*		bucket_,
	guint			rate_per_sec,
	guint			iphdr_len
	)
{
}

static
void
mock_pgm_rate_destroy (
	gpointer		bucket
	)
{
}

/** reed solomon module */
static
void
mock_pgm_rs_create (
	gpointer*		rs_,
	const guint		n,
	const guint		k
	)
{
}

static
void
mock_pgm_rs_destroy (
	gpointer		rs
	)
{
}

/** time module */
static
pgm_time_t
mock_pgm_time_update_now (void)
{
	return 0x1;
}


/* mock functions for external references */

#define ipproto_pgm		mock_ipproto_pgm
#define pgm_peer_unref		mock_pgm_peer_unref
#define pgm_on_nak_notify	mock_pgm_on_nak_notify
#define pgm_send_spm_unlocked	mock_pgm_send_spm_unlocked
#define pgm_timer_thread	mock_pgm_timer_thread
#define pgm_timer_add		mock_pgm_timer_add
#define pgm_txw_create		mock_pgm_txw_create
#define pgm_txw_shutdown	mock_pgm_txw_shutdown
#define pgm_rate_create		mock_pgm_rate_create
#define pgm_rate_destroy	mock_pgm_rate_destroy
#define pgm_rs_create		mock_pgm_rs_create
#define pgm_rs_destroy		mock_pgm_rs_destroy
#define pgm_time_update_now	mock_pgm_time_update_now

#define TRANSPORT_DEBUG
#include "transport.c"


/* target:
 *	gboolean
 *	pgm_transport_create (
 *		pgm_transport_t**		transport,
 *		struct pgm_transport_info_t*	tinfo,
 *		GError**			error
 *	)
 */

START_TEST (test_create_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
}
END_TEST

/* NULL transport */
START_TEST (test_create_fail_002)
{
	GError* err = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (FALSE == pgm_transport_create (NULL, tinfo, &err));
}
END_TEST

/* NULL tinfo */
START_TEST (test_create_fail_003)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	fail_unless (FALSE == pgm_transport_create (&transport, NULL, &err));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_bind (
 *		pgm_transport_t*	transport,
 *		GError**		error
 *		)
 */

START_TEST (test_bind_fail_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (FALSE == pgm_transport_bind (transport, &err));
}
END_TEST

START_TEST (test_bind_fail_002)
{
	GError* err = NULL;
	fail_unless (FALSE == pgm_transport_bind (NULL, &err));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_destroy (
 *		pgm_transport_t*	transport,
 *		gboolean		flush
 *		)
 */

START_TEST (test_destroy_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_destroy (transport, FALSE));
}
END_TEST

START_TEST (test_destroy_fail_001)
{
	fail_unless (FALSE == pgm_transport_destroy (NULL, FALSE));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_max_tpdu (
 *		pgm_transport_t*	transport,
 *		guint16			max_tpdu
 *		)
 */

START_TEST (test_set_max_tpdu_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_set_max_tpdu (transport, 1500));
}
END_TEST

START_TEST (test_set_max_tpdu_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_max_tpdu (NULL, 1500));
}
END_TEST

START_TEST (test_set_max_tpdu_fail_002)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (FALSE == pgm_transport_set_max_tpdu (transport, 1));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_multicast_loop (
 *		pgm_transport_t*	transport,
 *		gboolean		use_multicast_loop
 *		)
 */

START_TEST (test_set_multicast_loop_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_set_multicast_loop (transport, TRUE));
}
END_TEST

START_TEST (test_set_multicast_loop_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_multicast_loop (NULL, TRUE));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_hops (
 *		pgm_transport_t*	transport,
 *		gint			hops
 *	)
 */

START_TEST (test_set_hops_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_set_hops (transport, 16));
}
END_TEST

START_TEST (test_set_hops_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_hops (NULL, 16));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_sndbuf (
 *		pgm_transport_t*	transport,
 *		int			size
 *	)
 */

START_TEST (test_set_sndbuf_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_set_sndbuf (transport, 131071));
}
END_TEST

START_TEST (test_set_sndbuf_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_sndbuf (NULL, 131071));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_rcvbuf (
 *		pgm_transport_t*	transport,
 *		int			size
 *	)
 */

START_TEST (test_set_rcvbuf_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_set_rcvbuf (transport, 131071));
}
END_TEST

START_TEST (test_set_rcvbuf_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_rcvbuf (NULL, 131071));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_fec (
 *		pgm_transport_t*	transport,
 *		guint			proactive_h,
 *		gboolean		use_ondemand_parity,
 *		gboolean		use_varpkt_len,
 *		guint			default_n,
 *		guint			default_k
 *	)
 */

START_TEST (test_set_fec_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_set_fec (transport, 239, TRUE, TRUE, 255, 16));
}
END_TEST

START_TEST (test_set_fec_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_fec (NULL, 0, TRUE, TRUE, 255, 16));
}
END_TEST

/* TODO: invalid Reed-Solomon parameters
 */

/* target:
 *	gboolean
 *	pgm_transport_set_send_only (
 *		pgm_transport_t*	transport,
 *		gboolean		send_only
 *	)
 */

START_TEST (test_set_send_only_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_set_send_only (transport, TRUE));
}
END_TEST

START_TEST (test_set_send_only_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_send_only (NULL, TRUE));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_recv_only (
 *		pgm_transport_t*	transport,
 *		gboolean		is_passive
 *	)
 */

START_TEST (test_set_recv_only_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_set_recv_only (transport, TRUE));
}
END_TEST

START_TEST (test_set_recv_only_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_recv_only (NULL, TRUE));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_abort_on_reset (
 *		pgm_transport_t*	transport,
 *		gboolean		abort_on_reset
 *	)
 */

START_TEST (test_set_abort_on_reset_pass_001)
{
	GError* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_transport_set_abort_on_reset (transport, TRUE));
}
END_TEST

START_TEST (test_set_abort_on_reset_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_abort_on_reset (NULL, TRUE));
}
END_TEST

#if 0
static inline gsize pgm_transport_max_tsdu (pgm_transport_t* transport, gboolean can_fragment)
int pgm_transport_select_info (pgm_transport_t*, fd_set*, fd_set*, int*);
#ifdef CONFIG_HAVE_POLL
int pgm_transport_poll_info (pgm_transport_t*, struct pollfd*, int*, int);
#endif
#ifdef CONFIG_HAVE_EPOLL
int pgm_transport_epoll_ctl (pgm_transport_t*, int, int, int);
#endif

int pgm_transport_join_group (pgm_transport_t*, struct group_req*, gsize);
int pgm_transport_leave_group (pgm_transport_t*, struct group_req*, gsize);
int pgm_transport_block_source (pgm_transport_t*, struct group_source_req*, gsize);
int pgm_transport_unblock_source (pgm_transport_t*, struct group_source_req*, gsize);
int pgm_transport_join_source_group (pgm_transport_t*, struct group_source_req*, gsize);
int pgm_transport_leave_source_group (pgm_transport_t*, struct group_source_req*, gsize);
int pgm_transport_msfilter (pgm_transport_t*, struct group_filter*, gsize);

gchar* pgm_print_tsi (const pgm_tsi_t*) G_GNUC_WARN_UNUSED_RESULT;
int pgm_print_tsi_r (const pgm_tsi_t*, char*, gsize);
guint pgm_tsi_hash (gconstpointer) G_GNUC_WARN_UNUSED_RESULT;
gboolean pgm_tsi_equal (gconstpointer, gconstpointer) G_GNUC_WARN_UNUSED_RESULT;
void pgm_drop_superuser (void);
#endif

static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_create = tcase_create ("create");
	suite_add_tcase (s, tc_create);
	tcase_add_checked_fixture (tc_create, mock_setup, mock_teardown);
	tcase_add_test (tc_create, test_create_pass_001);
	tcase_add_test (tc_create, test_create_fail_002);
	tcase_add_test (tc_create, test_create_fail_003);

	TCase* tc_bind = tcase_create ("bind");
	suite_add_tcase (s, tc_bind);
	tcase_add_checked_fixture (tc_bind, mock_setup, mock_teardown);
	tcase_add_test (tc_bind, test_bind_fail_001);
	tcase_add_test (tc_bind, test_bind_fail_002);

	TCase* tc_destroy = tcase_create ("destroy");
	suite_add_tcase (s, tc_destroy);
	tcase_add_checked_fixture (tc_destroy, mock_setup, mock_teardown);
	tcase_add_test (tc_destroy, test_destroy_pass_001);
	tcase_add_test (tc_destroy, test_destroy_fail_001);

	TCase* tc_set_max_tpdu = tcase_create ("set-max-tpdu");
	suite_add_tcase (s, tc_set_max_tpdu);
	tcase_add_checked_fixture (tc_set_max_tpdu, mock_setup, mock_teardown);
	tcase_add_test (tc_set_max_tpdu, test_set_max_tpdu_pass_001);
	tcase_add_test (tc_set_max_tpdu, test_set_max_tpdu_fail_001);

	TCase* tc_set_multicast_loop = tcase_create ("set-multicast-loop");
	suite_add_tcase (s, tc_set_multicast_loop);
	tcase_add_checked_fixture (tc_set_multicast_loop, mock_setup, mock_teardown);
	tcase_add_test (tc_set_multicast_loop, test_set_multicast_loop_pass_001);
	tcase_add_test (tc_set_multicast_loop, test_set_multicast_loop_fail_001);

	TCase* tc_set_hops = tcase_create ("set-hops");
	suite_add_tcase (s, tc_set_hops);
	tcase_add_checked_fixture (tc_set_hops, mock_setup, mock_teardown);
	tcase_add_test (tc_set_hops, test_set_hops_pass_001);
	tcase_add_test (tc_set_hops, test_set_hops_fail_001);

	TCase* tc_set_sndbuf = tcase_create ("set-sndbuf");
	suite_add_tcase (s, tc_set_sndbuf);
	tcase_add_checked_fixture (tc_set_sndbuf, mock_setup, mock_teardown);
	tcase_add_test (tc_set_sndbuf, test_set_sndbuf_pass_001);
	tcase_add_test (tc_set_sndbuf, test_set_sndbuf_fail_001);

	TCase* tc_set_rcvbuf = tcase_create ("set-rcvbuf");
	suite_add_tcase (s, tc_set_rcvbuf);
	tcase_add_checked_fixture (tc_set_rcvbuf, mock_setup, mock_teardown);
	tcase_add_test (tc_set_rcvbuf, test_set_rcvbuf_pass_001);
	tcase_add_test (tc_set_rcvbuf, test_set_rcvbuf_fail_001);

	TCase* tc_set_fec = tcase_create ("set-fec");
	suite_add_tcase (s, tc_set_fec);
	tcase_add_checked_fixture (tc_set_fec, mock_setup, mock_teardown);
	tcase_add_test (tc_set_fec, test_set_fec_pass_001);
	tcase_add_test (tc_set_fec, test_set_fec_fail_001);

	TCase* tc_set_send_only = tcase_create ("set-send-only");
	suite_add_tcase (s, tc_set_send_only);
	tcase_add_checked_fixture (tc_set_send_only, mock_setup, mock_teardown);
	tcase_add_test (tc_set_send_only, test_set_send_only_pass_001);
	tcase_add_test (tc_set_send_only, test_set_send_only_fail_001);

	TCase* tc_set_recv_only = tcase_create ("set-recv-only");
	suite_add_tcase (s, tc_set_recv_only);
	tcase_add_checked_fixture (tc_set_recv_only, mock_setup, mock_teardown);
	tcase_add_test (tc_set_recv_only, test_set_recv_only_pass_001);
	tcase_add_test (tc_set_recv_only, test_set_recv_only_fail_001);

	TCase* tc_set_abort_on_reset = tcase_create ("set-abort-on-reset");
	suite_add_tcase (s, tc_set_abort_on_reset);
	tcase_add_checked_fixture (tc_set_abort_on_reset, mock_setup, mock_teardown);
	tcase_add_test (tc_set_abort_on_reset, test_set_abort_on_reset_pass_001);
	tcase_add_test (tc_set_abort_on_reset, test_set_abort_on_reset_fail_001);
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
