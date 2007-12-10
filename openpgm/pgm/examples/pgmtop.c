/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM packet monitor.
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
#include <ncurses.h>
#include <netdb.h>
#include <panel.h>
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

#include <pgm/backtrace.h>
#include <pgm/signal.h>
#include <pgm/log.h>
#include <pgm/packet.h>
#include <pgm/transport.h>
#include <pgm/sn.h>

struct ncurses_window;

typedef void (*paint_func)(struct ncurses_window*);
typedef void (*resize_func)(struct ncurses_window*, int, int);

struct ncurses_window {
	WINDOW*		window;
	PANEL*		panel;
	char*		title;
	paint_func	paint;
	resize_func	resize;
};

struct stat {
	gulong		count, snap_count;
	gulong		bytes, snap_bytes;
	gulong		tsdu;

	gulong		duplicate;
	gulong		invalid;

	struct timeval	last;
	struct timeval	last_valid;
	struct timeval	last_invalid;
};

struct netstat {
	struct in_addr	s_addr;
	gulong		corrupt;
};

struct hoststat {
	struct tsi	tsi;

	struct in_addr	last_addr;
	struct in_addr	nla;

	gulong		txw_secs;
	gulong		txw_trail;
	gulong		txw_lead;
	gulong		txw_sqns;

	gulong		rxw_trail;
	gulong		rxw_lead;

	gulong		rxw_trail_init;
	gboolean	window_defined;
	gboolean	rxw_constrained;

	gulong		spm_sqn;

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

static int g_port = 7500;
static char* g_network = "226.0.0.1";
static struct in_addr g_filter = { 0 };

static GIOChannel* g_io_channel = NULL;
static GIOChannel* g_stdin_channel = NULL;

static GMainLoop* g_loop = NULL;

static int g_status_height = 6;
static int g_info_width = 10;
static time_t start_time;

static struct ncurses_window *g_peer, *g_info, *g_status, *g_active;
static GList* g_window_list = NULL;
static int g_paint_interval = ( 1 * 1000 ) / 15;
static int g_snap_interval = 10 * 1000;
static struct timeval g_last_snap, g_now;

static GList* g_status_list = NULL;

static guint32 g_packets = 0;
static GHashTable *g_hosts = NULL;
static GHashTable *g_nets = NULL;

static void init_ncurses (void);
static void paint_ncurses (void);
static void resize_ncurses (int, int);

static void paint_peer (struct ncurses_window*);
static gboolean tsi_row (gpointer, gpointer, gpointer);

static void paint_info (struct ncurses_window*);
static void paint_status (struct ncurses_window*);
static void resize_peer (struct ncurses_window*, int, int);
static void resize_info (struct ncurses_window*, int, int);
static void resize_status (struct ncurses_window*, int, int);

static void write_status (const gchar*, ...) G_GNUC_PRINTF (1, 2);
static void write_statusv (const gchar*, va_list);

static void on_signal (int);
static void on_winch (int);
static gboolean on_startup (gpointer);
static gboolean on_snap (gpointer);
static gboolean on_paint (gpointer);

static gboolean on_io_data (GIOChannel*, GIOCondition, gpointer);
static gboolean on_io_error (GIOChannel*, GIOCondition, gpointer);

static gboolean on_stdin_data (GIOChannel*, GIOCondition, gpointer);

int
main (
	int	argc,
	char   *argv[]
	)
{
	puts ("pgmtop");

	log_init ();
	pgm_init ();

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
	signal_install (SIGINT, on_signal);
	signal_install (SIGTERM, on_signal);
	signal_install (SIGHUP, SIG_IGN);

	g_loop = g_main_loop_new (NULL, FALSE);

/* delayed startup */
	puts ("scheduling startup.n");
	g_timeout_add(0, (GSourceFunc)on_startup, NULL);

/* dispatch loop */
	puts ("entering main event loop ...");
	g_main_loop_run (g_loop);

	endwin();
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

	if (g_stdin_channel) {
		puts ("unbinding stdin.");
		g_io_channel_unref (g_stdin_channel);
		g_stdin_channel = NULL;
	}

	puts ("finished.");
	return 0;
}

static struct ncurses_window*
create_window (
	char*		name,
	paint_func	paint,
	resize_func	resize
	)
{
	struct ncurses_window* nw = g_malloc0 (sizeof(struct ncurses_window));
	nw->window = newwin (0, 0, 0, 0);
	nw->panel = new_panel (nw->window);
	nw->title = name;

	nw->paint = paint;
	nw->resize = resize;

	g_window_list = g_list_append (g_window_list, nw);
	return nw;
}

/*  +-Peer list --------------++-Info-+
 *  |                         ||      |
 *  |      < peer window >    || < info window >
 *  |                         ||      |
 *  +-------------------------++------+
 *  +-Status--------------------------+
 *  |        < status window >        |
 *  +---------------------------------+
 */

static void
init_ncurses (void)
{
/* setup ncurses terminal display */
	initscr();		/* init ncurses library */

//	signal_install (SIGWINCH, on_winch);

	noecho();		/* hide entered keys */
	cbreak();

/* setup ncurses windows */
	g_peer = create_window ("Peers", paint_peer, resize_peer);

	g_info = create_window ("Info", paint_info, resize_info);
	start_time = time (0);

	g_status = create_window ("Status", paint_status, resize_status);
	scrollok (g_status->window, 1);

	g_active = g_peer;
	top_panel (g_active->panel);

	paint_ncurses();
}

static void
resize_ncurses (
	int		hsize,
	int		vsize
	)
{
	GList* nw_list = g_window_list;
	while (nw_list)
	{
		struct ncurses_window* nw = (struct ncurses_window*)nw_list->data;

		nw->resize (nw, hsize, vsize);

		nw_list = nw_list->next;
	}
}

static void
paint_ncurses (void)
{
	static int hsize = 0, vsize = 0;

	if (hsize != COLS || vsize != LINES)
	{
		hsize = COLS; vsize = LINES;
		resize_ncurses(hsize, vsize);
	}

	GList* nw_list = g_window_list;
	while (nw_list)
	{
		struct ncurses_window* nw = (struct ncurses_window*)nw_list->data;
		werase (nw->window);

		box (nw->window, ACS_VLINE, ACS_HLINE);
		mvwaddstr (nw->window, 0, 2, nw->title);

		nw->paint (nw);

		nw_list = nw_list->next;
	}

/* have cursor stay at top left of active window */
	wmove (g_active->window, 0, 0);

	update_panels();	/* update virtual screen */
	doupdate();		/* update real screen */
}

/* peer window */

static void
paint_peer (
	struct ncurses_window*	nw
	)
{

/*           1         2         3         4         5         6         7         8
 * 012345678901234567890123456789012345678901234567890123456789012345678901234567890
 *  TSI                            Packets Bytes   Packet/s  Bit/s     Data Inv  Dupe
 *  100.200.300.400.500.600.70000  1,000K  1,000MB 1,000     1,000     100% 100% 100%
 */
	mvwaddstr (nw->window, 1, 1, "TSI");
	mvwaddstr (nw->window, 1, 32, "Packets");
	mvwaddstr (nw->window, 1, 40, "Bytes");
	mvwaddstr (nw->window, 1, 48, "Packet/s");
	mvwaddstr (nw->window, 1, 58, "Bit/s");
	mvwaddstr (nw->window, 1, 68, "Data");
	mvwaddstr (nw->window, 1, 73, "Inv");
	mvwaddstr (nw->window, 1, 78, "Dupe");

	if (g_hosts)
	{
		int row = 2;
		gettimeofday(&g_now, NULL);
		g_hash_table_foreach (g_hosts, (GHFunc)tsi_row, &row);
	}
}

static const char*
print_si (
	float*			v
	)
{
	static char prefix[5] = "";

	if (*v > 100 * 1000 * 1000) {
		strcpy (prefix, "G");
		*v /= 1000.0 * 1000.0 * 1000.0;
	} else if (*v > 100 * 1000) {
		strcpy (prefix, "M");
		*v /= 1000.0 * 1000.0;
	} else if (*v > 100) {
		strcpy (prefix, "K");
		*v /= 1000.0;
	} else {
		*prefix = 0;
	}

	return prefix;
}

static gboolean
tsi_row (
	gpointer		key,
	gpointer		value,
	gpointer		user_data
	)
{
	struct hoststat* hoststat = value;
	int* row = user_data;

	float secs = (g_now.tv_sec - g_last_snap.tv_sec) +
			( (g_now.tv_usec - g_last_snap.tv_usec) / 1000.0 / 1000.0 );

/* TSI */
	char* tsi_string = pgm_print_tsi (&hoststat->tsi);
	mvwaddstr (g_peer->window, *row,  1, tsi_string);

/* Packets */
	char buffer[100];
	float v = hoststat->general.count;
	char* prefix = print_si (&v);
	snprintf (buffer, sizeof(buffer), "%lu%s", (gulong)v, prefix);
	mvwaddstr (g_peer->window, *row, 32, buffer);

/* Bytes */
	v = hoststat->general.bytes;
	prefix = print_si (&v);
	snprintf (buffer, sizeof(buffer), "%lu%s", (gulong)v, prefix);
	mvwaddstr (g_peer->window, *row, 40, buffer);

/* Packet/s */
	v = ( hoststat->general.count - hoststat->general.snap_count ) / secs;
	prefix = print_si (&v);
	snprintf (buffer, sizeof(buffer), "%.1f%s", v, prefix);
	mvwaddstr (g_peer->window, *row, 48, buffer);

/* Bit/s */
	float bitrate = ((float)( hoststat->general.bytes - hoststat->general.snap_bytes ) * 8.0 / secs);
	char* bitprefix = print_si (&bitrate);
	snprintf (buffer, sizeof(buffer), "%.1f%s", bitrate, bitprefix);
	mvwaddstr (g_peer->window, *row, 58, buffer);

/* % Data */
	snprintf (buffer, sizeof(buffer), "%d%%", (int)((100.0 * hoststat->odata.tsdu) / hoststat->general.bytes));
	mvwaddstr (g_peer->window, *row, 68, buffer);

/* % Invalid */
	snprintf (buffer, sizeof(buffer), "%d%%", (int)(hoststat->general.invalid ? (100.0 * hoststat->general.invalid) / hoststat->general.count : 0.0));
	mvwaddstr (g_peer->window, *row, 73, buffer);

/* % Duplicate */
	snprintf (buffer, sizeof(buffer), "%d%%", (int)(hoststat->general.duplicate ? (100.0 * hoststat->general.duplicate) / hoststat->general.count : 0.0));
	mvwaddstr (g_peer->window, *row, 78, buffer);

	*row = *row + 1;
			
	return FALSE;
}

static void
resize_peer (
	struct ncurses_window*	nw,
	int			hsize,	/* COLS */
	int			vsize	/* LINES */
	)
{
	wresize (nw->window, vsize - g_status_height, hsize - g_info_width);
	replace_panel (nw->panel, nw->window);
	move_panel (nw->panel, 0, 0);
}

/* info window */

static void
paint_info (
	struct ncurses_window*	nw
	)
{
	char buffer[20];

	mvwaddstr (nw->window, 1, 2, "Peers");
	snprintf (buffer, sizeof(buffer), "%d", g_hosts ? g_hash_table_size (g_hosts) : 0);
	mvwaddstr (nw->window, 2, 2, buffer);

	mvwaddstr (nw->window, 3, 2, "Packets");
	snprintf (buffer, sizeof(buffer), "%d", g_packets);
	mvwaddstr (nw->window, 4, 2, buffer);

	mvwaddstr (nw->window, LINES - g_status_height - 2, 2, "Elapsed");
	time_t elapsed = time(0) - start_time;
	snprintf (buffer, sizeof(buffer), "%02d:%02d:%02d",
			(int) (elapsed / 60) / 60, (int) (elapsed / 60) % 60,
			(int) elapsed % 60);
	mvwaddstr (nw->window, LINES - g_status_height - 1, 1, buffer);
}

static void
resize_info (
	struct ncurses_window*	nw,
	int			hsize,	/* COLS */
	int			vsize	/* LINES */
	)
{
	wresize (nw->window, vsize - g_status_height, g_info_width);
	replace_panel (nw->panel, nw->window);
	move_panel (nw->panel, 0, hsize - g_info_width);
}

/* status window */

static void
paint_status (
	struct ncurses_window*	nw
	)
{
	if (!g_status_list) return;

	guint len = g_list_length (g_status_list);
	while (len > g_status_height) {
		g_free (g_status_list->data);
		g_status_list = g_list_delete_link (g_status_list, g_status_list);
		len--;
	}
	guint y = 1;
	GList* list = g_status_list;
	while (list) {
		mvwaddstr (g_status->window, y++, 3, (char*)list->data);
		list = list->next;
	}
}

static void
resize_status (
	struct ncurses_window*	nw,
	int			hsize,	/* COLS */
	int			vsize	/* LINES */
	)
{
	wresize (nw->window, g_status_height, hsize);
	replace_panel (nw->panel, nw->window);
	move_panel (nw->panel, vsize - g_status_height, 0);
}

static void
write_status (
	const gchar*		format,
	...
	)
{
	va_list args;

	va_start (args, format);
	write_statusv (format, args);
	va_end (args);
}

static void
write_statusv (
	const gchar*		format,
	va_list			args1
	)
{
	char buffer[1024];
	vsnprintf (buffer, sizeof(buffer), format, args1);

	g_status_list = g_list_append (g_status_list, g_memdup (buffer, strlen(buffer)+1));
}

static void
on_signal (
	int	signum
	)
{
	puts ("on_signal");

	g_main_loop_quit(g_loop);
}

/* terminal resize signal
 */

static void
on_winch (
	int	signum
	)
{
	paint_ncurses ();
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
			printf("Setting PGM protocol number to %i from /etc/protocols.\n", proto->p_proto);
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
	int sock = socket(PF_INET, SOCK_RAW, ipproto_pgm);
	if (sock < 0) {
		int _e = errno;
		puts("on_startup() failed");

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
		printw("on_startup() failed\n");
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

	e = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (e < 0) {
		printw("on_startup() failed\n");
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
		printw("on_startup() failed\n");
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

/* add stdin to event manager */
	g_stdin_channel = g_io_channel_unix_new (fileno(stdin));
	printf ("binding stdin with encoding %s.\n", g_io_channel_get_encoding(g_stdin_channel));

	g_io_add_watch (g_stdin_channel, G_IO_IN | G_IO_PRI, on_stdin_data, NULL);

/* periodic timer to snapshot statistics */
	g_timeout_add (g_snap_interval, (GSourceFunc)on_snap, NULL);

/* period timer to update screen */
	g_timeout_add (g_paint_interval, (GSourceFunc)on_paint, NULL);

	puts ("READY");

	init_ncurses();

	return FALSE;
}

static gboolean
on_paint (
	gpointer data
	)
{
	paint_ncurses();

	return TRUE;
}

static guint
tsi_hash (
        gconstpointer v
        )
{
	return g_str_hash(pgm_print_tsi(v));
}

static gint
tsi_equal (
        gconstpointer   v,
        gconstpointer   v2
        )
{
	return memcmp (v, v2, (6 * sizeof(guint8)) + sizeof(guint16)) == 0;
}

static gboolean
on_io_data (
	GIOChannel* source,
	GIOCondition condition,
	gpointer data
	)
{
	struct timeval now;
	char buffer[4096];
	int fd = g_io_channel_unix_get_fd(source);
	struct sockaddr_in src_addr;
	socklen_t src_addr_len = sizeof(src_addr);
	int len = recvfrom(fd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr*)&src_addr, &src_addr_len);

	gettimeofday (&now, NULL);
	g_packets++;

	struct sockaddr_in dst_addr;
	socklen_t dst_addr_len;
	struct pgm_header *pgm_header;
	char *packet;
	int packet_length;
	int e = pgm_parse_raw(buffer, len, &dst_addr, &dst_addr_len, &pgm_header, &packet, &packet_length);
	if (e == -2)
	{
/* corrupt packet */
		if (!g_nets) {
			g_nets = g_hash_table_new (g_int_hash, g_int_equal);
		}

		struct netstat* netstat = g_hash_table_lookup (g_nets, &src_addr.sin_addr);
		if (netstat == NULL) {
			write_status ("new host publishing corrupt data, local nla %s", inet_ntoa(src_addr.sin_addr));
			netstat = g_malloc0(sizeof(struct netstat));
			netstat->s_addr = src_addr.sin_addr;
			g_hash_table_insert (g_nets, (gpointer)&netstat->s_addr, (gpointer)netstat);
		}

		netstat->corrupt++;
		return TRUE;
	}

	if (e == -1)
	{
/* general error */
		return TRUE;
	}

/* search for existing session */
	if (!g_hosts) {
		g_hosts = g_hash_table_new (tsi_hash, tsi_equal);
	}

	struct tsi tsi;
	memcpy (tsi.gsi, pgm_header->pgm_gsi, 6 * sizeof(guint8));
	tsi.sport = pgm_header->pgm_sport;

	struct hoststat* hoststat = g_hash_table_lookup (g_hosts, &tsi);
	if (hoststat == NULL) {
		write_status ("new tsi %s with local nla %s", pgm_print_tsi (&tsi), inet_ntoa(src_addr.sin_addr));

		hoststat = g_malloc0(sizeof(struct hoststat));
		memcpy (&hoststat->tsi, &tsi, sizeof(struct tsi));
		hoststat->session_start = now;

		g_hash_table_insert (g_hosts, (gpointer)&hoststat->tsi, (gpointer)hoststat);
	}

/* increment statistics */
	memcpy (&hoststat->last_addr, &src_addr.sin_addr, sizeof(src_addr.sin_addr));
	hoststat->general.count++;
	hoststat->general.bytes += len;
	hoststat->general.last = now;

	gboolean err = FALSE;
	switch (pgm_header->pgm_type) {
	case PGM_SPM:
		hoststat->spm.count++;
		hoststat->spm.bytes += len;
		hoststat->spm.last = now;

		err = pgm_verify_spm (pgm_header, packet, packet_length);
		
		if (err) {
			hoststat->spm.invalid++;
			hoststat->spm.last_invalid = now;
		} else {
			hoststat->nla.s_addr = ((struct pgm_spm*)packet)->spm_nla.s_addr;
			if (guint32_lte (g_ntohl( ((struct pgm_spm*)packet)->spm_sqn ), hoststat->spm_sqn)) {
				hoststat->general.duplicate++;
				break;
			}
			hoststat->spm_sqn = g_ntohl( ((struct pgm_spm*)packet)->spm_sqn );
			hoststat->txw_trail = g_ntohl( ((struct pgm_spm*)packet)->spm_trail );
			hoststat->txw_lead = g_ntohl( ((struct pgm_spm*)packet)->spm_lead );
			hoststat->rxw_trail = hoststat->txw_trail;
			hoststat->window_defined = TRUE;
		}
		break;

	case PGM_ODATA:
		hoststat->odata.count++;
		hoststat->odata.bytes += len;
		hoststat->odata.last = now;

		if (!hoststat->window_defined) {
			hoststat->rxw_lead = g_ntohl (((struct pgm_data*)packet)->data_sqn) - 1;
			hoststat->rxw_trail = hoststat->rxw_trail_init = hoststat->rxw_lead + 1;
			hoststat->rxw_constrained = TRUE;
			hoststat->window_defined = TRUE;
		} else {
			if (! guint32_gte( g_ntohl (((struct pgm_data*)packet)->data_sqn) , hoststat->rxw_trail ) )
			{
				hoststat->odata.invalid++;
				hoststat->odata.last_invalid = now;
				break;
			}
			hoststat->rxw_trail = g_ntohl (((struct pgm_data*)packet)->data_trail);
		}

		if (hoststat->rxw_constrained && txw_trail > hoststat->rxw_trail_init) {
			hoststat->rxw_constrained = FALSE;
		}

		if ( guint32_lte ( g_ntohl (((struct pgm_data*)packet)->data_sqn), hoststat->rxw_lead ) ) {
			hoststat->general.duplicate++;
			break;
		} else {
			hoststat->rxw_lead = g_ntohl (((struct pgm_data*)packet)->data_sqn);

			hoststat->odata.tsdu += g_ntohs (pgm_header->pgm_tsdu_length);
		}
		break;

	case PGM_RDATA:
		hoststat->rdata.count++;
		hoststat->rdata.bytes += len;
		hoststat->rdata.last = now;
		break;

	case PGM_POLL:
		hoststat->poll.count++;
		hoststat->poll.bytes += len;
		hoststat->poll.last = now;
		break;

	case PGM_POLR:
		hoststat->polr.count++;
		hoststat->polr.bytes += len;
		hoststat->polr.last = now;
		break;

	case PGM_NAK:
		hoststat->nak.count++;
		hoststat->nak.bytes += len;
		hoststat->nak.last = now;

		err = pgm_verify_nak (pgm_header, packet, packet_length);

		if (err) {
			hoststat->nak.invalid++;
			hoststat->nak.last_invalid = now;
		}
		break;

	case PGM_NNAK:
		hoststat->nnak.count++;
		hoststat->nnak.bytes += len;
		hoststat->nnak.last = now;
		break;

	case PGM_NCF:
		hoststat->ncf.count++;
		hoststat->ncf.bytes += len;
		hoststat->ncf.last = now;
		break;

	case PGM_SPMR:
		hoststat->spmr.count++;
		hoststat->spmr.bytes += len;
		hoststat->spmr.last = now;

		err = pgm_verify_spmr (pgm_header, packet, packet_length);

		if (err) {
			hoststat->spmr.invalid++;
			hoststat->spmr.last_invalid = now;
		}
		break;

	default:
		err = TRUE;
		break;
	}

	if (err) {
		hoststat->general.invalid++;
		hoststat->general.last_invalid = now;
	} else {
		hoststat->general.last_valid = now;
	}

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

/* process input commands from stdin/fd 
 */

static gboolean
on_stdin_data (
	GIOChannel*	source,
	GIOCondition	condition,
	gpointer	data
	)
{
	int ch = wgetch (g_active->window);
	if (ch == ERR) {
		goto out;
	}

/* force redraw */
	if (ch == 12) {
		clearok (curscr, TRUE);
		paint_ncurses ();
		goto out;
	}

	if (ch == 'q') {
		g_main_loop_quit(g_loop);
	}

out:
	return TRUE;
}

static gboolean
snap_stat (
	gpointer	key,
	gpointer	value,
	gpointer	user_data
	)
{
	struct hoststat* hoststat = value;

#define SNAP_STAT(name) \
	{ \
		hoststat->name.snap_count = hoststat->name.count; \
		hoststat->name.snap_bytes = hoststat->name.bytes; \
	}

	SNAP_STAT(spm);
	SNAP_STAT(poll);
	SNAP_STAT(polr);
	SNAP_STAT(odata);
	SNAP_STAT(rdata);
	SNAP_STAT(nak);
	SNAP_STAT(nnak);
	SNAP_STAT(ncf);
	SNAP_STAT(spmr);

	SNAP_STAT(general);

	return FALSE;
}

static gboolean
on_snap (
	gpointer	data
	)
{
	if (!g_hosts) return TRUE;

	gettimeofday (&g_last_snap, NULL);
	g_hash_table_foreach (g_hosts, (GHFunc)snap_stat, NULL);

	return TRUE;
}

/* eof */
