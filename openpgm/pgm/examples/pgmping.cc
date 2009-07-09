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

static int g_port = 7500;
static const char* g_network = "";
static int g_udp_encap_port = 0;

static int g_odata_rate = 10 * 1000;				/* 10 per second */
static int g_odata_interval = (1000 * 1000) / g_odata_rate;	/* 100 ms */
static guint32 g_payload = 0;
static int g_max_tpdu = 1500;
static int g_max_rte = 16*1000*1000;
static int g_sqns = 200;

static gboolean g_fec = FALSE;
static int g_k = 64;
static int g_n = 255;

static gboolean g_send_mode = TRUE;

static pgm_transport_t* g_transport = NULL;

/* stats */
static guint64 g_msg_sent = 0;
static guint64 g_msg_received = 0;
static pgm_time_t g_interval_start = 0;
static pgm_time_t g_latency_current = 0;
static guint64 g_latency_seqno = 0;
static guint64 g_last_seqno = 0;
static pgm_time_t g_latency_total = 0;
static guint64 g_latency_count = 0;
static pgm_time_t g_latency_max = 0;
static pgm_time_t g_latency_min = -1;
static pgm_time_t g_latency_running_average = 0;
static guint64 g_out_total = 0;
static guint64 g_in_total = 0;

static GMainLoop* g_loop = NULL;
static GThread* g_sender_thread = NULL;
static GThread* g_receiver_thread = NULL;
static gboolean g_quit = FALSE;

static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_shutdown (gpointer);
static gboolean on_mark (gpointer);

static void send_odata (void);
static int on_data (pgm_msgv_t*, guint, gpointer);

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
	fprintf (stderr, "  -l              : Listen mode (default send mode)\n");
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
	g_message ("pgmping");
	gboolean enable_http = FALSE;
	gboolean enable_snmpx = FALSE;

	int timeout = 0;

	GOOGLE_PROTOBUF_VERIFY_VERSION;

	setenv("PGM_TIMER", "GTOD", 1);
	setenv("PGM_SLEEP", "USLEEP", 1);

/* parse program arguments */
	const char* binary_name = g_get_prgname();
	int c;
	while ((c = getopt (argc, argv, "s:n:p:m:ld:r:e:k:g:txh")) != -1)
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

		case 'l':	g_send_mode = FALSE; break;

		case 'h':
		case '?': usage (binary_name);
		}
	}

	if (g_fec && ( !g_k || !g_n )) {
		puts ("Invalid Reed-Solomon parameters.");
		usage (binary_name);
	}

	log_init ();
	pgm_init ();

#ifdef CONFIG_WITH_HTTP
	if (enable_http)
		pgm_http_init(PGM_HTTP_DEFAULT_SERVER_PORT);
#endif
#ifdef CONFIG_WITH_SNMP
	if (enable_snmpx)
		pgm_snmp_init();
#endif

	g_loop = g_main_loop_new (NULL, FALSE);

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
	pgm_signal_install (SIGINT, on_signal);
	pgm_signal_install (SIGTERM, on_signal);
#ifndef _WIN32
	pgm_signal_install (SIGHUP, SIG_IGN);
#endif

/* delayed startup */
	g_message ("scheduling startup.");
	g_timeout_add (0, (GSourceFunc)on_startup, NULL);

	if (timeout) {
		g_message ("scheduling shutdown.");
		g_timeout_add (timeout, (GSourceFunc)on_shutdown, NULL);
	}

/* dispatch loop */
	g_message ("entering main event loop ... ");
	g_main_loop_run (g_loop);

	g_message ("event loop terminated, cleaning up.");

/* cleanup */
	g_main_loop_unref(g_loop);
	g_loop = NULL;

	g_quit = TRUE;
	if (g_send_mode)
		g_thread_join (g_sender_thread);
	g_thread_join (g_receiver_thread);

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

	g_message ("finished.");
	return 0;
}

static void
on_signal (
	G_GNUC_UNUSED int signum
	)
{
	g_message ("on_signal");

	g_main_loop_quit(g_loop);
}

static gboolean
on_shutdown (
	G_GNUC_UNUSED gpointer data
	)
{
	g_message ("shutdown");
	g_main_loop_quit(g_loop);
	return FALSE;
}

static gboolean
on_startup (
	G_GNUC_UNUSED gpointer data
	)
{
	g_message ("startup.");
	g_message ("create transport.");

	pgm_gsi_t gsi;
	GError* err = NULL;
	if (!pgm_gsi_create_from_hostname (&gsi, &err)) {
		g_error ("creating GSI: %s", err->message);
		g_error_free (err);
		g_main_loop_quit (g_loop);
		return FALSE;
	}

	struct group_source_req recv_gsr, send_gsr;
	gsize recv_len = 1;
	int e = pgm_if_parse_transport (g_network, AF_INET, &recv_gsr, &recv_len, &send_gsr);
	g_assert (e == 0);
	g_assert (recv_len == 1);

	if (g_udp_encap_port) {
		((struct sockaddr_in*)&send_gsr.gsr_group)->sin_port = g_htons (g_udp_encap_port);
		((struct sockaddr_in*)&recv_gsr.gsr_group)->sin_port = g_htons (g_udp_encap_port);
	}

	e = pgm_transport_create (&g_transport, &gsi, 0, g_port, &recv_gsr, 1, &send_gsr);
	g_assert (e == 0);

	pgm_transport_set_sndbuf (g_transport, 1024 * 1024);
	pgm_transport_set_rcvbuf (g_transport, 1024 * 1024);
	pgm_transport_set_max_tpdu (g_transport, g_max_tpdu);
	pgm_transport_set_txw_sqns (g_transport, g_sqns);
	pgm_transport_set_txw_max_rte (g_transport, g_max_rte);
	pgm_transport_set_rxw_sqns (g_transport, g_sqns);
	pgm_transport_set_hops (g_transport, 16);
	pgm_transport_set_ambient_spm (g_transport, pgm_secs(30));
	guint spm_heartbeat[] = { pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(1300), pgm_secs(7
), pgm_secs(16), pgm_secs(25), pgm_secs(30) };
	pgm_transport_set_heartbeat_spm (g_transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat));
	pgm_transport_set_peer_expiry (g_transport, pgm_secs(300));
	pgm_transport_set_spmr_expiry (g_transport, pgm_msecs(250));
	pgm_transport_set_nak_bo_ivl (g_transport, pgm_msecs(50));
	pgm_transport_set_nak_rpt_ivl (g_transport, pgm_secs(2));
	pgm_transport_set_nak_rdata_ivl (g_transport, pgm_secs(2));
	pgm_transport_set_nak_data_retries (g_transport, 50);
	pgm_transport_set_nak_ncf_retries (g_transport, 50);

	if (g_fec) {
		pgm_transport_set_fec (g_transport, 0, TRUE, TRUE, g_n, g_k);
	}

	e = pgm_transport_bind (g_transport);
	if (e < 0) {
#ifndef _WIN32
		if      (e == -1)
			g_critical ("pgm_transport_bind failed errno %i: \"%s\"", errno, strerror(errno));
		else if (e == -2)
			g_critical ("pgm_transport_bind failed h_errno %i: \"%s\"", h_errno, hstrerror(h_errno));
#else
		if	(e == -1) {
			DWORD dwError = WSAGetLastError();
			g_critical ("pgm_transport_bind failed error %ld", dwError);
		}
#endif /* _WIN32 */
		else
			g_critical ("pgm_transport_bind failed e %i", e);
		g_main_loop_quit(g_loop);
		return FALSE;
	}
	g_assert (e == 0);

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add(2 * 1000, (GSourceFunc)on_mark, NULL);

	if (g_send_mode)
	{
		GError* err;
		g_sender_thread = g_thread_create_full (sender_thread,
							g_transport,
							0,
							TRUE,
							TRUE,
							G_THREAD_PRIORITY_NORMAL,
							&err);
		if (!g_sender_thread) {
			g_critical ("g_thread_create_full failed errno %i: \"%s\"", err->code, err->message);
			g_main_loop_quit(g_loop);
			return FALSE;
		}
	}

	{
		GError* err;
		g_receiver_thread = g_thread_create_full (receiver_thread,
							g_transport,
							0,
							TRUE,
							TRUE,
							G_THREAD_PRIORITY_NORMAL,
							&err);
		if (!g_receiver_thread) {
			g_critical ("g_thread_create_full failed errno %i: \"%s\"", err->code, err->message);
			g_main_loop_quit(g_loop);
			return FALSE;
		}
	}

	g_message ("startup complete.");
	return FALSE;
}

static gpointer
sender_thread (
	gpointer	data
	)
{
	pgm_transport_t* transport = (pgm_transport_t*)data;
	example::Ping ping;
	string subject("PING.PGM.TEST.");
	char hostname[NI_MAXHOST + 1];
	char payload[1000];
	gpointer buffer = NULL;

	gethostname (hostname, sizeof(hostname));
	subject.append(hostname);

	ping.mutable_subscription_header()->set_subject (subject);
	ping.mutable_market_data_header()->set_msg_type (example::MarketDataHeader::MSG_VERIFY);
	ping.mutable_market_data_header()->set_rec_type (example::MarketDataHeader::PING);
	ping.mutable_market_data_header()->set_rec_status (example::MarketDataHeader::STATUS_OK);

	do {
		guint64 latency;
		if (g_msg_sent && g_latency_seqno + 1 == g_msg_sent)
			latency = g_latency_current;
		else
			latency = g_odata_interval;

		guint64 now = pgm_time_update_now();
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

		struct pgm_iovec vector[1];
		vector[0].iov_base = skb;
		vector[0].iov_len  = skb->len;

		gssize e = pgm_transport_send_skbv (g_transport, vector, G_N_ELEMENTS(vector), 0, FALSE);
	        if (e < 0) {
			g_warning ("pgm_transport_send failed: %i/%s.", errno, strerror(errno));
	                return NULL;
	        }

		g_out_total += e;
		g_msg_sent++;
	} while (!g_quit);

	return NULL;
}

static gpointer
receiver_thread (
	gpointer	data
	)
{
	pgm_transport_t* transport = (pgm_transport_t*)data;
	pgm_msgv_t msgv[20];
	gssize len = 0;
	pgm_time_t loss_tstamp = 0;
	pgm_tsi_t  loss_tsi;
	guint32	   loss_count = 0;

	memset (&loss_tsi, 0, sizeof(loss_tsi));

	int efd = epoll_create (IP_MAX_MEMBERSHIPS);
	if (efd < 0) {
		g_error ("epoll_create failed errno %i: \"%s\"", errno, strerror(errno));
 		g_main_loop_quit(g_loop);
		return NULL;
	}

	int retval = pgm_transport_epoll_ctl (g_transport, efd, EPOLL_CTL_ADD, EPOLLIN);
	if (retval < 0) {
		g_error ("pgm_epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit(g_loop);
		return NULL;
	}

	do {
		len = pgm_transport_recvmsgv (g_transport, msgv, G_N_ELEMENTS(msgv), MSG_DONTWAIT /* non-blocking */);

		if (loss_count) {
			pgm_time_t elapsed = pgm_time_update_now() - loss_tstamp;
			if (elapsed >= pgm_secs(1)) {
				g_warning ("pgm data lost %" G_GUINT32_FORMAT " packets detected from %s",
						loss_count, pgm_print_tsi (&loss_tsi));
				loss_count = 0;
			}
		}

		if (len >= 0)
		{
			on_data (msgv, len, NULL);
		}
		else if (errno == EAGAIN)		/* len == -1, an error occured */
		{
			struct epoll_event events[1];	/* wait for maximum 1 event */
			epoll_wait (efd, events, G_N_ELEMENTS(events), 1000 /* ms */);
		}
		else if (errno == ECONNRESET)
		{
			const pgm_sock_err_t* pgm_sock_err = (pgm_sock_err_t*)msgv[0].msgv_iov->iov_base;

			if (!loss_count) loss_tstamp = pgm_time_update_now();

			if (pgm_tsi_equal (&pgm_sock_err->tsi, &loss_tsi))
			{
				loss_count += pgm_sock_err->lost_count;
			}
			else
			{
				loss_count = pgm_sock_err->lost_count;
				memcpy (&loss_tsi, &pgm_sock_err->tsi, sizeof(pgm_tsi_t));
			}
		}
		else if (errno == ENOTCONN)		/* socket(s) closed */
		{
			g_error ("pgm socket closed.");
			g_main_loop_quit(g_loop);
			break;
		}
		else
		{
			g_error ("pgm socket failed errno %i: \"%s\"", errno, strerror(errno));
			g_main_loop_quit(g_loop);
			break;
		}
	} while (!g_quit);

	close (efd);
	return NULL;
}

static int
on_data (
	pgm_msgv_t*	msgv,
	guint		len,
	G_GNUC_UNUSED gpointer	user_data
	)
{
	example::Ping ping;

	while (len)
	{
		struct pgm_iovec* msgv_iov = msgv->msgv_iov;
		struct pgm_sk_buff_t* skb  = (struct pgm_sk_buff_t*)msgv->msgv_iov->iov_base;

/* only parse first fragment of each apdu */
		if (!ping.ParseFromArray (skb->data, skb->len))
			goto next_msg;
//		g_message ("payload: %s", ping.DebugString().c_str());

		{
			pgm_time_t send_time	= ping.time();
			guint64 seqno		= ping.seqno();
			guint64 latency		= ping.latency();

			g_in_total += skb->len;
			g_msg_received++;

/* handle ping */
			pgm_time_t elapsed	= skb->tstamp - send_time;

			if (pgm_time_after(send_time, skb->tstamp)) {
				g_message ("timer mismatch, send time = now + %.3f ms",
						pgm_to_msecsf(send_time - skb->tstamp));
				goto next_msg;
			}
			g_latency_current	= pgm_to_secs(elapsed);
			g_latency_seqno		= seqno;
			g_latency_total	       += elapsed;

			if (elapsed > g_latency_max) {
				g_latency_max = elapsed;
			} else if (elapsed < g_latency_min) {
				g_latency_min = elapsed;
			}

			g_latency_running_average += elapsed;
			g_latency_count++;
		}

/* move onto next apdu */
next_msg:
		{
			guint apdu_len = 0;
			struct pgm_iovec* p = msgv_iov;
			for (guint j = 0; j < msgv->msgv_iovlen; j++) { /* # elements */
				apdu_len += p->iov_len;
				p++;
			}
			len -= apdu_len;
			msgv++;
		}
	}

	return 0;
}

/* idle log notification
 */

static gboolean
on_mark (
	G_GNUC_UNUSED gpointer data
	)
{
	double interval = pgm_to_secsf(pgm_time_update_now() - g_interval_start);
	g_interval_start = pgm_time_now;

/* receiving a ping */
	if (g_latency_count)
	{
		pgm_time_t average = g_latency_total / g_latency_count;

		if (g_latency_count < 10)
		{
			g_message ("seqno=%" G_GUINT64_FORMAT " time=%.03f ms",
					g_latency_seqno,
					pgm_to_msecsf(average));
		}
		else
		{
			double seq_rate = (g_latency_seqno - g_last_seqno) / interval;
			double out_rate = g_out_total * 8.0 / 1000000.0 / interval;
			double  in_rate = g_in_total  * 8.0 / 1000000.0 / interval;
			g_message ("s=%.01f avg=%.03f min=%.03f max=%.03f ms o=%.2f i=%.2f mbit",
					seq_rate,
					pgm_to_msecsf(average),
					pgm_to_msecsf(g_latency_min),
					pgm_to_msecsf(g_latency_max),
					out_rate,
					in_rate);
		}

/* reset interval counters */
		g_latency_total		= 0;
		g_latency_count		= 0;
		g_last_seqno		= g_latency_seqno;
		g_latency_min		= -1;
		g_latency_max		= 0;
		g_out_total		= 0;
		g_in_total		= 0;
	}

	return TRUE;
}

/* eof */
