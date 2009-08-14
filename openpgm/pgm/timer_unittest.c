/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM timer thread.
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


/* mock state */

static pgm_time_t mock_pgm_time_now = 0x1;


static
pgm_transport_t*
generate_transport (void)
{
	pgm_transport_t* transport = g_malloc0 (sizeof(pgm_transport_t));
	return transport;
}


/* mock functions for external references */

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
mock_pgm_time_update_now (void)
{
	return mock_pgm_time_now;
}

/** receiver module */
static
pgm_time_t
mock_pgm_min_nak_expiry (
	pgm_time_t		expiration,
	pgm_transport_t*	transport
	)
{
	g_assert (NULL != transport);
	return 0x1;
}

static
void
mock_pgm_check_peer_nak_state (
	pgm_transport_t*	transport
	)
{
	g_assert (NULL != transport);
}

/** source module */
static
int
mock_pgm_send_spm_unlocked (
	pgm_transport_t*	transport
	)
{
	g_assert (NULL != transport);
	return 0;
}


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
#define pgm_min_nak_expiry		mock_pgm_min_nak_expiry
#define pgm_check_peer_nak_state	mock_pgm_check_peer_nak_state
#define pgm_send_spm_unlocked		mock_pgm_send_spm_unlocked


#define TIMER_DEBUG
#include "timer.c"


/* target:
 *	GSource*
 *	pgm_timer_create (
 *		pgm_transport_t*	transport
 *	)
 */

START_TEST (test_create_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	GSource* source = pgm_timer_create (transport);
	fail_unless (NULL != source);
}
END_TEST

START_TEST (test_create_fail_001)
{
	GSource* source = pgm_timer_create (NULL);
	fail_unless (NULL == source);
}
END_TEST

/* target:
 *	int
 *	pgm_timer_add_full (
 *		pgm_transport_t*	transport
 *	)
 */

START_TEST (test_add_full_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	int source_id = pgm_timer_add_full (transport, G_PRIORITY_DEFAULT);
	fail_unless (source_id > 0);
}
END_TEST

START_TEST (test_add_full_fail_001)
{
	int source_id = pgm_timer_add_full (NULL, G_PRIORITY_DEFAULT);
	fail_unless (source_id <= 0);
}
END_TEST

/* target:
 *	int
 *	pgm_timer_add (
 *		pgm_transport_t*	transport
 *	)
 */

START_TEST (test_add_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	int source_id = pgm_timer_add (transport);
	fail_unless (source_id > 0);
}
END_TEST

START_TEST (test_add_fail_001)
{
	int source_id = pgm_timer_add (NULL);
	fail_unless (source_id <= 0);
}
END_TEST

/* target:
 *	gpointer
 *	pgm_timer_thread (
 *		gpointer	data	// pgm_transport_t
 *	)
 */

START_TEST (test_thread_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (NULL == pgm_timer_thread ((gpointer)transport));
}
END_TEST

START_TEST (test_thread_fail_001)
{
	gpointer retval = pgm_timer_thread (NULL);
	fail ();
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
	tcase_add_test (tc_create, test_create_fail_001);

	TCase* tc_add_full = tcase_create ("add-full");
	suite_add_tcase (s, tc_add_full);
	tcase_add_test (tc_add_full, test_add_full_pass_001);
	tcase_add_test (tc_add_full, test_add_full_fail_001);

	TCase* tc_add = tcase_create ("add");
	suite_add_tcase (s, tc_add);
	tcase_add_test (tc_add, test_add_pass_001);
	tcase_add_test (tc_add, test_add_fail_001);

	TCase* tc_thread = tcase_create ("thread");
	suite_add_tcase (s, tc_thread);
	tcase_add_test (tc_thread, test_thread_pass_001);
	tcase_add_test_raise_signal (tc_thread, test_thread_fail_001, SIGABRT);
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
