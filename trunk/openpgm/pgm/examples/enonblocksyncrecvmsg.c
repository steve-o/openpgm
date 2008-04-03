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

#include <pgm/if.h>
#include <pgm/backtrace.h>
#include <pgm/log.h>
#include <pgm/transport.h>
#include <pgm/gsi.h>


/* typedefs */

/* globals */

static int g_port = 7500;
static char* g_network = "";
static int g_udp_encap_port = 0;

static int g_max_tpdu = 1500;
static int g_sqns = 10;

static pgm_transport_t* g_transport = NULL;
static gboolean g_quit = FALSE;

static void on_signal (int);
static gboolean on_startup (void);

static int on_datav (pgm_msgv_t*, guint, gpointer);


static void
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

	on_startup();

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
		int len = pgm_transport_recvmsg (g_transport, &msgv, MSG_DONTWAIT /* non-blocking */);
		if (len > 0)
		{
			on_datav (&msgv, len, NULL);
		}
		else
		{
/* poll for next event */
			struct epoll_event events[1];	/* wait for maximum 1 event */
			epoll_wait (efd, events, G_N_ELEMENTS(events), 1000 /* ms */);
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
	int		signum
	)
{
	g_message ("on_signal");

	g_quit = TRUE;
}

static gboolean
on_startup (void)
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
#if 0
	((struct sockaddr_in*)&recv_smr.smr_multiaddr)->sin_family = AF_INET;
	((struct sockaddr_in*)&recv_smr.smr_multiaddr)->sin_addr.s_addr = inet_addr(g_network);
	((struct sockaddr_in*)&recv_smr.smr_interface)->sin_family = AF_INET;
	((struct sockaddr_in*)&recv_smr.smr_interface)->sin_addr.s_addr = INADDR_ANY;

	((struct sockaddr_in*)&send_smr.smr_multiaddr)->sin_family = AF_INET;
	((struct sockaddr_in*)&send_smr.smr_multiaddr)->sin_addr.s_addr = inet_addr(g_network);
	((struct sockaddr_in*)&send_smr.smr_interface)->sin_family = AF_INET;
	((struct sockaddr_in*)&send_smr.smr_interface)->sin_addr.s_addr = INADDR_ANY;
#else
	char network[1024];
	sprintf (network, ";%s", g_network);
	int smr_len = 1;
	e = pgm_if_parse_transport (network, AF_INET, &recv_smr, &send_smr, &smr_len);
	g_assert (e == 0);
	g_assert (smr_len == 1);
#endif

	if (g_udp_encap_port) {
		((struct sockaddr_in*)&send_smr.smr_multiaddr)->sin_port = g_htons (g_udp_encap_port);
		((struct sockaddr_in*)&recv_smr.smr_interface)->sin_port = g_htons (g_udp_encap_port);
	}

	e = pgm_transport_create (&g_transport, &gsi, g_port, &recv_smr, 1, &send_smr);
	g_assert (e == 0);

	pgm_transport_set_max_tpdu (g_transport, g_max_tpdu);
	pgm_transport_set_txw_sqns (g_transport, g_sqns);
	pgm_transport_set_rxw_sqns (g_transport, g_sqns);
	pgm_transport_set_hops (g_transport, 16);
	pgm_transport_set_ambient_spm (g_transport, 8192*1000);
	guint spm_heartbeat[] = { 1*1000, 1*1000, 2*1000, 4*1000, 8*1000, 16*1000, 32*1000, 64*1000, 128*1000, 256*1000, 512*1000, 1024*1000, 2048*1000, 4096*1000, 8192*1000 };
	pgm_transport_set_heartbeat_spm (g_transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat));
	pgm_transport_set_peer_expiry (g_transport, 5*8192*1000);
	pgm_transport_set_spmr_expiry (g_transport, 250*1000);
	pgm_transport_set_nak_bo_ivl (g_transport, 50*1000);
	pgm_transport_set_nak_rpt_ivl (g_transport, 200*1000);
	pgm_transport_set_nak_rdata_ivl (g_transport, 200*1000);
	pgm_transport_set_nak_data_retries (g_transport, 5);
	pgm_transport_set_nak_ncf_retries (g_transport, 2);

	e = pgm_transport_bind (g_transport);
	if (e != 0) {
		g_critical ("pgm_transport_bind failed errno %i: \"%s\"", e, strerror(e));
		G_BREAKPOINT();
	}

	g_message ("startup complete.");
	return FALSE;
}

static int
on_datav (
	pgm_msgv_t*	datav,
	guint		len,
	gpointer	user_data
	)
{
	static struct timeval tv;
	gettimeofday(&tv, NULL);

	g_message ("%s: (%i bytes)",
			ts_format((tv.tv_sec + g_timezone) % 86400, tv.tv_usec),
			len);

/* protect against non-null terminated strings */
	struct iovec* msgv_iov = datav->msgv_iov;
	int i = 0;
	while (len)
	{
		char buf[1024];
		snprintf (buf, sizeof(buf), "%s", (char*)msgv_iov->iov_base);
		g_message ("\t%i: %s (%" G_GSIZE_FORMAT " bytes)", ++i, buf, msgv_iov->iov_len);
		len -= msgv_iov->iov_len;
		msgv_iov++;
	}

	return 0;
}

/* eof */
