/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for concurrency operations.
 *
 * Copyright (c) 2011 Miru Limited.
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


#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>


/* mock state */


/* mock functions for external references */

#include "thread.c"


static
void
mock_setup (void)
{
	pgm_thread_init ();
}

static
void
mock_teardown (void)
{
	pgm_thread_shutdown ();
}

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	void
 *	pgm_thread_init (void)
 */

START_TEST (test_init_pass_001)
{
	pgm_thread_init ();
#ifdef PGM_CHECK_NOFORK
	pgm_thread_shutdown ();
#endif
}
END_TEST

/* target:
 *	void
 *	pgm_thread_shutdown (void)
 */

START_TEST (test_shutdown_pass_001)
{
/* unreferences are emitted as warnings */
	pgm_thread_shutdown ();
	pgm_thread_shutdown ();

	pgm_thread_init ();
	pgm_thread_shutdown ();
}
END_TEST

/* target:
 *	void
 *	pgm_mutex_init (pgm_mutex_t* mutex)
 */

START_TEST (test_mutex_init_pass_001)
{
	pgm_mutex_t mutex;
	pgm_mutex_init (&mutex);
#ifdef PGM_CHECK_NOFORK
	pgm_mutex_free (&mutex);
#endif
}
END_TEST

/* target:
 *	void
 *	pgm_mutex_free (pgm_mutex_t* mutex)
 */

START_TEST (test_mutex_free_pass_001)
{
	pgm_mutex_t mutex;
	pgm_mutex_init (&mutex);
	pgm_mutex_free (&mutex);
}
END_TEST

/* target:
 *	void
 *	pgm_mutex_lock (pgm_mutex_t* mutex)
 */

START_TEST (test_mutex_lock_pass_001)
{
	pgm_mutex_t mutex;
	pgm_mutex_init (&mutex);
	pgm_mutex_lock (&mutex);
#ifdef PGM_CHECK_NOFORK
	pgm_mutex_unlock (&mutex);
	pgm_mutex_free (&mutex);
#endif
}
END_TEST

/* target:
 *	void
 *	pgm_mutex_unlock (pgm_mutex_t* mutex)
 */

START_TEST (test_mutex_unlock_pass_001)
{
	pgm_mutex_t mutex;
	pgm_mutex_init (&mutex);
	pgm_mutex_lock (&mutex);
	pgm_mutex_unlock (&mutex);
	pgm_mutex_free (&mutex);
}
END_TEST

/* target:
 *	bool
 *	pgm_mutex_trylock (pgm_mutex_t* mutex)
 */

START_TEST (test_mutex_trylock_pass_001)
{
	pgm_mutex_t mutex;
	pgm_mutex_init (&mutex);
	fail_unless (TRUE == pgm_mutex_trylock (&mutex), "initial state lock");
#ifndef _WIN32
	fail_unless (FALSE == pgm_mutex_trylock (&mutex), "locked mutex");
#else
/* only works in separate threads */
	fail_unless (TRUE == pgm_mutex_trylock (&mutex), "locked mutex");
#endif
	pgm_mutex_unlock (&mutex);
	fail_unless (TRUE == pgm_mutex_trylock (&mutex), "unlocked mutex");
	pgm_mutex_unlock (&mutex);
	pgm_mutex_free (&mutex);
}
END_TEST

/* target:
 *	void
 *	pgm_spinlock_init (pgm_spinlock_t* spinlock)
 */

START_TEST (test_spinlock_init_pass_001)
{
	pgm_spinlock_t spinlock;
	pgm_spinlock_init (&spinlock);
#ifdef PGM_CHECK_NOFORK
	pgm_spinlock_free (&spinlock);
#endif
}
END_TEST

/* target:
 *	void
 *	pgm_spinlock_free (pgm_spinlock_t* spinlock)
 */

START_TEST (test_spinlock_free_pass_001)
{
	pgm_spinlock_t spinlock;
	pgm_spinlock_init (&spinlock);
	pgm_spinlock_free (&spinlock);
}
END_TEST

/* target:
 *	void
 *	pgm_spinlock_lock (pgm_spinlock_t* spinlock)
 */

START_TEST (test_spinlock_lock_pass_001)
{
	pgm_spinlock_t spinlock;
	pgm_spinlock_init (&spinlock);
	pgm_spinlock_lock (&spinlock);
#ifdef PGM_CHECK_NOFORK
	pgm_spinlock_unlock (&spinlock);
	pgm_spinlock_free (&spinlock);
#endif
}
END_TEST

/* target:
 *	void
 *	pgm_spinlock_unlock (pgm_spinlock_t* spinlock)
 */

START_TEST (test_spinlock_unlock_pass_001)
{
	pgm_spinlock_t spinlock;
	pgm_spinlock_init (&spinlock);
	pgm_spinlock_lock (&spinlock);
	pgm_spinlock_unlock (&spinlock);
	pgm_spinlock_free (&spinlock);
}
END_TEST

/* target:
 *	bool
 *	pgm_spinlock_trylock (pgm_spinlock_t* spinlock)
 */

START_TEST (test_spinlock_trylock_pass_001)
{
	pgm_spinlock_t spinlock;
	pgm_spinlock_init (&spinlock);
	fail_unless (TRUE == pgm_spinlock_trylock (&spinlock), "initial state lock");
	fail_unless (FALSE == pgm_spinlock_trylock (&spinlock), "locked spinlock");
	pgm_spinlock_unlock (&spinlock);
	fail_unless (TRUE == pgm_spinlock_trylock (&spinlock), "unlocked spinlock");
	pgm_spinlock_unlock (&spinlock);
	pgm_spinlock_free (&spinlock);
}
END_TEST

/* target:
 *	void
 *	pgm_rwlock_init (pgm_rwlock_t* rwlock)
 */

START_TEST (test_rwlock_init_pass_001)
{
	pgm_rwlock_t rwlock;
	pgm_rwlock_init (&rwlock);
#ifdef PGM_CHECK_NOFORK
	pgm_rwlock_free (&rwlock);
#endif
}
END_TEST

/* target:
 *	void
 *	pgm_rwlock_free (pgm_rwlock_t* rwlock)
 */

START_TEST (test_rwlock_free_pass_001)
{
	pgm_rwlock_t rwlock;
	pgm_rwlock_init (&rwlock);
	pgm_rwlock_free (&rwlock);
}
END_TEST

/* target:
 *	void
 *	pgm_rwlock_reader_lock (pgm_rwlock_t* rwlock)
 */

START_TEST (test_rwlock_reader_lock_pass_001)
{
	pgm_rwlock_t rwlock;
	pgm_rwlock_init (&rwlock);
/* multiple readers permitted */
	pgm_rwlock_reader_lock (&rwlock);
	pgm_rwlock_reader_lock (&rwlock);
#ifdef PGM_CHECK_NOFORK
	pgm_rwlock_reader_unlock (&rwlock);
	pgm_rwlock_reader_unlock (&rwlock);
	pgm_rwlock_free (&rwlock);
#endif
}
END_TEST

/* target:
 *	void
 *	pgm_rwlock_reader_unlock (pgm_rwlock_t* rwlock)
 */

START_TEST (test_rwlock_reader_unlock_pass_001)
{
	pgm_rwlock_t rwlock;
	pgm_rwlock_init (&rwlock);
/* single lock */
	pgm_rwlock_reader_lock (&rwlock);
	pgm_rwlock_reader_unlock (&rwlock);
/* multiple lock */
	pgm_rwlock_reader_lock (&rwlock);
	pgm_rwlock_reader_lock (&rwlock);
	pgm_rwlock_reader_unlock (&rwlock);
	pgm_rwlock_reader_unlock (&rwlock);
	pgm_rwlock_free (&rwlock);
}
END_TEST

/* target:
 *	bool
 *	pgm_rwlock_reader_trylock (pgm_rwlock_t* rwlock)
 */

START_TEST (test_rwlock_reader_trylock_pass_001)
{
	pgm_rwlock_t rwlock;
	pgm_rwlock_init (&rwlock);
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "initial state lock");
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "reader locked");
	pgm_rwlock_reader_unlock (&rwlock);
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "reader unlocked");
	pgm_rwlock_reader_unlock (&rwlock);
	pgm_rwlock_reader_unlock (&rwlock);
	pgm_rwlock_free (&rwlock);
}
END_TEST

/* target:
 *	void
 *	pgm_rwlock_writer_lock (pgm_rwlock_t* rwlock)
 */

START_TEST (test_rwlock_writer_lock_pass_001)
{
	pgm_rwlock_t rwlock;
	pgm_rwlock_init (&rwlock);
/* only single writer permitted */
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "initial state lock");
	pgm_rwlock_reader_unlock (&rwlock);
	pgm_rwlock_writer_lock (&rwlock);
	fail_unless (FALSE == pgm_rwlock_reader_trylock (&rwlock), "writer locked");
#ifdef PGM_CHECK_NOFORK
	pgm_rwlock_writer_unlock (&rwlock);
	pgm_rwlock_free (&rwlock);
#endif
}
END_TEST

/* target:
 *	void
 *	pgm_rwlock_writer_unlock (pgm_rwlock_t* rwlock)
 */

START_TEST (test_rwlock_writer_unlock_pass_001)
{
	pgm_rwlock_t rwlock;
	pgm_rwlock_init (&rwlock);
/* single lock */
	pgm_rwlock_writer_lock (&rwlock);
	fail_unless (FALSE == pgm_rwlock_reader_trylock (&rwlock), "writer locked");
	pgm_rwlock_writer_unlock (&rwlock);
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "writer unlocked");
	pgm_rwlock_reader_unlock (&rwlock);
	pgm_rwlock_writer_lock (&rwlock);
	fail_unless (FALSE == pgm_rwlock_reader_trylock (&rwlock), "writer locked");
	pgm_rwlock_writer_unlock (&rwlock);
	pgm_rwlock_free (&rwlock);
}
END_TEST

/* target:
 *	bool
 *	pgm_rwlock_writer_trylock (pgm_rwlock_t* rwlock)
 */

START_TEST (test_rwlock_writer_trylock_pass_001)
{
	pgm_rwlock_t rwlock;
	pgm_rwlock_init (&rwlock);
/* clean lock */
	fail_unless (TRUE == pgm_rwlock_writer_trylock (&rwlock), "writer lock");
	pgm_rwlock_writer_unlock (&rwlock);
/* blocked writer */
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "reader lock");
	fail_unless (FALSE == pgm_rwlock_writer_trylock (&rwlock), "writer on reader locked");
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "reader lock");
	fail_unless (FALSE == pgm_rwlock_writer_trylock (&rwlock), "writer on reader locked");
	pgm_rwlock_reader_unlock (&rwlock);
	fail_unless (FALSE == pgm_rwlock_writer_trylock (&rwlock), "writer on reader locked");
	pgm_rwlock_reader_unlock (&rwlock);
/* blocked reader */
	fail_unless (TRUE == pgm_rwlock_writer_trylock (&rwlock), "writer lock");
	fail_unless (FALSE == pgm_rwlock_reader_trylock (&rwlock), "reader on writer locked");
	fail_unless (FALSE == pgm_rwlock_reader_trylock (&rwlock), "reader on writer locked");
	pgm_rwlock_writer_unlock (&rwlock);
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "reader lock");
	pgm_rwlock_reader_unlock (&rwlock);
/* two step */
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "reader lock");
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "reader lock");
	pgm_rwlock_reader_unlock (&rwlock);
	fail_unless (TRUE == pgm_rwlock_reader_trylock (&rwlock), "reader lock");
	pgm_rwlock_reader_unlock (&rwlock);
	pgm_rwlock_reader_unlock (&rwlock);
/* wrapped around regular reader locks */
	pgm_rwlock_reader_lock (&rwlock);
	fail_unless (FALSE == pgm_rwlock_writer_trylock (&rwlock), "writer on reader locked");
	pgm_rwlock_reader_lock (&rwlock);
	fail_unless (FALSE == pgm_rwlock_writer_trylock (&rwlock), "writer on reader locked");
	pgm_rwlock_reader_unlock (&rwlock);
	fail_unless (FALSE == pgm_rwlock_writer_trylock (&rwlock), "writer on reader locked");
	pgm_rwlock_reader_unlock (&rwlock);
	fail_unless (TRUE == pgm_rwlock_writer_trylock (&rwlock), "writer lock");
	pgm_rwlock_writer_unlock (&rwlock);

	pgm_rwlock_free (&rwlock);
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

	TCase* tc_shutdown = tcase_create ("shutdown");
	suite_add_tcase (s, tc_shutdown);
	tcase_add_test (tc_shutdown, test_shutdown_pass_001);

	return s;
}

static
Suite*
make_mutex_suite (void)
{
	Suite* s;

	s = suite_create ("mutex");

	TCase* tc_init = tcase_create ("init");
	tcase_add_checked_fixture (tc_init, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_init);
	tcase_add_test (tc_init, test_mutex_init_pass_001);

	TCase* tc_free = tcase_create ("free");
	tcase_add_checked_fixture (tc_free, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_free);
	tcase_add_test (tc_free, test_mutex_free_pass_001);

	TCase* tc_lock = tcase_create ("lock");
	tcase_add_checked_fixture (tc_lock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_lock);
	tcase_add_test (tc_lock, test_mutex_lock_pass_001);

	TCase* tc_unlock = tcase_create ("unlock");
	tcase_add_checked_fixture (tc_unlock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_unlock);
	tcase_add_test (tc_unlock, test_mutex_unlock_pass_001);

	TCase* tc_trylock = tcase_create ("trylock");
	tcase_add_checked_fixture (tc_trylock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_trylock);
	tcase_add_test (tc_trylock, test_mutex_trylock_pass_001);

	return s;
}

static
Suite*
make_spinlock_suite (void)
{
	Suite* s;

	s = suite_create ("spinlock");

	TCase* tc_init = tcase_create ("init");
	tcase_add_checked_fixture (tc_init, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_init);
	tcase_add_test (tc_init, test_spinlock_init_pass_001);

	TCase* tc_free = tcase_create ("free");
	tcase_add_checked_fixture (tc_free, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_free);
	tcase_add_test (tc_free, test_spinlock_free_pass_001);

	TCase* tc_lock = tcase_create ("lock");
	tcase_add_checked_fixture (tc_lock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_lock);
	tcase_add_test (tc_lock, test_spinlock_lock_pass_001);

	TCase* tc_unlock = tcase_create ("unlock");
	tcase_add_checked_fixture (tc_unlock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_unlock);
	tcase_add_test (tc_unlock, test_spinlock_unlock_pass_001);

	TCase* tc_trylock = tcase_create ("trylock");
	tcase_add_checked_fixture (tc_trylock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_trylock);
	tcase_add_test (tc_trylock, test_spinlock_trylock_pass_001);

	return s;
}

static
Suite*
make_rwlock_suite (void)
{
	Suite* s;

	s = suite_create ("rwlock");

	TCase* tc_init = tcase_create ("init");
	tcase_add_checked_fixture (tc_init, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_init);
	tcase_add_test (tc_init, test_rwlock_init_pass_001);

	TCase* tc_free = tcase_create ("free");
	tcase_add_checked_fixture (tc_free, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_free);
	tcase_add_test (tc_free, test_rwlock_free_pass_001);

	TCase* tc_reader_lock = tcase_create ("reader: lock");
	tcase_add_checked_fixture (tc_reader_lock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_reader_lock);
	tcase_add_test (tc_reader_lock, test_rwlock_reader_lock_pass_001);

	TCase* tc_reader_unlock = tcase_create ("reader: unlock");
	tcase_add_checked_fixture (tc_reader_unlock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_reader_unlock);
	tcase_add_test (tc_reader_unlock, test_rwlock_reader_unlock_pass_001);

	TCase* tc_reader_trylock = tcase_create ("reader: trylock");
	tcase_add_checked_fixture (tc_reader_trylock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_reader_trylock);
	tcase_add_test (tc_reader_trylock, test_rwlock_reader_trylock_pass_001);

	TCase* tc_writer_lock = tcase_create ("writer: lock");
	tcase_add_checked_fixture (tc_writer_lock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_writer_lock);
	tcase_add_test (tc_writer_lock, test_rwlock_writer_lock_pass_001);

	TCase* tc_writer_unlock = tcase_create ("writer: unlock");
	tcase_add_checked_fixture (tc_writer_unlock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_writer_unlock);
	tcase_add_test (tc_writer_unlock, test_rwlock_writer_unlock_pass_001);

	TCase* tc_writer_trylock = tcase_create ("writer: trylock");
	tcase_add_checked_fixture (tc_writer_trylock, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_writer_trylock);
	tcase_add_test (tc_writer_trylock, test_rwlock_writer_trylock_pass_001);

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
	pgm_messages_init();
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_add_suite (sr, make_mutex_suite ());
	srunner_add_suite (sr, make_spinlock_suite ());
	srunner_add_suite (sr, make_rwlock_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	pgm_messages_shutdown();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
