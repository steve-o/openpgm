/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for global session ID helper functions.
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <check.h>


/* mock state */

static char* mock_localhost = "localhost";
static char* mock_invalid = "invalid.invalid";		/* RFC 2606 */
static char* mock_toolong = "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij12345"; /* 65 */
static char* mock_hostname = NULL;


static
void
mock_setup_invalid (void)
{
	mock_hostname = mock_invalid;
}

static
void
mock_setup_toolong (void)
{
	mock_hostname = mock_toolong;
}

static
void
mock_setup_localhost (void)
{
	mock_hostname = mock_localhost;
}

static
void
mock_teardown (void)
{
// null
}


/* mock functions for external references */

int
mock_gethostname (
	char*		name,
	size_t		len
	)
{
	if (mock_hostname == mock_toolong) {
		errno = EINVAL;
		return -1;
	}
	strncpy (name, mock_hostname, len);
	if (len > 0)
		name[len - 1] = '\0';
	return 0;
}


#define gethostname	mock_gethostname

#define GSI_DEBUG
#include "gsi.c"


/* target:
 *	gboolean
 *	pgm_gsi_create_from_hostname (
 *		pgm_gsi_t*		gsi,
 *		GError**		err
 *	)
 */

START_TEST (test_create_from_hostname_pass_001)
{
	pgm_gsi_t gsi;
	GError* err = NULL;
	fail_unless (pgm_gsi_create_from_hostname (&gsi, &err), "create_from_hostname failed");
	fail_if (err, "error raised");
	fail_unless (pgm_gsi_create_from_hostname (&gsi, NULL), "create_from_hostname failed");
}
END_TEST

START_TEST (test_create_from_hostname_pass_002)
{
	GError* err = NULL;
	fail_if (pgm_gsi_create_from_hostname (NULL, &err), "create_from_hostname failed");
	fail_if (err, "error raised");
	fail_if (pgm_gsi_create_from_hostname (NULL, NULL), "create_from_hostname failed");
}
END_TEST

/* hostname too long */
START_TEST (test_create_from_hostname_pass_003)
{
	pgm_gsi_t gsi;
	GError* err = NULL;
	fail_if (pgm_gsi_create_from_hostname (&gsi, &err), "create_from_hostname failed");
	fail_if (NULL == err, "error not raised");
	fail_if (NULL == err->message, "no error message");
	g_debug ("GError: %s", err->message);
	fail_if (pgm_gsi_create_from_hostname (&gsi, NULL), "create_from_hostname failed");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_gsi_create_from_addr (
 *		pgm_gsi_t*		gsi,
 *		GError**		err
 *	)
 */

START_TEST (test_create_from_addr_pass_001)
{
	pgm_gsi_t gsi;
	GError* err = NULL;
	fail_unless (pgm_gsi_create_from_addr (&gsi, &err), "create_from_addr failed");
	fail_if (err, "error raised");
	fail_unless (pgm_gsi_create_from_addr (&gsi, NULL), "create_from_addr failed");
}
END_TEST

START_TEST (test_create_from_addr_pass_002)
{
	GError* err = NULL;
	fail_if (pgm_gsi_create_from_addr (NULL, &err), "create_from_addr failed");
	fail_if (pgm_gsi_create_from_addr (NULL, NULL), "create_from_addr failed");
}
END_TEST

/* invalid hostname */
START_TEST (test_create_from_addr_pass_003)
{
	pgm_gsi_t gsi;
	GError* err = NULL;
	fail_if (pgm_gsi_create_from_addr (&gsi, &err), "create_from_addr failed");
	fail_if (NULL == err, "error not raised");
	fail_if (NULL == err->message, "no error message");
	g_debug ("GError: %s", err->message);
	fail_if (pgm_gsi_create_from_addr (&gsi, NULL), "create_from_addr failed");
}
END_TEST

/* target:
 *	gchar*
 *	pgm_gsi_print (
 *		const pgm_gsi_t*	gsi
 *	)
 */

START_TEST (test_print_pass_001)
{
	pgm_gsi_t gsi;
	fail_unless (pgm_gsi_create_from_hostname (&gsi, NULL), "create_from_hostname failed");
	fail_if (NULL == pgm_gsi_print (&gsi), "print failed");
}
END_TEST

START_TEST (test_print_pass_002)
{
	fail_unless (NULL == pgm_gsi_print (NULL), "print failed");
}
END_TEST

/* target:
 *	int
 *	pgm_gsi_print_r (
 *		const pgm_gsi_t*	gsi,
 *		char*			buf,
 *		gsize			bufsize
 *	)
 */

START_TEST (test_print_r_pass_001)
{
	pgm_gsi_t gsi;
	char buf[PGM_GSISTRLEN];
	fail_unless (pgm_gsi_create_from_hostname (&gsi, NULL), "create_from_hostname failed");
	fail_unless (pgm_gsi_print_r (&gsi, buf, sizeof(buf)) > 0, "print_r failed");
}
END_TEST

START_TEST (test_print_r_pass_002)
{
	pgm_gsi_t gsi;
	char buf[PGM_GSISTRLEN];
	fail_unless (pgm_gsi_create_from_hostname (&gsi, NULL), "create_from_hostname failed");
	fail_unless (pgm_gsi_print_r (NULL, buf, sizeof(buf)) == -1, "print_r failed");
	fail_unless (pgm_gsi_print_r (&gsi, NULL, sizeof(buf)) == -1, "print_r failed");
	fail_unless (pgm_gsi_print_r (&gsi, buf, 0) == -1, "print_r failed");
}
END_TEST

/* target:
 *	gint
 *	pgm_gsi_equal (
 *		gconstpointer	gsi1,
 *		gconstpointer	gsi2
 *	)
 */

START_TEST (test_equal_pass_001)
{
	pgm_gsi_t gsi1, gsi2;
	fail_unless (pgm_gsi_create_from_hostname (&gsi1, NULL), "create_from_hostname failed");
	fail_unless (pgm_gsi_create_from_hostname (&gsi2, NULL), "create_from_hostname failed");
	fail_unless (pgm_gsi_equal (&gsi1, &gsi2), "equal failed");
}
END_TEST

START_TEST (test_equal_pass_002)
{
	pgm_gsi_t gsi1, gsi2;
	fail_unless (pgm_gsi_create_from_hostname (&gsi1, NULL), "create_from_hostname failed");
	fail_unless (pgm_gsi_create_from_addr (&gsi2, NULL), "create_from_addr failed");
	fail_if (pgm_gsi_equal (&gsi1, &gsi2), "equal failed");
}
END_TEST

START_TEST (test_equal_fail_001)
{
	pgm_gsi_t gsi;
	fail_unless (pgm_gsi_create_from_hostname (&gsi, NULL), "create_from_hostname failed");
	gboolean retval = pgm_gsi_equal (NULL, &gsi);
	fail ("reached");
}
END_TEST

START_TEST (test_equal_fail_002)
{
	pgm_gsi_t gsi;
	fail_unless (pgm_gsi_create_from_hostname (&gsi, NULL), "create_from_hostname failed");
	gboolean retval = pgm_gsi_equal (&gsi, NULL);
	fail ("reached");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_create_from_hostname = tcase_create ("create-from-hostname");
	suite_add_tcase (s, tc_create_from_hostname);
	tcase_add_checked_fixture (tc_create_from_hostname, mock_setup_localhost, mock_teardown);
	tcase_add_test (tc_create_from_hostname, test_create_from_hostname_pass_001);
	tcase_add_test (tc_create_from_hostname, test_create_from_hostname_pass_002);

	TCase* tc_create_from_hostname2 = tcase_create ("create-from-hostname/2");
	suite_add_tcase (s, tc_create_from_hostname2);
	tcase_add_checked_fixture (tc_create_from_hostname2, mock_setup_toolong, mock_teardown);
	tcase_add_test (tc_create_from_hostname2, test_create_from_hostname_pass_003);

	TCase* tc_create_from_addr = tcase_create ("create-from-addr");
	suite_add_tcase (s, tc_create_from_addr);
	tcase_add_checked_fixture (tc_create_from_addr, mock_setup_localhost, mock_teardown);
	tcase_add_test (tc_create_from_addr, test_create_from_addr_pass_001);
	tcase_add_test (tc_create_from_addr, test_create_from_addr_pass_002);

	TCase* tc_create_from_addr2 = tcase_create ("create-from-addr/2");
	suite_add_tcase (s, tc_create_from_addr2);
	tcase_add_checked_fixture (tc_create_from_addr2, mock_setup_invalid, mock_teardown);
	tcase_add_test (tc_create_from_addr2, test_create_from_addr_pass_003);

	TCase* tc_print = tcase_create ("print");
	suite_add_tcase (s, tc_print);
	tcase_add_checked_fixture (tc_print, mock_setup_localhost, mock_teardown);
	tcase_add_test (tc_print, test_print_pass_001);
	tcase_add_test (tc_print, test_print_pass_002);

	TCase* tc_print_r = tcase_create ("print-r");
	suite_add_tcase (s, tc_print_r);
	tcase_add_checked_fixture (tc_print_r, mock_setup_localhost, mock_teardown);
	tcase_add_test (tc_print_r, test_print_r_pass_001);
	tcase_add_test (tc_print_r, test_print_r_pass_002);

	TCase* tc_equal = tcase_create ("equal");
	suite_add_tcase (s, tc_equal);
	tcase_add_checked_fixture (tc_equal, mock_setup_localhost, mock_teardown);
	tcase_add_test (tc_equal, test_equal_pass_001);
	tcase_add_test (tc_equal, test_equal_pass_002);
	tcase_add_test_raise_signal (tc_equal, test_equal_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_equal, test_equal_fail_002, SIGABRT);

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
