/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Sit periodically sending ODATA with interleaved ambient SPM's,
 * maintain a transmit window buffer responding to NAK's with RDATA packets.
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
#include "pgm/txwi.h"
#include "pgm/checksum.h"


/* typedefs */
struct tsi {
	guint8  gsi[6];			/* transport session identifier TSI */
	guint16 source_port;
};


/* globals */

static int g_port = 7500;
static const char* g_network = "226.0.0.1";
static struct ip_mreqn g_mreqn;

static int g_spm_ambient_interval = 10 * 1000;
static int g_odata_interval = 1 * 1000;

static int g_payload = 0;
static int g_corruption = 0;

static int g_spm_sqn = 0;

static int g_recv_sock = 0;			/* includes IP header */
static int g_send_sock = 0;
static int g_send_with_router_alert_sock = 0;	/* IP_ROUTER_ALERT */
static struct in_addr g_addr;
static GIOChannel* g_recv_channel = NULL;
static GMainLoop* g_loop = NULL;

static int g_max_tpdu = 1500;
static int g_txw_sqns = 10;
static gpointer g_txw = NULL;


static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static gboolean on_io_data (GIOChannel*, GIOCondition, gpointer);
static gboolean on_io_error (GIOChannel*, GIOCondition, gpointer);

static gboolean on_nak (struct pgm_header*, gpointer, gsize);

static gchar* print_tsi (gconstpointer);

static void send_spm (void);
static void send_odata (void);
static void send_rdata (int, gpointer, int);
static gboolean on_spm_timer (gpointer);
static gboolean on_odata_timer (gpointer);


G_GNUC_NORETURN static void
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
	puts ("stream_send_with_nak");

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

	if (g_recv_channel) {
		puts ("closing receive channel.");

		GError *err = NULL;
		g_io_channel_shutdown (g_recv_channel, FALSE, &err);
		g_recv_channel = NULL;
	}
	if (g_recv_sock) {
		puts ("closing receive socket.");
		close(g_recv_sock);
		g_recv_sock = 0;
	}
	if (g_send_sock) {
		puts ("closing send socket.");
		close(g_send_sock);
		g_send_sock = 0;
	}
	if (g_send_with_router_alert_sock) {
		puts ("closing send with router alert socket.");
		close(g_send_with_router_alert_sock);
		g_send_with_router_alert_sock = 0;
	}

	if (g_txw) {
		puts ("destroying transmit window.");

		pgm_txw_shutdown (g_txw);
		g_txw = NULL;
	}

	puts ("finished.");
	return 0;
}

static void
on_signal (
	G_GNUC_UNUSED int	signum
	)
{
	puts ("on_signal");

	g_main_loop_quit(g_loop);
}

static gboolean
on_startup (
	G_GNUC_UNUSED gpointer data
	)
{
	int e, e2, e3;

	puts ("startup.");

	puts ("construct transmit window.");
	g_txw = pgm_txw_init (g_max_tpdu, 0, g_txw_sqns, 0, 0);

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
	g_recv_sock = socket(PF_INET, SOCK_RAW, ipproto_pgm);
	g_send_sock = socket(PF_INET, SOCK_RAW, ipproto_pgm);
	g_send_with_router_alert_sock = socket(PF_INET, SOCK_RAW, ipproto_pgm);
	if (    g_recv_sock < 0 ||
		g_send_sock < 0 ||
		g_send_with_router_alert_sock < 0       )
	{
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
	e = setsockopt(g_recv_sock, IPPROTO_IP, IP_HDRINCL, &_t, sizeof(_t));
	if (e < 0) {
		perror("on_startup() failed");
		close(g_recv_sock);
		close(g_send_sock);
		close(g_send_with_router_alert_sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* buffers */
	int buffer_size = 0;
	socklen_t len = 0;
	e = getsockopt(g_recv_sock, SOL_SOCKET, SO_RCVBUF, &buffer_size, &len);
	if (e == 0) {
		printf ("receive buffer set at %i bytes.\n", buffer_size);
	}
	e = getsockopt(g_send_sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, &len);
	e2 = getsockopt(g_send_with_router_alert_sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, &len);
	if (e == 0 && e2 == 0) {
		printf ("send buffer set at %i bytes.\n", buffer_size);
	}

/* bind */
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	e = bind(g_recv_sock, (struct sockaddr*)&addr, sizeof(addr));
	e2 = bind(g_send_sock, (struct sockaddr*)&addr, sizeof(addr));
	e3 = bind(g_send_with_router_alert_sock, (struct sockaddr*)&addr, sizeof(addr));
	if (e < 0 || e2 < 0 || e3 < 0) {
		perror("on_startup() failed");
		close(g_recv_sock);
		close(g_send_sock);
		close(g_send_with_router_alert_sock);
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
		close(g_recv_sock);
		close(g_send_sock);
		close(g_send_with_router_alert_sock);
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
	{
		printf ("joining multicast group %s.\n", inet_ntoa(g_mreqn.imr_multiaddr));

		e = setsockopt(g_recv_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &g_mreqn, sizeof(g_mreqn));
		e2 = setsockopt(g_send_sock, IPPROTO_IP, IP_MULTICAST_IF, &g_mreqn, sizeof(g_mreqn)); 
		e3 = setsockopt(g_send_with_router_alert_sock, IPPROTO_IP, IP_MULTICAST_IF, &g_mreqn, sizeof(g_mreqn));
		if (e < 0 || e2 < 0 || e3 < 0) {
			perror("on_startup() failed");
			close(g_recv_sock);
			close(g_send_sock);
			close(g_send_with_router_alert_sock);
			g_main_loop_quit(g_loop);
			return FALSE;
		}

/* multicast loopback */
		gboolean n = 0;
		e = setsockopt(g_recv_sock, IPPROTO_IP, IP_MULTICAST_LOOP, &n, sizeof(n));
		e2 = setsockopt(g_send_sock, IPPROTO_IP, IP_MULTICAST_LOOP, &n, sizeof(n));
		e3 = setsockopt(g_send_with_router_alert_sock, IPPROTO_IP, IP_MULTICAST_LOOP, &n, sizeof(n));
		if (e < 0 || e2 < 0 || e3 < 0) {
			perror("on_startup() failed");
			close(g_recv_sock);
			close(g_send_sock);
			close(g_send_with_router_alert_sock);
			g_main_loop_quit(g_loop);
			return FALSE;
		}

/* multicast ttl */
	        int ttl = 1;
		e = setsockopt(g_recv_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
		e2 = setsockopt(g_send_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
		e3 = setsockopt(g_send_with_router_alert_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
		if (e < 0 || e2 < 0 || e3 < 0) {
			perror("on_startup() failed");
			close(g_recv_sock);
			close(g_send_sock);
			close(g_send_with_router_alert_sock);
			g_main_loop_quit(g_loop);
			return FALSE;
		}

	}
	else
	{
		printf ("sending unicast to address %s.\n", inet_ntoa(g_mreqn.imr_multiaddr));
	}

/* add socket to event manager */
	g_recv_channel = g_io_channel_unix_new (g_recv_sock);
	printf ("socket opened with encoding %s.\n", g_io_channel_get_encoding(g_recv_channel));

	/* guint event = */ g_io_add_watch (g_recv_channel, G_IO_IN | G_IO_PRI, on_io_data, NULL);
	/* guint event = */ g_io_add_watch (g_recv_channel, G_IO_ERR | G_IO_HUP | G_IO_NVAL, on_io_error, NULL);


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

static gboolean
on_io_data (
	GIOChannel* source,
	G_GNUC_UNUSED GIOCondition condition,
	G_GNUC_UNUSED gpointer data
	)
{
	printf ("on_data: ");

	char buffer[4096];
	static struct timeval tv;
	gettimeofday(&tv, NULL);

	int fd = g_io_channel_unix_get_fd(source);
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	int len = recvfrom(fd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr*)&addr, &addr_len);

	printf ("%i bytes received from %s.\n", len, inet_ntoa(addr.sin_addr));

	struct sockaddr_in dst_addr;
	socklen_t dst_addr_len;
	struct pgm_header *pgm_header;
	gpointer packet;
	gsize packet_length;
	int e = pgm_parse_raw(buffer, len, (struct sockaddr*)&dst_addr, &dst_addr_len, &pgm_header, &packet, &packet_length);

	switch (e) {
	case -2:
	case -1:
		fflush(stdout);
		return TRUE;

	default: break;
	}

	struct tsi tsi;
	memcpy (tsi.gsi, pgm_header->pgm_gsi, 6 * sizeof(guint8));
	tsi.source_port = pgm_header->pgm_sport;

	printf ("tsi %s\n", print_tsi (&tsi));

	gboolean err = FALSE;
	switch (pgm_header->pgm_type) {
	case PGM_NAK:
		err = on_nak (pgm_header, pgm_header + 1, packet_length - sizeof(pgm_header));
		break;

	default:
		puts ("unknown packet type :(");
		err = TRUE;
		break;
	}

	fflush(stdout);
	return TRUE;
}

static gboolean
on_io_error (
	GIOChannel* source,
	G_GNUC_UNUSED GIOCondition condition,
	G_GNUC_UNUSED gpointer data
	)
{
	puts ("on_error.");

	GError *err;
	g_io_channel_shutdown (source, FALSE, &err);

/* remove event */
	return FALSE;
}

static gboolean
on_nak (
	G_GNUC_UNUSED struct pgm_header*	header,
	gpointer		data,
	gsize			len
	)
{
	printf ("NAK: ");

	if (len < sizeof(struct pgm_nak) ) {
		puts ("packet truncated :(");
		return TRUE;
	}

	struct pgm_nak* nak = (struct pgm_nak*)data;
	nak->nak_src_nla_afi = g_ntohs (nak->nak_src_nla_afi);

	if (nak->nak_src_nla_afi != AFI_IP) {
		puts ("not IPv4 :(");
		return TRUE;
	}

	nak->nak_grp_nla_afi = g_ntohs (nak->nak_grp_nla_afi);
	if (nak->nak_grp_nla_afi != nak->nak_grp_nla_afi) {
		puts ("different source & group afi very wibbly wobbly :(");
		return TRUE;
	}

	char s[INET6_ADDRSTRLEN];
	inet_ntop ( AF_INET, &nak->nak_src_nla, s, sizeof(s) );

	nak->nak_sqn = g_ntohl (nak->nak_sqn);

	printf ("src %s for #%i", s, nak->nak_sqn);

	gpointer rdata = NULL;
	guint16 rlen = 0;
	if (!pgm_txw_peek (g_txw, nak->nak_sqn, &rdata, &rlen))
	{
		puts (", in window");

		send_rdata (nak->nak_sqn, rdata, rlen);
	} else {
		puts (", sequence number not available.");
	}

	printf ("\n");
	return FALSE;
}

static gchar*
print_tsi (
	gconstpointer v
	)
{
	const guint8* gsi = v;
	guint16 source_port = *(const guint16*)(gsi + 6);
	static char buf[sizeof("000.000.000.000.000.000.00000")];
	snprintf(buf, sizeof(buf), "%i.%i.%i.%i.%i.%i.%i",
		gsi[0], gsi[1], gsi[2], gsi[3], gsi[4], gsi[5], g_ntohs (source_port));
	return buf;
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
	G_GNUC_UNUSED gpointer data
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
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_spm) + sizeof(struct in_addr);
	gchar *buf = (gchar*)malloc( tpdu_length );
	if (buf == NULL) {
		perror ("oh crap.");
		return;
	}

printf ("PGM header size %" G_GSIZE_FORMAT "\n"
	"PGM SPM block size %" G_GSIZE_FORMAT "\n",
	sizeof(struct pgm_header),
	sizeof(struct pgm_spm) + sizeof(struct in_addr));

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
	spm->spm_trail		= g_htonl (pgm_txw_lead(g_txw));
	spm->spm_lead		= g_htonl (pgm_txw_trail(g_txw));
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
	e = sendto (g_send_sock,
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
	G_GNUC_UNUSED gpointer data
	)
{
	send_odata ();
	return TRUE;
}

static void
send_odata (void)
{
	puts ("send_odata.");

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
        odata->data_sqn         = g_htonl (pgm_txw_next_lead(g_txw));
        odata->data_trail       = g_htonl (pgm_txw_trail(g_txw));

        memcpy (odata + 1, payload_string, strlen(payload_string) + 1);

        header->pgm_checksum = pgm_csum_fold (pgm_csum_partial(buf, tpdu_length, 0));

/* add to transmit window */
	pgm_txw_push_copy (g_txw, payload_string, strlen(payload_string) + 1);

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
        e = sendto (g_send_sock,
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

static void
send_rdata (
	int		sequence_number,
	gpointer	data,
	int		len
	)
{
	puts ("send_rdata.");

	int e;
	char* payload_string = (char*)data;

/* construct PGM packet */
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + len;
	gchar *buf = (gchar*)malloc( tpdu_length );
	if (buf == NULL) {
		perror ("oh crap.");
		return;
	}

printf ("PGM header size %" G_GSIZE_FORMAT "\n"
	"PGM data header size %" G_GSIZE_FORMAT "\n"
	"payload size %i\n",
	sizeof(struct pgm_header),
	sizeof(struct pgm_data),
	len );

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_data *rdata = (struct pgm_data*)(header + 1);

	header->pgm_sport       = g_htons (g_port);
	header->pgm_dport       = g_htons (g_port);
	header->pgm_type        = PGM_RDATA;
        header->pgm_options     = 0;
        header->pgm_checksum    = 0;

        header->pgm_gsi[0]      = 1;
        header->pgm_gsi[1]      = 2;
        header->pgm_gsi[2]      = 3;
        header->pgm_gsi[3]      = 4;
        header->pgm_gsi[4]      = 5;
        header->pgm_gsi[5]      = 6;

        header->pgm_tsdu_length = g_htons (len);               /* transport data unit length */

/* RDATA */
        rdata->data_sqn         = g_htonl (sequence_number);
        rdata->data_trail       = g_htonl (pgm_txw_trail(g_txw));

        memcpy (rdata + 1, payload_string, len);

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
        e = sendto (g_send_sock,
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
	G_GNUC_UNUSED gpointer data
	)
{
	static struct timeval tv;
	gettimeofday(&tv, NULL);
	printf ("%s on_mark.\n", ts_format((tv.tv_sec + g_timezone) % 86400, tv.tv_usec));

	return TRUE;
}

/* eof */
