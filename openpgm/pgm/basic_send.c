/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Send very basic packets.
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
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <linux/if.h>
#include <arpa/inet.h>

#include <glib.h>

#include "log.h"
#include "pgm.h"


/* globals */

static int g_port = 7500;
static char* g_network = "226.0.0.1";
static struct ip_mreqn g_mreqn;

static char* g_payload = "banana man!";

/* MTU is messy, 1500 for regular ethernet, 9000 for jumbo but various jumbo
 * sizes in between are only supported by some hardware, similarly some IP
 * options can reduce the maxium size, e.g. LLC/SNAP or VPN encapsulation.
 * 1492 is good for ATM.
 */
static int g_mtu = 1500;
#define g_tpdu_limit  (g_mtu - sizeof(struct iphdr))

/* TSDU limit is more complicated as PGM options reduce the available size
 */
#define g_tsdu_limit  (g_tpdu_limit - sizeof(struct pgm_header) - sizeof(struct pgm_data))

static int g_io_channel_sock = -1;
static GIOChannel* g_io_channel = NULL;
static GMainLoop* g_loop = NULL;


void banana_man (void);

static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);


static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options] message...\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	puts ("basic_send");

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:h")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	if (optind < argc) {
		g_payload = argv[optind];
		printf ("payload \"%s\"\n", g_payload);
	}

	if (strlen(g_payload) > g_tsdu_limit) {
		printf ("WARNING: payload string is longer than the interface MTU will allow.\n");
	}

	log_init ();

/* setup signal handlers */
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, SIG_IGN);

/* delayed startup */
	puts ("scheduling startup.");
	g_timeout_add(0, (GSourceFunc)on_startup, NULL);

/* dispatch loop */
	g_loop = g_main_loop_new(NULL, FALSE);

	puts ("entering main event loop ... ");
	g_main_loop_run(g_loop);

	puts ("event loop terminated, cleaning up.");

/* cleanup */
	g_main_loop_unref(g_loop);
	g_loop = NULL;

	if (g_io_channel) {
		puts ("closing io channel.");

		GError *err = NULL;
		g_io_channel_shutdown (g_io_channel, FALSE, &err);
		g_io_channel = NULL;
	}

	if (g_io_channel_sock) {
		puts ("closing socket.");

		close(g_io_channel_sock);
		g_io_channel_sock = -1;
	}

	puts ("finished.");
	return 0;
}

static void
on_signal (
	int	signum
	)
{
	puts ("on_signal");

	g_main_loop_quit(g_loop);
}

static gboolean
on_startup (
	gpointer data
	)
{
	int e;

	puts ("startup.");

/* find PGM protocol id */
// TODO: fix valgrind errors
	int ipproto_pgm = IPPROTO_PGM;
#if HAVE_GETPROTOBYNAME_R
	char b[1024];
	struct protoent protobuf, *proto;
	e = getprotobyname_r("pgm", &protobuf, b, sizeof(b), &proto);
	if (e != -1 && proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			printf("Setting PGM protocol number to %i from /etc/protocols.\n");
			ipproto_pgm = proto->p_proto;
		}
	}
#else
	struct protoent *proto = getprotobyname("pgm");
	if (proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			printf("Setting PGM protocol number to %i from /etc/protocols.\n", proto->p_proto);
			ipproto_pgm = proto->p_proto;
		}
	}
#endif

/* open socket for snooping */
	puts ("opening raw socket.");
	g_io_channel_sock = socket(PF_INET, SOCK_RAW, ipproto_pgm);
	if (g_io_channel_sock < 0) {
		int _e = errno;
		perror("on_startup() failed");

		if (_e == EPERM && 0 != getuid()) {
			puts ("PGM protocol requires this program to run as superuser.");
		}
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* drop out of setuid 0 */
	if (0 == getuid()) {
		puts ("dropping superuser privileges.");
		setuid((gid_t)65534);
		setgid((uid_t)65534);
	}

	char _t = 0;
	e = setsockopt(g_io_channel_sock, IPPROTO_IP, IP_HDRINCL, &_t, sizeof(_t));
	if (e < 0) {
		perror("on_startup() failed");
		close(g_io_channel_sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* buffers */
	int buffer_size = 0;
	socklen_t len = 0;
	e = getsockopt(g_io_channel_sock, SOL_SOCKET, SO_RCVBUF, &buffer_size, &len);
	if (e == 0) {
		printf ("receive buffer set at %i bytes.\n", buffer_size);
	}
	e = getsockopt(g_io_channel_sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, &len);
	if (e == 0) {
		printf ("send buffer set at %i bytes.\n", buffer_size);
	}

/* bind */
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(g_port);

	e = bind(g_io_channel_sock, (struct sockaddr*)&addr, sizeof(addr));
	if (e < 0) {
		perror("on_startup() failed");
		close(g_io_channel_sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* query bound socket for actual interface address */
	char hostname[NI_MAXHOST + 1];
	gethostname (hostname, sizeof(hostname));
	struct hostent *he = gethostbyname (hostname);

	if (he == NULL) {
		printf ("error code %i\n", errno);
		perror("on_startup() failed");
		close(g_io_channel_sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

	struct in_addr g_addr;
	g_addr.s_addr = ((struct in_addr*)(he->h_addr_list[0]))->s_addr;
	printf ("socket bound to %s (%s)\n", inet_ntoa(g_addr), hostname);

/* check for MTU */
	struct ifreq ifreq;
	e = ioctl (g_io_channel_sock, SIOCGIFMTU, &ifreq);
	if (e < 0) {
		perror("Cannot determine interface MTU");
	} else {
		printf ("interface MTU %i bytes.\n", ifreq.ifr_mtu);
	}

/* multicast */
	memset(&g_mreqn, 0, sizeof(g_mreqn));
	g_mreqn.imr_address.s_addr = htonl(INADDR_ANY);
	printf ("sending on interface %s.\n", inet_ntoa(g_mreqn.imr_address));
	g_mreqn.imr_multiaddr.s_addr = inet_addr(g_network);
	printf ("sending on multicast address %s.\n", inet_ntoa(g_mreqn.imr_multiaddr));

/* IP_ADD_MEMBERSHIP = subscription
 * IP_MULTICAST_IF = send only
 */
	e = setsockopt(g_io_channel_sock, IPPROTO_IP, IP_MULTICAST_IF, &g_mreqn, sizeof(g_mreqn));
//	e = setsockopt(g_io_channel_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &g_mreqn, sizeof(g_mreqn));
	if (e < 0) {
		perror("on_startup() failed");
		close(g_io_channel_sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* multicast loopback */
	gboolean n = 0;
	e = setsockopt(g_io_channel_sock, IPPROTO_IP, IP_MULTICAST_LOOP, &n, sizeof(n));
	if (e < 0) {
		perror("on_startup() failed");
		close(g_io_channel_sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* multicast ttl */
	int ttl = 1;
	e = setsockopt(g_io_channel_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	if (e < 0) {
		perror("on_startup() failed");
		close(g_io_channel_sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add(10 * 1000, (GSourceFunc)on_mark, NULL);

	puts ("startup complete.");
	banana_man();

	return FALSE;
}

static gboolean
on_shutdown (
	gpointer data
	)
{
	puts ("on_shutdown()");

	g_main_loop_quit (g_loop);
	return FALSE;
}


/***************************************************************
 * banana man!
 */

void
banana_man (
	void
	)
{
	puts ("banana man.");

	int e;

/* construct PGM packet */
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + strlen(g_payload) + 1;
	gchar *buf = (gchar*)malloc( tpdu_length );
	if (buf == NULL) {
		perror ("oh crap.");
		return;
	}

printf ("PGM header size %lu\n"
	"PGM data header size %lu\n"
	"payload size %lu\n",
	sizeof(struct pgm_header),
	sizeof(struct pgm_data),
	strlen(g_payload) + 1);

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_data *odata = (struct pgm_data*)(header + 1);

	header->pgm_sport	= g_htons (g_port);
	header->pgm_dport	= g_htons (g_port);
	header->pgm_type	= PGM_ODATA;
	header->pgm_options	= 0;
	header->pgm_checksum	= 0;

	header->pgm_gsi[0]	= 1;
	header->pgm_gsi[1]	= 2;
	header->pgm_gsi[2]	= 3;
	header->pgm_gsi[3]	= 4;
	header->pgm_gsi[4]	= 5;
	header->pgm_gsi[5]	= 6;

	header->pgm_tsdu_length	= g_htons (strlen(g_payload) + 1);		/* transport data unit length */

/* ODATA */
	odata->data_sqn		= 0;
	odata->data_trail	= 0;

	memcpy (odata + 1, g_payload, strlen(g_payload) + 1);

	header->pgm_checksum = pgm_cksum(buf, tpdu_length, 0);

	int flags = MSG_CONFIRM;	/* not expecting a reply */

/* IP header handled by sendto() */
	struct sockaddr_in mc;
	mc.sin_family		= AF_INET;
	mc.sin_addr.s_addr	= g_mreqn.imr_multiaddr.s_addr;
	mc.sin_port		= 0;

	printf("TPDU %i bytes.\n", tpdu_length);
	e = sendto (g_io_channel_sock,
		buf,
		tpdu_length,
		flags,
		(struct sockaddr*)&mc,	/* to address */
		sizeof(mc));		/* address size */
	if (e == EMSGSIZE) {
		perror ("message too large for ip stack.");
		return;
	}
	if (e < 0) {
		perror ("sendto() failed.");
		return;
	}

	puts ("sent.");

	puts ("schedule shutdown in 1000ms.");
	g_timeout_add(1000, (GSourceFunc)on_shutdown, NULL);
}

static gboolean
on_mark (
	gpointer data
	)
{
	static struct timeval tv;
	gettimeofday(&tv, NULL);
	printf ("%s on_mark.\n", ts_format((tv.tv_sec + g_timezone) % 86400, tv.tv_usec));

	return TRUE;
}

/* eof */
