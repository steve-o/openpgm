/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM checksum routines
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


/* mock functions for external references */


#define CHECKSUM_DEBUG
#include "checksum.c"


/* target:
 *	guint16
 *	pgm_inet_checksum (
 *		const void*		src,
 *		guint			len,
 *		int			csum
 *	)
 */

START_TEST (test_inet_pass_001)
{
	const char source[] = "i am not a string";
	guint16 csum = pgm_inet_checksum (source, sizeof(source), 0);
	g_message ("IP checksum of \"%s\" is 0x%04x",
		   source, csum);
	fail_unless (0xda1f == csum);
}
END_TEST

START_TEST (test_inet_fail_001)
{
	pgm_inet_checksum (NULL, 0, 0);
	fail ();
}
END_TEST

/* target:
 *	guint16
 *	pgm_csum_fold (
 *		guint32			csum
 *	)
 */

START_TEST (test_fold_pass_001)
{
	guint32 csum = 0x325dd;
	guint16 folded_csum = pgm_csum_fold (csum);
	g_message ("0x%08x folds into 0x%08x",
		   csum, folded_csum);
}
END_TEST


/* target:
 *	guint32
 *	pgm_csum_partial (
 *		const void*		src,
 *		guint			len,
 *		guint32			sum
 *	)
 */

START_TEST (test_partial_pass_001)
{
	const char source[] = "i am not a string";
	guint32 csum = pgm_csum_partial (source, sizeof(source), 0);
	g_message ("Checksum of \"%s\" is 0x%08x",
		   source, csum);
	fail_unless (0x325dd == csum);
}
END_TEST

START_TEST (test_partial_fail_001)
{
	pgm_csum_partial (NULL, 0, 0);
	fail ();
}
END_TEST

/* target:
 *	guint32
 *	pgm_csum_partial_copy (
 *		const void*		src,
 *		void*			dst,
 *		guint			len,
 *		guint32			sum
 *	)
 */

START_TEST (test_partial_copy_pass_001)
{
	const char source[] = "i am not a string";
	char dest[1024];
	guint32 csum_source = pgm_csum_partial_copy (source, dest, sizeof(source), 0);
	guint32 csum_dest = pgm_csum_partial (dest, sizeof(source), 0);
	g_message ("Checksum of \"%s\" is 0x%08x, checksum of dest is 0x%08x",
		   source, csum_source, csum_dest);
	fail_unless (0x325dd == csum_source);
	fail_unless (0x325dd == csum_dest);
}
END_TEST

START_TEST (test_partial_copy_fail_001)
{
	pgm_csum_partial_copy (NULL, NULL, 0, 0);
	fail ();
}
END_TEST

/* target:
 *	guint32
 *	pgm_csum_block_add (
 *		guint32			csum,
 *		guint32			csum2,
 *		guint			offset
 *	)
 */

START_TEST (test_block_add_pass_001)
{
	const char source[] = "i am not a string";
	guint32 csum_a = pgm_csum_partial (source, sizeof(source) / 2, 0);
	guint32 csum_b = pgm_csum_partial (source + sizeof(source) / 2, sizeof(source) - (sizeof(source) / 2), 0);
	guint32 csum   = pgm_csum_block_add (csum_a, csum_b, sizeof(source) / 2);
	guint16 fold   = pgm_csum_fold (csum);
	g_message ("Checksum A:0x%08x + B:0x%08x = 0x%08x -> 0x%08x",
		   csum_a, csum_b, csum, fold);
	fail_unless (0xda1f == fold);
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_inet = tcase_create ("inet");
	suite_add_tcase (s, tc_inet);
	tcase_add_test (tc_inet, test_inet_pass_001);
	tcase_add_test_raise_signal (tc_inet, test_inet_fail_001, SIGABRT);

	TCase* tc_fold = tcase_create ("fold");
	suite_add_tcase (s, tc_fold);
	tcase_add_test (tc_fold, test_fold_pass_001);

	TCase* tc_block_add = tcase_create ("block-add");
	suite_add_tcase (s, tc_block_add);
	tcase_add_test (tc_block_add, test_block_add_pass_001);

	TCase* tc_partial = tcase_create ("partial");
	suite_add_tcase (s, tc_partial);
	tcase_add_test (tc_partial, test_partial_pass_001);
	tcase_add_test_raise_signal (tc_partial, test_partial_fail_001, SIGABRT);

	TCase* tc_partial_copy = tcase_create ("partial-copy");
	suite_add_tcase (s, tc_partial_copy);
	tcase_add_test (tc_partial_copy, test_partial_copy_pass_001);
	tcase_add_test_raise_signal (tc_partial_copy, test_partial_copy_fail_001, SIGABRT);
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
