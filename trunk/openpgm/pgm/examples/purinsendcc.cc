/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * プリン C++ PGM sender.  A port of purinsend.c to the overly
 * cumbersome Boost Asio styled C++ wrapper.
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

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#ifndef _WIN32
#	include <unistd.h>
#	include <netinet/in.h>
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
static int		max_rte = 400*1000;		/* very conservative rate, 2.5mb/s */
static int		sqns = 100;

static bool		use_fec = FALSE;
static int		rs_k = 8;
static int		rs_n = 255;

static ip::pgm::endpoint* endpoint = NULL;
static ip::pgm::socket* sock = NULL;

#ifndef _MSC_VER
static void usage (const char*) __attribute__((__noreturn__));
#else
static void usage (const char*);
#endif
static bool create_sock (void);


static void
usage (
	const char*	bin
	)
{
	std::cerr << "Usage: " << bin << " [options] message" << std::endl;
	std::cerr << "  -n <network>    : Multicast group or unicast IP address" << std::endl;
	std::cerr << "  -s <port>       : IP port" << std::endl;
	std::cerr << "  -p <port>       : Encapsulate PGM in UDP on IP port" << std::endl;
	std::cerr << "  -r <rate>       : Regulate to rate bytes per second" << std::endl;
	std::cerr << "  -f <type>       : Enable FEC with either proactive or ondemand parity" << std::endl;
	std::cerr << "  -K <k>          : Configure Reed-Solomon code (n, k)" << std::endl;
	std::cerr << "  -N <n>" << std::endl;
	std::cerr << "  -l              : Enable multicast loopback and address sharing" << std::endl;
	std::cerr << "  -i              : List available interfaces" << std::endl;
	exit (EXIT_SUCCESS);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	cpgm::pgm_error_t* pgm_err = NULL;

	std::setlocale (LC_ALL, "");

	if (!cpgm::pgm_init (&pgm_err)) {
		std::cerr << "Unable to start PGM engine: " << pgm_err->message << std::endl;
		cpgm::pgm_error_free (pgm_err);
		return EXIT_FAILURE;
	}

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:p:r:f:K:N:lih")) != -1)
	{
		switch (c) {
		case 'n':	network = optarg; break;
		case 's':	port = atoi (optarg); break;
		case 'p':	udp_encap_port = atoi (optarg); break;
		case 'r':	max_rte = atoi (optarg); break;
		case 'f':	use_fec = TRUE; break;
		case 'K':	rs_k = atoi (optarg); break;
		case 'N':	rs_n = atoi (optarg); break;

		case 'l':	use_multicast_loop = TRUE; break;

		case 'i':
			cpgm::pgm_if_print_all();
			return EXIT_SUCCESS;

		case 'h':
		case '?':
			usage (binary_name);
		}
	}

	if (use_fec && ( !rs_n || !rs_k )) {
		std::cerr << "Invalid Reed-Solomon parameters RS(" << rs_n << "," << rs_k << ")." << std::endl;
		usage (binary_name);
	}

	if (create_sock())
	{
		while (optind < argc) {
			const int status = sock->send (argv[optind], strlen (argv[optind]) + 1, NULL);
		        if (cpgm::PGM_IO_STATUS_NORMAL != status) {
				std::cerr << "pgm_send() failed.." << std::endl;
		        }
			optind++;
		}
	}

/* cleanup */
	if (sock) {
		sock->close (TRUE);
		sock = NULL;
	}
	cpgm::pgm_shutdown();
	return EXIT_SUCCESS;
}

static
bool
create_sock (void)
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
		if (!sock->open (sa_family, SOCK_SEQPACKET, IPPROTO_UDP, &pgm_err)) {
			std::cerr << "Creating PGM/UDP socket: " << pgm_err->message << std::endl;
			goto err_abort;
		}
		sock->set_option (IPPROTO_PGM, cpgm::PGM_UDP_ENCAP_UCAST_PORT, &udp_encap_port, sizeof(udp_encap_port));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_UDP_ENCAP_MCAST_PORT, &udp_encap_port, sizeof(udp_encap_port));
	} else {
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

		sock->set_option (IPPROTO_PGM, cpgm::PGM_SEND_ONLY, &send_only, sizeof(send_only));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_MTU, &max_tpdu, sizeof(max_tpdu));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_TXW_SQNS, &sqns, sizeof(sqns));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_TXW_MAX_RTE, &max_rte, sizeof(max_rte));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_AMBIENT_SPM, &ambient_spm, sizeof(ambient_spm));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_HEARTBEAT_SPM, &heartbeat_spm, sizeof(heartbeat_spm));
	}
	if (use_fec) {
		struct cpgm::pgm_fecinfo_t fecinfo; 
		fecinfo.block_size		= rs_n;
		fecinfo.proactive_packets	= 0;
		fecinfo.group_size		= rs_k;
		fecinfo.ondemand_parity_enabled	= TRUE;
		fecinfo.var_pktlen_enabled	= TRUE;
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
		const int blocking = 0,
			  multicast_loop = use_multicast_loop ? 1 : 0,
			  multicast_hops = 16,
			  dscp = 0x2e << 2;		/* Expedited Forwarding PHB for network elements, no ECN. */

		sock->set_option (IPPROTO_PGM, cpgm::PGM_MULTICAST_LOOP, &multicast_loop, sizeof(multicast_loop));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_TOS, &dscp, sizeof(dscp));
		sock->set_option (IPPROTO_PGM, cpgm::PGM_NOBLOCK, &blocking, sizeof(blocking));
	}

	if (!sock->connect (&pgm_err)) {
		fprintf (stderr, "Connecting PGM socket: %s\n", pgm_err->message);
		goto err_abort;
	}

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

/* eof */
