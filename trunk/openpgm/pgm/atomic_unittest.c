/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for atomic operations.
 *
 * Copyright (c) 2010 Miru Limited.
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
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>


/* mock state */

int pgm_min_log_level = 0;


/** messages module */
void
pgm__log (
	const int		log_level,
	const char*		format,
	...
	)
{
	va_list args;

	va_start (args, format);
	vprintf (format, args);
	va_end (args);
}


/* mock functions for external references */

#define ATOMIC_DEBUG
#include "atomic.c"

static
void
mock_setup (void)
{
	pgm_atomic_init ();
}

static
void
mock_teardown (void)
{
	pgm_atomic_shutdown ();
}


/* target:
 *	int32_t
 *	pgm_atomic_int32_exchange_and_add (
 *		volatile int32_t*	atomic,
 *		const int32_t		val
 *	)
 */

START_TEST (test_int32_exchange_and_add_pass_001)
{
	volatile int32_t atomic = 0;
	fail_unless (0 == pgm_atomic_int32_exchange_and_add (&atomic, 5));
	fail_unless (5 == atomic);
	fail_unless (5 == pgm_atomic_int32_exchange_and_add (&atomic, -10));
	fail_unless (-5 == atomic);
}
END_TEST

/* target:
 *	void
 *	pgm_atomic_int32_add (
 *		volatile int32_t*	atomic,
 *		const int32_t		val
 *	)
 */

START_TEST (test_int32_add_pass_001)
{
	volatile int32_t atomic = -5;
	pgm_atomic_int32_add (&atomic, 20);
	fail_unless (15 == atomic);
	pgm_atomic_int32_add (&atomic, -35);
	fail_unless (-20 == atomic);
}
END_TEST

/* ensure wrap around when casting uint32 */
START_TEST (test_int32_add_pass_002)
{
	volatile uint32_t atomic = 0;
	pgm_atomic_int32_add ((volatile gint32*)&atomic, UINT32_MAX/2);
	fail_unless ((UINT32_MAX/2) == atomic);
	pgm_atomic_int32_add ((volatile gint32*)&atomic, UINT32_MAX - (UINT32_MAX/2));
	fail_unless (UINT32_MAX == atomic);
	pgm_atomic_int32_add ((volatile gint32*)&atomic, 1);
	fail_unless (0 == atomic);
}
END_TEST

/* target:
 *	int32_t
 *	pgm_atomic_int32_get (
 *		volatile int32_t*	atomic
 *	)
 */

START_TEST (test_int32_get_pass_001)
{
	volatile int32_t atomic = -20;
	fail_unless (-20 == pgm_atomic_int32_get (&atomic));
}
END_TEST

/* target:
 *	void
 *	pgm_atomic_int32_set (
 *		volatile int32_t*	atomic,
 *		const int32_t		val
 *	)
 */

START_TEST (test_int32_set_pass_001)
{
	volatile int32_t atomic = -20;
	pgm_atomic_int32_set (&atomic, 5);
	fail_unless (5 == atomic);
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_exchange_and_add = tcase_create ("exchange-and-add");
	suite_add_tcase (s, tc_exchange_and_add);
	tcase_add_checked_fixture (tc_exchange_and_add, mock_setup, mock_teardown);
	tcase_add_test (tc_exchange_and_add, test_int32_exchange_and_add_pass_001);

	TCase* tc_add = tcase_create ("add");
	suite_add_tcase (s, tc_add);
	tcase_add_checked_fixture (tc_add, mock_setup, mock_teardown);
	tcase_add_test (tc_add, test_int32_add_pass_001);
	tcase_add_test (tc_add, test_int32_add_pass_002);

	TCase* tc_get = tcase_create ("get");
	suite_add_tcase (s, tc_get);
	tcase_add_checked_fixture (tc_get, mock_setup, mock_teardown);
	tcase_add_test (tc_get, test_int32_get_pass_001);

	TCase* tc_set = tcase_create ("set");
	suite_add_tcase (s, tc_set);
	tcase_add_checked_fixture (tc_set, mock_setup, mock_teardown);
	tcase_add_test (tc_set, test_int32_set_pass_001);

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
