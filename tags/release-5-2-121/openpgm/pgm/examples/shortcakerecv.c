/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * ショートケーキ PGM receiver.  An asynchronous receive-
 * only socket example.  Many middleware libraries implement a queue between
 * the raw socket and the application.  The separation can be used to present
 * reconstructed fragmented packets, filtering, and more fundamentally event
 * notification that is a 1:1 correlation with available data.
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

#include <assert.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#	include <unistd.h>
#else
#	include "getopt.h"
#	define snprintf		_snprintf
#endif
#ifdef __APPLE__
#	include <pgm/in.h>
#endif
#include <pgm/pgm.h>

#include "async.h"


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

static pgm_sock_t*	sock = NULL;
static async_t*		async = NULL;
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
static int on_data (const void*restrict, const size_t, const struct pgm_sockaddr_t*restrict);


static void
usage (
	const char*	bin
	)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	fprintf (stderr, "  -f <type>       : Enable FEC with either proactive or ondemand parity\n");
	fprintf (stderr, "  -K <k>          : Configure Reed-Solomon code (n, k)\n");
	fprintf (stderr, "  -N <n>\n");
	fprintf (stderr, "  -l              : Enable multicast loopback and address sharing\n");
	fprintf (stderr, "  -i              : List available interfaces\n");
	exit (EXIT_SUCCESS);
}

int
main (
	int		argc,
	char*		argv[]
	)
{
	pgm_error_t* pgm_err = NULL;

	setlocale (LC_ALL, "");

#ifndef _WIN32
	puts ("いちごのショートケーキ");
#else
	puts ("ichigo no shōtokēki");
#endif

	if (!pgm_init (&pgm_err)) {
		fprintf (stderr, "Unable to start PGM engine: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		return EXIT_FAILURE;
	}

/* parse program arguments */
#ifdef _WIN32
	const char* binary_name = strrchr (argv[0], '\\');
#else
	const char* binary_name = strrchr (argv[0], '/');
#endif
	if (NULL == binary_name)	binary_name = argv[0];
	else				binary_name++;

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
			pgm_if_print_all();
			return EXIT_SUCCESS;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	if (use_fec && ( !rs_n || !rs_k )) {
		fprintf (stderr, "Invalid Reed-Solomon parameters RS(%d,%d).\n", rs_n, rs_k);
		usage (binary_name);
	}

/* setup signal handlers */
#ifdef SIGHUP
	signal (SIGHUP,  SIG_IGN);
#endif
#ifndef _WIN32
	int e = pipe (terminate_pipe);
	assert (0 == e);
	signal (SIGINT,  on_signal);
	signal (SIGTERM, on_signal);
#else
	terminateEvent = WSACreateEvent ();
	SetConsoleCtrlHandler ((PHANDLER_ROUTINE)on_console_ctrl, TRUE);
	setvbuf (stdout, (char *) NULL, _IONBF, 0);
#endif /* !_WIN32 */

	if (!on_startup()) {
		fprintf (stderr, "Startup failed\n");
		return EXIT_FAILURE;
	}

/* dispatch loop */
#ifndef _WIN32
	int fds, read_fd = async_get_socket (async);
	fd_set readfds;
#else
	DWORD cEvents = 2;
	WSAEVENT waitEvents[ 2 ];
	DWORD dwEvents;

	waitEvents[0] = terminateEvent;
	waitEvents[1] = async_get_event (async);
#endif /* !_WIN32 */
	puts ("Entering PGM message loop ... ");
	do {
		char buffer[4096];
		struct pgm_sockaddr_t from;
		socklen_t fromlen = sizeof (from);
		const ssize_t len = async_recvfrom (async,
					            buffer,
					            sizeof(buffer),
					            &from,
						    &fromlen);
		if (len >= 0) {
			on_data (buffer, len, &from);
		} else {
#ifndef _WIN32
			fds = MAX(terminate_pipe[0], read_fd) + 1;
			FD_ZERO(&readfds);
			FD_SET(terminate_pipe[0], &readfds);
			FD_SET(read_fd, &readfds);
			fds = select (fds, &readfds, NULL, NULL, NULL);
#else
			dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, WSA_INFINITE, FALSE);
			switch (dwEvents) {
			case WSA_WAIT_EVENT_0+1: WSAResetEvent (waitEvents[1]); break;
			default: break;
			}
#endif /* _WIN32 */
		}
	} while (!is_terminated);

	puts ("Message loop terminated, cleaning up.");

/* cleanup */
#ifndef _WIN32
	close (terminate_pipe[0]);
	close (terminate_pipe[1]);
#else
	WSACloseEvent (terminateEvent);
#endif /* !_WIN32 */

	if (async) {
		puts ("Destroying asynchronous queue.");
		async_destroy (async);
		async = NULL;
	}

	if (sock) {
		puts ("Closing PGM socket.");
		pgm_close (sock, TRUE);
		sock = NULL;
	}

	puts ("PGM engine shutdown.");
	pgm_shutdown ();
	puts ("finished.");
	return EXIT_SUCCESS;
}

#ifndef _WIN32
static
void
on_signal (
	int		signum
	)
{
	printf ("on_signal (signum:%d)\n", signum);
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
	printf ("on_console_ctrl (dwCtrlType:%lu)\n", (unsigned long)dwCtrlType);
	is_terminated = TRUE;
	WSASetEvent (terminateEvent);
	return TRUE;
}
#endif /* !_WIN32 */

static
bool
on_startup (void)
{
	struct pgm_addrinfo_t* res = NULL;
	pgm_error_t* pgm_err = NULL;
	sa_family_t sa_family = AF_UNSPEC;

/* parse network parameter into PGM socket address structure */
	if (!pgm_getaddrinfo (network, NULL, &res, &pgm_err)) {
		fprintf (stderr, "Parsing network parameter: %s\n", pgm_err->message);
		goto err_abort;
	}

	sa_family = res->ai_send_addrs[0].gsr_group.ss_family;

	puts ("Create PGM socket.");
	if (udp_encap_port) {
		if (!pgm_socket (&sock, sa_family, SOCK_SEQPACKET, IPPROTO_UDP, &pgm_err)) {
			fprintf (stderr, "Creating PGM/UDP socket: %s\n", pgm_err->message);
			goto err_abort;
		}
		pgm_setsockopt (sock, IPPROTO_PGM, PGM_UDP_ENCAP_UCAST_PORT, &udp_encap_port, sizeof(udp_encap_port));
		pgm_setsockopt (sock, IPPROTO_PGM, PGM_UDP_ENCAP_MCAST_PORT, &udp_encap_port, sizeof(udp_encap_port));
	} else {
		if (!pgm_socket (&sock, sa_family, SOCK_SEQPACKET, IPPROTO_PGM, &pgm_err)) {
			fprintf (stderr, "Creating PGM/IP socket: %s\n", pgm_err->message);
			goto err_abort;
		}
	}

/* Use RFC 2113 tagging for PGM Router Assist */
	const int no_router_assist = 0;
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_IP_ROUTER_ALERT, &no_router_assist, sizeof(no_router_assist));

	pgm_drop_superuser();

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

	pgm_setsockopt (sock, IPPROTO_PGM, PGM_RECV_ONLY, &recv_only, sizeof(recv_only));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_PASSIVE, &passive, sizeof(passive));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_MTU, &max_tpdu, sizeof(max_tpdu));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_RXW_SQNS, &sqns, sizeof(sqns));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_PEER_EXPIRY, &peer_expiry, sizeof(peer_expiry));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_SPMR_EXPIRY, &spmr_expiry, sizeof(spmr_expiry));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_NAK_BO_IVL, &nak_bo_ivl, sizeof(nak_bo_ivl));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_NAK_RPT_IVL, &nak_rpt_ivl, sizeof(nak_rpt_ivl));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_NAK_RDATA_IVL, &nak_rdata_ivl, sizeof(nak_rdata_ivl));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_NAK_DATA_RETRIES, &nak_data_retries, sizeof(nak_data_retries));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_NAK_NCF_RETRIES, &nak_ncf_retries, sizeof(nak_ncf_retries));
	if (use_fec) {
		struct pgm_fecinfo_t fecinfo;
		fecinfo.block_size		= rs_n;
		fecinfo.proactive_packets	= 0;
		fecinfo.group_size		= rs_k;
		fecinfo.ondemand_parity_enabled	= TRUE;
		fecinfo.var_pktlen_enabled	= TRUE;
		pgm_setsockopt (sock, IPPROTO_PGM, PGM_USE_FEC, &fecinfo, sizeof(fecinfo));
	}

/* create global session identifier */
	struct pgm_sockaddr_t addr;
	memset (&addr, 0, sizeof(addr));
	addr.sa_port = port ? port : DEFAULT_DATA_DESTINATION_PORT;
	addr.sa_addr.sport = DEFAULT_DATA_SOURCE_PORT;
	if (!pgm_gsi_create_from_hostname (&addr.sa_addr.gsi, &pgm_err)) {
		fprintf (stderr, "Creating GSI: %s\n", pgm_err->message);
		goto err_abort;
	}

/* assign socket to specified address */
	struct pgm_interface_req_t if_req;
	memset (&if_req, 0, sizeof(if_req));
	if_req.ir_interface = res->ai_recv_addrs[0].gsr_interface;
	if_req.ir_scope_id  = 0;
	if (AF_INET6 == sa_family) {
		struct sockaddr_in6 sa6;
		memcpy (&sa6, &res->ai_recv_addrs[0].gsr_group, sizeof(sa6));
		if_req.ir_scope_id = sa6.sin6_scope_id;
	}
	if (!pgm_bind3 (sock,
			&addr, sizeof(addr),
			&if_req, sizeof(if_req),        /* tx interface */
			&if_req, sizeof(if_req),        /* rx interface */
			&pgm_err))
	{
		fprintf (stderr, "Binding PGM socket: %s\n", pgm_err->message);
		goto err_abort;
	}

/* join IP multicast groups */
	for (unsigned i = 0; i < res->ai_recv_addrs_len; i++)
		pgm_setsockopt (sock, IPPROTO_PGM, PGM_JOIN_GROUP, &res->ai_recv_addrs[i], sizeof(struct group_req));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_SEND_GROUP, &res->ai_send_addrs[0], sizeof(struct group_req));
	pgm_freeaddrinfo (res);

/* set IP parameters */
	const int nonblocking = 1,
		  multicast_loop = use_multicast_loop ? 1 : 0,
		  multicast_hops = 16,
		  dscp = 0x2e << 2;		/* Expedited Forwarding PHB for network elements, no ECN. */

	pgm_setsockopt (sock, IPPROTO_PGM, PGM_MULTICAST_LOOP, &multicast_loop, sizeof(multicast_loop));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops));
	if (AF_INET6 != sa_family)
		pgm_setsockopt (sock, IPPROTO_PGM, PGM_TOS, &dscp, sizeof(dscp));
	pgm_setsockopt (sock, IPPROTO_PGM, PGM_NOBLOCK, &nonblocking, sizeof(nonblocking));

	if (!pgm_connect (sock, &pgm_err)) {
		fprintf (stderr, "Connecting PGM socket: %s\n", pgm_err->message);
		goto err_abort;
	}

/* wrap bound socket in asynchronous queue */
	if (0 != async_create (&async, sock)) {
		fprintf (stderr, "Creating asynchronous queue failed: %s\n", strerror(errno));
		goto err_abort;
	}

	puts ("Startup complete.");
	return TRUE;

err_abort:
	if (NULL != sock) {
		pgm_close (sock, FALSE);
		sock = NULL;
	}
	if (NULL != res) {
		pgm_freeaddrinfo (res);
		res = NULL;
	}
	if (NULL != pgm_err) {
		pgm_error_free (pgm_err);
		pgm_err = NULL;
	}
	return FALSE;
}

static
int
on_data (
	const void*      	     restrict data,
	const size_t		    	      len,
	const struct pgm_sockaddr_t* restrict from
	)
{
/* protect against non-null terminated strings */
	char buf[1024], tsi[PGM_TSISTRLEN];
	const size_t buflen = MIN(sizeof(buf) - 1, len);
#ifndef CONFIG_HAVE_SECURITY_ENHANCED_CRT
	strncpy (buf, (const char*)data, buflen);
	buf[buflen] = '\0';
#else
	strncpy_s (buf, buflen, (const char*)data, _TRUNCATE);
#endif
	pgm_tsi_print_r (&from->sa_addr, tsi, sizeof(tsi));
#ifndef _WIN32
	printf ("\"%s\" (%zu bytes from %s)\n",
			buf, len, tsi);
#else
/* Microsoft CRT will crash on %zu */
	printf ("\"%s\" (%lu bytes from %s)\n",
			buf, (unsigned long)len, tsi);
#endif
	return 0;
}

/* eof */
