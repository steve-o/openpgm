/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Simple one-sot unreliable sender using the PGM transport.  Depends
 * upon the GLib framework for portability and rapid development.
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
#include <sys/types.h>
#include <glib.h>
#ifdef G_OS_UNIX
#	include <netdb.h>
#	include <unistd.h>
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <sys/time.h>
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
static int		g_max_rte = 400*1000;
static int		g_sqns = 100;

static gboolean		g_fec = FALSE;
static int		g_k = 8;
static int		g_n = 255;

static pgm_sock_t*	g_sock = NULL;

static gboolean create_pgm_socket (void);


G_GNUC_NORETURN static
void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options] message\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	fprintf (stderr, "  -r <rate>       : Regulate to rate bytes per second\n");
	fprintf (stderr, "  -f <type>       : Enable FEC with either proactive or ondemand parity\n");
	fprintf (stderr, "  -K <k>          : Configure Reed-Solomon code (n, k)\n");
	fprintf (stderr, "  -N <n>\n");
	fprintf (stderr, "  -l              : Enable multicast loopback and address sharing\n");
	fprintf (stderr, "  -i              : List available interfaces\n");
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	pgm_error_t* pgm_err = NULL;

	setlocale (LC_ALL, "");

/* pre-initialise PGM messages module to add hook for GLib logging */
	pgm_messages_init();
	log_init();
	if (!pgm_init (&pgm_err)) {
		g_error ("Unable to start PGM engine: %s", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_messages_shutdown();
		return EXIT_FAILURE;
	}

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:p:r:f:K:N:lih")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;
		case 'p':	g_udp_encap_port = atoi (optarg); break;
		case 'r':	g_max_rte = atoi (optarg); break;

		case 'f':	g_fec = TRUE; break;
		case 'K':	g_k = atoi (optarg); break;
		case 'N':	g_n = atoi (optarg); break;

		case 'l':	g_multicast_loop = TRUE; break;

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

	if (g_fec && ( !g_k || !g_n )) {
		pgm_messages_shutdown();
		g_error ("Invalid Reed-Solomon parameters RS(%d, %d).", g_n, g_k);
		usage (binary_name);
	}

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
#ifdef SIGHUP
	signal (SIGHUP, SIG_IGN);
#endif

	if (create_pgm_socket())
	{
		while (optind < argc) {
			const int status = pgm_send (g_sock, argv[optind], strlen(argv[optind]) + 1, NULL);
		        if (PGM_IO_STATUS_NORMAL != status) {
				g_warning ("pgm_send failed.");
		        }
			optind++;
		}
	}

/* cleanup */
	if (g_sock) {
		pgm_close (g_sock, TRUE);
		g_sock = NULL;
	}
	pgm_shutdown();
	pgm_messages_shutdown();
	return EXIT_SUCCESS;
}

static
gboolean
create_pgm_socket (void)
{
	struct pgm_addrinfo_t* res = NULL;
	pgm_error_t* pgm_err = NULL;
	sa_family_t sa_family = AF_UNSPEC;

/* parse network parameter into PGM socket address structure */
	if (!pgm_getaddrinfo (g_network, NULL, &res, &pgm_err)) {
		g_error ("Parsing network parameter: %s", pgm_err->message);
		goto err_abort;
	}

	sa_family = res->ai_send_addrs[0].gsr_group.ss_family;

	if (g_udp_encap_port) {
		if (!pgm_socket (&g_sock, sa_family, SOCK_SEQPACKET, IPPROTO_UDP, &pgm_err)) {
			g_error ("Creating PGM/UDP socket: %s", pgm_err->message);
			goto err_abort;
		}
		pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_UDP_ENCAP_UCAST_PORT, &g_udp_encap_port, sizeof(g_udp_encap_port));
		pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_UDP_ENCAP_MCAST_PORT, &g_udp_encap_port, sizeof(g_udp_encap_port));
	} else {
		if (!pgm_socket (&g_sock, sa_family, SOCK_SEQPACKET, IPPROTO_PGM, &pgm_err)) {
			g_error ("Creating PGM/IP socket: %s", pgm_err->message);
			goto err_abort;
		}
	}

/* Use RFC 2113 tagging for PGM Router Assist */
	const int no_router_assist = 0;
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_IP_ROUTER_ALERT, &no_router_assist, sizeof(no_router_assist));

	pgm_drop_superuser();

/* set PGM parameters */
	const int send_only = 1,
		  ambient_spm = pgm_secs (30),
		  heartbeat_spm[] = { pgm_msecs (100),
				      pgm_msecs (100),
                                      pgm_msecs (100),
				      pgm_msecs (100),
				      pgm_msecs (1300),
				      pgm_secs  (7),
				      pgm_secs  (16),
				      pgm_secs  (25),
				      pgm_secs  (30) };

	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_SEND_ONLY, &send_only, sizeof(send_only));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_MTU, &g_max_tpdu, sizeof(g_max_tpdu));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_TXW_SQNS, &g_sqns, sizeof(g_sqns));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_TXW_MAX_RTE, &g_max_rte, sizeof(g_max_rte));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_AMBIENT_SPM, &ambient_spm, sizeof(ambient_spm));
	pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_HEARTBEAT_SPM, &heartbeat_spm, sizeof(heartbeat_spm));
	if (g_fec) {
		struct pgm_fecinfo_t fecinfo; 
		fecinfo.block_size		= g_n;
		fecinfo.proactive_packets	= 0;
		fecinfo.group_size		= g_k;
		fecinfo.ondemand_parity_enabled	= TRUE;
		fecinfo.var_pktlen_enabled	= TRUE;
		pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_USE_FEC, &fecinfo, sizeof(fecinfo));
	}

/* create global session identifier */
	struct pgm_sockaddr_t addr;
	memset (&addr, 0, sizeof(addr));
	addr.sa_port = g_port ? g_port : DEFAULT_DATA_DESTINATION_PORT;
	addr.sa_addr.sport = DEFAULT_DATA_SOURCE_PORT;
	if (!pgm_gsi_create_from_hostname (&addr.sa_addr.gsi, &pgm_err)) {
		g_error ("Creating GSI: %s", pgm_err->message);
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
		g_error ("Binding PGM socket: %s", pgm_err->message);
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
		g_error ("Connecting PGM socket: %s", pgm_err->message);
		goto err_abort;
	}

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

/* eof */
