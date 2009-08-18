/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Simple PGM receiver: blocking synchronous receiver with scatter/gather io
 *
 * Copyright (c) 2006-2008 Miru Limited.
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
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <glib.h>

#include <pgm/pgm.h>
#include <pgm/backtrace.h>
#include <pgm/log.h>


/* typedefs */

/* globals */

static int g_port = 7500;
static const char* g_network = "";
static int g_udp_encap_port = 0;

static int g_max_tpdu = 1500;
static int g_sqns = 100;

static pgm_transport_t* g_transport = NULL;
static gboolean g_quit = FALSE;

static void on_signal (int);
static gboolean on_startup (void);

static int on_datav (pgm_msgv_t*, guint, gpointer);


G_GNUC_NORETURN static void
usage (
	const char*	bin
	)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	exit (1);
}

int
main (
	int		argc,
	char*		argv[]
	)
{
	g_message ("syncrecv");

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:p:h")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;
		case 'p':	g_udp_encap_port = atoi (optarg); break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	log_init ();
	pgm_init ();

/* setup signal handlers */
	signal(SIGSEGV, on_sigsegv);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, SIG_IGN);

	if (!on_startup()) {
		g_error ("startup failed");
		exit(1);
	}

/* epoll file descriptor */
	int efd = epoll_create (IP_MAX_MEMBERSHIPS);
	if (efd < 0) {
		g_error ("epoll_create failed errno %i: \"%s\"", errno, strerror(errno));
		exit(1);
	}

	int retval = pgm_transport_epoll_ctl (g_transport, efd, EPOLL_CTL_ADD, EPOLLIN);
	if (retval < 0) {
		g_error ("pgm_epoll_ctl failed.");
		exit(1);
	}

/* incoming message buffer */
	pgm_msgv_t msgv;

/* dispatch loop */
	g_message ("entering PGM message loop ... ");
	do {
		gsize len;
		GError* err = NULL;
		const GIOStatus status = pgm_recvmsg (g_transport,
						      &msgv,
						      MSG_DONTWAIT /* non-blocking */,
						      &len,
						      &err);
		if (G_IO_STATUS_NORMAL == status)
			on_datav (&msgv, len, NULL);
		else if (G_IO_STATUS_AGAIN == status) {
/* poll for next event */
			struct epoll_event events[1];	/* wait for maximum 1 event */
			epoll_wait (efd, events, G_N_ELEMENTS(events), 1000 /* ms */);
		} else {
			if (err) {
				g_warning (err->message);
				g_error_free (err);
			}
			if (G_IO_STATUS_ERROR == status)
				break;
		}
	} while (!g_quit);

	g_message ("message loop terminated, cleaning up.");

/* cleanup */
	close (efd);

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
	G_GNUC_UNUSED int signum
	)
{
	g_message ("on_signal");

	g_quit = TRUE;
}

static gboolean
on_startup (void)
{
	struct pgm_transport_info_t* res = NULL;
	GError* err = NULL;

	g_message ("startup.");
	g_message ("create transport.");

/* parse network parameter into transport address structure */
	char network[1024];
	sprintf (network, "%s", g_network);
	if (!pgm_if_get_transport_info (network, NULL, &res, &err)) {
		g_error ("parsing network parameter: %s", err->message);
		g_error_free (err);
		return FALSE;
	}
/* create global session identifier */
	if (!pgm_gsi_create_from_hostname (&res->ti_gsi, &err)) {
		g_error ("creating GSI: %s", err->message);
		g_error_free (err);
		pgm_if_free_transport_info (res);
		return FALSE;
	}
	if (g_udp_encap_port) {
		res->ti_udp_encap_ucast_port = g_udp_encap_port;
		res->ti_udp_encap_mcast_port = g_udp_encap_port;
	}
	if (!pgm_transport_create (&g_transport, res, &err)) {
		g_error ("creating transport: %s", err->message);
		g_error_free (err);
		pgm_if_free_transport_info (res);
		return FALSE;
	}
	pgm_if_free_transport_info (res);

/* set PGM parameters */
	pgm_transport_set_recv_only (g_transport, FALSE);
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
	if (!pgm_transport_bind (g_transport, &err)) {
		g_error ("binding transport: %s", err->message);
		g_error_free (err);
		pgm_transport_destroy (g_transport, FALSE);
		g_transport = NULL;
		return FALSE;
	}

	g_message ("startup complete.");
	return TRUE;
}

static int
on_datav (
	pgm_msgv_t*	datav,			/* one msgv object */
	guint		len,
	G_GNUC_UNUSED gpointer user_data
	)
{
	char tsi[PGM_TSISTRLEN];
	pgm_tsi_print_r (&datav->msgv_skb[0]->tsi, tsi, sizeof(tsi));
	g_message ("(%i bytes from %s)", len, tsi);

/* protect against non-null terminated strings */
	struct pgm_sk_buff_t* skb = datav->msgv_skb[0];
	int i = 0;
	while (len)
	{
		char buf[1024];
		snprintf (buf, sizeof(buf), "%s", (char*)skb->data);
		g_message ("\t%i: %s (%" G_GUINT16_FORMAT " bytes)", ++i, buf, skb->len);
		len -= skb->len;
		skb++;
	}

	return 0;
}

/* eof */
