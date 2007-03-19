/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Listen to PGM packets, note per host details and data loss.
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
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include <glib.h>

#include "log.h"
#include "pgm.h"


/* typedefs */
struct tsi {
	guint8	gsi[6];			/* transport session identifier TSI */
	guint16	source_port;
};	

struct hoststat {
	struct tsi tsi;

	struct in_addr last_addr;
	struct in_addr nla;

	guint32	txw_secs;		/* seconds of repair data */
	guint32	txw_trail;		/* trailing edge sequence number */
	guint32	txw_lead;		/* leading edge sequence number */
	guint32	txw_sqns;		/* size of transmit window */

	guint32	rxw_trail;		/* trailing edge of receive window */
	guint32	rxw_lead;

	guint32	spm_trail;
	guint32	spm_lead;

	int	count_spm;
	int	count_poll;
	int	count_polr;
	int	count_odata;
	int	count_rdata;
	int	count_nak;
	int	count_nnak;
	int	count_ncf;
	int	count_spmr;

	int	count_valid;
	int	count_invalid;
	int	count_corrupt;
	int	count_total;

	int	bytes_payload;
	int	bytes_spm;
	int	bytes_poll;
	int	bytes_polr;
	int	bytes_odata;
	int	bytes_rdata;
	int	bytes_nak;
	int	bytes_nnak;
	int	bytes_ncf;
	int	bytes_spmr;

	int	bytes_valid;
	int	bytes_invalid;
	int	bytes_corrupt;
	int	bytes_total;

	struct timeval	last_spm;
	struct timeval	last_poll;
	struct timeval	last_polr;
	struct timeval	last_odata;
	struct timeval	last_rdata;
	struct timeval	last_nak;
	struct timeval	last_nnak;
	struct timeval	last_ncf;
	struct timeval	last_spmr;

	struct timeval	last_valid;
	struct timeval	last_invalid;
	struct timeval	last_corrupt;
	struct timeval	last_packet;

	struct timeval	session_start;
};


/* globals */

static int g_port = 7500;
static char* g_network = "226.0.0.1";

static GHashTable *g_hosts = NULL;

static GIOChannel* g_io_channel = NULL;
static GMainLoop* g_loop = NULL;


static guint tsi_hash (gconstpointer);
static gint tsi_equal (gconstpointer, gconstpointer);

static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static gboolean on_io_data (GIOChannel*, GIOCondition, gpointer);
static gboolean on_io_error (GIOChannel*, GIOCondition, gpointer);


int
main (
	int	argc,
	char   *argv[]
	)
{
	puts ("basic_recv");

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
		puts ("closing socket.");

		GError *err = NULL;
		g_io_channel_shutdown (g_io_channel, FALSE, &err);
		g_io_channel = NULL;
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
	int sock = socket(PF_INET, SOCK_RAW, ipproto_pgm);
	if (sock < 0) {
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

	char _t = 1;
	e = setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &_t, sizeof(_t));
	if (e < 0) {
		perror("on_startup() failed");
		close(sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* buffers */
	int buffer_size = 0;
	socklen_t len = 0;
	e = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buffer_size, &len);
	if (e == 0) {
		printf ("receive buffer set at %i bytes.\n", buffer_size);
	}
	e = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, &len);
	if (e == 0) {
		printf ("send buffer set at %i bytes.\n", buffer_size);
	}

/* bind */
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(g_port);

	e = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (e < 0) {
		perror("on_startup() failed");
		close(sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* multicast */
	struct ip_mreqn mreqn;
	memset(&mreqn, 0, sizeof(mreqn));
	mreqn.imr_address.s_addr = htonl(INADDR_ANY);
	printf ("listening on interface %s.\n", inet_ntoa(mreqn.imr_address));
	mreqn.imr_multiaddr.s_addr = inet_addr(g_network);
	printf ("subscription on multicast address %s.\n", inet_ntoa(mreqn.imr_multiaddr));
	e = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreqn, sizeof(mreqn));
	if (e < 0) {
		perror("on_startup() failed");
		close(sock);
		g_main_loop_quit(g_loop);
		return FALSE;
	}

/* multicast loopback */
/* multicast ttl */

/* add socket to event manager */
	g_io_channel = g_io_channel_unix_new (sock);
	printf ("socket opened with encoding %s.\n", g_io_channel_get_encoding(g_io_channel));

	/* guint event = */ g_io_add_watch (g_io_channel, G_IO_IN | G_IO_PRI, on_io_data, NULL);
	/* guint event = */ g_io_add_watch (g_io_channel, G_IO_ERR | G_IO_HUP | G_IO_NVAL, on_io_error, NULL);

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add(10 * 1000, (GSourceFunc)on_mark, NULL);

	puts ("startup complete.");
	return FALSE;
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

static gboolean
on_io_data (
	GIOChannel* source,
	GIOCondition condition,
	gpointer data
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

	struct pgm_header *pgm_header;
	char *packet;
	int packet_length;
	if (!pgm_parse_packet(buffer, len, &pgm_header, &packet, &packet_length)) {
		puts ("invalid packet :(");
	}

/* search for existing session */
	if (!g_hosts) {
		g_hosts = g_hash_table_new (tsi_hash, tsi_equal);
	}

	struct tsi tsi;
	memcpy (tsi.gsi, pgm_header->pgm_gsi, sizeof(pgm_header->pgm_gsi));
	tsi.source_port = pgm_header->pgm_sport;

	struct hoststat* hoststat = g_hash_table_lookup (g_hosts, &tsi);
	if (hoststat == NULL) {
		hoststat = g_malloc0(sizeof(struct hoststat));
		memcpy (&hoststat->tsi, &tsi, sizeof(struct tsi));

		hoststat->session_start = tv;

		g_hash_table_insert (g_hosts, (gpointer)&hoststat->tsi, (gpointer)&hoststat);
	}

/* increment statistics */
	memcpy (&hoststat->last_addr, &addr, sizeof(addr));
	hoststat->count_total++;
	hoststat->bytes_total += len;
	hoststat->last_packet = tv;

	gboolean err = FALSE;
        switch (pgm_header->pgm_type) {
        case PGM_SPM:
		err = pgm_parse_spm (pgm_header, packet, packet_length, &hoststat->nla);

		if (!err) {
			hoststat->count_spm++;
			hoststat->bytes_spm += len;
			hoststat->last_spm = tv;
		}
		break;

        case PGM_POLL:
		hoststat->count_poll++;
		hoststat->bytes_poll += len;
		hoststat->last_poll = tv;
		break;

        case PGM_POLR:
		hoststat->count_polr++;
		hoststat->bytes_polr += len;
		hoststat->last_polr = tv;
		break;

        case PGM_ODATA:
		hoststat->bytes_payload += pgm_header->pgm_tsdu_length;

		hoststat->count_odata++;
		hoststat->bytes_odata += len;
		hoststat->last_odata = tv;
		break;

        case PGM_RDATA:
		hoststat->count_rdata++;
		hoststat->bytes_rdata += len;
		hoststat->last_rdata = tv;
		break;

        case PGM_NAK:
		hoststat->count_nak++;
		hoststat->bytes_nak += len;
		hoststat->last_nak = tv;
		break;

        case PGM_NNAK:
		hoststat->count_nnak++;
		hoststat->bytes_nnak += len;
		hoststat->last_nnak = tv;
		break;

        case PGM_NCF:
		hoststat->count_ncf++;
		hoststat->bytes_ncf += len;
		hoststat->last_ncf = tv;
		break;

        case PGM_SPMR:
		hoststat->count_spmr++;
		hoststat->bytes_spmr += len;
		hoststat->last_spmr = tv;
		break;

	default:
		puts ("unknown packet type :(");
		err = TRUE;
		break;
	}

	if (err) {
		hoststat->count_invalid++;
		hoststat->bytes_invalid += len;
		hoststat->last_invalid = tv;
	} else {
		hoststat->count_valid++;
		hoststat->count_valid += len;
		hoststat->last_valid = tv;
	}

	fflush(stdout);

	return TRUE;
}

static gboolean
on_io_error (
	GIOChannel* source,
	GIOCondition condition,
	gpointer data
	)
{
	puts ("on_error.");

	GError *err;
	g_io_channel_shutdown (source, FALSE, &err);

/* remove event */
	return FALSE;
}

/* convert a transport session identifier TSI to a hash value
 */

static guint
tsi_hash (
	gconstpointer v
	)
{
	guint8* gsi = (guint8*)v;
	guint16 source_port = *(guint16*)(gsi + 6);
	char buf[sizeof("000.000.000.000.000.000.00000")];
	snprintf(buf, sizeof(buf), "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu.%hu",
		gsi[0], gsi[1], gsi[2], gsi[3], gsi[4], gsi[5], source_port);
	return g_str_hash(buf);
}

/* compare two transport session identifier TSI values and return TRUE if they are equal
 */

static gint
tsi_equal (
	gconstpointer	v,
	gconstpointer	v2
	)
{
	return memcmp (v, v2, (6 * sizeof(guint8)) + sizeof(guint16)) == 0;
}

/* eof */
