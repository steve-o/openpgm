/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * performance tests for PGM checksum routines
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


#include <signal.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>

#include <pgm/time.h>
#include <pgm/timep.h>


/* mock state */

static unsigned perf_testsize	= 0;
static unsigned perf_answer	= 0;


static
void
mock_setup (void)
{
	pgm_time_init (NULL);
}

static
void
mock_teardown (void)
{
	pgm_time_shutdown ();
}

static
void
mock_setup_100b (void)
{
	perf_testsize	= 100;
	perf_answer	= 0x6ea8;
}

static
void
mock_setup_200b (void)
{
	perf_testsize	= 200;
	perf_answer	= 0x86e1;
}

static
void
mock_setup_1500b (void)
{
	perf_testsize	= 1500;
	perf_answer	= 0xec69;
}

static
void
mock_setup_9kb (void)
{
	perf_testsize	= 9000;
	perf_answer	= 0x576e;
}

static
void
mock_setup_64kb (void)
{
	perf_testsize	= 65535;
	perf_answer	= 0x3fc0;
}

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

START_TEST (test_8bit)
{
	const unsigned iterations = 1000;
	char* source = alloca (perf_testsize);
	for (unsigned i = 0, j = 0; i < perf_testsize; i++) {
		j = j * 1103515245 + 12345;
		source[i] = j;
	}
	const guint16 answer = perf_answer;		/* network order */

	guint16 csum;
	pgm_time_t start, check;

	start = pgm_time_update_now();
	for (unsigned i = iterations; i; i--) {
		csum = ~do_csum_8bit (source, perf_testsize, 0);
/* function calculates answer in host order */
		csum = g_htons (csum);
		fail_unless (answer == csum, "checksum mismatch 0x%04x (0x%04x)", csum, answer);
	}

	check = pgm_time_update_now();
	g_message ("8-bit/%u: elapsed time %" PGM_TIME_FORMAT " us, unit time %" PGM_TIME_FORMAT " us",
		perf_testsize,
		(guint64)(check - start),
		(guint64)((check - start) / iterations));
}
END_TEST

START_TEST (test_16bit)
{
	const unsigned iterations = 1000;
	char* source = alloca (perf_testsize);
	for (unsigned i = 0, j = 0; i < perf_testsize; i++) {
		j = j * 1103515245 + 12345;
		source[i] = j;
	}
	const guint16 answer = perf_answer;		/* network order */

	guint16 csum;
	pgm_time_t start, check;

	start = pgm_time_update_now();
	for (unsigned i = iterations; i; i--) {
		csum = ~do_csum_16bit (source, perf_testsize, 0);
/* function calculates answer in host order */
		csum = g_htons (csum);
		fail_unless (answer == csum, "checksum mismatch 0x%04x (0x%04x)", csum, answer);
	}

	check = pgm_time_update_now();
	g_message ("16-bit/%u: elapsed time %" PGM_TIME_FORMAT " us, unit time %" PGM_TIME_FORMAT " us",
		perf_testsize,
		(guint64)(check - start),
		(guint64)((check - start) / iterations));
}
END_TEST

START_TEST (test_32bit)
{
	const unsigned iterations = 1000;
	char* source = alloca (perf_testsize);
	for (unsigned i = 0, j = 0; i < perf_testsize; i++) {
		j = j * 1103515245 + 12345;
		source[i] = j;
	}
	const guint16 answer = perf_answer;		/* network order */

	guint16 csum;
	pgm_time_t start, check;

	start = pgm_time_update_now();
	for (unsigned i = iterations; i; i--) {
		csum = ~do_csum_32bit (source, perf_testsize, 0);
/* function calculates answer in host order */
		csum = g_htons (csum);
		fail_unless (answer == csum, "checksum mismatch 0x%04x (0x%04x)", csum, answer);
	}

	check = pgm_time_update_now();
	g_message ("32-bit/%u: elapsed time %" PGM_TIME_FORMAT " us, unit time %" PGM_TIME_FORMAT " us",
		perf_testsize,
		(guint64)(check - start),
		(guint64)((check - start) / iterations));
}
END_TEST

START_TEST (test_64bit)
{
	const unsigned iterations = 1000;
	char* source = alloca (perf_testsize);
	for (unsigned i = 0, j = 0; i < perf_testsize; i++) {
		j = j * 1103515245 + 12345;
		source[i] = j;
	}
	const guint16 answer = perf_answer;		/* network order */

	guint16 csum;
	pgm_time_t start, check;

	start = pgm_time_update_now();
	for (unsigned i = iterations; i; i--) {
		csum = ~do_csum_64bit (source, perf_testsize, 0);
/* function calculates answer in host order */
		csum = g_htons (csum);
		fail_unless (answer == csum, "checksum mismatch 0x%04x (0x%04x)", csum, answer);
	}

	check = pgm_time_update_now();
	g_message ("64-bit/%u: elapsed time %" PGM_TIME_FORMAT " us, unit time %" PGM_TIME_FORMAT " us",
		perf_testsize,
		(guint64)(check - start),
		(guint64)((check - start) / iterations));
}
END_TEST

#if defined(__amd64) || defined(__x86_64__)
START_TEST (test_vector)
{
	const unsigned iterations = 1000;
	char* source = alloca (perf_testsize);
	for (unsigned i = 0, j = 0; i < perf_testsize; i++) {
		j = j * 1103515245 + 12345;
		source[i] = j;
	}
	const guint16 answer = perf_answer;		/* network order */

	guint16 csum;
	pgm_time_t start, check;

	start = pgm_time_update_now();
	for (unsigned i = iterations; i; i--) {
		csum = ~do_csum_vector (source, perf_testsize, 0);
/* function calculates answer in host order */
		csum = g_htons (csum);
		fail_unless (answer == csum, "checksum mismatch 0x%04x (0x%04x)", csum, answer);
	}

	check = pgm_time_update_now();
	g_message ("vector/%u: elapsed time %" PGM_TIME_FORMAT " us, unit time %" PGM_TIME_FORMAT " us",
		perf_testsize,
		(guint64)(check - start),
		(guint64)((check - start) / iterations));
}
END_TEST
#endif /* defined(__amd64) || defined(__x86_64__) */



static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_100b = tcase_create ("100b");
	suite_add_tcase (s, tc_100b);
	tcase_add_checked_fixture (tc_100b, mock_setup, mock_teardown);
	tcase_add_checked_fixture (tc_100b, mock_setup_100b, NULL);
	tcase_add_test (tc_100b, test_8bit);
	tcase_add_test (tc_100b, test_16bit);
	tcase_add_test (tc_100b, test_32bit);
	tcase_add_test (tc_100b, test_64bit);
#if defined(__amd64) || defined(__x86_64__)
	tcase_add_test (tc_100b, test_vector);
#endif

	TCase* tc_200b = tcase_create ("200b");
	suite_add_tcase (s, tc_200b);
	tcase_add_checked_fixture (tc_200b, mock_setup, mock_teardown);
	tcase_add_checked_fixture (tc_200b, mock_setup_200b, NULL);
	tcase_add_test (tc_200b, test_8bit);
	tcase_add_test (tc_200b, test_16bit);
	tcase_add_test (tc_200b, test_32bit);
	tcase_add_test (tc_200b, test_64bit);
#if defined(__amd64) || defined(__x86_64__)
	tcase_add_test (tc_200b, test_vector);
#endif

	TCase* tc_1500b = tcase_create ("1500b");
	suite_add_tcase (s, tc_1500b);
	tcase_add_checked_fixture (tc_1500b, mock_setup, mock_teardown);
	tcase_add_checked_fixture (tc_1500b, mock_setup_1500b, NULL);
	tcase_add_test (tc_1500b, test_8bit);
	tcase_add_test (tc_1500b, test_16bit);
	tcase_add_test (tc_1500b, test_32bit);
	tcase_add_test (tc_1500b, test_64bit);
#if defined(__amd64) || defined(__x86_64__)
	tcase_add_test (tc_1500b, test_vector);
#endif

	TCase* tc_9kb = tcase_create ("9KB");
	suite_add_tcase (s, tc_9kb);
	tcase_add_checked_fixture (tc_9kb, mock_setup, mock_teardown);
	tcase_add_checked_fixture (tc_9kb, mock_setup_9kb, NULL);
	tcase_add_test (tc_9kb, test_8bit);
	tcase_add_test (tc_9kb, test_16bit);
	tcase_add_test (tc_9kb, test_32bit);
	tcase_add_test (tc_9kb, test_64bit);
#if defined(__amd64) || defined(__x86_64__)
	tcase_add_test (tc_9kb, test_vector);
#endif

	TCase* tc_64kb = tcase_create ("64KB");
	suite_add_tcase (s, tc_64kb);
	tcase_add_checked_fixture (tc_64kb, mock_setup, mock_teardown);
	tcase_add_checked_fixture (tc_64kb, mock_setup_64kb, NULL);
	tcase_add_test (tc_64kb, test_8bit);
	tcase_add_test (tc_64kb, test_16bit);
	tcase_add_test (tc_64kb, test_32bit);
	tcase_add_test (tc_64kb, test_64bit);
#if defined(__amd64) || defined(__x86_64__)
	tcase_add_test (tc_64kb, test_vector);
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
