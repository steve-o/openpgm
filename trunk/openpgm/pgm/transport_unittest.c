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
#include <stdbool.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <glib.h>
#include <check.h>


/* mock state */



#define pgm_ipproto_pgm		mock_pgm_ipproto_pgm
#define pgm_peer_unref		mock_pgm_peer_unref
#define pgm_on_nak_notify	mock_pgm_on_nak_notify
#define pgm_send_spm		mock_pgm_send_spm
#define pgm_timer_prepare	mock_pgm_timer_prepare
#define pgm_timer_check		mock_pgm_timer_check
#define pgm_timer_expiration	mock_pgm_timer_expiration
#define pgm_timer_dispatch	mock_pgm_timer_dispatch
#define pgm_txw_create		mock_pgm_txw_create
#define pgm_txw_shutdown	mock_pgm_txw_shutdown
#define pgm_rate_create		mock_pgm_rate_create
#define pgm_rate_destroy	mock_pgm_rate_destroy
#define pgm_rate_remaining	mock_pgm_rate_remaining
#define pgm_rs_create		mock_pgm_rs_create
#define pgm_rs_destroy		mock_pgm_rs_destroy
#define pgm_time_update_now	mock_pgm_time_update_now

#define TRANSPORT_DEBUG
#include "transport.c"

int mock_pgm_ipproto_pgm = IPPROTO_PGM;


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
	struct pgm_transport_info_t* tinfo = g_new0(struct pgm_transport_info_t, 1);
	tinfo->ti_recv_addrs_len = 1;
	tinfo->ti_recv_addrs = g_new0(struct group_source_req, 1);
	((struct sockaddr*)&tinfo->ti_recv_addrs[0].gsr_group)->sa_family = AF_INET;
	((struct sockaddr_in*)&tinfo->ti_recv_addrs[0].gsr_group)->sin_addr.s_addr = default_group;
	((struct sockaddr*)&tinfo->ti_recv_addrs[0].gsr_source)->sa_family = AF_INET;
	((struct sockaddr_in*)&tinfo->ti_recv_addrs[0].gsr_source)->sin_addr.s_addr = default_group;
	tinfo->ti_send_addrs_len = 1;
	tinfo->ti_send_addrs = g_new0(struct group_source_req, 1);
	((struct sockaddr*)&tinfo->ti_send_addrs[0].gsr_group)->sa_family = AF_INET;
	((struct sockaddr_in*)&tinfo->ti_send_addrs[0].gsr_group)->sin_addr.s_addr = default_group;
	((struct sockaddr*)&tinfo->ti_send_addrs[0].gsr_source)->sa_family = AF_INET;
	((struct sockaddr_in*)&tinfo->ti_send_addrs[0].gsr_source)->sin_addr.s_addr = default_group;
	return tinfo;
}

/** receiver module */
PGM_GNUC_INTERNAL
void
mock_pgm_peer_unref (
	pgm_peer_t*		peer
	)
{
}

/** source module */
static
bool
mock_pgm_on_nak_notify (
	GIOChannel*		source,
	GIOCondition		condition,
	gpointer		data
	)
{
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_send_spm (
	pgm_transport_t*	transport,
	int			flags
	)
{
	return TRUE;
}

/** timer module */
PGM_GNUC_INTERNAL
bool
mock_pgm_timer_prepare (
	pgm_transport_t* const		transport
	)
{
	return FALSE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_timer_check (
	pgm_transport_t* const		transport
	)
{
	return FALSE;
}

PGM_GNUC_INTERNAL
pgm_time_t
mock_pgm_timer_expiration (
	pgm_transport_t* const		transport
	)
{
	return 100L;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_timer_dispatch (
	pgm_transport_t* const		transport
	)
{
	return TRUE;
}

/** transmit window module */
pgm_txw_t*
mock_pgm_txw_create (
	const pgm_tsi_t* const	tsi,
	const uint16_t		tpdu_size,
	const uint32_t		sqns,
	const unsigned		secs,
	const ssize_t		max_rte,
	const bool		use_fec,
	const uint8_t		rs_n,
	const uint8_t		rs_k
	)
{
	pgm_txw_t* window = g_new0 (pgm_txw_t, 1);
	return window;
}

void
mock_pgm_txw_shutdown (
	pgm_txw_t* const	window
	)
{
	g_free (window);
}

/** rate control module */
PGM_GNUC_INTERNAL
void
mock_pgm_rate_create (
	pgm_rate_t*		bucket,
	ssize_t			rate_per_sec,
	size_t			iphdr_len,
	uint16_t		max_tpdu
	)
{
}

PGM_GNUC_INTERNAL
void
mock_pgm_rate_destroy (
	pgm_rate_t*		bucket
	)
{
}

PGM_GNUC_INTERNAL
pgm_time_t
mock_pgm_rate_remaining (
	pgm_rate_t*		bucket,
	gsize			packetlen
	)
{
	return 0;
}

/** reed solomon module */
void
mock_pgm_rs_create (
	pgm_rs_t*		rs,
	const uint8_t		n,
	const uint8_t		k
	)
{
}

void
mock_pgm_rs_destroy (
	pgm_rs_t*		rs
	)
{
}

/** time module */
static pgm_time_t _mock_pgm_time_update_now (void);
pgm_time_update_func mock_pgm_time_update_now = _mock_pgm_time_update_now;

static
pgm_time_t
_mock_pgm_time_update_now (void)
{
	return 0x1;
}


/* mock functions for external references */


/* target:
 *	bool
 *	pgm_transport_create (
 *		pgm_transport_t**		transport,
 *		struct pgm_transport_info_t*	tinfo,
 *		pgm_error_t**			error
 *	)
 */

START_TEST (test_create_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
}
END_TEST

/* NULL transport */
START_TEST (test_create_fail_002)
{
	pgm_error_t* err = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (FALSE == pgm_transport_create (NULL, tinfo, &err), "create failed");
}
END_TEST

/* NULL tinfo */
START_TEST (test_create_fail_003)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	fail_unless (FALSE == pgm_transport_create (&transport, NULL, &err), "create failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_bind (
 *		pgm_transport_t*	transport,
 *		pgm_error_t**		error
 *		)
 */

START_TEST (test_bind_fail_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (FALSE == pgm_transport_bind (transport, &err), "bind failed");
}
END_TEST

START_TEST (test_bind_fail_002)
{
	pgm_error_t* err = NULL;
	fail_unless (FALSE == pgm_transport_bind (NULL, &err), "bind failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_destroy (
 *		pgm_transport_t*	transport,
 *		bool			flush
 *		)
 */

START_TEST (test_destroy_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_destroy (transport, FALSE), "destroy failed");
}
END_TEST

START_TEST (test_destroy_fail_001)
{
	fail_unless (FALSE == pgm_transport_destroy (NULL, FALSE), "destroy failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_set_max_tpdu (
 *		pgm_transport_t*	transport,
 *		uint16_t		max_tpdu
 *		)
 */

START_TEST (test_set_max_tpdu_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_set_max_tpdu (transport, 1500), "set_max_tpdu failed");
}
END_TEST

START_TEST (test_set_max_tpdu_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_max_tpdu (NULL, 1500), "set_max_tpdu failed");
}
END_TEST

START_TEST (test_set_max_tpdu_fail_002)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (FALSE == pgm_transport_set_max_tpdu (transport, 1), "set_max_tpdu failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_set_multicast_loop (
 *		pgm_transport_t*	transport,
 *		bool			use_multicast_loop
 *		)
 */

START_TEST (test_set_multicast_loop_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_set_multicast_loop (transport, TRUE), "set_multicast_loop failed");
}
END_TEST

START_TEST (test_set_multicast_loop_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_multicast_loop (NULL, TRUE), "set_multicast_loop failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_set_hops (
 *		pgm_transport_t*	transport,
 *		unsigned		hops
 *	)
 */

START_TEST (test_set_hops_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_set_hops (transport, 16), "set_hops failed");
}
END_TEST

START_TEST (test_set_hops_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_hops (NULL, 16), "set_hops failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_set_sndbuf (
 *		pgm_transport_t*	transport,
 *		size_t			size
 *	)
 */

START_TEST (test_set_sndbuf_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_set_sndbuf (transport, 131071), "set_sndbuf failed");
}
END_TEST

START_TEST (test_set_sndbuf_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_sndbuf (NULL, 131071), "set_sndbuf failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_set_rcvbuf (
 *		pgm_transport_t*	transport,
 *		size_t			size
 *	)
 */

START_TEST (test_set_rcvbuf_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_set_rcvbuf (transport, 131071), "set_rcvbuf failed");
}
END_TEST

START_TEST (test_set_rcvbuf_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_rcvbuf (NULL, 131071), "set_rcvbuf failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_set_fec (
 *		pgm_transport_t*	transport,
 *		uint8_t			proactive_h,
 *		bool			use_ondemand_parity,
 *		bool			use_varpkt_len,
 *		uint8_t			default_n,
 *		uint8_t			default_k
 *	)
 */

START_TEST (test_set_fec_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_set_fec (transport, 239, TRUE, TRUE, 255, 16), "set_fec failed");
}
END_TEST

START_TEST (test_set_fec_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_fec (NULL, 0, TRUE, TRUE, 255, 16), "set_fec failed");
}
END_TEST

/* TODO: invalid Reed-Solomon parameters
 */

/* target:
 *	bool
 *	pgm_transport_set_send_only (
 *		pgm_transport_t*	transport,
 *		bool			send_only
 *	)
 */

START_TEST (test_set_send_only_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_set_send_only (transport, TRUE), "set_send_only failed");
}
END_TEST

START_TEST (test_set_send_only_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_send_only (NULL, TRUE), "set_send_only failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_set_recv_only (
 *		pgm_transport_t*	transport,
 *		bool			recv_only,
 *		bool			is_passive
 *	)
 */

START_TEST (test_set_recv_only_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_set_recv_only (transport, TRUE, FALSE), "set_recv_only failed");
}
END_TEST

START_TEST (test_set_recv_only_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_recv_only (NULL, TRUE, FALSE), "set_recv_only failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_transport_set_abort_on_reset (
 *		pgm_transport_t*	transport,
 *		bool		abort_on_reset
 *	)
 */

START_TEST (test_set_abort_on_reset_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_transport_t* transport = NULL;
	struct pgm_transport_info_t* tinfo = generate_asm_tinfo ();
	fail_if (NULL == tinfo, "generate_asm_tinfo failed");
	fail_unless (TRUE == pgm_transport_create (&transport, tinfo, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_transport_set_abort_on_reset (transport, TRUE), "set_abort_on_reset failed");
}
END_TEST

START_TEST (test_set_abort_on_reset_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_abort_on_reset (NULL, TRUE), "set_abort_on_reset failed");
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
