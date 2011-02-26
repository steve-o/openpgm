/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for ip stack.
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

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef _WIN32
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
#include <glib.h>
#include <check.h>
#include <pgm/zinttypes.h>


/* getsockopt(3SOCKET)
 * level is the protocol number of the protocl that controls the option.
 */
#ifndef SOL_IP
#	define SOL_IP		IPPROTO_IP
#endif
#ifndef SOL_IPV6
#	define SOL_IPV6		IPPROTO_IPV6
#endif

/* mock state */

size_t
pgm_transport_pkt_offset2 (
        const bool                      can_fragment,
        const bool                      use_pgmcc
        )
{
        return 0;
}

#define PGM_COMPILATION
#include "impl/sockaddr.h"
#include "impl/indextoaddr.h"
#include "impl/ip.h"

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* target:
 *   testing platform capability to loop send multicast packets to a listening
 * receive socket.
 */

START_TEST (test_multicast_loop_pass_001)
{
	struct sockaddr_in	multicast_group;
	struct sockaddr_in	recv_addr;
	struct sockaddr_in	send_addr;
	struct sockaddr_in	if_addr;

	memset (&multicast_group, 0, sizeof(multicast_group));
	multicast_group.sin_family = AF_INET;
	multicast_group.sin_port = 0;
	multicast_group.sin_addr.s_addr = inet_addr ("239.192.0.1");

	fail_unless (TRUE == pgm_if_indextoaddr (0, AF_INET, 0, (struct sockaddr*)&if_addr, NULL), "if_indextoaddr failed");

	SOCKET recv_sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	fail_if (INVALID_SOCKET == recv_sock, "socket failed");
	memcpy (&recv_addr, &multicast_group, sizeof(multicast_group));
/* listen for UDP packets on port 7500 */
	recv_addr.sin_port = htons (7500);
#ifdef _WIN32
/* Can only bind to interfaces */
	recv_addr.sin_addr.s_addr = INADDR_ANY;
#endif
	fail_unless (0 == bind (recv_sock, (struct sockaddr*)&recv_addr, pgm_sockaddr_len ((struct sockaddr*)&recv_addr)), "bind failed");
#ifndef NO_MCAST_JOIN_GROUP
	struct group_req gr;
	memset (&gr, 0, sizeof(gr));
	((struct sockaddr*)&gr.gr_group)->sa_family = multicast_group.sin_family;
	((struct sockaddr_in*)&gr.gr_group)->sin_addr.s_addr = multicast_group.sin_addr.s_addr;
	fail_unless (0 == setsockopt (recv_sock, SOL_IP, MCAST_JOIN_GROUP, (const char*)&gr, sizeof(gr)), "setsockopt(MCAST_JOIN_GROUP) failed");
#else
	struct ip_mreq mreq;
	memset (&mreq, 0, sizeof(mreq));
	mreq.imr_multiaddr.s_addr = multicast_group.sin_addr.s_addr;
	mreq.imr_interface.s_addr = if_addr.sin_addr.s_addr;
	fail_unless (0 == setsockopt (recv_sock, SOL_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq)), "setsockopt(IP_ADD_MEMBERSHIP) failed");
#endif
	fail_unless (0 == pgm_sockaddr_multicast_loop (recv_sock, AF_INET, TRUE), "multicast_loop failed");

	SOCKET send_sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	fail_if (INVALID_SOCKET == send_sock, "socket failed");
	memcpy (&send_addr, &multicast_group, sizeof(multicast_group));
/* random port, default routing */
	send_addr.sin_port = 0;
	send_addr.sin_addr.s_addr = INADDR_ANY;
	fail_unless (0 == bind (send_sock, (struct sockaddr*)&send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr)), "bind failed");
	fail_unless (0 == pgm_sockaddr_multicast_if (send_sock, (struct sockaddr*)&if_addr, 0), "multicast_if failed");
	fail_unless (0 == pgm_sockaddr_multicast_loop (send_sock, AF_INET, TRUE), "multicast_loop failed");

	const char data[] = "apple pie";
	send_addr.sin_port = htons (7500);
	send_addr.sin_addr.s_addr = multicast_group.sin_addr.s_addr;
	ssize_t bytes_sent = sendto (send_sock, data, sizeof(data), 0, (struct sockaddr*)&send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));
	if (SOCKET_ERROR == bytes_sent) {
		char errbuf[1024];
		g_message ("sendto: %s",
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), pgm_get_last_sock_error()));
	}
	fail_unless (sizeof(data) == bytes_sent, "sendto underrun");

	char recv_data[1024];
	ssize_t bytes_read = recv (recv_sock, recv_data, sizeof(recv_data), MSG_DONTWAIT);
	if (SOCKET_ERROR == bytes_read) {
		char errbuf[1024];
		g_message ("recv: %s",
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), pgm_get_last_sock_error()));
	}
	fail_unless (sizeof(data) == bytes_read, "recv underrun");

	fail_unless (0 == closesocket (recv_sock), "closesocket failed");
	fail_unless (0 == closesocket (send_sock), "closesocket failed");
}
END_TEST

/* target:
 *   testing whether unicast bind accepts packets to multicast join on a
 * different port.
 */

START_TEST (test_port_bind_pass_001)
{
	struct sockaddr_in	multicast_group;
	struct sockaddr_in	recv_addr;
	struct sockaddr_in	send_addr;
	struct sockaddr_in	if_addr;

	memset (&multicast_group, 0, sizeof(multicast_group));
	multicast_group.sin_family = AF_INET;
	multicast_group.sin_port = 0;
	multicast_group.sin_addr.s_addr = inet_addr ("239.192.0.1");

	fail_unless (TRUE == pgm_if_indextoaddr (0, AF_INET, 0, (struct sockaddr*)&if_addr, NULL), "if_indextoaddr failed");

	SOCKET recv_sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	fail_if (INVALID_SOCKET == recv_sock, "socket failed");
	memcpy (&recv_addr, &multicast_group, sizeof(multicast_group));
	recv_addr.sin_port = htons (3056);
#ifdef _WIN32
/* Can only bind to interfaces */
	recv_addr.sin_addr.s_addr = INADDR_ANY;
#endif
	fail_unless (0 == bind (recv_sock, (struct sockaddr*)&recv_addr, pgm_sockaddr_len ((struct sockaddr*)&recv_addr)), "bind failed");
	struct group_req gr;
	memset (&gr, 0, sizeof(gr));
	((struct sockaddr*)&gr.gr_group)->sa_family = multicast_group.sin_family;
	((struct sockaddr_in*)&gr.gr_group)->sin_addr.s_addr = multicast_group.sin_addr.s_addr;
	((struct sockaddr_in*)&gr.gr_group)->sin_port = htons (3055);
	fail_unless (0 == setsockopt (recv_sock, SOL_IP, MCAST_JOIN_GROUP, (const char*)&gr, sizeof(gr)), "setsockopt failed");
	fail_unless (0 == pgm_sockaddr_multicast_loop (recv_sock, AF_INET, TRUE), "multicast_loop failed");

	SOCKET send_sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	fail_if (INVALID_SOCKET == send_sock, "socket failed");
	memcpy (&send_addr, &multicast_group, sizeof(multicast_group));
	send_addr.sin_port = 0;
	send_addr.sin_addr.s_addr = INADDR_ANY;
	fail_unless (0 == bind (send_sock, (struct sockaddr*)&send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr)), "bind failed");
	fail_unless (0 == pgm_sockaddr_multicast_if (send_sock, (struct sockaddr*)&if_addr, 0), "multicast_if failed");
	fail_unless (0 == pgm_sockaddr_multicast_loop (send_sock, AF_INET, TRUE), "multicast_loop failed");

	const char data[] = "apple pie";
	send_addr.sin_port = htons (3056);
	send_addr.sin_addr.s_addr = multicast_group.sin_addr.s_addr;
	ssize_t bytes_sent = sendto (send_sock, data, sizeof(data), 0, (struct sockaddr*)&send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));
	if (SOCKET_ERROR == bytes_sent) {
		char errbuf[1024];
		g_message ("sendto: %s",
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), pgm_get_last_sock_error()));
	}
	fail_unless (sizeof(data) == bytes_sent, "sendto underrun");

	char recv_data[1024];
	ssize_t bytes_read = recv (recv_sock, recv_data, sizeof(recv_data), MSG_DONTWAIT);
	if (SOCKET_ERROR == bytes_read) {
		char errbuf[1024];
		g_message ("recv: %s",
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), pgm_get_last_sock_error()));
	}
	if (sizeof(data) != bytes_read)
		g_message ("recv returned %d bytes expected %d.", bytes_read, sizeof(data));
	fail_unless (sizeof(data) == bytes_read, "recv underrun");

	fail_unless (0 == closesocket (recv_sock), "closesocket failed");
	fail_unless (0 == closesocket (send_sock), "closesocket failed");
}
END_TEST

/* target:
 *   test setting hop limit, aka time-to-live.
 *
 *   NB: whilst convenient, we cannot use SOCK_RAW & IPPROTO_UDP on Solaris 10
 *   as it crashes the IP stack.
 */

START_TEST (test_hop_limit_pass_001)
{
	struct sockaddr_in	multicast_group;
	struct sockaddr_in	recv_addr;
	struct sockaddr_in	send_addr;
	struct sockaddr_in	if_addr;

	memset (&multicast_group, 0, sizeof(multicast_group));
	multicast_group.sin_family = AF_INET;
	multicast_group.sin_port = 0;
	multicast_group.sin_addr.s_addr = inet_addr ("239.192.0.1");

	fail_unless (TRUE == pgm_if_indextoaddr (0, AF_INET, 0, (struct sockaddr*)&if_addr, NULL), "if_indextoaddr failed");

	SOCKET recv_sock = socket (AF_INET, SOCK_RAW, 113);
	fail_if (INVALID_SOCKET == recv_sock, "socket failed");
	memcpy (&recv_addr, &multicast_group, sizeof(multicast_group));
	recv_addr.sin_port = 7500;
#ifdef _WIN32
/* Can only bind to interfaces */
	recv_addr.sin_addr.s_addr = INADDR_ANY;
#endif
	fail_unless (0 == bind (recv_sock, (struct sockaddr*)&recv_addr, pgm_sockaddr_len ((struct sockaddr*)&recv_addr)), "bind failed");
	struct group_req gr;
	memset (&gr, 0, sizeof(gr));
	((struct sockaddr*)&gr.gr_group)->sa_family = multicast_group.sin_family;
	((struct sockaddr_in*)&gr.gr_group)->sin_addr.s_addr = multicast_group.sin_addr.s_addr;
	fail_unless (0 == setsockopt (recv_sock, SOL_IP, MCAST_JOIN_GROUP, (const char*)&gr, sizeof(gr)), "setsockopt failed");
	fail_unless (0 == pgm_sockaddr_multicast_loop (recv_sock, AF_INET, TRUE), "multicast_loop failed");
	fail_unless (0 == pgm_sockaddr_hdrincl (recv_sock, AF_INET, TRUE), "hdrincl failed");

	SOCKET send_sock = socket (AF_INET, SOCK_RAW, 113);
	fail_if (INVALID_SOCKET == send_sock, "socket failed");
	memcpy (&send_addr, &multicast_group, sizeof(multicast_group));
	send_addr.sin_port = 0;
	send_addr.sin_addr.s_addr = INADDR_ANY;
	fail_unless (0 == bind (send_sock, (struct sockaddr*)&send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr)), "bind failed");
	fail_unless (0 == pgm_sockaddr_multicast_if (send_sock, (struct sockaddr*)&if_addr, 0), "multicast_if failed");
	fail_unless (0 == pgm_sockaddr_multicast_loop (send_sock, AF_INET, TRUE), "multicast_loop failed");
	fail_unless (0 == pgm_sockaddr_multicast_hops (send_sock, AF_INET, 16), "multicast_hops failed");

	const char data[] = "apple pie";
	send_addr.sin_port = htons (7500);
	send_addr.sin_addr.s_addr = multicast_group.sin_addr.s_addr;
	ssize_t bytes_sent = sendto (send_sock, data, sizeof(data), 0, (struct sockaddr*)&send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));
	if (SOCKET_ERROR == bytes_sent) {
		char errbuf[1024];
		g_message ("sendto: %s",
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), pgm_get_last_sock_error()));
	}
	fail_unless (sizeof(data) == bytes_sent, "sendto underrun");

	char recv_data[1024];
	ssize_t bytes_read = recv (recv_sock, recv_data, sizeof(recv_data), MSG_DONTWAIT);
	if (SOCKET_ERROR == bytes_read) {
		char errbuf[1024];
		g_message ("recv: %s",
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), pgm_get_last_sock_error()));
	}
	const size_t pkt_len = sizeof(struct pgm_ip) + sizeof(data);
	if (pkt_len != bytes_read)
		g_message ("recv returned %" PRIzd " bytes expected %" PRIzu ".", bytes_read, pkt_len);
	fail_unless (pkt_len == bytes_read, "recv underrun");
	const struct pgm_ip* iphdr = (void*)recv_data;
	fail_unless (4 == iphdr->ip_v, "Incorrect IP version, found %u expecting 4.", iphdr->ip_v);
	fail_unless (16 == iphdr->ip_ttl, "hop count mismatch, found %u expecting 16.", iphdr->ip_ttl);

	fail_unless (0 == closesocket (recv_sock), "closesocket failed");
	fail_unless (0 == closesocket (send_sock), "closesocket failed");
}
END_TEST

/* target:
 *   router alert.
 */

START_TEST (test_router_alert_pass_001)
{
	struct sockaddr_in	multicast_group;
	struct sockaddr_in	recv_addr;
	struct sockaddr_in	send_addr;
	struct sockaddr_in	if_addr;

	memset (&multicast_group, 0, sizeof(multicast_group));
	multicast_group.sin_family = AF_INET;
	multicast_group.sin_port = 0;
	multicast_group.sin_addr.s_addr = inet_addr ("239.192.0.1");

	fail_unless (TRUE == pgm_if_indextoaddr (0, AF_INET, 0, (struct sockaddr*)&if_addr, NULL), "if_indextoaddr failed");

	SOCKET recv_sock = socket (AF_INET, SOCK_RAW, 113);
	fail_if (INVALID_SOCKET == recv_sock, "socket failed");
	memcpy (&recv_addr, &multicast_group, sizeof(multicast_group));
	recv_addr.sin_port = htons (7500);
#ifdef _WIN32
/* Can only bind to interfaces */
	recv_addr.sin_addr.s_addr = INADDR_ANY;
#endif
	fail_unless (0 == bind (recv_sock, (struct sockaddr*)&recv_addr, pgm_sockaddr_len ((struct sockaddr*)&recv_addr)), "bind failed");
	struct group_req gr;
	memset (&gr, 0, sizeof(gr));
	((struct sockaddr*)&gr.gr_group)->sa_family = multicast_group.sin_family;
	((struct sockaddr_in*)&gr.gr_group)->sin_addr.s_addr = multicast_group.sin_addr.s_addr;
	fail_unless (0 == setsockopt (recv_sock, SOL_IP, MCAST_JOIN_GROUP, (const char*)&gr, sizeof(gr)), "setsockopt failed");
	fail_unless (0 == pgm_sockaddr_multicast_loop (recv_sock, AF_INET, TRUE), "multicast_loop failed");
	fail_unless (0 == pgm_sockaddr_hdrincl (recv_sock, AF_INET, TRUE), "hdrincl failed");

	SOCKET send_sock = socket (AF_INET, SOCK_RAW, 113);
	fail_if (INVALID_SOCKET == send_sock, "socket failed");
	memcpy (&send_addr, &multicast_group, sizeof(multicast_group));
	send_addr.sin_port = 0;
	send_addr.sin_addr.s_addr = INADDR_ANY;
	fail_unless (0 == bind (send_sock, (struct sockaddr*)&send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr)), "bind failed");
	fail_unless (0 == pgm_sockaddr_multicast_if (send_sock, (struct sockaddr*)&if_addr, 0), "multicast_if failed");
	fail_unless (0 == pgm_sockaddr_multicast_loop (send_sock, AF_INET, TRUE), "multicast_loop failed");
	fail_unless (0 == pgm_sockaddr_router_alert (send_sock, AF_INET, TRUE), "router_alert failed");

	const char data[] = "apple pie";
	send_addr.sin_port = htons (7500);
	send_addr.sin_addr.s_addr = multicast_group.sin_addr.s_addr;
	ssize_t bytes_sent = sendto (send_sock, data, sizeof(data), 0, (struct sockaddr*)&send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));
	if (SOCKET_ERROR == bytes_sent) {
		char errbuf[1024];
		g_message ("sendto: %s",
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), pgm_get_last_sock_error()));
	}
	fail_unless (sizeof(data) == bytes_sent, "sendto underrun");

	char recv_data[1024];
	ssize_t bytes_read = recv (recv_sock, recv_data, sizeof(recv_data), MSG_DONTWAIT);
	if (SOCKET_ERROR == bytes_read) {
		char errbuf[1024];
		g_message ("recv: %s",
			pgm_sock_strerror_s (errbuf, sizeof (errbuf), pgm_get_last_sock_error()));
	}
	const size_t ra_iphdr_len = sizeof(uint32_t) + sizeof(struct pgm_ip);
	const size_t ra_pkt_len = ra_iphdr_len + sizeof(data);
	if (ra_pkt_len != bytes_read)
		g_message ("recv returned %" PRIzd " bytes expected %" PRIzu ".", bytes_read, ra_pkt_len);
	fail_unless (ra_pkt_len == bytes_read, "recv underrun");
	const struct pgm_ip* iphdr = (void*)recv_data;
	fail_unless (4 == iphdr->ip_v, "Incorrect IP version, found %u expecting 4.", iphdr->ip_v);
	if (ra_iphdr_len != (iphdr->ip_hl << 2)) {
		g_message ("IP header length mismatch, found %" PRIzu " expecting %" PRIzu ".",
			(size_t)(iphdr->ip_hl << 2), ra_iphdr_len);
	}
	g_message ("IP header length = %" PRIzu, (size_t)(iphdr->ip_hl << 2));
	const uint32_t* ipopt = (const void*)&recv_data[ iphdr->ip_hl << 2 ];
	const uint32_t ipopt_ra = ((uint32_t)PGM_IPOPT_RA << 24) | (0x04 << 16);
	const uint32_t router_alert = htonl(ipopt_ra);
	if (router_alert == *ipopt) {
		g_message ("IP option router alert found after IP header length.");
		ipopt += sizeof(uint32_t);
	} else {
		ipopt = (const void*)&recv_data[ sizeof(struct pgm_ip) ];
		fail_unless (router_alert == *ipopt, "IP router alert option not found.");
		g_message ("IP option router alert found before end of IP header length.");
	}
	g_message ("Final IP header length = %" PRIzu, (size_t)((const char*)ipopt - (const char*)recv_data));

	fail_unless (0 == closesocket (recv_sock), "closesocket failed");
	fail_unless (0 == closesocket (send_sock), "closesocket failed");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_multicast_loop = tcase_create ("multicast loop");
	suite_add_tcase (s, tc_multicast_loop);
	tcase_add_test (tc_multicast_loop, test_multicast_loop_pass_001);

	TCase* tc_port_bind = tcase_create ("port bind");
	suite_add_tcase (s, tc_port_bind);
	tcase_add_test (tc_port_bind, test_port_bind_pass_001);

	TCase* tc_hop_limit = tcase_create ("hop limit");
	suite_add_tcase (s, tc_hop_limit);
	tcase_add_test (tc_hop_limit, test_hop_limit_pass_001);

	TCase* tc_router_alert = tcase_create ("router alert");
	suite_add_tcase (s, tc_router_alert);
	tcase_add_test (tc_router_alert, test_router_alert_pass_001);
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
#ifndef _WIN32
	if (0 != getuid()) {
		fprintf (stderr, "This test requires super-user privileges to run.\n");
		return EXIT_FAILURE;
	}
#else
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
