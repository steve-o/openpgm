/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Simple receiver using the PGM socket and the GLib framework.
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

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef CONFIG_HAVE_EPOLL
#	include <sys/epoll.h>
#endif
#include <sys/types.h>
#include <glib.h>
#ifdef G_OS_UNIX
#	include <netdb.h>
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <unistd.h>
#	include <sys/socket.h>
#	include <sys/uio.h>
#	include <sys/time.h>
#else
#	include "getopt.h"
#endif
#include <pgm/pgm.h>
#ifdef CONFIG_WITH_HTTP
#	include <pgm/http.h>
#endif
#ifdef CONFIG_WITH_SNMP
#	include <pgm/snmp.h>
#endif

/* example dependencies */
#include <pgm/backtrace.h>
#include <pgm/log.h>
#include <pgm/signal.h>


/* globals */

static int		g_port = 0;
static const char*	g_network = "";
static const char*	g_source = "";
static gboolean		g_multicast_loop = FALSE;
static int		g_udp_encap_port = 0;

static int		g_max_tpdu = 1500;
static int		g_sqns = 100;

static pgm_sock_t*	g_sock = NULL;
static GThread*		g_thread = NULL;
static GMainLoop*	g_loop = NULL;
static gboolean		g_quit;
#ifdef G_OS_UNIX
static int		g_quit_pipe[2];
static void on_signal (int, gpointer);
#else
static HANDLE		g_quit_event;
static BOOL on_console_ctrl (DWORD);
#endif

static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static gpointer receiver_thread (gpointer);
static int on_msgv (struct pgm_msgv_t*, size_t);


G_GNUC_NORETURN static void
usage (
	const char*	bin
	)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -a <ip address> : Source unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	fprintf (stderr, "  -l              : Enable multicast loopback and address sharing\n");
#ifdef CONFIG_WITH_HTTP
	fprintf (stderr, "  -H              : Enable HTTP administrative interface\n");
#endif
#ifdef CONFIG_WITH_SNMP
	fprintf (stderr, "  -S              : Enable SNMP interface\n");
#endif
	fprintf (stderr, "  -i              : List available interfaces\n");
	exit (1);
}

int
main (
	int		argc,
	char*		argv[]
	)
{
	pgm_error_t* pgm_err = NULL;
#ifdef CONFIG_WITH_HTTP
	gboolean enable_http = FALSE;
#endif
#ifdef CONFIG_WITH_SNMP
	gboolean enable_snmpx = FALSE;
#endif

	setlocale (LC_ALL, "");

/* pre-initialise PGM messages module to add hook for GLib logging */
	pgm_messages_init();
	log_init ();
	g_message ("pgmrecv");

	if (!pgm_init (&pgm_err)) {
		g_error ("Unable to start PGM engine: %s", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
		pgm_error_free (pgm_err);
		pgm_messages_shutdown();
		return EXIT_FAILURE;
	}

	g_thread_init (NULL);

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "a:s:n:p:lih"
#ifdef CONFIG_WITH_HTTP
					"H"
#endif
#ifdef CONFIG_WITH_SNMP
					"S"
#endif
					)) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 'a':	g_source = optarg; break;
		case 's':	g_port = atoi (optarg); break;
		case 'p':	g_udp_encap_port = atoi (optarg); break;

		case 'l':	g_multicast_loop = TRUE; break;
#ifdef CONFIG_WITH_HTTP
		case 'H':	enable_http = TRUE; break;
#endif
#ifdef CONFIG_WITH_SNMP
		case 'S':	enable_snmpx = TRUE; break;
#endif

		case 'i':
			pgm_if_print_all();
			pgm_messages_shutdown();
			return EXIT_SUCCESS;

		case 'h':
		case '?':
			pgm_messages_shutdown();
			usage (binary_name);
		}
	}

#ifdef CONFIG_WITH_HTTP
	if (enable_http) {
		if (!pgm_http_init (PGM_HTTP_DEFAULT_SERVER_PORT, &pgm_err)) {
			g_error ("Unable to start HTTP interface: %s", pgm_err->message);
			pgm_error_free (pgm_err);
			pgm_shutdown();
			pgm_messages_shutdown();
			return EXIT_FAILURE;
		}
	}
#endif
#ifdef CONFIG_WITH_SNMP
	if (enable_snmpx) {
		if (!pgm_snmp_init (&pgm_err)) {
			g_error ("Unable to start SNMP interface: %s", pgm_err->message);
			pgm_error_free (pgm_err);
#ifdef CONFIG_WITH_HTTP
			if (enable_http)
				pgm_http_shutdown ();
#endif
			pgm_shutdown ();
			pgm_messages_shutdown();
	 		return EXIT_FAILURE;
		}
	}
#endif

	g_loop = g_main_loop_new (NULL, FALSE);

	g_quit = FALSE;

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
#ifdef SIGHUP
	signal (SIGHUP,  SIG_IGN);
#endif
#ifdef G_OS_UNIX
	const int e = pipe (g_quit_pipe);
	g_assert (0 == e);
	pgm_signal_install (SIGINT,  on_signal, g_loop);
	pgm_signal_install (SIGTERM, on_signal, g_loop);
#else
	g_quit_event = CreateEvent (NULL, TRUE, FALSE, TEXT("QuitEvent"));
	SetConsoleCtrlHandler ((PHANDLER_ROUTINE)on_console_ctrl, TRUE);
	setvbuf (stdout, (char *) NULL, _IONBF, 0);
#endif

/* delayed startup */
	g_message ("scheduling startup.");
	g_timeout_add (0, (GSourceFunc)on_startup, NULL);

/* dispatch loop */
	g_message ("entering main event loop ... ");
	g_main_loop_run (g_loop);

	g_message ("event loop terminated, cleaning up.");

/* cleanup */
	g_quit = TRUE;
#ifdef G_OS_UNIX
	const char one = '1';
	const size_t writelen = write (g_quit_pipe[1], &one, sizeof(one));
	g_assert (sizeof(one) == writelen);
	g_thread_join (g_thread);
	close (g_quit_pipe[0]);
	close (g_quit_pipe[1]);
#else
	WSASetEvent (g_quit_event);
	g_thread_join (g_thread);
	WSACloseEvent (g_quit_event);
#endif

	g_main_loop_unref (g_loop);
	g_loop = NULL;

	if (g_sock) {
		g_message ("closing PGM socket.");

		pgm_close (g_sock, TRUE);
		g_sock = NULL;
	}

#ifdef CONFIG_WITH_HTTP
	if (enable_http)
		pgm_http_shutdown();
#endif
#ifdef CONFIG_WITH_SNMP
	if (enable_snmpx)
		pgm_snmp_shutdown();
#endif

	g_message ("PGM engine shutdown.");
	pgm_shutdown();
	g_message ("finished.");
	pgm_messages_shutdown();
	return EXIT_SUCCESS;
}

#ifdef G_OS_UNIX
static
void
on_signal (
	int		signum,
	gpointer	user_data
	)
{
	GMainLoop* loop = (GMainLoop*)user_data;
	g_message ("on_signal (signum:%d user_data:%p)",
		   signum, user_data);
	g_main_loop_quit (loop);
}
#else
static
BOOL
on_console_ctrl (
	DWORD		dwCtrlType
	)
{
	g_message ("on_console_ctrl (dwCtrlType:%lu)", (unsigned long)dwCtrlType);
	g_main_loop_quit (g_loop);
	return TRUE;
}
#endif /* !G_OS_UNIX */

static
gboolean
on_startup (
	G_GNUC_UNUSED gpointer data
	)
{
	struct pgm_addrinfo_t* res = NULL;
	pgm_error_t* pgm_err = NULL;
	sa_family_t sa_family = AF_UNSPEC;

	g_message ("startup.");

/* parse network parameter into transport address structure */
	if (!pgm_getaddrinfo (g_network, NULL, &res, &pgm_err)) {
		g_error ("parsing network parameter: %s", pgm_err->message);
		goto err_abort;
	}

	sa_family = res->ai_send_addrs[0].gsr_group.ss_family;

	if (g_udp_encap_port) {
		g_message ("create PGM/UDP socket.");
		if (!pgm_socket (&g_sock, sa_family, SOCK_SEQPACKET, IPPROTO_UDP, &pgm_err)) {
			g_error ("socket: %s", pgm_err->message);
			goto err_abort;
		}
		pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_UDP_ENCAP_UCAST_PORT, &g_udp_encap_port, sizeof(g_udp_encap_port));
		pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_UDP_ENCAP_MCAST_PORT, &g_udp_encap_port, sizeof(g_udp_encap_port));
	} else {
		g_message ("create PGM/IP socket.");
		if (!pgm_socket (&g_sock, sa_family, SOCK_SEQPACKET, IPPROTO_PGM, &pgm_err)) {
			g_error ("socket: %s", pgm_err->message);
			goto err_abort;
		}
	}

/* Use RFC 2113 tagging for PGM Router Assist */
	const int no_router_assist = 0;
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_IP_ROUTER_ALERT, &no_router_assist, sizeof(no_router_assist));

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

	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_RECV_ONLY, &recv_only, sizeof(recv_only));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_PASSIVE, &passive, sizeof(passive));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_MTU, &g_max_tpdu, sizeof(g_max_tpdu));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_RXW_SQNS, &g_sqns, sizeof(g_sqns));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_PEER_EXPIRY, &peer_expiry, sizeof(peer_expiry));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_SPMR_EXPIRY, &spmr_expiry, sizeof(spmr_expiry));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_BO_IVL, &nak_bo_ivl, sizeof(nak_bo_ivl));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_RPT_IVL, &nak_rpt_ivl, sizeof(nak_rpt_ivl));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_RDATA_IVL, &nak_rdata_ivl, sizeof(nak_rdata_ivl));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_DATA_RETRIES, &nak_data_retries, sizeof(nak_data_retries));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_NCF_RETRIES, &nak_ncf_retries, sizeof(nak_ncf_retries));

/* create global session identifier */
	struct pgm_sockaddr_t addr;
	memset (&addr, 0, sizeof(addr));
	addr.sa_port = g_port ? g_port : DEFAULT_DATA_DESTINATION_PORT;
	addr.sa_addr.sport = DEFAULT_DATA_SOURCE_PORT;
	if (!pgm_gsi_create_from_hostname (&addr.sa_addr.gsi, &pgm_err)) {
		g_error ("creating GSI: %s", pgm_err->message);
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
	if (!pgm_bind3 (g_sock,
			&addr, sizeof(addr),
			&if_req, sizeof(if_req),	/* tx interface */
			&if_req, sizeof(if_req),	/* rx interface */
			&pgm_err))
	{
		g_error ("binding PGM socket: %s", pgm_err->message);
		goto err_abort;
	}

/* join IP multicast groups */
	for (unsigned i = 0; i < res->ai_recv_addrs_len; i++)
		pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_JOIN_GROUP, &res->ai_recv_addrs[i], sizeof(struct group_req));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_SEND_GROUP, &res->ai_send_addrs[0], sizeof(struct group_req));
	pgm_freeaddrinfo (res);

/* set IP parameters */
	const int nonblocking = 1,
		  multicast_loop = g_multicast_loop ? 1 : 0,
		  multicast_hops = 16,
		  dscp = 0x2e << 2;		/* Expedited Forwarding PHB for network elements, no ECN. */

	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_MULTICAST_LOOP, &multicast_loop, sizeof(multicast_loop));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops));
	if (AF_INET6 != sa_family)
		pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_TOS, &dscp, sizeof(dscp));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NOBLOCK, &nonblocking, sizeof(nonblocking));

	if (!pgm_connect (g_sock, &pgm_err)) {
		g_error ("connecting PGM socket: %s", pgm_err->message);
		goto err_abort;
	}

/* create receiver thread */
	GError* glib_err = NULL;
	g_thread = g_thread_create_full (receiver_thread,
					 g_sock,
					 0,
					 TRUE,
					 TRUE,
					 G_THREAD_PRIORITY_HIGH,
					 &glib_err);
	if (!g_thread) {
		g_error ("g_thread_create_full failed errno %i: \"%s\"", glib_err->code, glib_err->message);
		g_error_free (glib_err);
		goto err_abort;
	}

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add(10 * 1000, (GSourceFunc)on_mark, NULL);

	g_message ("startup complete.");
	return FALSE;

err_abort:
	if (NULL != g_sock) {
		pgm_close (g_sock, FALSE);
		g_sock = NULL;
	}
	if (NULL != res) {
		pgm_freeaddrinfo (res);
		res = NULL;
	}
	if (NULL != pgm_err) {
		pgm_error_free (pgm_err);
		pgm_err = NULL;
	}
	g_main_loop_quit (g_loop);
	return FALSE;
}

/* idle log notification
 */

static
gboolean
on_mark (
	G_GNUC_UNUSED gpointer data
	)
{
	g_message ("-- MARK --");
	return TRUE;
}

static
gpointer
receiver_thread (
	gpointer	data
	)
{
	pgm_sock_t* rx_sock = (pgm_sock_t*)data;
	const long iov_len = 20;
	const long ev_len  = 1;
	struct pgm_msgv_t msgv[iov_len];

#ifdef CONFIG_HAVE_EPOLL
	struct epoll_event events[ev_len];	/* wait for maximum 1 event */
	const int efd = epoll_create (IP_MAX_MEMBERSHIPS);
	if (efd < 0) {
		g_error ("epoll_create failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}

	if (pgm_epoll_ctl (rx_sock, efd, EPOLL_CTL_ADD, EPOLLIN) < 0)
	{
		g_error ("pgm_epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = g_quit_pipe[0];
	if (epoll_ctl (efd, EPOLL_CTL_ADD, g_quit_pipe[0], &event) < 0)
	{
		g_error ("epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
#elif defined(CONFIG_HAVE_POLL)
	int n_fds = 2;
	struct pollfd fds[ 1 + n_fds ];
#elif defined(G_OS_UNIX) /* HAVE_SELECT */
	int n_fds;
	fd_set readfds;
#else /* G_OS_WIN32 */
	SOCKET recv_sock, pending_sock;
	DWORD cEvents = PGM_RECV_SOCKET_READ_COUNT + 1;
	WSAEVENT waitEvents[ PGM_RECV_SOCKET_READ_COUNT + 1 ];
	socklen_t socklen = sizeof (SOCKET);

	waitEvents[0] = g_quit_event;
	waitEvents[1] = WSACreateEvent ();
	pgm_getsockopt (rx_sock, IPPROTO_PGM, PGM_RECV_SOCK, &recv_sock, &socklen);
	WSAEventSelect (recv_sock, waitEvents[1], FD_READ);
	waitEvents[2] = WSACreateEvent ();
	pgm_getsockopt (rx_sock, IPPROTO_PGM, PGM_PENDING_SOCK, &pending_sock, &socklen);
	WSAEventSelect (pending_sock, waitEvents[2], FD_READ);
#endif /* !CONFIG_HAVE_EPOLL */

	do {
		struct timeval tv;
#ifndef _WIN32
		int timeout;
#else
		DWORD dwTimeout, dwEvents;
#endif
		size_t len;
		pgm_error_t* pgm_err = NULL;
		const int status = pgm_recvmsgv (rx_sock,
					         msgv,
					         G_N_ELEMENTS(msgv),
					         0,
					         &len,
					         &pgm_err);
		switch (status) {
		case PGM_IO_STATUS_NORMAL:
			on_msgv (msgv, len);
			break;
		case PGM_IO_STATUS_TIMER_PENDING:
			{
				socklen_t optlen = sizeof (tv);
				pgm_getsockopt (rx_sock, IPPROTO_PGM, PGM_TIME_REMAIN, &tv, &optlen);
			}
			goto block;
		case PGM_IO_STATUS_RATE_LIMITED:
			{
				socklen_t optlen = sizeof (tv);
				pgm_getsockopt (rx_sock, IPPROTO_PGM, PGM_RATE_REMAIN, &tv, &optlen);
			}
/* fall through */
		case PGM_IO_STATUS_WOULD_BLOCK:
block:
#ifdef CONFIG_HAVE_EPOLL
			timeout = PGM_IO_STATUS_WOULD_BLOCK == status ? -1 : ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
			epoll_wait (efd, events, G_N_ELEMENTS(events), timeout /* ms */);
#elif defined(CONFIG_HAVE_POLL)
			timeout = PGM_IO_STATUS_WOULD_BLOCK == status ? -1 : ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
			memset (fds, 0, sizeof(fds));
			fds[0].fd = g_quit_pipe[0];
			fds[0].events = POLLIN;
			pgm_poll_info (rx_sock, &fds[1], &n_fds, POLLIN);
			poll (fds, 1 + n_fds, timeout /* ms */);
#elif defined(G_OS_UNIX) /* HAVE_SELECT */
			FD_ZERO(&readfds);
			FD_SET(g_quit_pipe[0], &readfds);
			n_fds = g_quit_pipe[0] + 1;
			pgm_select_info (rx_sock, &readfds, NULL, &n_fds);
			select (n_fds, &readfds, NULL, NULL, PGM_IO_STATUS_RATE_LIMITED == status ? &tv : NULL);
#else /* G_OS_WIN32 */
			dwTimeout = PGM_IO_STATUS_WOULD_BLOCK == status ? WSA_INFINITE : ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
			dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, dwTimeout, FALSE);
			switch (dwEvents) {
			case WSA_WAIT_EVENT_0+1: WSAResetEvent (waitEvents[1]); break;
			case WSA_WAIT_EVENT_0+2: WSAResetEvent (waitEvents[2]); break;
			default: break;
			}
#endif /* !CONFIG_HAVE_EPOLL */
			break;

		default:
			if (pgm_err) {
				g_warning ("%s", pgm_err->message);
				pgm_error_free (pgm_err);
				pgm_err = NULL;
			}
			if (PGM_IO_STATUS_ERROR == status)
				break;
		}
	} while (!g_quit);

#ifdef CONFIG_HAVE_EPOLL
	close (efd);
#elif defined(G_OS_WIN32)
	WSACloseEvent (waitEvents[1]);
	WSACloseEvent (waitEvents[2]);
#  if (__STDC_VERSION__ < 199901L)
	g_free (waitHandles);
#  endif
#endif
	return NULL;
}

static
int
on_msgv (
	struct pgm_msgv_t*	msgv,		/* an array of msgvs */
	size_t			len
	)
{
        g_message ("(%u bytes)",
                        (unsigned)len);

        guint i = 0;
/* for each apdu */
	do {
                const struct pgm_sk_buff_t* pskb = msgv[i].msgv_skb[0];
		gsize apdu_len = 0;
		for (unsigned j = 0; j < msgv[i].msgv_len; j++)
			apdu_len += msgv[i].msgv_skb[j]->len;
/* truncate to first fragment to make GLib printing happy */
		char buf[2048], tsi[PGM_TSISTRLEN];
		const gsize buflen = MIN(sizeof(buf) - 1, pskb->len);
		strncpy (buf, (const char*)pskb->data, buflen);
		buf[buflen] = '\0';
		pgm_tsi_print_r (&pskb->tsi, tsi, sizeof(tsi));
		if (msgv[i].msgv_len > 1)
			g_message ("\t%u: \"%s\" ... (%" G_GSIZE_FORMAT " bytes from %s)",
				   i, buf, apdu_len, tsi);
		else
			g_message ("\t%u: \"%s\" (%" G_GSIZE_FORMAT " bytes from %s)",
				   i, buf, apdu_len, tsi);
		i++;
		len -= apdu_len;
        } while (len);

	return 0;
}

/* eof */
