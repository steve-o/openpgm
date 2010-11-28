/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Simple PGM receiver: blocking synchronous receiver 
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

#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#ifndef G_OS_WIN32
#	include <unistd.h>
#else
#	include "getopt.h"
#endif
#include <pgm/pgm.h>

/* example dependencies */
#include <pgm/backtrace.h>
#include <pgm/log.h>


/* typedefs */

/* globals */

static int		g_port = 0;
static const char*	g_network = "";
static gboolean		g_multicast_loop = FALSE;
static int		g_udp_encap_port = 0;

static int		g_max_tpdu = 1500;
static int		g_sqns = 100;

static pgm_sock_t*	g_sock = NULL;
static gboolean		g_quit = FALSE;

#ifdef G_OS_UNIX
static void on_signal (int);
#else
static BOOL on_console_ctrl (DWORD);
#endif
static gboolean on_startup (void);
static int on_data (gconstpointer, size_t, struct pgm_sockaddr_t*);


G_GNUC_NORETURN static
void
usage (
	const char*	bin
	)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	fprintf (stderr, "  -l              : Enable multicast loopback and address sharing\n");
	exit (1);
}

int
main (
	int		argc,
	char*		argv[]
	)
{
	pgm_error_t* pgm_err = NULL;

	setlocale (LC_ALL, "");

	log_init ();
	g_message ("blocksyncrecv");

	if (!pgm_init (&pgm_err)) {
		g_error ("Unable to start PGM engine: %s", pgm_err->message);
		pgm_error_free (pgm_err);
		return EXIT_FAILURE;
	}

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:p:lh")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;
		case 'p':	g_udp_encap_port = atoi (optarg); break;
		case 'l':	g_multicast_loop = TRUE; break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

/* setup signal handlers */
	signal(SIGSEGV, on_sigsegv);
#ifdef SIGHUP
	signal(SIGHUP,  SIG_IGN);
#endif
#ifdef G_OS_UNIX
	signal(SIGINT,  on_signal);
	signal(SIGTERM, on_signal);
#else
	SetConsoleCtrlHandler ((PHANDLER_ROUTINE)on_console_ctrl, TRUE);
	setvbuf (stdout, (char *) NULL, _IONBF, 0);
#endif

	on_startup();

/* dispatch loop */
	g_message ("entering PGM message loop ... ");
	do {
		char buffer[4096];
		size_t len;
		struct pgm_sockaddr_t from;
		socklen_t fromlen = sizeof(from);
		const int status = pgm_recvfrom (g_sock,
					         buffer,
					         sizeof(buffer),
					         0,
					         &len,
					         &from,
						 &fromlen,
					         &pgm_err);
		if (PGM_IO_STATUS_NORMAL == status)
			on_data (buffer, len, &from);
		else {
			if (pgm_err) {
				g_warning ("%s", pgm_err->message);
				pgm_error_free (pgm_err);
				pgm_err = NULL;
			}
			if (PGM_IO_STATUS_ERROR == status)
				break;
		}
	} while (!g_quit);

	g_message ("message loop terminated, cleaning up.");

/* cleanup */
	if (g_sock) {
		g_message ("closing PGM socket.");
		pgm_close (g_sock, TRUE);
		g_sock = NULL;
	}

	g_message ("PGM engine shutdown.");
	pgm_shutdown ();
	g_message ("finished.");
	return EXIT_SUCCESS;
}

#ifdef G_OS_UNIX
static
void
on_signal (
	int		signum
	)
{
	g_message ("on_signal (signum:%d)", signum);
	g_quit = TRUE;
}
#else
static
BOOL
on_console_ctrl (
	DWORD		dwCtrlType
	)
{
	g_message ("on_console_ctrl (dwCtrlType:%lu)", (unsigned long)dwCtrlType);
	g_quit = TRUE;
	return TRUE;
}
#endif /* !G_OS_UNIX */

static
gboolean
on_startup (void)
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
	const int blocking = 0,
		  multicast_loop = g_multicast_loop ? 1 : 0,
		  multicast_hops = 16,
		  dscp = 0x2e << 2;		/* Expedited Forwarding PHB for network elements, no ECN. */

	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_MULTICAST_LOOP, &multicast_loop, sizeof(multicast_loop));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops));
	if (AF_INET6 != sa_family)
		pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_TOS, &dscp, sizeof(dscp));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NOBLOCK, &blocking, sizeof(blocking));

	if (!pgm_connect (g_sock, &pgm_err)) {
		g_error ("connecting PGM socket: %s", pgm_err->message);
		goto err_abort;
	}

	g_message ("startup complete.");
	return TRUE;

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
	return FALSE;
}

static
int
on_data (
	gconstpointer		data,
	size_t			len,
	struct pgm_sockaddr_t*	from
	)
{
/* protect against non-null terminated strings */
	char buf[1024], tsi[PGM_TSISTRLEN];
	const size_t buflen = MIN( sizeof(buf) - 1, len );
	strncpy (buf, data, buflen);
	buf[buflen] = '\0';
	pgm_tsi_print_r (&from->sa_addr, tsi, sizeof(tsi));

	g_message ("\"%s\" (%u bytes from %s)",
			buf,
			(unsigned)len,
			tsi);

	return 0;
}

/* eof */
