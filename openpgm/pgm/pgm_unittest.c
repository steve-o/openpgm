/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM engine.
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


/* mock state */

static gboolean mock_time_init;


static
void
mock_setup (void)
{
	mock_time_init = FALSE;
}

static
void
mock_teardown (void)
{
// null
}


/* mock functions for external references */

int
mock__pgm_time_init (void)
{
	if (mock_time_init)
		return -1;
	mock_time_init = TRUE;
	return 0;
}

gboolean
mock__pgm_time_supported (void)
{
	return mock_time_init;
}

int
mock__pgm_time_shutdown (void)
{
	if (!mock_time_init)
		return -1;
	mock_time_init = FALSE;
	return 0;
}


#define _pgm_time_init		mock__pgm_time_init
#define _pgm_time_supported	mock__pgm_time_supported
#define _pgm_time_shutdown	mock__pgm_time_shutdown

#define PGM_DEBUG
#include "pgm.c"


/* target:
 *	void
 *	pgm_init (void)
 */

START_TEST (test_init_pass_001)
{
	fail_unless (0 == pgm_init ());
	fail_unless (-1 == pgm_init ());
}
END_TEST

/* init should succeed if glib threading already initialized */
START_TEST (test_init_pass_002)
{
	g_thread_init (NULL);
	fail_unless (0 == pgm_init ());
	fail_unless (-1 == pgm_init ());
}
END_TEST

/* timing module already init */
START_TEST (test_init_pass_003)
{
	fail_unless (0 == _pgm_time_init ());
	fail_unless (-1 == pgm_init ());
}
END_TEST

/* target:
 *	void
 *	pgm_shutdown (void)
 */

START_TEST (test_shutdown_pass_001)
{
	fail_unless (0 == pgm_init ());
	fail_unless (0 == pgm_shutdown ());
}
END_TEST

/* no init */
START_TEST (test_shutdown_pass_002)
{
	fail_unless (-1 == pgm_shutdown ());
}
END_TEST

/* double call */
START_TEST (test_shutdown_pass_003)
{
	fail_unless (0 == pgm_init ());
	fail_unless (0 == pgm_shutdown ());
	fail_unless (-1 == pgm_shutdown ());
}
END_TEST

/* target:
 *	gboolean
 *	pgm_supported (void)
 */

START_TEST (test_supported_pass_001)
{
	fail_unless (FALSE == pgm_supported());
	fail_unless (0 == pgm_init ());
	fail_unless (TRUE == pgm_supported());
	fail_unless (0 == pgm_shutdown ());
	fail_unless (FALSE == pgm_supported());
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_init = tcase_create ("init");
	tcase_add_checked_fixture (tc_init, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_init);
	tcase_add_test (tc_init, test_init_pass_001);
	tcase_add_test (tc_init, test_init_pass_002);
	tcase_add_test (tc_init, test_init_pass_003);

	TCase* tc_shutdown = tcase_create ("shutdown");
	tcase_add_checked_fixture (tc_shutdown, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_shutdown);
	tcase_add_test (tc_shutdown, test_shutdown_pass_001);
	tcase_add_test (tc_shutdown, test_shutdown_pass_002);
	tcase_add_test (tc_shutdown, test_shutdown_pass_003);
	
	TCase* tc_supported = tcase_create ("supported");
	tcase_add_checked_fixture (tc_supported, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_supported);
	tcase_add_test (tc_supported, test_supported_pass_001);

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
