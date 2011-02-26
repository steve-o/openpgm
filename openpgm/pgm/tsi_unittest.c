/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for transport session ID helper functions.
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


#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <check.h>

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


/* mock state */

/* mock functions for external references */

size_t
pgm_transport_pkt_offset2 (
        const bool                      can_fragment,
        const bool                      use_pgmcc
        )
{
        return 0;
}

#define TSI_DEBUG
#include "tsi.c"

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	gchar*
 *	pgm_tsi_print (
 *		const pgm_tsi_t*	tsi
 *	)
 */

START_TEST (test_print_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_if (NULL == pgm_tsi_print (&tsi), "print failed");
}
END_TEST

START_TEST (test_print_pass_002)
{
	fail_unless (NULL == pgm_tsi_print (NULL), "print failed");
}
END_TEST

/* target:
 *	int
 *	pgm_tsi_print_r (
 *		const pgm_tsi_t*	tsi,
 *		char*			buf,
 *		gsize			bufsize
 *	)
 */

START_TEST (test_print_r_pass_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	char buf[PGM_TSISTRLEN];
	fail_unless (pgm_tsi_print_r (&tsi, buf, sizeof(buf)) > 0, "print_r failed");
}
END_TEST

START_TEST (test_print_r_pass_002)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	char buf[PGM_TSISTRLEN];
	fail_unless (pgm_tsi_print_r (NULL, buf, sizeof(buf)) == -1, "print_r failed");
	fail_unless (pgm_tsi_print_r (&tsi, NULL, sizeof(buf)) == -1, "print_r failed");
	fail_unless (pgm_tsi_print_r (&tsi, buf, 0) == -1, "print_r failed");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_tsi_equal (
 *		gconstpointer	tsi1,
 *		gconstpointer	tsi2
 *	)
 */

START_TEST (test_equal_pass_001)
{
	const pgm_tsi_t tsi1 = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const pgm_tsi_t tsi2 = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	fail_unless (pgm_tsi_equal (&tsi1, &tsi2), "equal failed");
}
END_TEST

START_TEST (test_equal_pass_002)
{
	const pgm_tsi_t tsi1 = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	const pgm_tsi_t tsi2 = { { 9, 8, 7, 6, 5, 4 }, 2000 };
	fail_if (pgm_tsi_equal (&tsi1, &tsi2), "equal failed");
}
END_TEST

START_TEST (test_equal_fail_001)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	gboolean retval = pgm_tsi_equal (NULL, &tsi);
	fail ("reached");
}
END_TEST

START_TEST (test_equal_fail_002)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	gboolean retval = pgm_tsi_equal (&tsi, NULL);
	fail ("reached");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_print = tcase_create ("print");
	suite_add_tcase (s, tc_print);
	tcase_add_test (tc_print, test_print_pass_001);
	tcase_add_test (tc_print, test_print_pass_002);

	TCase* tc_print_r = tcase_create ("print-r");
	suite_add_tcase (s, tc_print_r);
	tcase_add_test (tc_print_r, test_print_r_pass_001);
	tcase_add_test (tc_print_r, test_print_r_pass_002);

	TCase* tc_equal = tcase_create ("equal");
	suite_add_tcase (s, tc_equal);
	tcase_add_test (tc_equal, test_equal_pass_001);
	tcase_add_test (tc_equal, test_equal_pass_002);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_equal, test_equal_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_equal, test_equal_fail_002, SIGABRT);
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
	pgm_messages_init();
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	pgm_messages_shutdown();
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
