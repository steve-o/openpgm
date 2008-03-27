/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Simple sender using the PGM transport.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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
#include <getopt.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <glib.h>

#include <pgm/backtrace.h>
#include <pgm/log.h>
#include <pgm/transport.h>
#include <pgm/gsi.h>
#include <pgm/signal.h>
#include <pgm/if.h>


/* typedefs */

/* globals */

static int g_port = 7500;
static char* g_network = "";
static int g_udp_encap_port = 0;

static int g_max_tpdu = 1500;
static int g_max_rte = 400*1000;
static int g_sqns = 10;

static gboolean g_fec = FALSE;
static int g_k = 64;
static int g_n = 255;

static pgm_transport_t* g_transport = NULL;

static gboolean create_transport (void);


static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options] message\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	fprintf (stderr, "  -r <rate>       : Regulate to rate bytes per second\n");
	fprintf (stderr, "  -f <type>       : Enable FEC with either proactive or ondemand parity\n");
	fprintf (stderr, "  -k <k>          : Configure Reed-Solomon code (n, k)\n");
	fprintf (stderr, "  -g <n>\n");
	fprintf (stderr, "  -t              : Enable HTTP administrative interface\n");
	fprintf (stderr, "  -x              : Enable SNMP interface\n");
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:p:r:f:k:g:h")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;
		case 'p':	g_udp_encap_port = atoi (optarg); break;
		case 'r':	g_max_rte = atoi (optarg); break;

		case 'f':	g_fec = TRUE; break;
		case 'k':	g_k = atoi (optarg); break;
		case 'g':	g_n = atoi (optarg); break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	if (g_fec && ( !g_k || !g_n )) {
		puts ("Invalid Reed-Solomon parameters.");
		usage (binary_name);
	}

	log_init ();
	pgm_init ();

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
	signal (SIGHUP, SIG_IGN);

	if (!create_transport ())
	{
		while (optind < argc)
		{
			int e = pgm_transport_send (g_transport, argv[optind], strlen(argv[optind]) + 1, 0);
		        if (e < 0) {
				g_warning ("pgm_transport_send failed.");
		        }

			optind++;
		}
	}

/* cleanup */
	if (g_transport) {
		pgm_transport_destroy (g_transport, TRUE);
		g_transport = NULL;
	}

	return 0;
}

static gboolean
create_transport (void)
{
	pgm_gsi_t gsi;

	int e = pgm_create_md5_gsi (&gsi);
	g_assert (e == 0);

	struct pgm_sock_mreq recv_smr, send_smr;
	int smr_len = 1;
	e = pgm_if_parse_transport (g_network, AF_INET, &recv_smr, &send_smr, &smr_len);
	g_assert (e == 0);
	g_assert (smr_len == 1);

	if (g_udp_encap_port) {
		((struct sockaddr_in*)&send_smr.smr_multiaddr)->sin_port = g_htons (g_udp_encap_port);
		((struct sockaddr_in*)&recv_smr.smr_interface)->sin_port = g_htons (g_udp_encap_port);
	}

	e = pgm_transport_create (&g_transport, &gsi, g_port, &recv_smr, 1, &send_smr);
	g_assert (e == 0);

	pgm_transport_set_max_tpdu (g_transport, g_max_tpdu);
	pgm_transport_set_txw_sqns (g_transport, g_sqns);
	pgm_transport_set_txw_max_rte (g_transport, g_max_rte);
	pgm_transport_set_rxw_sqns (g_transport, g_sqns);
	pgm_transport_set_hops (g_transport, 16);
	pgm_transport_set_ambient_spm (g_transport, 8192*1000);
	guint spm_heartbeat[] = { 1*1000, 1*1000, 2*1000, 4*1000, 8*1000, 16*1000, 32*1000, 64*1000, 128*1000, 256*1000, 512*1000, 1024*1000, 2048*1000, 4096*1000, 8192*1000 };
	pgm_transport_set_heartbeat_spm (g_transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat));
	pgm_transport_set_peer_expiry (g_transport, 5*8192*1000);
	pgm_transport_set_spmr_expiry (g_transport, 250*1000);

	if (g_fec) {
		pgm_transport_set_fec (g_transport, FALSE, TRUE, g_n, g_k);
	}

	e = pgm_transport_bind (g_transport);
	if (e != 0) {
		g_critical ("pgm_transport_bind failed errno %i: \"%s\"", e, strerror(e));
		G_BREAKPOINT();
	}

	return FALSE;
}

/* eof */
