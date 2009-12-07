/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Simple send/reply ping tool using the PGM transport.
 *
 * With no arguments, one message is sent per second.
 *
 * Copyright (c) 2006-2009 Miru Limited.
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
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#ifndef _WIN32
#	include <sched.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <arpa/inet.h>
#endif

#include <glib.h>

#include <pgm/pgm.h>
#include <pgm/backtrace.h>
#include <pgm/log.h>
#ifdef CONFIG_WITH_HTTP
#	include <pgm/http.h>
#endif
#ifdef CONFIG_WITH_SNMP
#	include <pgm/snmp.h>
#endif

#include "ping.pb.h"

using namespace std;


/* typedefs */


/* globals */

static int g_port = 0;
static const char* g_network = "";
static int g_udp_encap_port = 0;

static int g_odata_rate = 0;
static int g_odata_interval = 0;
static guint32 g_payload = 0;
static int g_max_tpdu = 1500;
static int g_max_rte = 16*1000*1000;
static int g_sqns = 200;

static gboolean g_fec = FALSE;
static int g_k = 64;
static int g_n = 255;

static enum {
	PGMPING_MODE_SOURCE,
	PGMPING_MODE_RECEIVER,
	PGMPING_MODE_INITIATOR,
	PGMPING_MODE_REFLECTOR
} g_mode = PGMPING_MODE_INITIATOR;

static pgm_transport_t* g_transport = NULL;

/* stats */
static guint64 g_msg_sent = 0;
static guint64 g_msg_received = 0;
static pgm_time_t g_interval_start = 0;
static pgm_time_t g_latency_current = 0;
static guint64 g_latency_seqno = 0;
static guint64 g_last_seqno = 0;
static double g_latency_total = 0.0;
static double g_latency_square_total = 0.0;
static guint64 g_latency_count = 0;
static double g_latency_max = 0.0;
static double g_latency_min = INFINITY;
static double g_latency_running_average = 0.0;
static guint64 g_out_total = 0;
static guint64 g_in_total = 0;

static GMainLoop* g_loop = NULL;
static GThread* g_sender_thread = NULL;
static GThread* g_receiver_thread = NULL;
static gboolean g_quit;
#ifdef G_OS_UNIX
static int g_quit_pipe[2];
static void on_signal (int, gpointer);
#else
static HANDLE g_quit_event;
static BOOL on_console_ctrl (DWORD);
#endif

static gboolean on_startup (gpointer);
static gboolean on_shutdown (gpointer);
static gboolean on_mark (gpointer);

static void send_odata (void);
static int on_msgv (pgm_msgv_t*, guint, gpointer);

static gpointer sender_thread (gpointer);
static gpointer receiver_thread (gpointer);


G_GNUC_NORETURN static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	fprintf (stderr, "  -n <network>    : Multicast group or unicast IP address\n");
	fprintf (stderr, "  -s <port>       : IP port\n");
        fprintf (stderr, "  -p <port>       : Encapsulate PGM in UDP on IP port\n");
	fprintf (stderr, "  -d <seconds>    : Terminate transport after duration.\n");
	fprintf (stderr, "  -m <frequency>  : Number of message to send per second\n");
	fprintf (stderr, "  -o              : Send-only mode (default send & receive mode)\n");
	fprintf (stderr, "  -l              : Listen-only mode\n");
	fprintf (stderr, "  -e              : Relect mode\n");
        fprintf (stderr, "  -r <rate>       : Regulate to rate bytes per second\n");
        fprintf (stderr, "  -f <type>       : Enable FEC with either proactive or ondemand parity\n");
        fprintf (stderr, "  -k <k>          : Configure Reed-Solomon code (n, k)\n");
        fprintf (stderr, "  -g <n>\n");
        fprintf (stderr, "  -t              : Enable HTTP administrative interface\n");
        fprintf (stderr, "  -x              : Enable SNMP interface\n");
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	GError* err = NULL;
	gboolean enable_http = FALSE;
	gboolean enable_snmpx = FALSE;
	int timeout = 0;

	GOOGLE_PROTOBUF_VERIFY_VERSION;

	setenv("PGM_TIMER", "GTOD", 1);
	setenv("PGM_SLEEP", "USLEEP", 1);

	g_message ("pgmping");

/* parse program arguments */
	const char* binary_name = g_get_prgname();
	int c;
	while ((c = getopt (argc, argv, "s:n:p:m:old:r:fek:g:txh")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;
		case 'p':	g_udp_encap_port = atoi (optarg); break;
		case 'r':	g_max_rte = atoi (optarg); break;

		case 'f':	g_fec = TRUE; break;
		case 'k':	g_k = atoi (optarg); break;
		case 'g':	g_n = atoi (optarg); break;

		case 't':	enable_http = TRUE; break;
		case 'x':	enable_snmpx = TRUE; break;

		case 'm':	g_odata_rate = atoi (optarg);
				g_odata_interval = (1000 * 1000) / g_odata_rate; break;
		case 'd':	timeout = 1000 * atoi (optarg); break;

		case 'o':	g_mode = PGMPING_MODE_SOURCE; break;
		case 'l':	g_mode = PGMPING_MODE_RECEIVER; break;
		case 'e':	g_mode = PGMPING_MODE_REFLECTOR; break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	if (g_fec && ( !g_k || !g_n )) {
		g_error ("Invalid Reed-Solomon parameters.");
		usage (binary_name);
	}

	log_init ();
	pgm_init ();

#ifdef CONFIG_WITH_HTTP
	if (enable_http) {
		if (!pgm_http_init (PGM_HTTP_DEFAULT_SERVER_PORT, &err)) {
			g_error ("Unable to start HTTP interface: %s", err->message);
			g_error_free (err);
			pgm_shutdown ();
			return EXIT_FAILURE;
		}
	}
#endif
#ifdef CONFIG_WITH_SNMP
	if (enable_snmpx) {
		if (!pgm_snmp_init (&err)) {
			g_error ("Unable to start SNMP interface: %s", err->message);
			g_error_free (err);
#ifdef CONFIG_WITH_HTTP
			if (enable_http)
				pgm_http_shutdown ();
#endif
			pgm_shutdown ();
			return EXIT_FAILURE;
		}
	}
#endif

	g_loop = g_main_loop_new (NULL, FALSE);

	g_quit = FALSE;

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
#ifdef SIGHUP
	signal (SIGHUP,  SIG_IGN);
#endif
#ifdef G_OS_UNIX
	pipe (g_quit_pipe);
	pgm_signal_install (SIGINT,  on_signal, g_loop);
	pgm_signal_install (SIGTERM, on_signal, g_loop);
#else
	g_quit_event = CreateEvent (NULL, TRUE, FALSE, TEXT("QuitEvent"));
	SetConsoleCtrlHandler ((PHANDLER_ROUTINE)on_console_ctrl, TRUE);
#endif /* !G_OS_UNIX */

/* delayed startup */
	g_message ("scheduling startup.");
	g_timeout_add (0, (GSourceFunc)on_startup, g_loop);

	if (timeout) {
		g_message ("scheduling shutdown.");
		g_timeout_add (timeout, (GSourceFunc)on_shutdown, g_loop);
	}

/* dispatch loop */
	g_message ("entering main event loop ... ");
	g_main_loop_run (g_loop);

	g_message ("event loop terminated, cleaning up.");

/* cleanup */
	g_quit = TRUE;
#ifdef G_OS_UNIX
	const char one = '1';
	write (g_quit_pipe[1], &one, sizeof(one));
	if (PGMPING_MODE_SOURCE == g_mode || PGMPING_MODE_INITIATOR == g_mode)
		g_thread_join (g_sender_thread);
	g_thread_join (g_receiver_thread);
	close (g_quit_pipe[0]);
	close (g_quit_pipe[1]);
#else
	SetEvent (g_quit_event);
	if (PGMPING_MODE_SOURCE == g_mode || PGMPING_MODE_INITIATOR == g_mode)
		g_thread_join (g_sender_thread);
	g_thread_join (g_receiver_thread);
	CloseHandle (g_quit_event);
#endif

	g_main_loop_unref (g_loop);
	g_loop = NULL;

	if (g_transport) {
		g_message ("destroying transport.");
		pgm_transport_destroy (g_transport, TRUE);
		g_transport = NULL;
	}

#ifdef CONFIG_WITH_HTTP
	if (enable_http)
		pgm_http_shutdown();
#endif
#ifdef CONFIG_WITH_SNMP
	if (enable_snmpx)
		pgm_snmp_shutdown();
#endif

	google::protobuf::ShutdownProtobufLibrary();

	g_message ("PGM engine shutdown.");
	pgm_shutdown ();
	g_message ("finished.");
	return EXIT_SUCCESS;
}

#ifdef G_OS_UNIX
static
void
on_signal (
	int		signum,
	gpointer	user_data
	)
{
	GMainLoop* loop = (GMainLoop*)user_data;
	g_message ("on_signal (signum:%d user-data:%p)",
		   signum, user_data);
	g_main_loop_quit (loop);
}
#else
static
BOOL
on_console_ctrl (
	DWORD		dwCtrlType
	)
{
	g_message ("on_console_ctrl (dwCtrlType:%lu)", (unsigned long)dwCtrlType);
	g_main_loop_quit (g_loop);
	return TRUE;
}
#endif /* !G_OS_UNIX */

static
gboolean
on_shutdown (
	gpointer	user_data
	)
{
	GMainLoop* loop = (GMainLoop*)user_data;
	g_message ("on_shutdown (user-data:%p)", user_data);
	g_main_loop_quit (loop);
	return FALSE;
}

static
gboolean
on_startup (
	gpointer	user_data
	)
{
	GMainLoop* loop = (GMainLoop*)user_data;
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
		g_main_loop_quit (loop);
		return FALSE;
	}
/* create global session identifier */
	if (!pgm_gsi_create_from_hostname (&res->ti_gsi, &err)) {
		g_error ("creating GSI: %s", err->message);
		g_error_free (err);
		pgm_if_free_transport_info (res);
		g_main_loop_quit (loop);
		return FALSE;
	}
/* UDP encapsulation */
	if (g_udp_encap_port) {
		res->ti_udp_encap_ucast_port = g_udp_encap_port;
		res->ti_udp_encap_mcast_port = g_udp_encap_port;
	}
	if (g_port)
		res->ti_dport = g_port;
	if (!pgm_transport_create (&g_transport, res, &err)) {
		g_error ("creating transport: %s", err->message);
		g_error_free (err);
		pgm_if_free_transport_info (res);
		g_main_loop_quit (loop);
		return FALSE;
	}
	pgm_if_free_transport_info (res);

/* set PGM parameters */
	pgm_transport_set_nonblocking (g_transport, TRUE);
	if (PGMPING_MODE_SOURCE == g_mode ||
	    PGMPING_MODE_INITIATOR == g_mode ||
	    PGMPING_MODE_REFLECTOR == g_mode)
	{
		const guint spm_heartbeat[] = { pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(1300), pgm_secs(7), pgm_secs(16), pgm_secs(25), pgm_secs(30) };

		if (PGMPING_MODE_SOURCE == g_mode)
			pgm_transport_set_send_only (g_transport, TRUE);
		pgm_transport_set_txw_sqns (g_transport, g_sqns * 4);
		pgm_transport_set_txw_max_rte (g_transport, g_max_rte);
		pgm_transport_set_ambient_spm (g_transport, pgm_secs(30));
		pgm_transport_set_heartbeat_spm (g_transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat));
	}
	if (PGMPING_MODE_RECEIVER == g_mode ||
	    PGMPING_MODE_INITIATOR == g_mode ||
	    PGMPING_MODE_REFLECTOR == g_mode)
	{
		if (PGMPING_MODE_RECEIVER == g_mode)
			pgm_transport_set_recv_only (g_transport, TRUE, FALSE);
		pgm_transport_set_peer_expiry (g_transport, pgm_secs(300));
		pgm_transport_set_spmr_expiry (g_transport, pgm_msecs(250));
		pgm_transport_set_nak_bo_ivl (g_transport, pgm_msecs(50));
		pgm_transport_set_nak_rpt_ivl (g_transport, pgm_secs(2));
		pgm_transport_set_nak_rdata_ivl (g_transport, pgm_secs(2));
		pgm_transport_set_nak_data_retries (g_transport, 50);
		pgm_transport_set_nak_ncf_retries (g_transport, 50);
	}
	pgm_transport_set_sndbuf (g_transport, 1024 * 1024);
	pgm_transport_set_rcvbuf (g_transport, 1024 * 1024);
	pgm_transport_set_max_tpdu (g_transport, g_max_tpdu);
	pgm_transport_set_rxw_sqns (g_transport, g_sqns);
	pgm_transport_set_hops (g_transport, 16);

	if (g_fec)
		pgm_transport_set_fec (g_transport, 0, TRUE, TRUE, g_n, g_k);

/* assign transport to specified address */
	if (!pgm_transport_bind (g_transport, &err)) {
		g_error ("binding transport: %s", err->message);
		g_error_free (err);
		pgm_transport_destroy (g_transport, FALSE);
		g_transport = NULL;
		g_main_loop_quit (loop);
		return FALSE;
	}

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add (2 * 1000, (GSourceFunc)on_mark, NULL);

	if (PGMPING_MODE_SOURCE == g_mode || PGMPING_MODE_INITIATOR == g_mode)
	{
		g_sender_thread = g_thread_create_full (sender_thread,
							g_transport,
							0,
							TRUE,
							TRUE,
							G_THREAD_PRIORITY_NORMAL,
							&err);
		if (!g_sender_thread) {
			g_critical ("g_thread_create_full failed errno %i: \"%s\"", err->code, err->message);
			g_main_loop_quit (loop);
			return FALSE;
		}
	}

	{
		g_receiver_thread = g_thread_create_full (receiver_thread,
							g_transport,
							0,
							TRUE,
							TRUE,
							G_THREAD_PRIORITY_HIGH,
							&err);
		if (!g_receiver_thread) {
			g_critical ("g_thread_create_full failed errno %i: \"%s\"", err->code, err->message);
			g_main_loop_quit (loop);
			return FALSE;
		}
	}

	g_message ("startup complete.");
	return FALSE;
}

static
gpointer
sender_thread (
	gpointer	user_data
	)
{
	pgm_transport_t* transport = (pgm_transport_t*)user_data;
	example::Ping ping;
	string subject("PING.PGM.TEST.");
	char hostname[NI_MAXHOST + 1];
	char payload[1000];
	gpointer buffer = NULL;
	struct epoll_event events[1];
	guint64 latency, now, last = pgm_time_update_now();

	gethostname (hostname, sizeof(hostname));
	subject.append(hostname);

	ping.mutable_subscription_header()->set_subject (subject);
	ping.mutable_market_data_header()->set_msg_type (example::MarketDataHeader::MSG_VERIFY);
	ping.mutable_market_data_header()->set_rec_type (example::MarketDataHeader::PING);
	ping.mutable_market_data_header()->set_rec_status (example::MarketDataHeader::STATUS_OK);

	int efd_again = epoll_create (IP_MAX_MEMBERSHIPS);
	if (efd_again < 0) {
		g_error ("epoll_create failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
	if (pgm_transport_epoll_ctl (g_transport, efd_again, EPOLL_CTL_ADD, EPOLLOUT | EPOLLONESHOT) < 0) {
		g_error ("pgm_epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = g_quit_pipe[0];
	if (epoll_ctl (efd_again, EPOLL_CTL_ADD, g_quit_pipe[0], &event) < 0) {
		g_error ("epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}

	do {
		if (g_msg_sent && g_latency_seqno + 1 == g_msg_sent)
			latency = g_latency_current;
		else
			latency = g_odata_interval;

/* wait on packet rate limit */
		now = pgm_time_update_now();
		while ((now - last) < g_odata_interval) {
			pgm_time_sleep (g_odata_interval - (now - last));
			now = pgm_time_update_now();
		}
		last = now;
		ping.set_time (now);
		ping.set_seqno (g_msg_sent);
		ping.set_latency (latency);
		ping.set_payload (payload, sizeof(payload));

		const int header_size = pgm_transport_pkt_offset(FALSE);
		const int apdu_size = ping.ByteSize();
		struct pgm_sk_buff_t* skb = pgm_alloc_skb (g_max_tpdu);
		pgm_skb_reserve (skb, header_size);
		pgm_skb_put (skb, apdu_size);
		ping.SerializeToArray (skb->data, skb->len);

		struct timeval tv;
		int timeout;
		gsize bytes_written;
		PGMIOStatus status;
again:
		status = pgm_send_skbv (g_transport, &skb, 1, TRUE, &bytes_written);
		switch (status) {
		case PGM_IO_STATUS_RATE_LIMITED:
		{
			pgm_transport_get_rate_remaining (g_transport, &tv);
			timeout = (tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000);
			if (timeout < 2) timeout = 0;
			int ready = epoll_wait (efd_again, events, G_N_ELEMENTS(events), timeout /* ms */);
			if (G_UNLIKELY(g_quit))
				break;
			goto again;
		}
		case PGM_IO_STATUS_WOULD_BLOCK:
		{
			int ready = epoll_wait (efd_again, events, G_N_ELEMENTS(events), -1 /* ms */);
			if (G_UNLIKELY(g_quit))
				break;
			if (ready > 0 &&
			    pgm_transport_epoll_ctl (g_transport, efd_again, EPOLL_CTL_MOD, EPOLLOUT | EPOLLONESHOT) < 0)
			{
				g_error ("pgm_epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
				g_main_loop_quit (g_loop);
				return NULL;
			}
			goto again;
		}
		case PGM_IO_STATUS_NORMAL:
			break;
		default:
			g_warning ("pgm_send_skbv failed");
			g_main_loop_quit (g_loop);
			return NULL;
		}
		g_out_total += bytes_written;
		g_msg_sent++;
	} while (!g_quit);

	return NULL;
}

static
gpointer
receiver_thread (
	gpointer	data
	)
{
	pgm_transport_t* transport = (pgm_transport_t*)data;
	pgm_msgv_t msgv[20];
	struct epoll_event events[20];
	pgm_time_t lost_tstamp = 0;
	pgm_tsi_t  lost_tsi;
	guint32	   lost_count = 0;

	memset (&lost_tsi, 0, sizeof(lost_tsi));

	int efd = epoll_create (IP_MAX_MEMBERSHIPS);
	if (efd < 0) {
		g_error ("epoll_create failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
	if (pgm_transport_epoll_ctl (g_transport, efd, EPOLL_CTL_ADD, EPOLLIN) < 0) {
		g_error ("pgm_epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
	struct epoll_event event;
	event.events = EPOLLIN;
	if (epoll_ctl (efd, EPOLL_CTL_ADD, g_quit_pipe[0], &event) < 0) {
		g_error ("epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}

	do {
		struct timeval tv;
		int timeout;
		gsize len;
		GError* err = NULL;
		const PGMIOStatus status = pgm_recvmsgv (g_transport,
						         msgv,
						         G_N_ELEMENTS(msgv),
						         MSG_ERRQUEUE,
						         &len,
						         &err);
		if (lost_count) {
			pgm_time_t elapsed = pgm_time_update_now() - lost_tstamp;
			if (elapsed >= pgm_secs(1)) {
				g_warning ("pgm data lost %" G_GUINT32_FORMAT " packets detected from %s",
						lost_count, pgm_tsi_print (&lost_tsi));
				lost_count = 0;
			}
		}

		switch (status) {
		case PGM_IO_STATUS_NORMAL:
			on_msgv (msgv, len, NULL);
			break;
		case PGM_IO_STATUS_TIMER_PENDING:
			pgm_transport_get_timer_pending (g_transport, &tv);
			goto block;
		case PGM_IO_STATUS_RATE_LIMITED:
			pgm_transport_get_rate_remaining (g_transport, &tv);
/* fall through */
		case PGM_IO_STATUS_WOULD_BLOCK:
block:
			timeout = PGM_IO_STATUS_WOULD_BLOCK == status ? -1 : ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
			if (timeout > 0 && timeout < 2) timeout = 0;
			epoll_wait (efd, events, G_N_ELEMENTS(events), timeout /* ms */);
			break;
		case PGM_IO_STATUS_RESET:
		{
			struct pgm_sk_buff_t* skb = msgv[0].msgv_skb[0];
			lost_tstamp = skb->tstamp;
			if (pgm_tsi_equal (&skb->tsi, &lost_tsi))
				lost_count += skb->sequence;
			else {
				lost_count = skb->sequence;
				memcpy (&lost_tsi, &skb->tsi, sizeof(pgm_tsi_t));
			}
			pgm_free_skb (skb);
			break;
		}
		default:
			if (err) {
				g_warning ("%s", err->message);
				g_error_free (err);
			}
			break;
		}
	} while (!g_quit);

	close (efd);
	return NULL;
}

static
int
on_msgv (
	pgm_msgv_t*	msgv,		/* an array of msgvs */
	guint		len,
	G_GNUC_UNUSED gpointer	user_data
	)
{
	example::Ping ping;
	guint i = 0;

	while (len)
	{
		const struct pgm_sk_buff_t* pskb = msgv[i].msgv_skb[0];
		gsize apdu_len = 0;
		for (unsigned j = 0; j < msgv[i].msgv_len; j++)
			apdu_len += msgv[i].msgv_skb[j]->len;

		if (PGMPING_MODE_REFLECTOR == g_mode)
		{
			PGMIOStatus status;
again:
			status = pgm_send (g_transport, pskb->data, pskb->len, NULL);
			switch (status) {
			case PGM_IO_STATUS_RATE_LIMITED:
			case PGM_IO_STATUS_WOULD_BLOCK:
				goto again;

			case PGM_IO_STATUS_NORMAL:
				break;

			default:
				g_warning ("pgm_send_skbv failed");
				g_main_loop_quit (g_loop);
				return 0;
			}
			goto next_msg;
		}

/* only parse first fragment of each apdu */
		if (!ping.ParseFromArray (pskb->data, pskb->len))
			goto next_msg;
//		g_message ("payload: %s", ping.DebugString().c_str());

		{
			const pgm_time_t send_time	= ping.time();
			const guint64 seqno		= ping.seqno();
			const guint64 latency		= ping.latency();

			g_in_total += pskb->len;
			g_msg_received++;

/* handle ping */
			const double elapsed = pgm_to_usecsf (pskb->tstamp - send_time);

			if (pgm_time_after(send_time, pskb->tstamp)){
				g_message ("timer mismatch, send time = now + %.3f ms",
					   pgm_to_msecsf(send_time - pskb->tstamp));
				goto next_msg;
			}
			g_latency_current	= pgm_to_secs(pskb->tstamp - send_time);
			g_latency_seqno		= seqno;
			g_latency_total	       += elapsed;
			g_latency_square_total += elapsed * elapsed;

			if (elapsed > g_latency_max)
				g_latency_max = elapsed;
			if (elapsed < g_latency_min)
				g_latency_min = elapsed;

			g_latency_running_average += elapsed;
			g_latency_count++;
		}

/* move onto next apdu */
next_msg:
		i++;
		len -= apdu_len;
	}

	return 0;
}

/* idle log notification
 */

static
gboolean
on_mark (
	G_GNUC_UNUSED gpointer data
	)
{
	const pgm_time_t now = pgm_time_update_now ();
	const double interval = pgm_to_secsf(now - g_interval_start);
	g_interval_start = now;

/* receiving a ping */
	if (g_latency_count)
	{
		const double average = g_latency_total / g_latency_count;
		const double variance = g_latency_square_total / g_latency_count
					- average * average;
		const double standard_deviation = sqrt (variance);

		if (g_latency_count < 10)
		{
			if (average < 1000.0)
				g_message ("seqno=%" G_GUINT64_FORMAT " time=%.01f us",
						g_latency_seqno, average);
			else
				g_message ("seqno=%" G_GUINT64_FORMAT " time=%.01f ms",
						g_latency_seqno, average / 1000);
		}
		else
		{
			double seq_rate = (g_latency_seqno - g_last_seqno) / interval;
			double out_rate = g_out_total * 8.0 / 1000000.0 / interval;
			double  in_rate = g_in_total  * 8.0 / 1000000.0 / interval;
			if (g_latency_min < 1000.0)
				g_message ("s=%.01f avg=%.01f min=%.01f max=%.01f stddev=%0.1f us o=%.2f i=%.2f mbit",
					seq_rate, average, g_latency_min, g_latency_max, standard_deviation, out_rate, in_rate);
			else
				g_message ("s=%.01f avg=%.01f min=%.01f max=%.01f stddev=%0.1f ms o=%.2f i=%.2f mbit",
					seq_rate, average / 1000, g_latency_min / 1000, g_latency_max / 1000, standard_deviation / 1000, out_rate, in_rate);
		}

/* reset interval counters */
		g_latency_total		= 0.0;
		g_latency_square_total	= 0.0;
		g_latency_count		= 0;
		g_last_seqno		= g_latency_seqno;
		g_latency_min		= INFINITY;
		g_latency_max		= 0.0;
		g_out_total		= 0;
		g_in_total		= 0;
	}

	return TRUE;
}

/* eof */
