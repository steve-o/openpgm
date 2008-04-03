/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Sit periodically sending ODATA with interleaved ambient SPM's.
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
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <glib.h>

#include "pgm/backtrace.h"
#include "pgm/log.h"
#include "pgm/packet.h"
#include "pgm/checksum.h"


/* globals */

static int g_port = 7500;
static char* g_network = "226.0.0.1";
static struct ip_mreqn g_mreqn;

static int g_spm_ambient_interval = 10 * 1000;
static int g_odata_interval = 1 * 1000;

static int g_payload = 0;
static int g_corruption = 0;

static int g_txw_lead = 0;
static int g_spm_sqn = 0;

static int g_io_channel_sock = -1;
static struct in_addr g_addr;
static GIOChannel* g_io_channel = NULL;
static GMainLoop* g_loop = NULL;


static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static void send_spm (void);
static void send_odata (void);
static gboolean on_spm_timer (gpointer);
static gboolean on_odata_timer (gpointer);


static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
	fprintf (stderr, "  -c <percent>    : Percentage of packets to corrupt.\n");
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	puts ("stream_send");

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "s:n:c:h")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;
		case 'c':	g_corruption = atoi (optarg); break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	log_init ();

/* setup signal handlers */
	signal(SIGSEGV, on_sigsegv);
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
			printf("Setting PGM protocol number to %i from /etc/protocols.\n", proto
->p_proto);
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

#if 0
	struct in_addr *he_addr = he->h_addr_list[0];
	g_addr.s_addr = he_addr->s_addr;
#else
	g_addr.s_addr = ((struct in_addr*)(he->h_addr_list[0]))->s_addr;
#endif
	printf ("socket bound to %s (%s)\n", inet_ntoa(g_addr), hostname);

/* multicast */
	memset(&g_mreqn, 0, sizeof(g_mreqn));
	g_mreqn.imr_address.s_addr = htonl(INADDR_ANY);
	printf ("sending on interface %s.\n", inet_ntoa(g_mreqn.imr_address));
	g_mreqn.imr_multiaddr.s_addr = inet_addr(g_network);
	if (IN_MULTICAST(g_htonl(g_mreqn.imr_multiaddr.s_addr)))
		printf ("sending on multicast address %s.\n", inet_ntoa(g_mreqn.imr_multiaddr));
	else
		printf ("sending unicast to address %s.\n", inet_ntoa(g_mreqn.imr_multiaddr));

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

	printf ("scheduling ODATA broadcasts every %i secs.\n", g_odata_interval);
	g_timeout_add(g_odata_interval, (GSourceFunc)on_odata_timer, NULL);

	printf ("scheduling SPM ambient broadcasts every %i secs.\n", g_spm_ambient_interval);
	g_timeout_add(g_spm_ambient_interval, (GSourceFunc)on_spm_timer, NULL);

	puts ("startup complete.");
	return FALSE;
}

/* ambient/heartbeat SPM's
 *
 * heartbeat: ihb_tmr decaying between ihb_min and ihb_max 2x after last packet
 *
 * ambient interval: 30s
 * hearbeat intervals: 0.4, 1.3, 7.0, 16.0, 25.0, 30.0
 */

static gboolean
on_spm_timer (
	gpointer data
	)
{
	send_spm ();
	return TRUE;
}

static void
send_spm (
	void
	)
{
	puts ("send_spm.");

	int e;

/* construct PGM packet */
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_spm);
	gchar *buf = (gchar*)malloc( tpdu_length );
	if (buf == NULL) {
		perror ("oh crap.");
		return;
	}

printf ("PGM header size %" G_GSIZE_FORMAT "\n"
	"PGM SPM block size %" G_GSIZE_FORMAT "\n",
	sizeof(struct pgm_header),
	sizeof(struct pgm_spm));

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_spm *spm = (struct pgm_spm*)(header + 1);

	header->pgm_sport	= g_htons (g_port);
	header->pgm_dport	= g_htons (g_port);
	header->pgm_type	= PGM_SPM;
	header->pgm_options	= 0;
	header->pgm_checksum	= 0;

	header->pgm_gsi[0]	= 1;
	header->pgm_gsi[1]	= 2;
	header->pgm_gsi[2]	= 3;
	header->pgm_gsi[3]	= 4;
	header->pgm_gsi[4]	= 5;
	header->pgm_gsi[5]	= 6;

	header->pgm_tsdu_length	= 0;		/* transport data unit length */

/* SPM */
	spm->spm_sqn		= g_htonl (g_spm_sqn); g_spm_sqn++;
	spm->spm_trail		= g_htonl (g_txw_lead);
	spm->spm_lead		= g_htonl (g_txw_lead);
	spm->spm_nla_afi	= g_htons (AFI_IP);
	spm->spm_reserved	= 0;

	spm->spm_nla.s_addr	= g_addr.s_addr;	/* IPv4 */
//	((struct in_addr*)(spm + 1))->s_addr = g_addr.s_addr;

	header->pgm_checksum = pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

/* corrupt packet */
	if (g_corruption && g_random_int_range (0, 100) < g_corruption)
	{
		puts ("corrupting packet.");
		*(buf + g_random_int_range (0, tpdu_length)) = 0;
	}

/* send packet */
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
		(struct sockaddr*)&mc,		/* to address */
		sizeof(mc));	/* address size */
	if (e == EMSGSIZE) {
		perror ("message too large for ip stack.");
		return;
	}
	if (e < 0) {
		perror ("sendto() failed.");
		return;
	}

	puts ("sent.");
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
	puts ("send_data.");

	int e;
	char payload_string[100];

	snprintf (payload_string, sizeof(payload_string), "%i", g_payload++);

/* construct PGM packet */
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + strlen(payload_string) + 1;
	gchar *buf = (gchar*)malloc( tpdu_length );
	if (buf == NULL) {
		perror ("oh crap.");
		return;
	}

printf ("PGM header size %" G_GSIZE_FORMAT "\n"
	"PGM data header size %" G_GSIZE_FORMAT "\n"
	"payload size %" G_GSIZE_FORMAT "\n",
	sizeof(struct pgm_header),
	sizeof(struct pgm_data),
	strlen(payload_string) + 1);

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_data *odata = (struct pgm_data*)(header + 1);

	header->pgm_sport       = g_htons (g_port);
	header->pgm_dport       = g_htons (g_port);
	header->pgm_type        = PGM_ODATA;
        header->pgm_options     = 0;
        header->pgm_checksum    = 0;

        header->pgm_gsi[0]      = 1;
        header->pgm_gsi[1]      = 2;
        header->pgm_gsi[2]      = 3;
        header->pgm_gsi[3]      = 4;
        header->pgm_gsi[4]      = 5;
        header->pgm_gsi[5]      = 6;

        header->pgm_tsdu_length = g_htons (strlen(payload_string) + 1);               /* transport data unit length */

/* ODATA */
        odata->data_sqn         = g_htonl (g_txw_lead);
        odata->data_trail       = g_htonl (g_txw_lead); g_txw_lead++;

        memcpy (odata + 1, payload_string, strlen(payload_string) + 1);

        header->pgm_checksum = pgm_csum_fold (pgm_csum_partial (buf, tpdu_length, 0));

/* corrupt packet */
	if (g_corruption && g_random_int_range (0, 100) < g_corruption)
	{
		puts ("corrupting packet.");
		*(buf + g_random_int_range (0, tpdu_length)) = 0;
	}

/* send packet */
        int flags = MSG_CONFIRM;        /* not expecting a reply */

/* IP header handled by sendto() */
        struct sockaddr_in mc;
        mc.sin_family           = AF_INET;
        mc.sin_addr.s_addr      = g_mreqn.imr_multiaddr.s_addr;
        mc.sin_port             = 0;

        printf("TPDU %i bytes.\n", tpdu_length);
        e = sendto (g_io_channel_sock,
                buf,
                tpdu_length,
                flags,
                (struct sockaddr*)&mc,  /* to address */
                sizeof(mc));            /* address size */
        if (e == EMSGSIZE) {
                perror ("message too large for ip stack.");
                return;
        }
        if (e < 0) {
                perror ("sendto() failed.");
                return;
        }

        puts ("sent.");
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
	printf ("%s on_mark.\n", ts_format((tv.tv_sec + g_timezone) % 86400, tv.tv_usec));

	return TRUE;
}

/* eof */
