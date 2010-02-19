/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for ip stack.
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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <glib.h>
#include <check.h>

#include <pgm/indextoaddr.h>
#include <pgm/sockaddr.h>


/* target:
 *  testing platform capability to loop send multicast packets to a listening
 * receive socket.
 */

START_TEST (test_multicast_loop_pass_001)
{
	struct sockaddr_in addr;
	memset (&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr ("239.192.0.1");

	int recv_sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	fail_if (-1 == recv_sock);
	struct sockaddr_in recv_addr;
	memcpy (&recv_addr, &addr, sizeof(addr));
	recv_addr.sin_port = 7500;
	fail_unless (0 == bind (recv_sock, (struct sockaddr*)&recv_addr, pgm_sockaddr_len (&recv_addr)));
	struct group_req gr;
	memset (&gr, 0, sizeof(gr));
	((struct sockaddr*)&gr.gr_group)->sa_family = addr.sin_family;
	((struct sockaddr_in*)&gr.gr_group)->sin_addr.s_addr = addr.sin_addr.s_addr;
	fail_unless (0 == setsockopt (recv_sock, SOL_IP, MCAST_JOIN_GROUP, (const char*)&gr, sizeof(gr)));
	fail_unless (0 == pgm_sockaddr_multicast_loop (recv_sock, AF_INET, FALSE));

	int send_sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	fail_if (-1 == send_sock);
	struct sockaddr_in send_addr;
	memcpy (&send_addr, &addr, sizeof(addr));
	fail_unless (0 == bind (send_sock, (struct sockaddr*)&send_addr, pgm_sockaddr_len (&send_addr)));
        struct sockaddr_in if_addr;
	fail_unless (TRUE == pgm_if_indextoaddr (0, AF_INET, 0, (struct sockaddr*)&if_addr, NULL));
	fail_unless (0 == pgm_sockaddr_multicast_if (send_sock, (struct sockaddr*)&if_addr, 0));
	fail_unless (0 == pgm_sockaddr_multicast_loop (send_sock, AF_INET, TRUE));

	const char data[] = "apple pie";
	addr.sin_port = 7500;
	ssize_t bytes_sent = sendto (send_sock, data, sizeof(data), 0, (struct sockaddr*)&addr, pgm_sockaddr_len (&addr));
	if (-1 == bytes_sent)
		g_message ("sendto: %s", strerror (errno));
	fail_unless (sizeof(data) == bytes_sent);

	char recv_data[1024];
	ssize_t bytes_read = recv (recv_sock, recv_data, sizeof(recv_data), MSG_DONTWAIT);
	if (-1 == bytes_read)
		g_message ("sendto: %s", strerror (errno));
	fail_unless (sizeof(data) == bytes_read);

	fail_unless (0 == close (recv_sock));
	fail_unless (0 == close (send_sock));
}
END_TEST

/* target:
 *  testing unicast and multicast port settings.
 */

START_TEST (test_port_bind_pass_001)
{
	struct sockaddr_in addr;
	memset (&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr ("239.192.0.1");

	int recv_sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	fail_if (-1 == recv_sock);
	struct sockaddr_in recv_addr;
	memcpy (&recv_addr, &addr, sizeof(addr));
	recv_addr.sin_port = 3055;
	fail_unless (0 == bind (recv_sock, (struct sockaddr*)&recv_addr, pgm_sockaddr_len (&recv_addr)));
	struct group_req gr;
	memset (&gr, 0, sizeof(gr));
	((struct sockaddr*)&gr.gr_group)->sa_family = addr.sin_family;
	((struct sockaddr_in*)&gr.gr_group)->sin_addr.s_addr = addr.sin_addr.s_addr;
	((struct sockaddr_in*)&gr.gr_group)->sin_port = 3056;
	fail_unless (0 == setsockopt (recv_sock, SOL_IP, MCAST_JOIN_GROUP, (const char*)&gr, sizeof(gr)));
	fail_unless (0 == pgm_sockaddr_multicast_loop (recv_sock, AF_INET, FALSE));

	int send_sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	fail_if (-1 == send_sock);
	struct sockaddr_in send_addr;
	memcpy (&send_addr, &addr, sizeof(addr));
	fail_unless (0 == bind (send_sock, (struct sockaddr*)&send_addr, pgm_sockaddr_len (&send_addr)));
        struct sockaddr_in if_addr;
	fail_unless (TRUE == pgm_if_indextoaddr (0, AF_INET, 0, (struct sockaddr*)&if_addr, NULL));
	fail_unless (0 == pgm_sockaddr_multicast_if (send_sock, (struct sockaddr*)&if_addr, 0));
	fail_unless (0 == pgm_sockaddr_multicast_loop (send_sock, AF_INET, TRUE));

	const char data[] = "apple pie";
	addr.sin_port = 3056;
	ssize_t bytes_sent = sendto (send_sock, data, sizeof(data), 0, (struct sockaddr*)&addr, pgm_sockaddr_len (&addr));
	if (-1 == bytes_sent)
		g_message ("sendto: %s", strerror (errno));
	fail_unless (sizeof(data) == bytes_sent);

	char recv_data[1024];
	ssize_t bytes_read = recv (recv_sock, recv_data, sizeof(recv_data), MSG_DONTWAIT);
	if (-1 == bytes_read)
		g_message ("sendto: %s", strerror (errno));
	fail_unless (sizeof(data) == bytes_read);

	fail_unless (0 == close (recv_sock));
	fail_unless (0 == close (send_sock));
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
