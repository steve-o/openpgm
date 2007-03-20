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
#include <libsoup/soup.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-address.h>

#include "log.h"
#include "pgm.h"


/* typedefs */
struct tsi {
	guint8	gsi[6];			/* transport session identifier TSI */
	guint16	source_port;
};

struct stat {
	int	count;
	int	bytes;
	int	tsdu;

	int	corrupt;
	int	invalid;

	struct timeval	last;
	struct timeval	last_valid;
	struct timeval	last_corrupt;
	struct timeval	last_invalid;
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

	struct stat	spm,
			poll,
			polr,
			odata,
			rdata,
			nak,
			nnak,
			ncf,
			spmr,

			general;

	struct timeval	session_start;
};


/* globals */

#define WWW_NOTFOUND    "<html><head><title>404</title></head><body>lah, 404 :)</body></html>\r\n"

#define WWW_HEADER	"<html><head><meta http-equiv=\"refresh\" content=\"10\" /><title>basic_recv</title></head><body>"
#define	WWW_FOOTER	"</body></html>\r\n"


static int g_port = 7500;
static char* g_network = "226.0.0.1";

static int g_http = 4968;

static GHashTable *g_hosts = NULL;
static GMutex *g_hosts_mutex = NULL;

static GIOChannel* g_io_channel = NULL;
static GMainLoop* g_loop = NULL;
static SoupServer* g_soup_server = NULL;

static guint tsi_hash (gconstpointer);
static gint tsi_equal (gconstpointer, gconstpointer);

static gchar* print_tsi (gconstpointer);

static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static gboolean on_io_data (GIOChannel*, GIOCondition, gpointer);
static gboolean on_io_error (GIOChannel*, GIOCondition, gpointer);

static void default_callback (SoupServerContext*, SoupMessage*, gpointer);
static void index_callback (SoupServerContext*, SoupMessage*, gpointer);
static int tsi_callback (SoupServerContext*, SoupMessage*, gpointer);


static void
usage (const char* bin)
{
        fprintf (stderr, "Usage: %s [options]\n", bin);
        fprintf (stderr, "  -p <port>       : IP port for web interface\n");
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
	puts ("basic_recv");

	/* parse program arguments */
        const char* binary_name = strrchr (argv[0], '/');
        int c;
        while ((c = getopt (argc, argv, "p:s:n:h")) != -1)
        {
                switch (c) {
                case 'p':       g_http = atoi (optarg); break;
                case 'n':       g_network = optarg; break;
                case 's':       g_port = atoi (optarg); break;

                case 'h':
                case '?': usage (binary_name);
                }
        }

	log_init ();

	g_type_init ();
	g_thread_init (NULL);

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

        if (g_soup_server) {
                g_object_unref (g_soup_server);
                g_soup_server = NULL;
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

        puts ("starting soup server.");
        g_soup_server = soup_server_new (SOUP_SERVER_PORT, g_http,
                                        NULL);
        if (!g_soup_server) {
                printf ("soup server failed: %s\n", strerror (errno));
                g_main_loop_quit (g_loop);
                return FALSE;
        }

        char hostname[NI_MAXHOST + 1];
        gethostname (hostname, sizeof(hostname));

        printf ("web interface: http://%s:%i\n",
                hostname,
                soup_server_get_port (g_soup_server));

        soup_server_add_handler (g_soup_server, NULL,	NULL, default_callback, NULL, NULL);
        soup_server_add_handler (g_soup_server, "/",	NULL, index_callback, NULL, NULL);

        soup_server_run_async (g_soup_server);
        g_object_unref (g_soup_server);


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
//	printf ("on_data: ");

	char buffer[4096];
	static struct timeval tv;
	gettimeofday(&tv, NULL);

	int fd = g_io_channel_unix_get_fd(source);
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	int len = recvfrom(fd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr*)&addr, &addr_len);

//	printf ("%i bytes received from %s.\n", len, inet_ntoa(addr.sin_addr));

	struct pgm_header *pgm_header;
	char *packet;
	int packet_length;
	if (!pgm_parse_packet(buffer, len, &pgm_header, &packet, &packet_length)) {
		puts ("invalid packet :(");
	}

/* search for existing session */
	if (!g_hosts) {
		g_hosts = g_hash_table_new (tsi_hash, tsi_equal);
		g_hosts_mutex = g_mutex_new ();
	}

	struct tsi tsi;
	memcpy (tsi.gsi, pgm_header->pgm_gsi, 6 * sizeof(guint8));
	tsi.source_port = pgm_header->pgm_sport;

//	printf ("tsi %s\n", print_tsi (&tsi));

	struct hoststat* hoststat = g_hash_table_lookup (g_hosts, &tsi);
	if (hoststat == NULL) {
		printf ("new tsi %s\n", print_tsi (&tsi));

		hoststat = g_malloc0(sizeof(struct hoststat));
		memcpy (&hoststat->tsi, &tsi, sizeof(struct tsi));

		hoststat->session_start = tv;

		g_mutex_lock (g_hosts_mutex);
		g_hash_table_insert (g_hosts, (gpointer)&hoststat->tsi, (gpointer)hoststat);
		g_mutex_unlock (g_hosts_mutex);
	}

/* increment statistics */
	memcpy (&hoststat->last_addr, &addr.sin_addr, sizeof(addr.sin_addr));
	hoststat->general.count++;
	hoststat->general.bytes += len;
	hoststat->general.last = tv;

	gboolean err = FALSE;
        switch (pgm_header->pgm_type) {
        case PGM_SPM:
		err = pgm_parse_spm (pgm_header, packet, packet_length, &hoststat->nla);

		if (!err) {
			hoststat->spm.count++;
			hoststat->spm.bytes += len;
			hoststat->spm.last = tv;
		}
		break;

        case PGM_POLL:
		hoststat->poll.count++;
		hoststat->poll.bytes += len;
		hoststat->poll.last = tv;
		break;

        case PGM_POLR:
		hoststat->polr.count++;
		hoststat->polr.bytes += len;
		hoststat->polr.last = tv;
		break;

        case PGM_ODATA:
		hoststat->odata.tsdu += g_ntohs (pgm_header->pgm_tsdu_length);

		hoststat->odata.count++;
		hoststat->odata.bytes += len;
		hoststat->odata.last = tv;
		break;

        case PGM_RDATA:
		hoststat->rdata.count++;
		hoststat->rdata.bytes += len;
		hoststat->rdata.last = tv;
		break;

        case PGM_NAK:
		hoststat->nak.count++;
		hoststat->nak.bytes += len;
		hoststat->nak.last = tv;
		break;

        case PGM_NNAK:
		hoststat->nnak.count++;
		hoststat->nnak.bytes += len;
		hoststat->nnak.last = tv;
		break;

        case PGM_NCF:
		hoststat->ncf.count++;
		hoststat->ncf.bytes += len;
		hoststat->ncf.last = tv;
		break;

        case PGM_SPMR:
		hoststat->spmr.count++;
		hoststat->spmr.bytes += len;
		hoststat->spmr.last = tv;
		break;

	default:
		puts ("unknown packet type :(");
		err = TRUE;
		break;
	}

	if (err) {
		hoststat->general.invalid++;
		hoststat->general.last_invalid = tv;
	} else {
		hoststat->general.last_valid = tv;
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

static gchar*
print_tsi (
	gconstpointer v
	)
{
	guint8* gsi = (guint8*)v;
	guint16 source_port = *(guint16*)(gsi + 6);
	static char buf[sizeof("000.000.000.000.000.000.00000")];
	snprintf(buf, sizeof(buf), "%i.%i.%i.%i.%i.%i.%i",
		gsi[0], gsi[1], gsi[2], gsi[3], gsi[4], gsi[5], g_ntohs (source_port));
	return buf;
}

/* convert a transport session identifier TSI to a hash value
 */

static guint
tsi_hash (
	gconstpointer v
	)
{
	return g_str_hash(print_tsi(v));
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

/* request on web interface
 */

static void
default_callback (
                SoupServerContext*      context,
                SoupMessage*            msg,
                gpointer                data
                )
{
        char *path;

        path = soup_uri_to_string (soup_message_get_uri (msg), TRUE);
        printf ("%s %s HTTP/1.%d\n", msg->method, path,
                soup_message_get_http_version (msg));

	int e = -1;
	if (g_hosts && strncmp ("/tsi/", path, strlen("/tsi/")) == 0)
	{
		e = tsi_callback (context, msg, data);
	}

	if (e)
	{
	        soup_message_set_response (msg, "text/html", SOUP_BUFFER_STATIC,
	                                        WWW_NOTFOUND, strlen(WWW_NOTFOUND));
	        soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
	        soup_message_add_header (msg->response_headers, "Connection", "close");
	}
}

/* transport session identifier TSI index
 */

static struct timeval g_tv_now;

static const char*
print_si (
		float* v
		)
{
	static char prefix[5] = "";

	if (*v > 100 * 1000 * 1000) {
		strcpy (prefix, "giga");
		*v /= 1000.0 * 1000.0 * 1000.0;
	} else if (*v > 100 * 1000) {
		strcpy (prefix, "mega");
		*v /= 1000.0 * 1000.0;
	} else if (*v > 100) {
		strcpy (prefix, "kilo");
		*v /= 1000.0;
	}

	return prefix;
}

static gboolean
index_tsi_row (
		gpointer	key,
		gpointer	value,
		gpointer	user_data
		)
{
	struct hoststat* hoststat = value;
	GString *response = user_data;

	float secs = (g_tv_now.tv_sec - hoststat->session_start.tv_sec) +
			( (g_tv_now.tv_usec - hoststat->session_start.tv_usec) / 1000.0 / 1000.0 );
	float bitrate = ((float)hoststat->general.bytes * 8.0 / secs);
	const char* bitprefix = print_si (&bitrate);
	char* tsi_string = print_tsi (&hoststat->tsi);

	g_string_append_printf (response, 
			"<tr>"
				"<td><a href=\"/tsi/%s\">%s</a></td>"
				"<td>%i</td>"
				"<td>%i</td>"
				"<td>%.1f pps</td>"
				"<td>%.1f %sbit/s</td>"
				"<td>%3.1f%%</td>"
				"<td>%3.1f%%</td>"
				"<td>%3.1f%%</td>"
			"</tr>",
			tsi_string, tsi_string,
			hoststat->general.count,
			hoststat->general.bytes,
			hoststat->general.count / secs,
			bitrate, bitprefix,
			(100.0 * hoststat->odata.tsdu) / hoststat->general.bytes,
			hoststat->general.corrupt ? (100.0 * hoststat->general.corrupt) / hoststat->general.count : 0.0,
			hoststat->general.invalid ? (100.0 * hoststat->general.invalid) / hoststat->general.count : 0.0
			);

	return FALSE;
}

static void
index_callback (
                SoupServerContext*      context,
                SoupMessage*            msg,
                gpointer                data
                )
{
	GString *response;

	response = g_string_new (WWW_HEADER);

	if (!g_hosts) {
		g_string_append (response, "<i>No TSI's.</i>");
	} else {
		g_string_append (response, "<table>"
						"<tr>"
							"<th>TSI</th>"
							"<th># Packets</th>"
							"<th># Bytes</th>"
							"<th>Packet Rate</th>"
							"<th>Bitrate</th>"
							"<th>% Data</th>"
							"<th>% Corrupt</th>"
							"<th>% Invalid</th>"
						"</tr>");

		gettimeofday(&g_tv_now, NULL);
		g_mutex_lock (g_hosts_mutex);
		g_hash_table_foreach (g_hosts, (GHFunc)index_tsi_row, response);
		g_mutex_unlock (g_hosts_mutex);
		g_string_append (response, "</table>");
	}

	g_string_append (response, WWW_FOOTER);
	gchar* resp = g_string_free (response, FALSE);
	soup_message_set_response (msg, "text/html", SOUP_BUFFER_SYSTEM_OWNED, resp, strlen(resp));
}

/* transport session identifier TSI details
 */

static char*
print_stat (
		const char*	name,
		struct stat*	stat,
		float		secs,
		const char*	el		/* xml element name */
		)
{
	static char buf[1024];
	float bitrate = ((float)stat->bytes * 8.0 / secs);
	const char* bitprefix = print_si (&bitrate);
	
	snprintf (buf, sizeof(buf),
		"<tr>"
			"<%s>%s</%s>"			/* type */
			"<%s>%i</%s>"			/* # packets */
			"<%s>%i</%s>"			/* # bytes */
			"<%s>%.1f pps</%s>"		/* packet rate */
			"<%s>%.1f %sbit/s</%s>"		/* bitrate */
			"<%s>%i / %3.1f%%</%s>"		/* corrupt */
			"<%s>%i / %3.1f%%</%s>"		/* invalid */
		"</tr>",
		el, name, el,
		el, stat->count, el,
		el, stat->bytes, el,
		el, stat->count / secs, el,
		el, bitrate, bitprefix, el,
		el, stat->corrupt, stat->corrupt ? (100.0 * stat->corrupt) / stat->count : 0.0, el,
		el, stat->invalid, stat->invalid ? (100.0 * stat->invalid) / stat->invalid : 0.0, el
		);

	return buf;
}

static int
tsi_callback (
                SoupServerContext*      context,
                SoupMessage*            msg,
                gpointer                data
                )
{
        char *path;
	struct hoststat* hoststat = NULL;
	struct tsi tsi;

        path = soup_uri_to_string (soup_message_get_uri (msg), TRUE);

	int e = sscanf (path + strlen("/tsi/"), "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu.%hu",
		&tsi.gsi[0], &tsi.gsi[1], &tsi.gsi[2], &tsi.gsi[3], &tsi.gsi[4], &tsi.gsi[5],
		&tsi.source_port);
	tsi.source_port = g_ntohs (tsi.source_port);

	if (e == 7) {
		hoststat = g_hash_table_lookup (g_hosts, &tsi);
	} else {
		printf ("sscanf found %i elements to tsi, expected 7.\n", e);
	}

	if (!hoststat) {
		return -1;
	}

	GString *response;
	response = g_string_new (WWW_HEADER);

	gettimeofday(&g_tv_now, NULL);
	float secs = (g_tv_now.tv_sec - hoststat->session_start.tv_sec) +
			( (g_tv_now.tv_usec - hoststat->session_start.tv_usec) / 1000.0 / 1000.0 );

	g_string_append_printf (response, 
				"<h3>%s</h3>"
				"<p>"
				"<table>"
					"<tr><td><b>Last IP address:</b></td><td>%s</td></tr>"
					"<tr><td><b>NLA:</b></td><td>%s</td></tr>"
				"</table>"
				"</p><p>"
				"<table>"
					"<thead><tr>"
						"<th>Type</th>"
						"<th># Packets</th>"
						"<th># Bytes</th>"
						"<th>Packet Rate</th>"
						"<th>Bitrate</th>"
						"<th>Corrupt</th>"
						"<th>Invalid</th>"
					"</tr></thead>",
				path + strlen("/tsi/"),
				inet_ntoa(hoststat->last_addr),
				inet_ntoa(hoststat->nla)
				);

/* per packet stats */
	g_string_append (response, print_stat ("SPM", &hoststat->spm, secs, "td"));
	g_string_append (response, print_stat ("POLL", &hoststat->poll, secs, "td"));
	g_string_append (response, print_stat ("POLR", &hoststat->polr, secs, "td"));
	g_string_append (response, print_stat ("ODATA", &hoststat->odata, secs, "td"));
	g_string_append (response, print_stat ("RDATA", &hoststat->rdata, secs, "td"));
	g_string_append (response, print_stat ("NAK", &hoststat->nak, secs, "td"));
	g_string_append (response, print_stat ("N-NAK", &hoststat->nnak, secs, "td"));
	g_string_append (response, print_stat ("NCF", &hoststat->ncf, secs, "td"));
	g_string_append (response, print_stat ("SPMR", &hoststat->spmr, secs, "td"));

	g_string_append (response, "<tfoot>");
	g_string_append (response, print_stat ("TOTAL", &hoststat->general, secs, "th"));

	g_string_append (response, 
			"</tfoot>"
			"</table>"
			"</p><p><a href=\"/\">Return to index.</a></p>"
			);

	g_string_append (response, WWW_FOOTER);
	gchar* resp = g_string_free (response, FALSE);
	soup_message_set_response (msg, "text/html", SOUP_BUFFER_SYSTEM_OWNED, resp, strlen(resp));

	return 0;
}


/* eof */
