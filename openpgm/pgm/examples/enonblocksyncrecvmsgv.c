/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Simple PGM receiver: epoll based non-blocking synchronous receiver with scatter/gather io
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
#include <limits.h>
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
#include <sys/uio.h>
#include <arpa/inet.h>

#include <glib.h>

#include <pgm/pgm.h>
#include <pgm/backtrace.h>
#include <pgm/log.h>


/* typedefs */

/* globals */

#ifndef SC_IOV_MAX
#ifdef _SC_IOV_MAX
#	define SC_IOV_MAX	_SC_IOV_MAX
#else
#	error _SC_IOV_MAX undefined too, please fix.
#endif
#endif

static int g_port = 7500;
static const char* g_network = "";
static int g_udp_encap_port = 0;

static int g_max_tpdu = 1500;
static int g_sqns = 100;

static pgm_transport_t* g_transport = NULL;
static gboolean g_quit = FALSE;

static void on_signal (int);
static gboolean on_startup (void);

static int on_msgv (pgm_msgv_t*, guint, gpointer);


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

	on_startup();

/* incoming message buffer */
	long iov_max = sysconf( SC_IOV_MAX );
	g_message ("IOV_MAX defined as %li", iov_max);

	pgm_msgv_t msgv[iov_max];

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

/* dispatch loop */
	g_message ("entering PGM message loop ... ");
	do {
		gssize len = pgm_transport_recvmsgv (g_transport, msgv, iov_max, MSG_DONTWAIT /* non-blocking */);
		if (len >= 0)
		{
			on_msgv (msgv, len, NULL);
		}
		else if (errno == EAGAIN)
		{
/* poll for next event */
			struct epoll_event events[1];	/* wait for maximum 1 event */
			epoll_wait (efd, events, G_N_ELEMENTS(events), 1000 /* ms */);
		}
		else if (errno == ECONNRESET)
		{
			pgm_sock_err_t* pgm_sock_err = (pgm_sock_err_t*)msgv[0].msgv_iov->iov_base;
			g_warning ("pgm socket lost %" G_GUINT32_FORMAT " packets detected from %s",
					pgm_sock_err->lost_count,
					pgm_print_tsi(&pgm_sock_err->tsi));
			continue;
		}
		else if (errno == ENOTCONN)
		{
			g_error ("pgm socket closed.");
		}
		else
		{
			g_error ("pgm socket failed errno %i: \"%s\"", errno, strerror(errno));
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
	g_message ("startup.");
	g_message ("create transport.");

	pgm_gsi_t gsi;
	int e = pgm_create_md5_gsi (&gsi);
	g_assert (e == 0);

	struct group_source_req recv_gsr, send_gsr;
	char network[1024];
	sprintf (network, ";%s", g_network);
	gsize recv_len = 1;
	e = pgm_if_parse_transport (network, AF_INET, &recv_gsr, &recv_len, &send_gsr);
	g_assert (e == 0);
	g_assert (recv_len == 1);

	if (g_udp_encap_port) {
		((struct sockaddr_in*)&send_gsr.gsr_group)->sin_port = g_htons (g_udp_encap_port);
		((struct sockaddr_in*)&recv_gsr.gsr_group)->sin_port = g_htons (g_udp_encap_port);
	}

	e = pgm_transport_create (&g_transport, &gsi, 0, g_port, &recv_gsr, 1, &send_gsr);
	g_assert (e == 0);

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

	e = pgm_transport_bind (g_transport);
	if (e < 0) {
		if      (e == -1)
			g_critical ("pgm_transport_bind failed errno %i: \"%s\"", errno, strerror(errno));
		else if (e == -2)
			g_critical ("pgm_transport_bind failed h_errno %i: \"%s\"", h_errno, hstrerror(h_errno));
		else
			g_critical ("pgm_transport_bind failed e %i", e);
		G_BREAKPOINT();
	}
	g_assert (e == 0);

	g_message ("startup complete.");
	return FALSE;
}

static int
on_msgv (
	pgm_msgv_t*	msgv,		/* an array of msgv's */
	guint		len,
	G_GNUC_UNUSED gpointer user_data
	)
{
	g_message ("(%i bytes)",
			len);

/* protect against non-null terminated strings */

/* for each apdu display each fragment */
	guint i = 0;
	while (len)
	{
		struct iovec* msgv_iov = msgv->msgv_iov;

		guint apdu_len = 0; 
		struct iovec* p = msgv_iov;
		for (guint j = 0; j < msgv->msgv_iovlen; j++) {	/* # elements */
			apdu_len += p->iov_len;
			p++;
		}

/* truncate to first fragment to make GLib printing happy */
		char buf[1024];
		snprintf (buf, sizeof(buf), "%s", (char*)msgv_iov->iov_base);
		if (msgv->msgv_iovlen > 1) {
			g_message ("\t%u: \"%s\" ... (%u bytes)", ++i, buf, apdu_len);
		} else {
			g_message ("\t%u: \"%s\" (%u bytes)", ++i, buf, apdu_len);
		}

		len -= apdu_len;
		msgv++;
	}

	return 0;
}

/* eof */
