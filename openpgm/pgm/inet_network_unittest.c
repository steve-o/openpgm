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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
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

#define INET_NETWORK_DEBUG
#include "inet_network.c"

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *	int
 *	pgm_inet_network (
 *		const char*		s,
 *		struct in_addr*		in	-- in host byte order
 *	)
 */

struct test_case_t {
	const char* network;
	const char* answer;
};

static const struct test_case_t cases_001[] = {
	{ "127",			"127.0.0.0"		}, /* different to inet_addr/inet_network */
	{ "127/8",			"127.0.0.0"		},
	{ "127.1/8",			"127.0.0.0"		},
	{ "127.1",			"127.1.0.0"		}, /* different to inet_addr/inet_network */
	{ "127.x.x.x",			"127.0.0.0"		},
	{ "127.X.X.X",			"127.0.0.0"		},
	{ "127.0.0.0",			"127.0.0.0"		},
	{ "127.0.0.1/8",		"127.0.0.0"		},
	{ "127.0.0.1/32",		"127.0.0.1"		},
	{ "10.0.0.0/8",			"10.0.0.0"		},	/* RFC1918 class A */
	{ "10.255.255.255/8",		"10.0.0.0"		},
	{ "172.16.0.0/12",		"172.16.0.0"		},	/* RFC1918 class B */
	{ "172.31.255.255/12",		"172.16.0.0"		},
	{ "192.168.0.0/16",		"192.168.0.0"		},	/* RFC1918 class C */
	{ "192.168.255.255/16",		"192.168.0.0"		},
	{ "169.254.0.0/16",		"169.254.0.0"		},	/* RFC3927 link-local */
	{ "192.88.99.0/24",		"192.88.99.0"		},	/* RFC3068 6to4 relay anycast */
	{ "224.0.0.0/4",		"224.0.0.0"		},	/* RFC3171 multicast */
	{ "0.0.0.0",			"0.0.0.0"		},
	{ "255.255.255.255",		"255.255.255.255"	},
};

START_TEST (test_inet_network_pass_001)
{
	const char* network = cases_001[_i].network;
	const char* answer  = cases_001[_i].answer;

	struct in_addr host_order, network_order;
	fail_unless (0 == pgm_inet_network (network, &host_order), "inet_network failed");
	network_order.s_addr = g_htonl (host_order.s_addr);

	g_message ("Resolved \"%s\" to \"%s\"",
		   network, inet_ntoa (network_order));
#ifdef DEBUG_INET_NETWORK
{
struct in_addr t = { .s_addr = g_htonl (inet_network (network)) };
g_message ("inet_network (%s) = %s", network, inet_ntoa (t));
}
#endif

	fail_unless (0 == strcmp (answer, inet_ntoa (network_order)), "unexpected answer");
}
END_TEST

START_TEST (test_inet_network_fail_001)
{
	fail_unless (-1 == pgm_inet_network (NULL, NULL), "inet_network failed");
}
END_TEST

START_TEST (test_inet_network_fail_002)
{
	const char* network = "192.168.0.1/0";

	struct in_addr host_order;
	fail_unless (-1 == pgm_inet_network (network, &host_order), "inet_network failed");
}
END_TEST

/* target:
 *	int
 *	pgm_inet6_network (
 *		const char*		s,
 *		struct in6_addr*	in6
 *	)
 */

static const struct test_case_t cases6_001[] = {
	{ "::1/128",				"::1"			},
	{ "2002:dec8:d28e::36/64",		"2002:dec8:d28e::"	},	/* 6to4 */
	{ "fe80::203:baff:fe4e:6cc8/10",	"fe80::"		},	/* link-local */
	{ "ff02::1/8",				"ff00::"		},	/* multicast */
	{ "fc00:6::61/7",			"fc00::"		},	/* ULA */
};

START_TEST (test_inet6_network_pass_001)
{
	const char* network = cases6_001[_i].network;
	const char* answer  = cases6_001[_i].answer;

	char snetwork[INET6_ADDRSTRLEN];
	struct in6_addr addr;
	fail_unless (0 == pgm_inet6_network (network, &addr), "inet6_network failed");
	g_message ("Resolved \"%s\" to \"%s\"",
		   network, pgm_inet_ntop (AF_INET6, &addr, snetwork, sizeof(snetwork)));

	fail_unless (0 == strcmp (answer, snetwork), "unexpected answer");
}
END_TEST

START_TEST (test_inet6_network_fail_001)
{
	fail_unless (-1 == pgm_inet6_network (NULL, NULL), "inet6_network failed");
}
END_TEST

START_TEST (test_sa6_network_pass_001)
{
	const char* network = cases6_001[_i].network;
	const char* answer  = cases6_001[_i].answer;

	char snetwork[INET6_ADDRSTRLEN];
	struct sockaddr_in6 addr;
	fail_unless (0 == pgm_sa6_network (network, &addr), "sa6_network failed");
	pgm_sockaddr_ntop ((struct sockaddr*)&addr, snetwork, sizeof (snetwork));
	g_message ("Resolved \"%s\" to \"%s\"", network, snetwork);
	fail_unless (0 == strcmp (answer, snetwork), "unexpected answer");
}
END_TEST

START_TEST (test_sa6_network_fail_001)
{
	fail_unless (-1 == pgm_sa6_network (NULL, NULL), "sa6_network failed");
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
	tcase_add_loop_test (tc_inet_network, test_inet_network_pass_001, 0, G_N_ELEMENTS(cases_001));
	tcase_add_test (tc_inet_network, test_inet_network_fail_001);

	TCase* tc_inet6_network = tcase_create ("inet6-network");
	suite_add_tcase (s, tc_inet6_network);
	tcase_add_loop_test (tc_inet6_network, test_inet6_network_pass_001, 0, G_N_ELEMENTS(cases6_001));
	tcase_add_test (tc_inet6_network, test_inet6_network_fail_001);

	TCase* tc_sa6_network = tcase_create ("sa6-network");
	suite_add_tcase (s, tc_sa6_network);
	tcase_add_loop_test (tc_sa6_network, test_sa6_network_pass_001, 0, G_N_ELEMENTS(cases6_001));
	tcase_add_test (tc_sa6_network, test_sa6_network_fail_001);
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
