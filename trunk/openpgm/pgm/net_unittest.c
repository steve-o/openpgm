/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for network send wrapper.
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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#	include <sys/types.h>
#	include <sys/socket.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
#include <glib.h>
#include <check.h>

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


/* mock state */

#ifndef _WIN32
ssize_t mock_sendto (int, const void*, size_t, int, const struct sockaddr*, socklen_t);
#else
int mock_sendto (SOCKET, const char*, int, int, const struct sockaddr*, int);
int mock_select (int, fd_set*, fd_set*, fd_set*, struct timeval*);
#endif


#define pgm_rate_check		mock_pgm_rate_check
#define sendto			mock_sendto
#define poll			mock_poll
#define select			mock_select
#define fcntl			mock_fcntl

#define NET_DEBUG
#include "net.c"


static
pgm_sock_t*
generate_sock (void)
{
	pgm_sock_t* sock = g_malloc0 (sizeof(pgm_sock_t));
	return sock;
}

static
char*
flags_string (
	int		flags
	)
{
	static char s[1024];

	s[0] = '\0';
	if (flags & MSG_OOB)
		strcat (s, "MSG_OOB");
#define MSG(flag) \
	do { \
		if (flags & flag) { \
			strcat (s, s[0] ? ("|" #flag) : (#flag)); \
		} \
	} while (0)
#ifdef MSG_PEEK
	MSG(MSG_PEEK);
#endif
#ifdef MSG_DONTROUTE
	MSG(MSG_DONTROUTE);
#endif
#ifdef MSG_CTRUNC
	MSG(MSG_CTRUNC);
#endif
#ifdef MSG_PROXY
	MSG(MSG_PROXY);
#endif
#ifdef MSG_TRUNC
	MSG(MSG_TRUNC);
#endif
#ifdef MSG_DONTWAIT
	MSG(MSG_DONTWAIT);
#endif
#ifdef MSG_EOR
	MSG(MSG_EOR);
#endif
#ifdef MSG_WAITALL
	MSG(MSG_WAITALL);
#endif
#ifdef MSG_FIN
	MSG(MSG_FIN);
#endif
#ifdef MSG_SYN
	MSG(MSG_SYN);
#endif
#ifdef MSG_CONFIRM
	MSG(MSG_CONFIRM);
#endif
#ifdef MSG_RST
	MSG(MSG_RST);
#endif
#ifdef MSG_ERRQUEUE
	MSG(MSG_ERRQUEUE);
#endif
#ifdef MSG_NOSIGNAL
	MSG(MSG_NOSIGNAL);
#endif
#ifdef MSG_MORE
	MSG(MSG_MORE);
#endif
#ifdef MSG_CMSG_CLOEXEC
	MSG(MSG_CMSG_CLOEXEC);
#endif
	if (!s[0]) {
		if (flags)
			sprintf (s, "0x%x", flags);
		else
			strcpy (s, "0");
	}
	return s;
}


/* mock functions for external references */

size_t
pgm_pkt_offset (
        const bool                      can_fragment,
        const sa_family_t		pgmcc_family	/* 0 = disable */
        )
{
        return 0;
}

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_rate_check (
	pgm_rate_t*		bucket,
	const size_t		data_size,
	const bool		is_nonblocking
	)
{
	g_debug ("mock_pgm_rate_check (bucket:%p data-size:%" PRIzu " is-nonblocking:%s)",
		(gpointer)bucket, data_size, is_nonblocking ? "TRUE" : "FALSE");
	return TRUE;
}

#ifndef _WIN32
ssize_t
mock_sendto (
	int			s,
	const void*		buf,
	size_t			len,
	int			flags,
	const struct sockaddr*	to,
	socklen_t		tolen
	)
#else
int
mock_sendto (
	SOCKET			s,
	const char*		buf,
	int			len,
	int			flags,
	const struct sockaddr*	to,
	int			tolen
	)
#endif
{
	char saddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (to, saddr, sizeof(saddr));
	g_debug ("mock_sendto (s:%i buf:%p len:%u flags:%s to:%s tolen:%d)",
		s, buf, (unsigned)len, flags_string (flags), saddr, tolen);
	return len;
}

#ifdef HAVE_POLL
int
mock_poll (
	struct pollfd*		fds,
	nfds_t			nfds,
	int			timeout
	)
{
	g_debug ("mock_poll (fds:%p nfds:%d timeout:%d)",
		(gpointer)fds, (int)nfds, timeout);
	return 0;
}
#else
int
mock_select (
	int			nfds,
	fd_set*			readfds,
	fd_set*			writefds,
	fd_set*			exceptfds,
	struct timeval*		timeout
	)
{
	g_debug ("mock_select (nfds:%d readfds:%p writefds:%p exceptfds:%p timeout:%p)",
		nfds, (gpointer)readfds, (gpointer)writefds, (gpointer)exceptfds, (gpointer)timeout);
	return 0;
}
#endif

/* non-blocking API */
#ifndef _WIN32
int
mock_fcntl (
	int			fd,
	int			cmd,
	...
	)
{
	long arg;
	va_list args;
	if (F_GETFL == cmd) {
		g_debug ("mock_fcntl (fd:%d cmd:F_GETFL)", fd);
		return 0;
	}
	if (F_SETFL == cmd) {
		va_start (args, cmd);
		arg = va_arg (args, long);
		va_end (args);
		g_debug ("mock_fcntl (fd:%d cmd:F_SETFL arg:%ld)", fd, arg);
		return arg;
	}
	g_assert_not_reached();
}
#else
/* ioctlsocket */
#endif


/* target:
 *	ssize_t
 *	pgm_sendto (
 *		pgm_sock_t*		sock,
 *		bool			use_rate_limit,
 *		pgm_rate_t*		minor_rate_control,
 *		bool			use_router_alert,
 *		const void*		buf,
 *		size_t			len,
 *		const struct sockaddr*	to,
 *		socklen_t		tolen
 *	)
 */

START_TEST (test_sendto_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	const char* buf = "i am not a string";
	struct sockaddr_in addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr ("172.12.90.1")
	};
	gssize len = pgm_sendto (sock, FALSE, NULL, FALSE, buf, sizeof(buf), (struct sockaddr*)&addr, sizeof(addr));
	fail_unless (sizeof(buf) == len, "sendto underrun");
}
END_TEST

START_TEST (test_sendto_fail_001)
{
	const char* buf = "i am not a string";
	struct sockaddr_in addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr ("172.12.90.1")
	};
	gssize len = pgm_sendto (NULL, FALSE, NULL, FALSE, buf, sizeof(buf), (struct sockaddr*)&addr, sizeof(addr));
	fail ("reached");
}
END_TEST

START_TEST (test_sendto_fail_002)
{
	pgm_sock_t* sock = generate_sock ();
	const char* buf = "i am not a string";
	struct sockaddr_in addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr ("172.12.90.1")
	};
	gssize len = pgm_sendto (sock, FALSE, NULL, FALSE, NULL, sizeof(buf), (struct sockaddr*)&addr, sizeof(addr));
	fail ("reached");
}
END_TEST

START_TEST (test_sendto_fail_003)
{
	pgm_sock_t* sock = generate_sock ();
	const char* buf = "i am not a string";
	struct sockaddr_in addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr ("172.12.90.1")
	};
	gssize len = pgm_sendto (sock, FALSE, NULL, FALSE, buf, 0, (struct sockaddr*)&addr, sizeof(addr));
	fail ("reached");
}
END_TEST

START_TEST (test_sendto_fail_004)
{
	pgm_sock_t* sock = generate_sock ();
	const char* buf = "i am not a string";
	struct sockaddr_in addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr ("172.12.90.1")
	};
	gssize len = pgm_sendto (sock, FALSE, NULL, FALSE, buf, sizeof(buf), NULL, sizeof(addr));
	fail ("reached");
}
END_TEST

START_TEST (test_sendto_fail_005)
{
	pgm_sock_t* sock = generate_sock ();
	const char* buf = "i am not a string";
	struct sockaddr_in addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr ("172.12.90.1")
	};
	gssize len = pgm_sendto (sock, FALSE, NULL, FALSE, buf, sizeof(buf), (struct sockaddr*)&addr, 0);
	fail ("reached");
}
END_TEST

/* target:
 * 	int
 * 	pgm_set_nonblocking (
 * 		int			filedes[2]
 * 	)
 */

START_TEST (test_set_nonblocking_pass_001)
{
	int filedes[2] = { fileno (stdout), fileno (stderr) };
	int retval = pgm_set_nonblocking (filedes);
}
END_TEST

START_TEST (test_set_nonblocking_fail_001)
{
	int filedes[2] = { 0, 0 };
	int retval = pgm_set_nonblocking (filedes);
	fail ("reached");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_sendto = tcase_create ("sendto");
	suite_add_tcase (s, tc_sendto);
	tcase_add_test (tc_sendto, test_sendto_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_sendto, test_sendto_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_sendto, test_sendto_fail_002, SIGABRT);
	tcase_add_test_raise_signal (tc_sendto, test_sendto_fail_003, SIGABRT);
	tcase_add_test_raise_signal (tc_sendto, test_sendto_fail_004, SIGABRT);
	tcase_add_test_raise_signal (tc_sendto, test_sendto_fail_005, SIGABRT);
#endif

	TCase* tc_set_nonblocking = tcase_create ("set-nonblocking");
	suite_add_tcase (s, tc_set_nonblocking);
	tcase_add_test (tc_set_nonblocking, test_set_nonblocking_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_set_nonblocking, test_set_nonblocking_fail_001, SIGABRT);
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
