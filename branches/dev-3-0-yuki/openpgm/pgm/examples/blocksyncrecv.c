/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Simple PGM receiver: blocking synchronous receiver 
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
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include <glib.h>

#ifdef G_OS_UNIX
#	include <netdb.h>
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#endif

#include <pgm/pgm.h>
#include <pgm/backtrace.h>
#include <pgm/log.h>


/* typedefs */

/* globals */

static int g_port = 0;
static const char* g_network = "";
static gboolean g_multicast_loop = FALSE;
static int g_udp_encap_port = 0;

static int g_max_tpdu = 1500;
static int g_sqns = 100;

static pgm_transport_t* g_transport = NULL;
static gboolean g_quit = FALSE;

#ifdef G_OS_UNIX
static void on_signal (int);
#else
static BOOL on_console_ctrl (DWORD);
#endif

static gboolean on_startup (void);

static int on_data (gpointer, guint, pgm_tsi_t*);


G_GNUC_NORETURN static void
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
#endif

	on_startup();

/* dispatch loop */
	g_message ("entering PGM message loop ... ");
	do {
		pgm_tsi_t from;
		char buffer[4096];
		gsize len;
		const pgm_io_status_e status = pgm_recvfrom (g_transport,
						         buffer,
						         sizeof(buffer),
						         0,
						         &len,
						         &from,
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
	if (g_transport) {
		g_message ("destroying transport.");
		pgm_transport_destroy (g_transport, TRUE);
		g_transport = NULL;
	}

	g_message ("PGM engine shutdown.");
	pgm_shutdown ();
	g_message ("finished.");
	return EXIT_SUCCESS;
}

#ifdef G_OS_UNIX
static void
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

static gboolean
on_startup (void)
{
	struct pgm_transport_info_t* res = NULL;
	pgm_error_t* pgm_err = NULL;

	g_message ("startup.");
	g_message ("create transport.");

/* parse network parameter into transport address structure */
	char network[1024];
	sprintf (network, "%s", g_network);
	if (!pgm_if_get_transport_info (network, NULL, &res, &pgm_err)) {
		g_error ("parsing network parameter: %s", pgm_err->message);
		pgm_error_free (pgm_err);
		return FALSE;
	}
/* create global session identifier */
	if (!pgm_gsi_create_from_hostname (&res->ti_gsi, &pgm_err)) {
		g_error ("creating GSI: %s", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_if_free_transport_info (res);
		return FALSE;
	}
	if (g_udp_encap_port) {
		res->ti_udp_encap_ucast_port = g_udp_encap_port;
		res->ti_udp_encap_mcast_port = g_udp_encap_port;
	}
	if (g_port)
		res->ti_dport = g_port;
	if (!pgm_transport_create (&g_transport, res, &pgm_err)) {
		g_error ("creating transport: %s", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_if_free_transport_info (res);
		return FALSE;
	}
	pgm_if_free_transport_info (res);

/* set PGM parameters */
	pgm_transport_set_nonblocking (g_transport, FALSE);	/* blocking */
	pgm_transport_set_recv_only (g_transport, TRUE, FALSE);
	pgm_transport_set_max_tpdu (g_transport, g_max_tpdu);
	pgm_transport_set_rxw_sqns (g_transport, g_sqns);
	pgm_transport_set_hops (g_transport, 16);
	pgm_transport_set_peer_expiry (g_transport, pgm_secs(300));
	pgm_transport_set_spmr_expiry (g_transport, pgm_msecs(250));
	pgm_transport_set_nak_bo_ivl (g_transport, pgm_msecs(50));
	pgm_transport_set_nak_rpt_ivl (g_transport, pgm_secs(2));
	pgm_transport_set_nak_rdata_ivl (g_transport, pgm_secs(2));
	pgm_transport_set_nak_data_retries (g_transport, 50);
	pgm_transport_set_nak_ncf_retries (g_transport, 50);

/* assign transport to specified address */
	if (!pgm_transport_bind (g_transport, &pgm_err)) {
		g_error ("binding transport: %s", pgm_err->message);
		pgm_error_free (pgm_err);
		pgm_transport_destroy (g_transport, FALSE);
		g_transport = NULL;
		return FALSE;
	}

	g_message ("startup complete.");
	return FALSE;
}

static int
on_data (
	gpointer	data,
	guint		len,
	pgm_tsi_t*      from
	)
{
/* protect against non-null terminated strings */
	char buf[1024], tsi[PGM_TSISTRLEN];
	snprintf (buf, sizeof(buf), "%s", (char*)data);
	pgm_tsi_print_r (from, tsi, sizeof(tsi));

	g_message ("\"%s\" (%i bytes from %s)",
			buf,
			len,
			tsi);

	return 0;
}

/* eof */
