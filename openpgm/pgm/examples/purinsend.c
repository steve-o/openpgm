/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * プリン PGM sender
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
#include <stdio.h>
#ifdef _WIN32
#	include "getopt.h"
#endif
#include <pgm/pgm.h>


/* globals */

static int		port = 0;
static const char*	network = "";
static bool		use_multicast_loop = FALSE;
static int		udp_encap_port = 0;

static int		max_tpdu = 1500;
static int		max_rte = 400*1000;		/* very conservative rate, 2.5mb/s */
static int		sqns = 100;

static bool		use_fec = FALSE;
static int		rs_k = 64;
static int		rs_n = 255;

static pgm_transport_t* transport = NULL;

static void usage (const char*) __attribute__((__noreturn__));
static bool create_transport (void);


static void
usage (
	const char*	bin
	)
{
	fprintf (stderr, "Usage: %s [options] message\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	fprintf (stderr, "  -r <rate>       : Regulate to rate bytes per second\n");
	fprintf (stderr, "  -f <type>       : Enable FEC with either proactive or ondemand parity\n");
	fprintf (stderr, "  -k <k>          : Configure Reed-Solomon code (n, k)\n");
	fprintf (stderr, "  -g <n>\n");
	fprintf (stderr, "  -l              : Enable multicast loopback and address sharing\n");
	fprintf (stderr, "  -i              : List available interfaces\n");
	exit (EXIT_SUCCESS);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	pgm_error_t* pgm_err = NULL;

	setlocale (LC_ALL, "");

	if (!pgm_init (&pgm_err)) {
		fprintf (stderr, "Unable to start PGM engine: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		return EXIT_FAILURE;
	}

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:p:r:f:k:g:lih")) != -1)
	{
		switch (c) {
		case 'n':	network = optarg; break;
		case 's':	port = atoi (optarg); break;
		case 'p':	udp_encap_port = atoi (optarg); break;
		case 'r':	max_rte = atoi (optarg); break;
		case 'f':	use_fec = TRUE; break;
		case 'k':	rs_k = atoi (optarg); break;
		case 'g':	rs_n = atoi (optarg); break;

		case 'l':	use_multicast_loop = TRUE; break;

		case 'i':
			pgm_if_print_all();
			return EXIT_SUCCESS;

		case 'h':
		case '?':
			usage (binary_name);
		}
	}

	if (use_fec && ( !rs_n || !rs_k )) {
		fprintf (stderr, "Invalid Reed-Solomon parameters RS(%d,%d).\n", rs_n, rs_k);
		usage (binary_name);
	}

	if (create_transport())
	{
		while (optind < argc) {
			const int status = pgm_send (transport, argv[optind], strlen (argv[optind]) + 1, NULL);
		        if (PGM_IO_STATUS_NORMAL != status) {
				fprintf (stderr, "pgm_send() failed.\n");
		        }
			optind++;
		}
	}

/* cleanup */
	if (transport) {
		pgm_transport_destroy (transport, TRUE);
		transport = NULL;
	}
	pgm_shutdown();
	return EXIT_SUCCESS;
}

static
bool
create_transport (void)
{
	struct pgm_transport_info_t* res = NULL;
	pgm_error_t* pgm_err = NULL;

/* parse network parameter into transport address structure */
	char network_param[1024];
	sprintf (network_param, "%s", network);
	if (!pgm_if_get_transport_info (network, NULL, &res, &pgm_err)) {
		fprintf (stderr, "Parsing network parameter: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		return FALSE;
	}
/* create global session identifier */
	if (!pgm_gsi_create_from_hostname (&res->ti_gsi, &pgm_err)) {
		fprintf (stderr, "Creating GSI: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_if_free_transport_info (res);
		return FALSE;
	}
	if (udp_encap_port) {
		res->ti_udp_encap_ucast_port = udp_encap_port;
		res->ti_udp_encap_mcast_port = udp_encap_port;
	}
	if (port)
		res->ti_dport = port;
	if (!pgm_transport_create (&transport, res, &pgm_err)) {
		fprintf (stderr, "Creating transport: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_if_free_transport_info (res);
		return FALSE;
	}
	pgm_if_free_transport_info (res);

/* set PGM parameters */
	pgm_transport_set_send_only (transport, TRUE);
	pgm_transport_set_max_tpdu (transport, max_tpdu);
	pgm_transport_set_txw_sqns (transport, sqns);
	pgm_transport_set_txw_max_rte (transport, max_rte);
	pgm_transport_set_multicast_loop (transport, use_multicast_loop);
	pgm_transport_set_hops (transport, 16);
	pgm_transport_set_ambient_spm (transport, pgm_secs(30));
	unsigned spm_heartbeat[] = { pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(1300), pgm_secs(7
), pgm_secs(16), pgm_secs(25), pgm_secs(30) };
	pgm_transport_set_heartbeat_spm (transport, spm_heartbeat, sizeof(spm_heartbeat)/sizeof(spm_heartbeat[0]));
	if (use_fec) {
		pgm_transport_set_fec (transport, 0, TRUE, TRUE, rs_n, rs_k);
	}

/* assign transport to specified address */
	if (!pgm_transport_bind (transport, &pgm_err)) {
		fprintf (stderr, "Binding transport: %s\n", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_transport_destroy (transport, FALSE);
		transport = NULL;
		return FALSE;
	}
	return TRUE;
}

/* eof */
