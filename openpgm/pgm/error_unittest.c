/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for error reporting.
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
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>


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

#define ERROR_DEBUG
#include "error.c"

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	void
 *	pgm_set_error (
 *		pgm_error_t**		err,
 *		int			err_domain,
 *		int			err_code,
 *		const char*		format,
 *		...
 *	)
 */

START_TEST (test_set_error_pass_001)
{
	pgm_error_t* err = NULL;
	const gint err_domain = PGM_ERROR_DOMAIN_ENGINE;
	const gint err_code = 100;
	pgm_set_error (&err, err_domain, err_code, "an error occurred.");
	fail_unless (NULL != err, "set_error failed");
}
END_TEST

START_TEST (test_set_error_pass_002)
{
	pgm_error_t* err = NULL;
	const gint err_domain = PGM_ERROR_DOMAIN_ENGINE;
	const gint err_code = 100;
	pgm_set_error (&err, err_domain, err_code, "an error occurred: value=%d.", 123);
	fail_unless (NULL != err, "set_error failed");
}
END_TEST

/* ignore NULL error */
START_TEST (test_set_error_pass_003)
{
	pgm_error_t* err = NULL;
	const gint err_domain = PGM_ERROR_DOMAIN_ENGINE;
	const gint err_code = 100;
	pgm_set_error (NULL, err_domain, err_code, "an error occurred.");
}
END_TEST

/* overwritten error */
START_TEST (test_set_error_pass_004)
{
	pgm_error_t* err = NULL;
	const gint err_domain = PGM_ERROR_DOMAIN_ENGINE;
	const gint err_code = 100;
	pgm_set_error (&err, err_domain, err_code, "an error occurred.");
	fail_unless (NULL != err, "set_error failed");
	pgm_set_error (&err, err_domain, err_code, "another error occurred.");
}
END_TEST

/* target:
 *	void
 *	pgm_prefix_error (
 *		pgm_error_t**		err,
 *		const char*		format,
 *		...
 *	)
 */

START_TEST (test_prefix_error_pass_001)
{
	pgm_error_t* err = NULL;
	const gint err_domain = PGM_ERROR_DOMAIN_ENGINE;
	const gint err_code = 100;
	pgm_set_error (&err, err_domain, err_code, "an error occurred.");
	fail_unless (NULL != err, "set_error failed");
	pgm_prefix_error (&err, "i am a prefix:");
	pgm_prefix_error (&err, "i am another prefix, value=%d:", 123);
}
END_TEST

/* ignore null original error */
START_TEST (test_prefix_error_pass_002)
{
	pgm_error_t* err = NULL;
	pgm_prefix_error (&err, "i am a prefix:");
}
END_TEST

/* target:
 *	void
 *	pgm_propagate_error (
 *		pgm_error_t**		dest,
 *		pgm_error_t*		src,
 *	)
 */

START_TEST (test_propagate_error_pass_001)
{
	pgm_error_t* dest = NULL;
	pgm_error_t* err = NULL;
	const gint err_domain = PGM_ERROR_DOMAIN_ENGINE;
	const gint err_code = 100;
	pgm_set_error (&err, err_domain, err_code, "an error occurred.");
	fail_unless (NULL != err, "set_error failed");
	pgm_propagate_error (&dest, err);
	fail_unless (NULL != dest, "propagate_error failed");
}
END_TEST

/* ignore NULL destination */
START_TEST (test_propagate_error_pass_002)
{
	pgm_error_t* err = NULL;
	const gint err_domain = PGM_ERROR_DOMAIN_ENGINE;
	const gint err_code = 100;
	pgm_set_error (&err, err_domain, err_code, "an error occurred.");
	fail_unless (NULL != err, "set_error failed");
	pgm_propagate_error (NULL, err);
}
END_TEST

/* src error SHOULD be valid */
START_TEST (test_propagate_error_pass_003)
{
	pgm_error_t* dest = NULL;
	pgm_error_t* err = NULL;
	pgm_propagate_error (&dest, err);
}
END_TEST

/* target:
 *	void
 *	pgm_clear_error (
 *		pgm_error_t**	err
 *	)
 */

START_TEST (test_clear_error_pass_001)
{
	pgm_error_t* err = NULL;
	const gint err_domain = PGM_ERROR_DOMAIN_ENGINE;
	const gint err_code = 100;
	pgm_set_error (&err, err_domain, err_code, "an error occurred.");
	fail_unless (NULL != err, "set_error failed");
	pgm_clear_error (&err);
	fail_unless (NULL == err, "clear_error failed");
}
END_TEST

START_TEST (test_clear_error_pass_002)
{
	pgm_error_t* err = NULL;
	pgm_clear_error (&err);
	fail_unless (NULL == err, "clear_error failed");
}
END_TEST

START_TEST (test_clear_error_pass_003)
{
	pgm_clear_error (NULL);
}
END_TEST

/* target:
 *	void
 *	pgm_error_free (
 *		pgm_error_t*	err
 *	)
 */

START_TEST (test_error_free_pass_001)
{
	pgm_error_t* err = NULL;
	const gint err_domain = PGM_ERROR_DOMAIN_ENGINE;
	const gint err_code = 100;
	pgm_set_error (&err, err_domain, err_code, "an error occurred.");
	fail_unless (NULL != err, "set_error failed");
	pgm_error_free (err);
}
END_TEST

START_TEST (test_error_free_pass_002)
{
	pgm_error_free (NULL);
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_set_error = tcase_create ("set-error");
	suite_add_tcase (s, tc_set_error);
	tcase_add_test (tc_set_error, test_set_error_pass_001);
	tcase_add_test (tc_set_error, test_set_error_pass_002);
	tcase_add_test (tc_set_error, test_set_error_pass_003);
	tcase_add_test (tc_set_error, test_set_error_pass_004);

	TCase* tc_prefix_error = tcase_create ("prefix-error");
	suite_add_tcase (s, tc_prefix_error);
	tcase_add_test (tc_prefix_error, test_prefix_error_pass_001);
	tcase_add_test (tc_prefix_error, test_prefix_error_pass_002);

	TCase* tc_propagate_error = tcase_create ("propagate-error");
	suite_add_tcase (s, tc_propagate_error);
	tcase_add_test (tc_propagate_error, test_propagate_error_pass_001);
	tcase_add_test (tc_propagate_error, test_propagate_error_pass_002);
	tcase_add_test (tc_propagate_error, test_propagate_error_pass_003);

	TCase* tc_clear_error = tcase_create ("clear-error");
	suite_add_tcase (s, tc_clear_error);
	tcase_add_test (tc_clear_error, test_clear_error_pass_001);
	tcase_add_test (tc_clear_error, test_clear_error_pass_002);
	tcase_add_test (tc_clear_error, test_clear_error_pass_003);

	TCase* tc_error_free = tcase_create ("error-free");
	suite_add_tcase (s, tc_error_free);
	tcase_add_test (tc_error_free, test_error_free_pass_001);
	tcase_add_test (tc_error_free, test_error_free_pass_002);

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
