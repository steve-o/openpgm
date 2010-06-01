/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for MD5 hashing (not actual algorithm).
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
#include <glib.h>
#include <check.h>


/* mock state */

/* mock functions for external references */


#define MD5_DEBUG
#include "md5.c"


/* target:
 *	void
 *	_md5_init_ctx (
 *		struct md5_ctx*		ctx
 *	)
 */

START_TEST (test_init_ctx_pass_001)
{
	struct md5_ctx ctx;
	memset (&ctx, 0, sizeof(ctx));
	_md5_init_ctx (&ctx);
}
END_TEST

START_TEST (test_init_ctx_fail_001)
{
	_md5_init_ctx (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	_md5_process_bytes (
 *		struct md5_ctx*		ctx,
 *		gconstpointer		buffer,
 *		gsize			len
 *	)
 */

START_TEST (test_process_bytes_pass_001)
{
	const char buffer[] = "i am not a string.";
	struct md5_ctx ctx;
	memset (&ctx, 0, sizeof(ctx));
	_md5_init_ctx (&ctx);
	_md5_process_bytes (&ctx, buffer, sizeof(buffer));
}
END_TEST

START_TEST (test_process_bytes_fail_001)
{
	const char buffer[] = "i am not a string.";
	_md5_process_bytes (NULL, buffer, sizeof(buffer));
}
END_TEST

/* target:
 *	gpointer	
 *	_md5_finish_ctx (
 *		struct md5_ctx*		ctx,
 *		gpointer		resbuf
 *	)
 */

START_TEST (test_finish_ctx_pass_001)
{
	const char* buffer = "i am not a string.";
	const char* answer = "13de-6066-151e-e5a5-6451-e154-1fb7-3b16";

	char md5[1024];
	char resblock[16];
	struct md5_ctx ctx;
	memset (&ctx, 0, sizeof(ctx));
	memset (resblock, 0, sizeof(resblock));
	_md5_init_ctx (&ctx);
	_md5_process_bytes (&ctx, buffer, sizeof(buffer));
	_md5_finish_ctx (&ctx, resblock);
	sprintf (md5, "%02.2hhx%02.2hhx-%02.2hhx%02.2hhx-%02.2hhx%02.2hhx-%02.2hhx%02.2hhx-%02.2hhx%02.2hhx-%02.2hhx%02.2hhx-%02.2hhx%02.2hhx-%02.2hhx%02.2hhx",
		   resblock[0], resblock[1],
		   resblock[2], resblock[3],
		   resblock[4], resblock[5],
		   resblock[6], resblock[7],
		   resblock[8], resblock[9],
		   resblock[10], resblock[11],
		   resblock[12], resblock[13],
		   resblock[14], resblock[15]);
	g_message ("md5: %s", md5);

	fail_unless (0 == strcmp (md5, answer), "md5 mismatch");
}
END_TEST

START_TEST (test_finish_ctx_fail_001)
{
	char resblock[16];
	_md5_finish_ctx (NULL, resblock);
	fail ("reached");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_init_ctx = tcase_create ("init-ctx");
	suite_add_tcase (s, tc_init_ctx);
	tcase_add_test (tc_init_ctx, test_init_ctx_pass_001);
	tcase_add_test_raise_signal (tc_init_ctx, test_init_ctx_fail_001, SIGABRT);

	TCase* tc_process_bytes = tcase_create ("process_bytes");
	suite_add_tcase (s, tc_process_bytes);
	tcase_add_test (tc_process_bytes, test_process_bytes_pass_001);
	tcase_add_test_raise_signal (tc_process_bytes, test_process_bytes_fail_001, SIGABRT);

	TCase* tc_finish_ctx = tcase_create ("finish-ctx");
	suite_add_tcase (s, tc_finish_ctx);
	tcase_add_test (tc_finish_ctx, test_finish_ctx_pass_001);
	tcase_add_test_raise_signal (tc_finish_ctx, test_finish_ctx_fail_001, SIGABRT);

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
