/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for portable getifaddrs implementation.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <check.h>


/* mock state */

/* mock functions for external references */


#define GETIFADDRS_DEBUG
#include "getifaddrs.c"


char*
ifflags_string (
	unsigned int	flags
	)
{
	static char s[1024];

	s[0] = '\0';
	if (flags & IFF_UP)
		strcat (s, "IFF_UP");
#define IFF(flag) \
	do { \
		if (flags & flag) { \
			strcat (s, s[0] ? ("|" #flag) : (#flag)); \
		} \
	} while (0)
#ifdef IFF_BROADCAST
	IFF(IFF_BROADCAST);
#endif
#ifdef IFF_DEBUG
	IFF(IFF_DEBUG);
#endif
#ifdef IFF_LOOPBACK
	IFF(IFF_LOOPBACK);
#endif
#ifdef IFF_POINTOPOINT
	IFF(IFF_POINTOPOINT);
#endif
#ifdef IFF_RUNNING
	IFF(IFF_RUNNING);
#endif
#ifdef IFF_NOARP
	IFF(IFF_NOARP);
#endif
#ifdef IFF_PROMISC
	IFF(IFF_PROMISC);
#endif
#ifdef IFF_NOTRAILERS
	IFF(IFF_NOTRAILERS);
#endif
#ifdef IFF_ALLMULTI
	IFF(IFF_ALLMULTI);
#endif
#ifdef IFF_MASTER
	IFF(IFF_MASTER);
#endif
#ifdef IFF_SLAVE
	IFF(IFF_SLAVE);
#endif
#ifdef IFF_MULTICAST
	IFF(IFF_MULTICAST);
#endif
#ifdef IFF_PORTSEL
	IFF(IFF_PORTSEL);
#endif
#ifdef IFF_AUTOMEDIA
	IFF(IFF_AUTOMEDIA);
#endif
#ifdef IFF_DYNAMIC
	IFF(IFF_DYNAMIC);
#endif
#ifdef IFF_LOWER_UP
	IFF(IFF_LOWER_UP);
#endif
#ifdef IFF_DORMANT
	IFF(IFF_DORMANT);
#endif
#ifdef IFF_ECHO
	IFF(IFF_ECHO);
#endif
	if (!s[0]) {
		if (flags)
			sprintf (s, "0x%x", flags);
		else
			strcpy (s, "(null)");
	}
	return s;
}

/* target:
 *	int
 *	pgm_getifaddrs (
 *		struct pgm_ifaddrs**	ifap
 *	)
 */

START_TEST (test_getifaddrs_pass_001)
{
	char saddr[INET6_ADDRSTRLEN], snetmask[INET6_ADDRSTRLEN];
	struct pgm_ifaddrs *ifap = NULL, *ifa;
	fail_unless (0 == pgm_getifaddrs (&ifap), "getifaddrs failed");
	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		fail_unless (NULL != ifa, "invalid address");
		if (ifa->ifa_addr) {
			if (AF_INET  == ifa->ifa_addr->sa_family ||
			    AF_INET6 == ifa->ifa_addr->sa_family)
				pgm_sockaddr_ntop (ifa->ifa_addr, saddr, sizeof(saddr));
#ifdef AF_PACKET
			else if (AF_PACKET == ifa->ifa_addr->sa_family)
				strcpy (saddr, "(AF_PACKET)");
#endif
			else
				sprintf (saddr, "(AF = %d)", ifa->ifa_addr->sa_family);
		} else
			strcpy (saddr, "(null)");
		if (ifa->ifa_netmask) {
			if (AF_INET  == ifa->ifa_addr->sa_family ||
			    AF_INET6 == ifa->ifa_addr->sa_family)
				pgm_sockaddr_ntop (ifa->ifa_netmask, snetmask, sizeof(snetmask));
#ifdef AF_PACKET
			else if (AF_PACKET == ifa->ifa_addr->sa_family)
				strcpy (snetmask, "(AF_PACKET)");
#endif
			else
				sprintf (snetmask, "(AF = %d)", ifa->ifa_addr->sa_family);
		} else
			strcpy (snetmask, "(null)");
		g_message ("ifa = {"
			"ifa_next = %p, "
			"ifa_name = \"%s\", "
			"ifa_flags = %s, "
			"ifa_addr = %s, "
			"ifa_netmask = %s"
			"}",
			ifa->ifa_next,
			ifa->ifa_name,
			ifflags_string (ifa->ifa_flags),
			saddr,
			snetmask);
	}
}
END_TEST

START_TEST (test_getifaddrs_fail_001)
{
	fail_unless (-1 == pgm_getifaddrs (NULL), "getifaddrs failed");
	g_message ("errno:%d", errno);
}
END_TEST

/* target:
 *	void
 *	pgm_freeifaddrs (
 *		struct pgm_ifaddrs*	ifa
 *	)
 */

START_TEST (test_freeifaddrs_pass_001)
{
	struct pgm_ifaddrs* ifap = NULL;
	fail_unless (0 == pgm_getifaddrs (&ifap), "getifaddrs failed");
	pgm_freeifaddrs (ifap);
}
END_TEST

/* silent failure */
START_TEST (test_freeifaddrs_pass_002)
{
	pgm_freeifaddrs (NULL);
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_getifaddrs = tcase_create ("getifaddrs");
	suite_add_tcase (s, tc_getifaddrs);
	tcase_add_test (tc_getifaddrs, test_getifaddrs_pass_001);
	tcase_add_test_raise_signal (tc_getifaddrs, test_getifaddrs_fail_001, SIGSEGV);

	TCase* tc_freeifaddrs = tcase_create ("freeifaddrs");
	suite_add_tcase (s, tc_freeifaddrs);
	tcase_add_test (tc_freeifaddrs, test_freeifaddrs_pass_001);
	tcase_add_test (tc_freeifaddrs, test_freeifaddrs_pass_002);

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
