/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM timer thread.
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


#define g_main_context_new		mock_g_main_context_new
#define g_main_context_unref		mock_g_main_context_unref
#define g_main_loop_new			mock_g_main_loop_new
#define g_main_loop_run			mock_g_main_loop_run
#define g_main_loop_unref		mock_g_main_loop_unref
#define g_source_new			mock_g_source_new
#define g_source_set_priority		mock_g_source_set_priority
#define g_source_attach			mock_g_source_attach
#define g_source_unref			mock_g_source_unref
#define pgm_time_now			mock_pgm_time_now
#define pgm_time_update_now		mock_pgm_time_update_now
#define pgm_min_receiver_expiry		mock_pgm_min_receiver_expiry
#define pgm_check_peer_state		mock_pgm_check_peer_state
#define pgm_send_spm			mock_pgm_send_spm


#define TIMER_DEBUG
#include "timer.c"

static pgm_time_t _mock_pgm_time_update_now(void);
pgm_time_update_func mock_pgm_time_update_now = _mock_pgm_time_update_now;
static pgm_time_t mock_pgm_time_now = 0x1;


static
pgm_sock_t*
generate_sock (void)
{
	pgm_sock_t* sock = g_new0 (pgm_sock_t, 1);
	return sock;
}


/* mock functions for external references */

size_t
pgm_pkt_offset (
	const bool		can_fragment,
	const sa_family_t	pgmcc_family	/* 0 = disable */
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

/** GLib */
static
GMainContext*
mock_g_main_context_new (void)
{
	GMainContext* context = g_malloc0 (sizeof(gpointer));
	return context;
}

static
GMainLoop*
mock_g_main_loop_new (
	GMainContext*		context,
	gboolean		is_running
	)
{
	g_assert (NULL != context);
	GMainLoop* loop = g_malloc0 (sizeof(gpointer));
	return loop;
}

static
void
mock_g_main_loop_run (
	GMainLoop*		loop
	)
{
	g_assert (NULL != loop);
}

static
void
mock_g_main_loop_unref (
	GMainLoop*		loop
	)
{
	g_assert (NULL != loop);
	g_free (loop);
}

static
void
mock_g_main_context_unref (
	GMainContext*		context
	)
{
	g_assert (NULL != context);
	g_free (context);
}

static
GSource*
mock_g_source_new (
	GSourceFuncs*		source_funcs,
	guint			struct_size
	)
{
	g_assert (struct_size > 0);
	GSource* source = g_malloc0 (struct_size);
	return source;
}

static
void
mock_g_source_set_priority (
	GSource*		source,
	gint			priority
	)
{
	g_assert (NULL != source);
}

static
guint
mock_g_source_attach (
	GSource*		source,
	GMainContext*		context
	)
{
	g_assert (NULL != source);
	return 1;
}

static
void
mock_g_source_unref (
	GSource*		source
	)
{
	g_assert (NULL != source);
	g_free (source);
}

/** time module */
static
pgm_time_t
_mock_pgm_time_update_now (void)
{
	return mock_pgm_time_now;
}

/** receiver module */
PGM_GNUC_INTERNAL
pgm_time_t
mock_pgm_min_receiver_expiry (
	pgm_sock_t*		sock,
	pgm_time_t		expiration
	)
{
	g_assert (NULL != sock);
	return 0x1;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_check_peer_state (
	pgm_sock_t*		sock,
	pgm_time_t		now
	)
{
	g_assert (NULL != sock);
	return TRUE;
}

/** source module */
PGM_GNUC_INTERNAL
bool
mock_pgm_send_spm (
	pgm_sock_t*		sock,
	int			flags
	)
{
	g_assert (NULL != sock);
	return TRUE;
}


/* target:
 *	bool
 *	pgm_timer_prepare (
 *		pgm_sock_t*	sock
 *	)
 */

START_TEST (test_prepare_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	sock->can_send_data = TRUE;
	sock->next_ambient_spm = mock_pgm_time_now + pgm_secs(10);
	fail_unless (FALSE == pgm_timer_prepare (sock), "prepare failed");
}
END_TEST

START_TEST (test_prepare_fail_001)
{
	gboolean expired = pgm_timer_prepare (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	bool
 *	pgm_timer_check (
 *		pgm_sock_t*	sock
 *	)
 */

START_TEST (test_check_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	fail_unless (TRUE == pgm_timer_check (sock), "check failed");
}
END_TEST

START_TEST (test_check_fail_001)
{
	gboolean expired = pgm_timer_check (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	pgm_time_t
 *	pgm_timer_expiration (
 *		pgm_sock_t*	sock
 *	)
 */

START_TEST (test_expiration_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	sock->next_poll = mock_pgm_time_now + pgm_secs(300);
	fail_unless (pgm_secs(300) == pgm_timer_expiration (sock), "expiration failed");
}
END_TEST

START_TEST (test_expiration_fail_001)
{
	long expiration = pgm_timer_expiration (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_timer_dispatch (
 *		pgm_sock_t*	sock
 *	)
 */

START_TEST (test_dispatch_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	pgm_timer_dispatch (sock);
}
END_TEST

START_TEST (test_dispatch_fail_001)
{
	pgm_timer_dispatch (NULL);
	fail ("reached");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_prepare = tcase_create ("prepare");
	suite_add_tcase (s, tc_prepare);
	tcase_add_test (tc_prepare, test_prepare_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_prepare, test_prepare_fail_001, SIGABRT);
#endif

	TCase* tc_check = tcase_create ("check");
	suite_add_tcase (s, tc_check);
	tcase_add_test (tc_check, test_check_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_check, test_check_fail_001, SIGABRT);
#endif

	TCase* tc_expiration = tcase_create ("expiration");
	suite_add_tcase (s, tc_expiration);
	tcase_add_test (tc_expiration, test_expiration_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_expiration, test_expiration_fail_001, SIGABRT);
#endif

	TCase* tc_dispatch = tcase_create ("dispatch");
	suite_add_tcase (s, tc_dispatch);
	tcase_add_test (tc_dispatch, test_dispatch_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_dispatch, test_dispatch_fail_001, SIGABRT);
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
