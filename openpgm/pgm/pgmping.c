/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Simple send/reply ping tool using the PGM transport.
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

#include "backtrace.h"
#include "log.h"
#include "transport.h"
#include "gsi.h"
#include "signal.h"


/* typedefs */

/* globals */

static int g_port = 7500;
static char* g_network = "226.0.0.1";

static int g_odata_interval = 1 * 100 * 1000;	/* 100 ms */
static int g_payload = 0;
static int g_max_tpdu = 1500;
static int g_sqns = 200 * 1000;

static gboolean g_send_mode = TRUE;

static struct pgm_transport* g_transport = NULL;

static GMainLoop* g_loop = NULL;


static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static void send_odata (void);
static gboolean on_odata_timer (gpointer);
static int on_data (gpointer, guint, gpointer);


static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -f <frequency>  : Number of message to send per second\n");
	fprintf (stderr, "  -l              : Listen mode (default send mode)\n");
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	g_message ("pgmping");

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:f:lh")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;

		case 'f':	g_odata_interval = (1000 * 1000) / atoi (optarg); break;

		case 'l':	g_send_mode = FALSE; break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	log_init ();
	pgm_init ();

	g_loop = g_main_loop_new (NULL, FALSE);

/* setup signal handlers */
	signal_install (SIGSEGV, on_sigsegv);
	signal_install (SIGINT, on_signal);
	signal_install (SIGTERM, on_signal);
	signal_install (SIGHUP, SIG_IGN);

/* delayed startup */
	g_message ("scheduling startup.");
	g_timeout_add (0, (GSourceFunc)on_startup, NULL);

/* dispatch loop */
	g_message ("entering main event loop ... ");
	g_main_loop_run (g_loop);

	g_message ("event loop terminated, cleaning up.");

/* cleanup */
	g_main_loop_unref(g_loop);
	g_loop = NULL;

	if (g_transport) {
		g_message ("destroying transport.");

		pgm_transport_destroy (g_transport, TRUE);
		g_transport = NULL;
	}

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

	char gsi[6];
#if 0
	char hostname[NI_MAXHOST];
	struct addrinfo hints, *res = NULL;

	gethostname (hostname, sizeof(hostname));
	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_ADDRCONFIG;
	getaddrinfo (hostname, NULL, &hints, &res);
	int e = gsi_create_ipv4_id (((struct sockaddr_in*)(res->ai_addr))->sin_addr, gsi);
	g_assert (e == 0);
	freeaddrinfo (res);
#else
	int e = gsi_create_md5_id (gsi);
	g_assert (e == 0);
#endif

	struct sock_mreq recv_smr, send_smr;
	((struct sockaddr_in*)&recv_smr.smr_multiaddr)->sin_family = AF_INET;
	((struct sockaddr_in*)&recv_smr.smr_multiaddr)->sin_port = g_htons (g_port);
	((struct sockaddr_in*)&recv_smr.smr_multiaddr)->sin_addr.s_addr = inet_addr(g_network);
	((struct sockaddr_in*)&recv_smr.smr_interface)->sin_family = AF_INET;
	((struct sockaddr_in*)&recv_smr.smr_interface)->sin_port = g_htons (g_port);
	((struct sockaddr_in*)&recv_smr.smr_interface)->sin_addr.s_addr = INADDR_ANY;

	((struct sockaddr_in*)&send_smr.smr_multiaddr)->sin_family = AF_INET;
	((struct sockaddr_in*)&send_smr.smr_multiaddr)->sin_port = g_htons (g_port);
	((struct sockaddr_in*)&send_smr.smr_multiaddr)->sin_addr.s_addr = inet_addr(g_network);
	((struct sockaddr_in*)&send_smr.smr_interface)->sin_family = AF_INET;
	((struct sockaddr_in*)&send_smr.smr_interface)->sin_port = 0;
	((struct sockaddr_in*)&send_smr.smr_interface)->sin_addr.s_addr = INADDR_ANY;

	e = pgm_transport_create (&g_transport, gsi, &recv_smr, 1, &send_smr);
	g_assert (e == 0);

	pgm_transport_set_max_tpdu (g_transport, g_max_tpdu);
	pgm_transport_set_txw_sqns (g_transport, g_sqns);
	pgm_transport_set_rxw_sqns (g_transport, g_sqns);
	pgm_transport_set_hops (g_transport, 16);
	pgm_transport_set_ambient_spm (g_transport, 8192*1000);
	guint spm_heartbeat[] = { 1*1000, 1*1000, 2*1000, 4*1000, 8*1000, 16*1000, 32*1000, 64*1000, 128*1000, 256*1000, 512*1000, 1024*1000, 2048*1000, 4096*1000, 8192*1000 };
	pgm_transport_set_heartbeat_spm (g_transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat));
	pgm_transport_set_nak_rb_ivl (g_transport, 50*1000);
	pgm_transport_set_nak_rpt_ivl (g_transport, 200*1000);
	pgm_transport_set_nak_rdata_ivl (g_transport, 200*1000);
	pgm_transport_set_nak_data_retries (g_transport, 5);
	pgm_transport_set_nak_ncf_retries (g_transport, 2);

	e = pgm_transport_bind (g_transport);
	if (e != 0) {
		g_critical ("pgm_transport_bind failed errno %i: \"%s\"", e, strerror(e));
		G_BREAKPOINT();
	}

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add(10 * 1000, (GSourceFunc)on_mark, NULL);

	if (g_send_mode)
	{
		g_message ("scheduling ODATA broadcasts every %i ms.", g_odata_interval / 1000);
		g_timeout_add(g_odata_interval / 1000, (GSourceFunc)on_odata_timer, NULL);
	}
	else
	{
		g_message ("adding PGM receiver watch");
		pgm_transport_add_watch (g_transport, on_data, NULL);
	}

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

	e = pgm_write_copy (g_transport, &g_payload, sizeof(g_payload));
        if (e < 0) {
		g_warning ("send failed.");
                return;
        }

	g_payload++;
}

static int
on_data (
	gpointer	data,
	guint		len,
	gpointer	user_data
	)
{
	if (len == sizeof(g_payload))
		g_payload = *(int*)data;
	else
		g_warning ("payload size %i bytes", len);

	return 0;
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
