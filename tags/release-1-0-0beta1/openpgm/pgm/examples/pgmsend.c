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
#include <pgm/http.h>
#include <pgm/snmp.h>


/* typedefs */

/* globals */

static int g_port = 7500;
static char* g_network = "";
static int g_udp_encap_port = 0;

static int g_odata_interval = 1 * 1000;	/* every 10ms */
static int g_payload = 0;
static int g_max_tpdu = 1500;
static int g_max_rte = 400*1000;
static int g_sqns = 10;

static gboolean g_fec = FALSE;
static int g_k = 0;
static int g_2t = 0;

static pgm_transport_t* g_transport = NULL;

static GMainLoop* g_loop = NULL;


static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static void send_odata (void);
static gboolean on_odata_timer (gpointer);


static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	fprintf (stderr, "  -r <rate>       : Regulate to rate bytes per second\n");
	fprintf (stderr, "  -f <type>       : Enable FEC with either proactive or ondemand parity\n");
	fprintf (stderr, "  -k <k>          : Configure FEC with k data packets, 2t parity\n");
	fprintf (stderr, "  -2 <2t>\n");
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
	g_message ("pgmsend");
	gboolean enable_http = FALSE;
	gboolean enable_snmpx = FALSE;

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:p:r:f:k:2:xth")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;
		case 'p':	g_udp_encap_port = atoi (optarg); break;
		case 'r':	g_max_rte = atoi (optarg); break;

		case 'f':	g_fec = TRUE; break;
		case 'k':	g_k = atoi (optarg); break;
		case '2':	g_2t = atoi (optarg); break;

		case 't':	enable_http = TRUE; break;
		case 'x':	enable_snmpx = TRUE; break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	if (g_fec && ( !g_k || !g_2t )) {
		puts ("Invalid FEC parameters.");
		usage (binary_name);
	}

	log_init ();
	pgm_init ();

	if (enable_http)
		pgm_http_init(PGM_HTTP_DEFAULT_SERVER_PORT);
	if (enable_snmpx)
		pgm_snmp_init();

	g_loop = g_main_loop_new(NULL, FALSE);

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
	pgm_signal_install (SIGINT, on_signal);
	pgm_signal_install (SIGTERM, on_signal);
	pgm_signal_install (SIGHUP, SIG_IGN);

/* delayed startup */
	g_message ("scheduling startup.");
	g_timeout_add (0, (GSourceFunc)on_startup, NULL);

/* dispatch loop */
	g_message ("entering main event loop ... ");
	g_main_loop_run (g_loop);

	g_message ("event loop terminated, cleaning up.");

/* cleanup */
	g_main_loop_unref (g_loop);
	g_loop = NULL;

	if (g_transport) {
		g_message ("destroying transport.");

		pgm_transport_destroy (g_transport, TRUE);
		g_transport = NULL;
	}

	if (enable_http)
		pgm_http_shutdown();
	if (enable_snmpx)
		pgm_snmp_shutdown();

	g_message ("finished.");
	return 0;
}

static void
on_signal (
	int	signum
	)
{
	g_message ("on_signal");

	g_main_loop_quit(g_loop);
}

static gboolean
on_startup (
	gpointer data
	)
{
	g_message ("startup.");
	g_message ("create transport.");

	pgm_gsi_t gsi;
#if 0
	char hostname[NI_MAXHOST];
	struct addrinfo hints, *res = NULL;

	gethostname (hostname, sizeof(hostname));
	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_ADDRCONFIG;
	getaddrinfo (hostname, NULL, &hints, &res);
	int e = pgm_create_ipv4_gsi (((struct sockaddr_in*)(res->ai_addr))->sin_addr, &gsi);
	g_assert (e == 0);
	freeaddrinfo (res);
#else
	int e = pgm_create_md5_gsi (&gsi);
	g_assert (e == 0);
#endif

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
		pgm_transport_set_fec (g_transport, TRUE, TRUE, g_k, g_2t);
	}

	e = pgm_transport_bind (g_transport);
	if (e != 0) {
		g_critical ("pgm_transport_bind failed errno %i: \"%s\"", e, strerror(e));
		G_BREAKPOINT();
	}

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add(10 * 1000, (GSourceFunc)on_mark, NULL);

	g_message ("scheduling ODATA broadcasts every %.1g secs.", (double)pgm_to_secs((double)g_odata_interval));
	g_timeout_add(g_odata_interval, (GSourceFunc)on_odata_timer, NULL);

	g_message ("startup complete.");
	return FALSE;
}

/* we send out a stream of ODATA packets with basic changing payload
 */

static gboolean
on_odata_timer (
	gpointer data
	)
{
	send_odata ();
	return TRUE;
}

static void
send_odata (void)
{
	int e;
	char payload_string[100];

	snprintf (payload_string, sizeof(payload_string), "%i", g_payload++);

	e = pgm_transport_send (g_transport, payload_string, strlen(payload_string) + 1, 0);
        if (e < 0) {
		g_warning ("pgm_transport_send failed.");
                return;
        }
}

/* idle log notification
 */

static gboolean
on_mark (
	gpointer data
	)
{
	static struct timeval tv;
	gettimeofday(&tv, NULL);
	g_message ("%s counter: %i", ts_format((tv.tv_sec + g_timezone) % 86400, tv.tv_usec), g_payload);

	return TRUE;
}

/* eof */
