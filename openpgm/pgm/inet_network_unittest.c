/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for portable implementations of inet_network and inet_network6.
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


#define INET_NETWORK_DEBUG
#include "inet_network.c"


/* target:
 *	int
 *	pgm_inet_network (
 *		const char*		s,
 *		struct in_addr*		in	-- in host byte order
 *	)
 */

START_TEST (test_inet_network_pass_001)
{
	const char network[] = "127.0.0.1/8";
	const char answer[]  = "127.0.0.0";

	struct in_addr addr;
//	fail_unless (0 == pgm_inet_network (network, &addr));
addr.s_addr = inet_network ("127.0.0.1");
	g_message ("Resolved \"%s\" to \"%s\"",
		   network, inet_ntoa (addr));
	struct in_addr network_addr = { .s_addr = g_htonl (addr.s_addr) };
g_message ("network order: %s", inet_ntoa (network_addr));
	fail_unless (0 == strcmp (answer, inet_ntoa (network_addr)));
}
END_TEST

START_TEST (test_inet_network_fail_001)
{
	fail_unless (-1 == pgm_inet_network (NULL, NULL));
}
END_TEST

/* target:
 *	int
 *	pgm_inet6_network (
 *		const char*		s,
 *		struct in6_addr*	in6
 *	)
 */

START_TEST (test_inet6_network_pass_001)
{
	const char network[] = "::1/128";
	const char answer[] = "";

	char snetwork[INET6_ADDRSTRLEN];
	struct in6_addr addr;
	fail_unless (0 == pgm_inet6_network (network, &addr));
	g_message ("Resolved \"%s\" to \"%s\"",
		   network, pgm_inet_ntop (AF_INET6, &addr, snetwork, sizeof(snetwork)));
}
END_TEST

START_TEST (test_inet6_network_fail_001)
{
	fail_unless (-1 == pgm_inet6_network (NULL, NULL));
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_inet_network = tcase_create ("inet-network");
	suite_add_tcase (s, tc_inet_network);
	tcase_add_test (tc_inet_network, test_inet_network_pass_001);
	tcase_add_test (tc_inet_network, test_inet_network_fail_001);

	TCase* tc_inet6_network = tcase_create ("inet6-network");
	suite_add_tcase (s, tc_inet6_network);
	tcase_add_test (tc_inet6_network, test_inet6_network_pass_001);
	tcase_add_test (tc_inet6_network, test_inet6_network_fail_001);
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
