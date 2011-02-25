/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for portable function to enumerate network names.
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

#ifndef _WIN32
#	define COMPARE_GETNETENT
#endif

#define GETNETBYNAME_DEBUG
#include "getnetbyname.c"

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

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

/* target:
 *	struct netent*
 *	pgm_getnetbyname (
 *		const char*	name
 *	)
 */

START_TEST (test_getnetbyname_pass_001)
{
	const char loopback[] = "loopback";

	fail_if (NULL == pgm_getnetbyname (loopback), "getnetbyname failed");
}
END_TEST

START_TEST (test_getnetbyname_fail_001)
{
	fail_unless (NULL == pgm_getnetbyname (NULL), "getnetbyname failed");
}
END_TEST

START_TEST (test_getnetbyname_fail_002)
{
	const char unknown[] = "qwertyuiop";

	fail_unless (NULL == pgm_getnetbyname (unknown), "getnetbyname failed");
}
END_TEST

/* target:
 *	struct pgm_netent_t*
 *	_pgm_compat_getnetent (void)
 */

START_TEST (test_getnetent_pass_001)
{
	int i = 1;
	struct pgm_netent_t* ne;
#ifdef COMPARE_GETNETENT
	struct netent* nne;
#endif

	_pgm_compat_setnetent();
#ifdef COMPARE_GETNETENT
	setnetent (0);
#endif
	while (ne = _pgm_compat_getnetent()) {
		char buffer[1024];
		char **p;

#ifdef COMPARE_GETNETENT
		nne = getnetent();
		if (NULL == nne)
			g_warning ("native ne = (null");
#endif

/* official network name */
		fail_if (NULL == ne->n_name, "no official name");
		g_debug ("%-6dn_name = %s", i++, ne->n_name);
#ifdef COMPARE_GETNETENT
		if (NULL != nne)
			fail_unless (0 == strcmp (ne->n_name, nne->n_name), "official name mismatch");
#endif

/* alias list */
		fail_if (NULL == ne->n_aliases, "invalid aliases pointer");
		p = ne->n_aliases;
		if (*p) {
			strcpy (buffer, *p++);
			while (*p) {
				strcat (buffer, ", ");
				strcat (buffer, *p++);
			}
		} else
			strcpy (buffer, "(nil)");
		g_debug ("      n_aliases = %s", buffer);
#ifdef COMPARE_GETNETENT
		if (NULL != nne) {
			char nbuffer[1024];

			fail_if (NULL == nne->n_aliases, "invalid aliases pointer");
			p = nne->n_aliases;
			if (*p) {
				strcpy (nbuffer, *p++);
				while (*p) {
					strcat (nbuffer, ", ");
					strcat (nbuffer, *p++);
				}
			} else
				strcpy (nbuffer, "(nil)");
			fail_unless (0 == strcmp (buffer, nbuffer), "aliases mismatch");
		}
#endif

/* net address type */
		fail_unless (AF_INET == ne->n_net.ss_family || AF_INET6 == ne->n_net.ss_family, "invalid address family");
		if (AF_INET == ne->n_net.ss_family) {
			struct sockaddr_in sa;
			struct in_addr net;

			g_debug ("      n_addrtype = AF_INET");
#ifdef COMPARE_GETNETENT
			fail_unless (ne->n_net.ss_family == nne->n_addrtype, "address family mismatch");
#endif
/* network number */
			memcpy (&sa, &ne->n_net, sizeof (sa));
			fail_unless (0 == getnameinfo ((struct sockaddr*)&sa, sizeof (sa),
						       buffer, sizeof (buffer),
						       NULL, 0,
						       NI_NUMERICHOST), "getnameinfo failed");
			g_debug ("      n_net = %s", buffer);
#ifdef COMPARE_GETNETENT
			net = pgm_inet_makeaddr (nne->n_net, 0);
			fail_unless (0 == memcmp (&sa.sin_addr, &net, sizeof (struct in_addr)), "network address mismatch");
#endif
		} else {
			struct sockaddr_in6 sa6;
			g_debug ("      n_addrtype = AF_INET6");
#ifdef COMPARE_GETNETENT
			if (ne->n_net.ss_family != nne->n_addrtype)
				g_warning ("native address type not AF_INET6");
#endif
			memcpy (&sa6, &ne->n_net, sizeof (sa6));
			fail_unless (0 == getnameinfo ((struct sockaddr*)&sa6, sizeof (sa6),
						       buffer, sizeof (buffer),
						       NULL, 0,
						       NI_NUMERICHOST), "getnameinfo failed");
			g_debug ("      n_net = %s", buffer);
		}
	}
	_pgm_compat_endnetent();
#ifdef COMPARE_GETNETENT
	endnetent();
#endif
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);
	TCase* tc_getnetbyname = tcase_create ("getnetbyname");
	suite_add_tcase (s, tc_getnetbyname);
	tcase_add_checked_fixture (tc_getnetbyname, mock_setup, mock_teardown);
	tcase_add_test (tc_getnetbyname, test_getnetbyname_pass_001);
	tcase_add_test (tc_getnetbyname, test_getnetbyname_fail_001);
	tcase_add_test (tc_getnetbyname, test_getnetbyname_fail_002);
	tcase_add_test (tc_getnetbyname, test_getnetent_pass_001);
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
