/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * プリン C++ PGM receiver.  A port of purinrecv.c to the overly
 * cumbersome Boost ASIO styled C++ wrapper.  Used to test C++ builds
 * don't break.
 *
 * Copyright (c) 2006-2010 Miru Limited.
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

#include <cassert>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#ifndef _WIN32
#	include <unistd.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#else
#	include "getopt.h"
#endif
#ifdef __APPLE__
#	include <pgm/in.h>
#endif
#include <pgm/pgm.hh>


/* globals */

static int		port = 0;
static const char*	network = "";
static bool		use_multicast_loop = FALSE;
static int		udp_encap_port = 0;

static int		max_tpdu = 1500;
static int		sqns = 100;

static bool		use_fec = FALSE;
static int		rs_k = 8;
static int		rs_n = 255;

static ip::pgm::endpoint* endpoint = NULL;
static ip::pgm::socket*	sock = NULL;
static bool		is_terminated = FALSE;

#ifndef _WIN32
static int		terminate_pipe[2];
static void on_signal (int);
#else
static WSAEVENT		terminateEvent;
static BOOL on_console_ctrl (DWORD);
#endif
#ifndef _MSC_VER
static void usage (const char*) __attribute__((__noreturn__));
#else
static void usage (const char*);
#endif

static bool on_startup (void);
static int on_data (const void*, size_t, const ip::pgm::endpoint&);


static void
usage (
	const char*	bin
	)
{
	std::cerr << "Usage: " << bin << " [options]" << std::endl;
	std::cerr << "  -n <network>    : Multicast group or unicast IP address" << std::endl;
	std::cerr << "  -s <port>       : IP port" << std::endl;
	std::cerr << "  -p <port>       : Encapsulate PGM in UDP on IP port" << std::endl;
	std::cerr << "  -f <type>       : Enable FEC with either proactive or ondemand parity" << std::endl;
	std::cerr << "  -K <k>          : Configure Reed-Solomon code (n, k)" << std::endl;
	std::cerr << "  -N <n>" << std::endl;
	std::cerr << "  -l              : Enable multicast loopback and address sharing" << std::endl;
	std::cerr << "  -i              : List available interfaces" << std::endl;
	exit (EXIT_SUCCESS);
}

int
main (
	int		argc,
	char*		argv[]
	)
{
	cpgm::pgm_error_t* pgm_err = NULL;

	std::setlocale (LC_ALL, "");

#if !defined(_WIN32) || defined(CONFIG_TARGET_WINE)
	std::cout << "プリン プリン" << std::endl;
#else
	std::wcout << L"プリン プリン" << std::endl;
#endif

	if (!cpgm::pgm_init (&pgm_err)) {
		std::cerr << "Unable to start PGM engine: " << pgm_err->message << std::endl;
		cpgm::pgm_error_free (pgm_err);
		return EXIT_FAILURE;
	}

/* parse program arguments */
	const char* binary_name = std::strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:p:f:K:N:lih")) != -1)
	{
		switch (c) {
		case 'n':	network = optarg; break;
		case 's':	port = atoi (optarg); break;
		case 'p':	udp_encap_port = atoi (optarg); break;
		case 'f':	use_fec = TRUE; break;
		case 'K':	rs_k = atoi (optarg); break;
		case 'N':	rs_n = atoi (optarg); break;
		case 'l':	use_multicast_loop = TRUE; break;

		case 'i':
			cpgm::pgm_if_print_all();
			return EXIT_SUCCESS;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	if (use_fec && ( !rs_n || !rs_k )) {
		std::cerr << "Invalid Reed-Solomon parameters RS(" << rs_n << "," << rs_k << ")." << std::endl;
		usage (binary_name);
	}

/* setup signal handlers */
#ifdef SIGHUP
	std::signal (SIGHUP,  SIG_IGN);
#endif
#ifndef _WIN32
	int e = pipe (terminate_pipe);
	assert (0 == e);
	std::signal (SIGINT,  on_signal);
	std::signal (SIGTERM, on_signal);
#else
	terminateEvent = WSACreateEvent();
	SetConsoleCtrlHandler ((PHANDLER_ROUTINE)on_console_ctrl, TRUE);
	setvbuf (stdout, (char *) NULL, _IONBF, 0);
#endif /* !_WIN32 */

	if (!on_startup()) {
		std::cerr << "Startup failed" << std::endl;
		return EXIT_FAILURE;
	}

/* dispatch loop */
#ifndef _WIN32
	int fds;
	fd_set readfds;
#else
	SOCKET recv_sock, pending_sock;
	DWORD cEvents = PGM_RECV_SOCKET_READ_COUNT + 1;
	WSAEVENT waitEvents[ PGM_RECV_SOCKET_READ_COUNT + 1 ];
	socklen_t socklen = sizeof (SOCKET);

	waitEvents[0] = terminateEvent;
	sock->get_option (IPPROTO_PGM, cpgm::PGM_RECV_SOCK, &recv_sock, &socklen);
	WSAEventSelect (recv_sock, waitEvents[1], FD_READ);
	sock->get_option (IPPROTO_PGM, cpgm::PGM_PENDING_SOCK, &pending_sock, &socklen);
	WSAEventSelect (pending_sock, waitEvents[2], FD_READ);
#endif /* !_WIN32 */
	std::cout << "Entering PGM message loop ... " << std::endl;
	do {
		socklen_t optlen;
		struct timeval tv;
#ifdef _WIN32
		DWORD dwTimeout, dwEvents;
#endif
		char buffer[4096];
		size_t len;
		ip::pgm::endpoint from;
		const int status = sock->receive_from (buffer,
						       sizeof(buffer),
						       0,
						       &len,
						       &from,
						       &pgm_err);
		switch (status) {
		case cpgm::PGM_IO_STATUS_NORMAL:
			on_data (buffer, len, from);
			break;
		case cpgm::PGM_IO_STATUS_TIMER_PENDING:
			optlen = sizeof (tv);
			sock->get_option (IPPROTO_PGM, cpgm::PGM_TIME_REMAIN, &tv, &optlen);
			goto block;
		case cpgm::PGM_IO_STATUS_RATE_LIMITED:
			optlen = sizeof (tv);
			sock->get_option (IPPROTO_PGM, cpgm::PGM_RATE_REMAIN, &tv, &optlen);
		case cpgm::PGM_IO_STATUS_WOULD_BLOCK:
/* select for next event */
block:
#ifndef _WIN32
			fds = terminate_pipe[0] + 1;
			FD_ZERO(&readfds);
			FD_SET(terminate_pipe[0], &readfds);
			pgm_select_info (sock->native(), &readfds, NULL, &fds);
			fds = select (fds, &readfds, NULL, NULL, cpgm::PGM_IO_STATUS_WOULD_BLOCK == status ? NULL : &tv);
#else
			dwTimeout = cpgm::PGM_IO_STATUS_WOULD_BLOCK == status ? WSA_INFINITE : (DWORD)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
			dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, dwTimeout, FALSE);
			switch (dwEvents) {
			case WSA_WAIT_EVENT_0+1: WSAResetEvent (waitEvents[1]); break;
			case WSA_WAIT_EVENT_0+2: WSAResetEvent (waitEvents[2]); break;
			default: break;
			}
#endif /* !_WIN32 */
			break;

		default:
			if (pgm_err) {
				std::cerr << pgm_err->message << std::endl;
				cpgm::pgm_error_free (pgm_err);
				pgm_err = NULL;
			}
			if (cpgm::PGM_IO_STATUS_ERROR == status)
				break;
		}
	} while (!is_terminated);

	std::cout << "Message loop terminated, cleaning up." << std::endl;

/* cleanup */
#ifndef _WIN32
	close (terminate_pipe[0]);
	close (terminate_pipe[1]);
#else
	WSACloseEvent (waitEvents[0]);
	WSACloseEvent (waitEvents[1]);
	WSACloseEvent (waitEvents[2]);
#endif /* !_WIN32 */

	if (sock) {
		std::cout << "Closing PGM socket." << std::endl;
		sock->close (TRUE);
		sock = NULL;
	}

	std::cout << "PGM engine shutdown." << std::endl;
	cpgm::pgm_shutdown ();
	std::cout << "finished." << std::endl;
	return EXIT_SUCCESS;
}

#ifndef _WIN32
static
void
on_signal (
	int		signum
	)
{
	std::cout << "on_signal (signum:" << signum << ")" << std::endl;
	is_terminated = TRUE;
	const char one = '1';
	const size_t writelen = write (terminate_pipe[1], &one, sizeof(one));
	assert (sizeof(one) == writelen);
}
#else
static
BOOL
on_console_ctrl (
	DWORD		dwCtrlType
	)
{
	std::cout << "on_console_ctrl (dwCtrlType:" << dwCtrlType << ")" << std::endl;
	is_terminated = TRUE;
	WSASetEvent (terminateEvent);
	return TRUE;
}
#endif /* !_WIN32 */

static
bool
on_startup (void)
{
	struct cpgm::pgm_addrinfo_t* res = NULL;
	cpgm::pgm_error_t* pgm_err = NULL;
	sa_family_t sa_family = AF_UNSPEC;

/* parse network parameter into PGM socket address structure */
	if (!cpgm::pgm_getaddrinfo (network, NULL, &res, &pgm_err)) {
		std::cerr << "Parsing network parameter: " << pgm_err->message << std::endl;
		goto err_abort;
	}

	sa_family = res->ai_send_addrs[0].gsr_group.ss_family;

	sock = new ip::pgm::socket();

	if (udp_encap_port) {
		std::cout << "Create PGM/UDP socket." << std::endl;
		if (!sock->open (sa_family, SOCK_SEQPACKET, IPPROTO_UDP, &pgm_err)) {
			std::cerr << "Creating PGM/UDP socket: " << pgm_err->message << std::endl;
			goto err_abort;
		}
		sock->set_option (IPPROTO_PGM, cpgm::PGM_UDP_ENCAP_UCAST_PORT, &udp_encap_port, sizeof(udp_encap_port));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_UDP_ENCAP_MCAST_PORT, &udp_encap_port, sizeof(udp_encap_port));
	} else {
		std::cout << "Create PGM/IP socket." << std::endl;
		if (!sock->open (sa_family, SOCK_SEQPACKET, IPPROTO_PGM, &pgm_err)) {
			std::cerr << "Creating PGM/IP socket: " << pgm_err->message << std::endl;
			goto err_abort;
		}
	}

	{
/* Use RFC 2113 tagging for PGM Router Assist */
		const int no_router_assist = 0;
		sock->set_option (IPPROTO_PGM, cpgm::PGM_IP_ROUTER_ALERT, &no_router_assist, sizeof(no_router_assist));
	}

	cpgm::pgm_drop_superuser();

	{
/* set PGM parameters */
		const int recv_only = 1,
			  passive = 0,
			  peer_expiry = pgm_secs (300),
			  spmr_expiry = pgm_msecs (250),
			  nak_bo_ivl = pgm_msecs (50),
			  nak_rpt_ivl = pgm_secs (2),
			  nak_rdata_ivl = pgm_secs (2),
			  nak_data_retries = 50,
			  nak_ncf_retries = 50;

		sock->set_option (IPPROTO_PGM, cpgm::PGM_RECV_ONLY, &recv_only, sizeof(recv_only));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_PASSIVE, &passive, sizeof(passive));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_MTU, &max_tpdu, sizeof(max_tpdu));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_RXW_SQNS, &sqns, sizeof(sqns));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_PEER_EXPIRY, &peer_expiry, sizeof(peer_expiry));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_SPMR_EXPIRY, &spmr_expiry, sizeof(spmr_expiry));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_NAK_BO_IVL, &nak_bo_ivl, sizeof(nak_bo_ivl));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_NAK_RPT_IVL, &nak_rpt_ivl, sizeof(nak_rpt_ivl));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_NAK_RDATA_IVL, &nak_rdata_ivl, sizeof(nak_rdata_ivl));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_NAK_DATA_RETRIES, &nak_data_retries, sizeof(nak_data_retries));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_NAK_NCF_RETRIES, &nak_ncf_retries, sizeof(nak_ncf_retries));
	}
	if (use_fec) {
		struct cpgm::pgm_fecinfo_t fecinfo;
		fecinfo.block_size		= rs_n;
		fecinfo.proactive_packets	= 0;
		fecinfo.group_size		= rs_k;
		fecinfo.ondemand_parity_enabled	= TRUE;
		fecinfo.var_pktlen_enabled	= FALSE;
		sock->set_option (IPPROTO_PGM, cpgm::PGM_USE_FEC, &fecinfo, sizeof(fecinfo));
	}

/* create global session identifier */
	endpoint = new ip::pgm::endpoint (DEFAULT_DATA_DESTINATION_PORT);

/* assign socket to specified address */
	if (!sock->bind (*endpoint, &pgm_err)) {
		std::cerr << "Binding PGM socket: " << pgm_err->message << std::endl;
		goto err_abort;
	}

/* join IP multicast groups */
	for (unsigned i = 0; i < res->ai_recv_addrs_len; i++)
		sock->set_option (IPPROTO_PGM, cpgm::PGM_JOIN_GROUP, &res->ai_recv_addrs[i], sizeof(struct group_req));
	sock->set_option (IPPROTO_PGM, cpgm::PGM_SEND_GROUP, &res->ai_send_addrs[0], sizeof(struct group_req));
	cpgm::pgm_freeaddrinfo (res);

	{
/* set IP parameters */
		const int nonblocking = 1,
			  multicast_loop = use_multicast_loop ? 1 : 0,
			  multicast_hops = 16,
			  dscp = 0x2e << 2;		/* Expedited Forwarding PHB for network elements, no ECN. */

		sock->set_option (IPPROTO_PGM, cpgm::PGM_MULTICAST_LOOP, &multicast_loop, sizeof(multicast_loop));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_TOS, &dscp, sizeof(dscp));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_NOBLOCK, &nonblocking, sizeof(nonblocking));
	}

	if (!sock->connect (&pgm_err)) {
		std::cerr << "Connecting PGM socket: " << pgm_err->message << std::endl;
		goto err_abort;
	}

	std::cout << "Startup complete." << std::endl;
	return TRUE;

err_abort:
	if (NULL != sock) {
		sock->close (FALSE);
		sock = NULL;
	}
	if (NULL != res) {
		cpgm::pgm_freeaddrinfo (res);
		res = NULL;
	}
	if (NULL != pgm_err) {
		cpgm::pgm_error_free (pgm_err);
		pgm_err = NULL;
	}
	return FALSE;
}

static
int
on_data (
	const void*       		data,
	size_t				len,
	const ip::pgm::endpoint&	from
	)
{
/* protect against non-null terminated strings */
	char buf[1024], tsi[PGM_TSISTRLEN];
	const size_t buflen = MIN(sizeof(buf) - 1, len);
	strncpy (buf, (char*)data, buflen);
	buf[buflen] = '\0';
	cpgm::pgm_tsi_print_r (from.address(), tsi, sizeof(tsi));
	std::cout << "\"" << buf << "\" (" << len << " bytes from " << tsi << ")" << std::endl;
	return 0;
}

/* eof */
