/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for portable function to enumerate protocol names.
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

#ifndef _WIN32
#	include <sys/socket.h>
#	include <netdb.h>
#else
#	include <ws2tcpip.h>
#endif

#include <glib.h>
#include <check.h>


/* mock state */

#define GETPROTOBYNAME_DEBUG
#include "getprotobyname.c"

static
void
mock_setup (void)
{
}

static
void
mock_teardown (void)
{
}

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	struct protoent*
 *	pgm_getprotobyname (
 *		const char*	name
 *	)
 */

START_TEST (test_getprotobyname_pass_001)
{
	const char ipv6[] = "ipv6";

	fail_if (NULL == pgm_getprotobyname (ipv6), "getprotobyname failed");
}
END_TEST

START_TEST (test_getprotobyname_fail_001)
{
	fail_unless (NULL == pgm_getprotobyname (NULL), "getprotobyname failed");
}
END_TEST

START_TEST (test_getprotobyname_fail_002)
{
	const char unknown[] = "qwertyuiop";

	fail_unless (NULL == pgm_getprotobyname (unknown), "getprotobyname failed");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);
	TCase* tc_getprotobyname = tcase_create ("getprotobyname");
	suite_add_tcase (s, tc_getprotobyname);
	tcase_add_checked_fixture (tc_getprotobyname, mock_setup, mock_teardown);
	tcase_add_test (tc_getprotobyname, test_getprotobyname_pass_001);
	tcase_add_test (tc_getprotobyname, test_getprotobyname_fail_001);
	tcase_add_test (tc_getprotobyname, test_getprotobyname_fail_002);
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
