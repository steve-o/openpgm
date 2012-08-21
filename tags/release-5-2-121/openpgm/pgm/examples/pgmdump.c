/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Dump PGM packets to the console similar to tcpdump, but not as good.
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
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <glib.h>
#ifdef G_OS_UNIX
#	include <netdb.h>
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#endif

#include <pgm/pgm.h>

#define GETTEXT_PACKAGE		"pgmdump"

/* PGM internals */
#include <impl/packet_test.h>

/* example dependencies */
#include <pgm/backtrace.h>
#include <pgm/log.h>


/* globals */

static const char* g_network = "239.192.0.1";

static GIOChannel* g_io_channel = NULL;
static GMainLoop* g_loop = NULL;


static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static gboolean on_io_data (GIOChannel*, GIOCondition, gpointer);


int
main (
	G_GNUC_UNUSED int	argc,
	G_GNUC_UNUSED char   *argv[]
	)
{
	GError* err = NULL;

	setlocale (LC_ALL, "");

	log_init ();
	g_message ("pgmdump");

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
	signal (SIGINT, on_signal);
	signal (SIGTERM, on_signal);
#ifdef SIGHUP
	signal (SIGHUP, SIG_IGN);
#endif

/* delayed startup */
	g_message ("scheduling startup.");
	g_timeout_add (0, (GSourceFunc)on_startup, NULL);

/* dispatch loop */
	g_loop = g_main_loop_new (NULL, FALSE);

	g_message ("entering main event loop ... ");
	g_main_loop_run (g_loop);

	g_message ("event loop terminated, cleaning up.");

/* cleanup */
	g_main_loop_unref (g_loop);
	g_loop = NULL;

	if (g_io_channel) {
		g_message ("closing socket.");
		g_io_channel_shutdown (g_io_channel, FALSE, &err);
		g_io_channel = NULL;
	}

	g_message ("finished.");
	return EXIT_SUCCESS;
}

static void
on_signal (
	G_GNUC_UNUSED int signum
	)
{
	puts ("on_signal");

	g_main_loop_quit(g_loop);
}

static
gboolean
on_startup (
	G_GNUC_UNUSED gpointer data
	)
{
	int e;

	g_message ("startup.");

/* find PGM protocol id */
// TODO: fix valgrind errors
	int ipproto_pgm = IPPROTO_PGM;
	struct protoent *proto = getprotobyname ("pgm");
	if (proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			g_message ("Setting PGM protocol number to %i from /etc/protocols.\n", proto
->p_proto);
			ipproto_pgm = proto->p_proto;
		}
	}

/* open socket for snooping */
	g_message ("opening raw socket.");
	int sock = socket (PF_INET, SOCK_RAW, ipproto_pgm);
	if (sock < 0) {
		perror("on_startup() failed");
#ifdef G_OS_UNIX
		if (EPERM == errno && 0 != getuid()) {
			g_message ("PGM protocol requires this program to run as superuser.");
		}
#endif
		g_main_loop_quit (g_loop);
		return FALSE;
	}

	int _t = 1;
	e = setsockopt (sock, IPPROTO_IP, IP_HDRINCL, (const char*)&_t, sizeof(_t));
	if (e < 0) {
		perror ("on_startup() failed");
		close (sock);
		g_main_loop_quit (g_loop);
		return FALSE;
	}

#ifdef G_OS_UNIX
/* drop out of setuid 0 */
	if (0 == getuid ()) {
		g_message ("dropping superuser privileges.");
		setuid ((gid_t)65534);
		setgid ((uid_t)65534);
	}
#endif

/* buffers */
	int buffer_size = 0;
	socklen_t len = 0;
	e = getsockopt (sock, SOL_SOCKET, SO_RCVBUF, (char*)&buffer_size, &len);
	if (e == 0) {
		g_message ("receive buffer set at %i bytes.\n", buffer_size);
	}
	e = getsockopt (sock, SOL_SOCKET, SO_SNDBUF, (char*)&buffer_size, &len);
	if (e == 0) {
		g_message ("send buffer set at %i bytes.\n", buffer_size);
	}

/* bind */
	struct sockaddr_in addr;
	memset (&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	e = bind (sock, (struct sockaddr*)&addr, sizeof(addr));
	if (e < 0) {
		perror ("on_startup() failed");
		close (sock);
		g_main_loop_quit (g_loop);
		return FALSE;
	}

/* multicast */
	struct ip_mreq mreq;
	memset (&mreq, 0, sizeof(mreq));
	mreq.imr_interface.s_addr = htonl (INADDR_ANY);
	g_message ("listening on interface %s.\n", inet_ntoa (mreq.imr_interface));
	mreq.imr_multiaddr.s_addr = inet_addr (g_network);
	g_message ("subscription on multicast address %s.\n", inet_ntoa (mreq.imr_multiaddr));
	e = setsockopt (sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
	if (e < 0) {
		perror ("on_startup() failed");
		close (sock);
		g_main_loop_quit (g_loop);
		return FALSE;
	}

/* multicast loopback */
/* multicast ttl */

/* add socket to event manager */
	g_io_channel = g_io_channel_unix_new (sock);
	g_message ("socket opened with encoding %s.\n", g_io_channel_get_encoding (g_io_channel));

	/* guint event = */ g_io_add_watch (g_io_channel, G_IO_IN | G_IO_PRI, on_io_data, NULL);

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add (10 * 1000, (GSourceFunc)on_mark, NULL);

	g_message ("startup complete.");
	return FALSE;
}

static gboolean
on_mark (
	G_GNUC_UNUSED gpointer data
	)
{
	g_message ("-- MARK --");
	return TRUE;
}

static gboolean
on_io_data (
	GIOChannel* source,
	G_GNUC_UNUSED GIOCondition condition,
	G_GNUC_UNUSED gpointer data
	)
{
	char buffer[4096];

	int fd = g_io_channel_unix_get_fd (source);
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	int len = recvfrom (fd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr*)&addr, &addr_len);

	g_message ("%i bytes received from %s.\n", len, inet_ntoa (addr.sin_addr));

	if (!pgm_print_packet (buffer, len)) {
		g_message ("invalid packet :(");
	}

	fflush (stdout);

	return TRUE;
}

/* eof */
