/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for the HTTP administration interface.
 *
 * Copyright (c) 2009-2010 Miru Limited.
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
#include <string.h>
#include <glib.h>
#include <check.h>

#include "impl/framework.h"

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


/* mock state */
static pgm_rwlock_t	mock_pgm_sock_list_lock;
static pgm_slist_t*	mock_pgm_sock_list;

/* mock functions for external references */

#define pgm_sock_list_lock	mock_pgm_sock_list_lock
#define pgm_sock_list		mock_pgm_sock_list

#define HTTP_DEBUG
#include "http.c"

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	bool
 *	pgm_http_init (
 *		uint16_t*	http_port,
 *		pgm_error_t**	error
 *	)
 */

START_TEST (test_init_pass_001)
{
	pgm_error_t* err = NULL;
	fail_unless (TRUE == pgm_http_init (8080, &err), "init failed");
	fail_unless (NULL == err, "init failed");
#ifdef PGM_CHECK_NOFORK
	fail_unless (TRUE == pgm_http_shutdown (), "shutdown failed");
	fail_unless (FALSE == pgm_http_shutdown (), "shutdown failed");
#endif
	
}
END_TEST

/* duplicate servers */
START_TEST (test_init_fail_001)
{
	pgm_error_t* err = NULL;
	fail_unless (TRUE == pgm_http_init (8080, &err), "init failed");
	fail_unless (TRUE == pgm_http_init (8080, &err), "init failed");
#ifdef PGM_CHECK_NOFORK
	fail_unless (TRUE == pgm_http_shutdown (), "shutdown failed");
	fail_unless (TRUE == pgm_http_shutdown (), "shutdown failed");
	fail_unless (FALSE == pgm_http_shutdown (), "shutdown failed");
#endif
}
END_TEST

/* target:
 *	bool
 *	pgm_http_shutdown (void)
 */

START_TEST (test_shutdown_pass_001)
{
	pgm_error_t* err = NULL;
	fail_unless (TRUE == pgm_http_init (8080, &err), "init failed");
	fail_unless (NULL == err, "init failed");
	fail_unless (TRUE == pgm_http_shutdown (), "shutdown failed");
	fail_unless (FALSE == pgm_http_shutdown (), "shutdown failed");
}
END_TEST

/* repeatability
 */
START_TEST (test_shutdown_pass_002)
{
	pgm_error_t* err = NULL;
	fail_unless (TRUE == pgm_http_init (8080, &err), "init failed");
	fail_unless (NULL == err, "init failed");
	fail_unless (TRUE == pgm_http_shutdown (), "shutdown failed");
	fail_unless (TRUE == pgm_http_init (8080, &err), "init failed");
	fail_unless (NULL == err, "init failed");
	fail_unless (TRUE == pgm_http_shutdown (), "shutdown failed");
	fail_unless (FALSE == pgm_http_shutdown (), "shutdown failed");
}
END_TEST

/* no running server */
START_TEST (test_shutdown_fail_001)
{
	fail_unless (FALSE == pgm_http_shutdown (), "shutdown failed");
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
	tcase_add_test (tc_init, test_init_fail_001);

	TCase* tc_shutdown = tcase_create ("shutdown");
	suite_add_tcase (s, tc_shutdown);
	tcase_add_test (tc_shutdown, test_shutdown_pass_001);
	tcase_add_test (tc_shutdown, test_shutdown_pass_002);
	tcase_add_test (tc_shutdown, test_shutdown_fail_001);

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
#ifdef _WIN32
	WORD wVersionRequested = MAKEWORD (2, 2);
	WSADATA wsaData;
	g_assert (0 == WSAStartup (wVersionRequested, &wsaData));
	g_assert (LOBYTE (wsaData.wVersion) == 2 && HIBYTE (wsaData.wVersion) == 2);
#endif
	pgm_messages_init();
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	pgm_messages_shutdown();
#ifdef _WIN32
	WSACleanup();
#endif
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
