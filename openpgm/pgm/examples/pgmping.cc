/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Complex send/reply ping tool for performance testing.  Performs
 * coarse grain rate limiting outside of the PGM transport in order to
 * leave network capacity for recovery traffic.
 *
 * Depends upon GLib framework for accelerated development, depends
 * upon Google Protocol Bufers for the messaging layer.  The build
 * scripts are hardcoded with paths and need to be manually updated.
 *
 * With no arguments, one message is sent per second.
 *
 * Copyright (c) 2006-2010 Miru Limited.
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

/* MSVC secure CRT */
#define _CRT_SECURE_NO_WARNINGS		1

/* c99 compatibility for c++ */
#define __STDC_LIMIT_MACROS

/* Must be first for Sun */
#include "ping.pb.h"

/* c99 compatibility for c++ */
#define __STDC_FORMAT_MACROS

#include <cerrno>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <math.h>
#include <time.h>
#ifdef CONFIG_HAVE_EPOLL
#	include <sys/epoll.h>
#endif
#include <sys/types.h>
#ifndef _WIN32
#	include <inttypes.h>
#	include <unistd.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <sched.h>
#	include <sys/socket.h>
#	include <arpa/inet.h>
#	include <sys/time.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#	include <pgm/wininttypes.h>
#	include "getopt.h"
#endif
#include <glib.h>
#include <pgm/pgm.h>
#ifdef CONFIG_WITH_HTTP
#	include <pgm/http.h>
#endif
#ifdef CONFIG_WITH_SNMP
#	include <pgm/snmp.h>
#endif

#ifndef MSG_ERRQUEUE
#	define MSG_ERRQUEUE		0x2000
#endif

/* PGM internal time keeper */
extern "C" {
	typedef pgm_time_t (*pgm_time_update_func)(void);
	extern pgm_time_update_func pgm_time_update_now;
	size_t pgm_pkt_offset (bool, sa_family_t);
}

/* example dependencies */
#include <pgm/backtrace.h>
#include <pgm/log.h>
#include <pgm/signal.h>


using namespace std;


/* globals */

static int		g_port = 0;
static const char*	g_network = "";
static int		g_udp_encap_port = 0;

static int		g_odata_rate = 0;
static int		g_odata_interval = 0;
static guint32		g_payload = 0;
static int		g_max_tpdu = 1500;
static int		g_max_rte = 16*1000*1000;
static int		g_odata_rte = 0;	/* 0 = disabled */
static int		g_rdata_rte = 0;	/* 0 = disabled */
static int		g_sqns = 200;

static gboolean		g_use_pgmcc = FALSE;
static sa_family_t	g_pgmcc_family = 0;	/* 0 = disabled */

static gboolean		g_use_fec = FALSE;
static int		g_rs_k = 8;
static int		g_rs_n = 255;

static enum {
	PGMPING_MODE_SOURCE,
	PGMPING_MODE_RECEIVER,
	PGMPING_MODE_INITIATOR,
	PGMPING_MODE_REFLECTOR
}			g_mode = PGMPING_MODE_INITIATOR;

static pgm_sock_t*	g_sock = NULL;

/* stats */
static guint64		g_msg_sent = 0;
static guint64		g_msg_received = 0;
static pgm_time_t	g_interval_start = 0;
static pgm_time_t	g_latency_current = 0;
static guint64		g_latency_seqno = 0;
static guint64		g_last_seqno = 0;
static double		g_latency_total = 0.0;
static double		g_latency_square_total = 0.0;
static guint64		g_latency_count = 0;
static double		g_latency_max = 0.0;
#ifdef INFINITY
static double		g_latency_min = INFINITY;
#else
static double		g_latency_min = (double)INT64_MAX;
#endif
static double		g_latency_running_average = 0.0;
static guint64		g_out_total = 0;
static guint64		g_in_total = 0;

#ifdef CONFIG_WITH_HEATMAP
static FILE*		g_heatmap_file = NULL;
static GHashTable*	g_heatmap_slice = NULL;		/* acting as sparse array */
static GMutex*		g_heatmap_lock = NULL;
static guint		g_heatmap_resolution = 10;	/* microseconds */
#endif

static GMainLoop*	g_loop = NULL;
static GThread*		g_sender_thread = NULL;
static GThread*		g_receiver_thread = NULL;
static gboolean		g_quit = FALSE;
#ifdef G_OS_UNIX
static int		g_quit_pipe[2];
static void on_signal (int, gpointer);
#else
static SOCKET		g_quit_socket[2];
static BOOL on_console_ctrl (DWORD);
#endif

static gboolean on_startup (gpointer);
static gboolean on_shutdown (gpointer);
static gboolean on_mark (gpointer);

static void send_odata (void);
static int on_msgv (struct pgm_msgv_t*, size_t);

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
        fprintf (stderr, "  -O <rate>       : Regulate ODATA packets to rate bps\n");
        fprintf (stderr, "  -D <rate>       : Regulate RDATA packets to rate bps\n");
	fprintf (stderr, "  -c              : Enable PGMCC\n");
        fprintf (stderr, "  -f <type>       : Enable FEC with either proactive or ondemand parity\n");
        fprintf (stderr, "  -K <k>          : Configure Reed-Solomon code (n, k)\n");
        fprintf (stderr, "  -N <n>\n");
#ifdef CONFIG_WITH_HEATMAP
	fprintf (stderr, "  -M <filename>   : Generate latency heap map\n");
#endif
        fprintf (stderr, "  -H              : Enable HTTP administrative interface\n");
        fprintf (stderr, "  -S              : Enable SNMP interface\n");
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	GError* err = NULL;
	pgm_error_t* pgm_err = NULL;
	gboolean enable_http = FALSE;
	gboolean enable_snmpx = FALSE;
	int timeout = 0;

	GOOGLE_PROTOBUF_VERIFY_VERSION;

	setlocale (LC_ALL, "");
#ifndef _WIN32
	setenv ("PGM_TIMER", "GTOD", 1);
	setenv ("PGM_SLEEP", "USLEEP", 1);
#endif

	log_init ();
	g_message ("pgmping");

	g_thread_init (NULL);

	if (!pgm_init (&pgm_err)) {
		g_error ("Unable to start PGM engine: %s", pgm_err->message);
		pgm_error_free (pgm_err);
		return EXIT_FAILURE;
	}

/* parse program arguments */
	const char* binary_name = g_get_prgname();
	int c;
	while ((c = getopt (argc, argv, "s:n:p:m:old:r:O:D:cfeK:N:M:HSh")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;
		case 'p':	g_udp_encap_port = atoi (optarg); break;
		case 'r':	g_max_rte = atoi (optarg); break;
		case 'O':	g_odata_rte = atoi (optarg); break;
		case 'D':	g_rdata_rte = atoi (optarg); break;

		case 'c':	g_use_pgmcc = TRUE; break;

		case 'f':	g_use_fec = TRUE; break;
		case 'K':	g_rs_k = atoi (optarg); break;
		case 'N':	g_rs_n = atoi (optarg); break;

#ifdef CONFIG_WITH_HEATMAP
		case 'M':	g_heatmap_file = fopen (optarg, "w"); break;
#else
		case 'M':	g_warning ("Heat map support not compiled in."); break;
#endif

		case 'H':	enable_http = TRUE; break;
		case 'S':	enable_snmpx = TRUE; break;

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

	if (g_use_fec && ( !g_rs_k || !g_rs_n )) {
		g_error ("Invalid Reed-Solomon parameters.");
		usage (binary_name);
	}

#ifdef CONFIG_WITH_HEATMAP
	if (NULL != g_heatmap_file) {
		g_heatmap_slice = g_hash_table_new (g_direct_hash, g_direct_equal);
		g_heatmap_lock = g_mutex_new ();
	}
#endif

#ifdef CONFIG_WITH_HTTP
	if (enable_http) {
		if (!pgm_http_init (PGM_HTTP_DEFAULT_SERVER_PORT, &pgm_err)) {
			g_error ("Unable to start HTTP interface: %s", pgm_err->message);
			pgm_error_free (pgm_err);
			pgm_shutdown ();
			return EXIT_FAILURE;
		}
	}
#endif
#ifdef CONFIG_WITH_SNMP
	if (enable_snmpx) {
		if (!pgm_snmp_init (&pgm_err)) {
			g_error ("Unable to start SNMP interface: %s", pgm_err->message);
			pgm_error_free (pgm_err);
#	ifdef CONFIG_WITH_HTTP
			if (enable_http)
				pgm_http_shutdown ();
#	endif
			pgm_shutdown ();
			return EXIT_FAILURE;
		}
	}
#endif

	g_loop = g_main_loop_new (NULL, FALSE);

/* setup signal handlers */
#ifdef G_OS_UNIX
	signal (SIGSEGV, on_sigsegv);
#	ifdef SIGHUP
	signal (SIGHUP,  SIG_IGN);
#	endif
	pipe (g_quit_pipe);
	pgm_signal_install (SIGINT,  on_signal, g_loop);
	pgm_signal_install (SIGTERM, on_signal, g_loop);
#else
	SOCKET s = socket (AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	int addrlen = sizeof (addr);
	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr ("127.0.0.1");
	bind (s, (const struct sockaddr*)&addr, sizeof (addr));
	getsockname (s, (struct sockaddr*)&addr, &addrlen);
	listen (s, 1);
	g_quit_socket[1] = socket (AF_INET, SOCK_STREAM, 0);
	connect (g_quit_socket[1], (struct sockaddr*)&addr, addrlen);
	g_quit_socket[0] = accept (s, NULL, NULL);
	closesocket (s);
	SetConsoleCtrlHandler ((PHANDLER_ROUTINE)on_console_ctrl, TRUE);
	setvbuf (stdout, (char *) NULL, _IONBF, 0);
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
	const char one = '1';
	send (g_quit_socket[1], &one, sizeof(one), 0);
	if (PGMPING_MODE_SOURCE == g_mode || PGMPING_MODE_INITIATOR == g_mode)
		g_thread_join (g_sender_thread);
	g_thread_join (g_receiver_thread);
	closesocket (g_quit_socket[0]);
	closesocket (g_quit_socket[1]);
#endif

	g_main_loop_unref (g_loop);
	g_loop = NULL;

	if (g_sock) {
		g_message ("closing PGM socket.");
		pgm_close (g_sock, TRUE);
		g_sock = NULL;
	}

#ifdef CONFIG_WITH_HTTP
	if (enable_http)
		pgm_http_shutdown();
#endif
#ifdef CONFIG_WITH_SNMP
	if (enable_snmpx)
		pgm_snmp_shutdown();
#endif

#ifdef CONFIG_WITH_HEATMAP
	if (NULL != g_heatmap_file) {
		fclose (g_heatmap_file);
		g_heatmap_file = NULL;
		g_mutex_free (g_heatmap_lock);
		g_heatmap_lock = NULL;
		g_hash_table_destroy (g_heatmap_slice);
		g_heatmap_slice = NULL;
	}
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
	struct pgm_addrinfo_t* res = NULL;
	GError* err = NULL;
	pgm_error_t* pgm_err = NULL;
	sa_family_t sa_family = AF_UNSPEC;

	g_message ("startup.");

/* parse network parameter into transport address structure */
	if (!pgm_getaddrinfo (g_network, NULL, &res, &pgm_err)) {
		g_error ("parsing network parameter: %s", pgm_err->message);
		goto err_abort;
	}

	sa_family = res->ai_send_addrs[0].gsr_group.ss_family;
	if (g_use_pgmcc)
		g_pgmcc_family = sa_family;

	if (g_udp_encap_port) {
		g_message ("create PGM/UDP socket.");
		if (!pgm_socket (&g_sock, sa_family, SOCK_SEQPACKET, IPPROTO_UDP, &pgm_err)) {
			g_error ("socket: %s", pgm_err->message);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_UDP_ENCAP_UCAST_PORT, &g_udp_encap_port, sizeof(g_udp_encap_port))) {
			g_error ("setting PGM_UDP_ENCAP_UCAST_PORT = %d", g_udp_encap_port);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_UDP_ENCAP_MCAST_PORT, &g_udp_encap_port, sizeof(g_udp_encap_port))) {
			g_error ("setting PGM_UDP_ENCAP_MCAST_PORT = %d", g_udp_encap_port);
			goto err_abort;
		}
	} else {
		g_message ("create PGM/IP socket.");
		if (!pgm_socket (&g_sock, sa_family, SOCK_SEQPACKET, IPPROTO_PGM, &pgm_err)) {
			g_error ("socket: %s", pgm_err->message);
			goto err_abort;
		}
	}

/* Use RFC 2113 tagging for PGM Router Assist */
	{
		const int no_router_assist = 0;
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_IP_ROUTER_ALERT, &no_router_assist, sizeof(no_router_assist))) {
			g_error ("setting PGM_IP_ROUTER_ALERT = %d", no_router_assist);
			goto err_abort;
		}
	}

#ifndef CONFIG_HAVE_SCHEDPARAM
	pgm_drop_superuser();
#endif
#ifdef CONFIG_PRIORITY_CLASS
/* Any priority above normal usually yields worse performance than expected */
	if (!SetPriorityClass (GetCurrentProcess(), NORMAL_PRIORITY_CLASS))
	{
		g_warning ("setting priority class (%d)", GetLastError());
	}
#endif

/* set PGM parameters */
/* common */
	{
#if defined(__FreeBSD__)
/* FreeBSD defaults to low maximum socket size */
		const int txbufsize = 128 * 1024, rxbufsize = 128 * 1024,
#else
		const int txbufsize = 1024 * 1024, rxbufsize = 1024 * 1024,
#endif
			  max_tpdu = g_max_tpdu;

		if (!pgm_setsockopt (g_sock, SOL_SOCKET, SO_RCVBUF, &rxbufsize, sizeof(rxbufsize))) {
			g_error ("setting SO_RCVBUF = %d", rxbufsize);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, SOL_SOCKET, SO_SNDBUF, &txbufsize, sizeof(txbufsize))) {
			g_error ("setting SO_SNDBUF = %d", txbufsize);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_MTU, &max_tpdu, sizeof(max_tpdu))) {
			g_error ("setting PGM_MTU = %d", max_tpdu);
			goto err_abort;
		}
	}

/* send side */
	if (PGMPING_MODE_SOURCE    == g_mode ||
	    PGMPING_MODE_INITIATOR == g_mode ||
	    PGMPING_MODE_REFLECTOR == g_mode    )
	{
		const int send_only	  = (PGMPING_MODE_SOURCE == g_mode) ? 1 : 0,
			  txw_sqns	  = g_sqns * 4,
			  txw_max_rte	  = g_max_rte,
			  odata_max_rte	  = g_odata_rte,
			  rdata_max_rte	  = g_rdata_rte,
			  ambient_spm	  = pgm_secs (30),
			  heartbeat_spm[] = { pgm_msecs (100),
					      pgm_msecs (100),
					      pgm_msecs (100),
					      pgm_msecs (100),
					      pgm_msecs (1300),
					      pgm_secs  (7),
					      pgm_secs  (16),
					      pgm_secs  (25),
					      pgm_secs  (30) };

		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_SEND_ONLY, &send_only, sizeof(send_only))) {
			g_error ("setting PGM_SEND_ONLY = %d", send_only);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_TXW_SQNS, &txw_sqns, sizeof(txw_sqns))) {
			g_error ("setting PGM_TXW_SQNS = %d", txw_sqns);
			goto err_abort;
		}
		if (txw_max_rte > 0 &&
		    !pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_TXW_MAX_RTE, &txw_max_rte, sizeof(txw_max_rte))) {
			g_error ("setting PGM_TXW_MAX_RTE = %d", txw_max_rte);
			goto err_abort;
		}
		if (odata_max_rte > 0 &&
		    !pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_ODATA_MAX_RTE, &odata_max_rte, sizeof(odata_max_rte))) {
			g_error ("setting PGM_ODATA_MAX_RTE = %d", odata_max_rte);
			goto err_abort;
		}
		if (rdata_max_rte > 0 &&
		    !pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_RDATA_MAX_RTE, &rdata_max_rte, sizeof(rdata_max_rte))) {
			g_error ("setting PGM_RDATA_MAX_RTE = %d", rdata_max_rte);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_AMBIENT_SPM, &ambient_spm, sizeof(ambient_spm))) {
			g_error ("setting PGM_AMBIENT_SPM = %d", ambient_spm);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_HEARTBEAT_SPM, &heartbeat_spm, sizeof(heartbeat_spm))) {
	                char buffer[1024];
	                sprintf (buffer, "%d", heartbeat_spm[0]);
	                for (unsigned i = 1; i < G_N_ELEMENTS(heartbeat_spm); i++) {
	                        char t[1024];
	                        sprintf (t, ", %d", heartbeat_spm[i]);
	                        strcat (buffer, t);
	                }
	                g_error ("setting HEARTBEAT_SPM = { %s }", buffer);
			goto err_abort;
	        }

	}

/* receive side */
	if (PGMPING_MODE_RECEIVER  == g_mode ||
	    PGMPING_MODE_INITIATOR == g_mode ||
	    PGMPING_MODE_REFLECTOR == g_mode    )
	{
		const int recv_only	   = (PGMPING_MODE_RECEIVER == g_mode) ? 1 : 0,
			  not_passive	   = 0,
			  rxw_sqns	   = g_sqns,
			  peer_expiry	   = pgm_secs (300),
			  spmr_expiry	   = pgm_msecs (250),
			  nak_bo_ivl	   = pgm_msecs (50),
			  nak_rpt_ivl	   = pgm_msecs (200), //pgm_secs (2),
			  nak_rdata_ivl    = pgm_msecs (200), //pgm_secs (2),
			  nak_data_retries = 50,
			  nak_ncf_retries  = 50;

		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_RECV_ONLY, &recv_only, sizeof(recv_only))) {
			g_error ("setting PGM_RECV_ONLY = %d", recv_only);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_PASSIVE, &not_passive, sizeof(not_passive))) {
			g_error ("setting PGM_PASSIVE = %d", not_passive);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_RXW_SQNS, &rxw_sqns, sizeof(rxw_sqns))) {
			g_error ("setting PGM_RXW_SQNS = %d", rxw_sqns);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_PEER_EXPIRY, &peer_expiry, sizeof(peer_expiry))) {
			g_error ("setting PGM_PEER_EXPIRY = %d", peer_expiry);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_SPMR_EXPIRY, &spmr_expiry, sizeof(spmr_expiry))) {
			g_error ("setting PGM_SPMR_EXPIRY = %d", spmr_expiry);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_BO_IVL, &nak_bo_ivl, sizeof(nak_bo_ivl))) {
			g_error ("setting PGM_NAK_BO_IVL = %d", nak_bo_ivl);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_RPT_IVL, &nak_rpt_ivl, sizeof(nak_rpt_ivl))) {
			g_error ("setting PGM_NAK_RPT_IVL = %d", nak_rpt_ivl);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_RDATA_IVL, &nak_rdata_ivl, sizeof(nak_rdata_ivl))) {
			g_error ("setting PGM_NAK_RDATA_IVL = %d", nak_rdata_ivl);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_DATA_RETRIES, &nak_data_retries, sizeof(nak_data_retries))) {
			g_error ("setting PGM_NAK_DATA_RETRIES = %d", nak_data_retries);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NAK_NCF_RETRIES, &nak_ncf_retries, sizeof(nak_ncf_retries))) {
			g_error ("setting PGM_NAK_NCF_RETRIES = %d", nak_ncf_retries);
			goto err_abort;
		}
	}

#ifdef I_UNDERSTAND_PGMCC_AND_FEC_ARE_NOT_SUPPORTED
/* PGMCC congestion control */
	if (g_use_pgmcc) {
		struct pgm_pgmccinfo_t pgmccinfo;
		pgmccinfo.ack_bo_ivl		= pgm_msecs (50);
		pgmccinfo.ack_c			= 75;
		pgmccinfo.ack_c_p		= 500;
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_USE_PGMCC, &pgmccinfo, sizeof(pgmccinfo))) {
			g_error ("setting PGM_USE_PGMCC = { ack_bo_ivl = %d ack_c = %d ack_c_p = %d }",
				pgmccinfo.ack_bo_ivl,
				pgmccinfo.ack_c,
				pgmccinfo.ack_c_p);
			goto err_abort;
		}
	}

/* Reed Solomon forward error correction */
	if (g_use_fec) {
		struct pgm_fecinfo_t fecinfo; 
		fecinfo.block_size		= g_rs_n;
		fecinfo.proactive_packets	= 0;
		fecinfo.group_size		= g_rs_k;
		fecinfo.ondemand_parity_enabled	= TRUE;
		fecinfo.var_pktlen_enabled	= TRUE;
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_USE_FEC, &fecinfo, sizeof(fecinfo))) {
			g_error ("setting PGM_USE_FEC = { block_size = %d proactive_packets = %d group_size = %d ondemand_parity_enabled = %s var_pktlen_enabled = %s }",
				fecinfo.block_size,
				fecinfo.proactive_packets,
				fecinfo.group_size,
				fecinfo.ondemand_parity_enabled ? "TRUE" : "FALSE",
				fecinfo.var_pktlen_enabled ? "TRUE" : "FALSE");
			goto err_abort;
		}
	}
#endif

/* create global session identifier */
	struct pgm_sockaddr_t addr;
	memset (&addr, 0, sizeof(addr));
	addr.sa_port = (0 != g_port) ? g_port : DEFAULT_DATA_DESTINATION_PORT;
	addr.sa_addr.sport = DEFAULT_DATA_SOURCE_PORT;
	if (!pgm_gsi_create_from_hostname (&addr.sa_addr.gsi, &pgm_err)) {
		g_error ("creating GSI: %s", pgm_err->message);
		goto err_abort;
	}

/* assign socket to specified address */
	struct pgm_interface_req_t if_req;
	memset (&if_req, 0, sizeof(if_req));
	if_req.ir_interface = res->ai_recv_addrs[0].gsr_interface;
	if_req.ir_scope_id  = 0;
	if (AF_INET6 == sa_family) {
		struct sockaddr_in6 sa6;
		memcpy (&sa6, &res->ai_recv_addrs[0].gsr_group, sizeof(sa6));
		if_req.ir_scope_id = sa6.sin6_scope_id;
	}
	if (!pgm_bind3 (g_sock,
			&addr, sizeof(addr),
			&if_req, sizeof(if_req),	/* tx interface */
			&if_req, sizeof(if_req),	/* rx interface */
			&pgm_err))
	{
		g_error ("binding PGM socket: %s", pgm_err->message);
		goto err_abort;
	}

/* join IP multicast groups */
	for (unsigned i = 0; i < res->ai_recv_addrs_len; i++)
	{
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_JOIN_GROUP, &res->ai_recv_addrs[i], sizeof(struct group_req))) {
			char group[INET6_ADDRSTRLEN];
			getnameinfo ((struct sockaddr*)&res->ai_recv_addrs[i].gsr_group, sizeof(struct sockaddr_in),
                                        group, sizeof(group),
                                        NULL, 0,
                                        NI_NUMERICHOST);
			g_error ("setting PGM_JOIN_GROUP = { #%u %s }",
				(unsigned)res->ai_recv_addrs[i].gsr_interface,
				group);
			goto err_abort;
		}
	}
	if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_SEND_GROUP, &res->ai_send_addrs[0], sizeof(struct group_req))) {
                char group[INET6_ADDRSTRLEN];
                getnameinfo ((struct sockaddr*)&res->ai_send_addrs[0].gsr_group, sizeof(struct sockaddr_in),
				group, sizeof(group),
                                NULL, 0,
                                NI_NUMERICHOST);
		g_error ("setting PGM_SEND_GROUP = { #%u %s }",
			(unsigned)res->ai_send_addrs[0].gsr_interface,
			group);
		goto err_abort;
	}
	pgm_freeaddrinfo (res);

/* set IP parameters */
	{
		const int nonblocking	   = 1,
			  multicast_direct = 0,
			  multicast_hops   = 16,
			  dscp		   = 0x2e << 2;	/* Expedited Forwarding PHB for network elements, no ECN. */

		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_MULTICAST_LOOP, &multicast_direct, sizeof(multicast_direct))) {
			g_error ("setting PGM_MULTICAST_LOOP = %d", multicast_direct);
			goto err_abort;
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops))) {
			g_error ("setting PGM_MULTICAST_HOPS = %d", multicast_hops);
			goto err_abort;
		}
		if (AF_INET6 != sa_family) {
			if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_TOS, &dscp, sizeof(dscp))) {
				g_error ("setting PGM_TOS = 0x%x", dscp);
				goto err_abort;
			}
		}
		if (!pgm_setsockopt (g_sock, IPPROTO_PGM, PGM_NOBLOCK, &nonblocking, sizeof(nonblocking))) {
			g_error ("setting PGM_NOBLOCK = %d", nonblocking);
			goto err_abort;
		}
	}

	if (!pgm_connect (g_sock, &pgm_err)) {
		g_error ("connecting PGM socket: %s", pgm_err->message);
		goto err_abort;
	}

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add (2 * 1000, (GSourceFunc)on_mark, NULL);

	if (PGMPING_MODE_SOURCE == g_mode || PGMPING_MODE_INITIATOR == g_mode)
	{
		g_sender_thread = g_thread_create_full (sender_thread,
							g_sock,
							0,
							TRUE,
							TRUE,
							G_THREAD_PRIORITY_NORMAL,
							&err);
		if (!g_sender_thread) {
			g_critical ("g_thread_create_full failed errno %i: \"%s\"", err->code, err->message);
			goto err_abort;
		}
	}

	{
		g_receiver_thread = g_thread_create_full (receiver_thread,
							  g_sock,
							  0,
							  TRUE,
							  TRUE,
							  G_THREAD_PRIORITY_HIGH,
							  &err);
		if (!g_receiver_thread) {
			g_critical ("g_thread_create_full failed errno %i: \"%s\"", err->code, err->message);
			goto err_abort;
		}
	}

	g_message ("startup complete.");
	return FALSE;

err_abort:
	if (NULL != g_sock) {
		pgm_close (g_sock, FALSE);
		g_sock = NULL;
	}
	if (NULL != res) {
		pgm_freeaddrinfo (res);
		res = NULL;
	}
	if (NULL != pgm_err) {
		pgm_error_free (pgm_err);
		pgm_err = NULL;
	}
	return FALSE;
}

static
gpointer
sender_thread (
	gpointer	user_data
	)
{
	pgm_sock_t* tx_sock = (pgm_sock_t*)user_data;
	example::Ping ping;
	string subject("PING.PGM.TEST.");
	char hostname[NI_MAXHOST + 1];
	const long payload_len = 1000;
	char payload[payload_len];
	gpointer buffer = NULL;
	guint64 latency, now, last = 0;

#ifdef CONFIG_HAVE_EPOLL
	const long ev_len = 1;
	struct epoll_event events[ev_len];

	int efd_again = epoll_create (IP_MAX_MEMBERSHIPS);
	if (efd_again < 0) {
		g_error ("epoll_create failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
/* Add write event to epoll domain in order to re-enable as required by return
 * value.  We use one-shot flag to disable ASAP, as we don't want such events
 * until triggered.
 */
	if (pgm_epoll_ctl (tx_sock, efd_again, EPOLL_CTL_ADD, EPOLLOUT | EPOLLONESHOT) < 0) {
		g_error ("pgm_epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
	struct epoll_event event;
	memset (&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = g_quit_pipe[0];
	if (epoll_ctl (efd_again, EPOLL_CTL_ADD, g_quit_pipe[0], &event) < 0) {
		g_error ("epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
#elif defined(CONFIG_HAVE_POLL)
/* does not include ACKs */
	int n_fds = PGM_BUS_SOCKET_WRITE_COUNT;
	struct pollfd fds[ PGM_BUS_SOCKET_WRITE_COUNT + 1 ];
#elif defined(CONFIG_HAVE_WSAPOLL)
	ULONG n_fds = PGM_BUS_SOCKET_WRITE_COUNT;
	WSAPOLLFD fds[ PGM_BUS_SOCKET_WRITE_COUNT + 1 ];
#elif defined(CONFIG_WSA_WAIT)
/* does not include ACKs */
	SOCKET send_sock;
	DWORD cEvents = PGM_BUS_SOCKET_WRITE_COUNT + 1;
	WSAEVENT waitEvents[ PGM_BUS_SOCKET_WRITE_COUNT + 1 ];
	socklen_t socklen = sizeof (SOCKET);

	waitEvents[0] = WSACreateEvent();
	WSAEventSelect (g_quit_socket[0], waitEvents[0], FD_READ);
	waitEvents[1] = WSACreateEvent();
	g_assert (1 == PGM_BUS_SOCKET_WRITE_COUNT);
	pgm_getsockopt (tx_sock, IPPROTO_PGM, PGM_SEND_SOCK, &send_sock, &socklen);
	WSAEventSelect (send_sock, waitEvents[1], FD_WRITE);
#endif /* !CONFIG_HAVE_EPOLL */

	gethostname (hostname, sizeof(hostname));
	subject.append(hostname);
	memset (payload, 0, sizeof(payload));

#ifdef CONFIG_HAVE_SCHEDPARAM
/* realtime scheduling */
	pthread_t thread_id = pthread_self ();
	int policy;
	struct sched_param param;

	if (0 == pthread_getschedparam (thread_id, &policy, &param)) {
		policy = SCHED_FIFO;
		param.sched_priority = 50;
		if (0 != pthread_setschedparam (thread_id, policy, &param))
			g_warning ("Cannot set thread scheduling parameters.");
	} else
		g_warning ("Cannot get thread scheduling parameters.");
#endif

	ping.mutable_subscription_header()->set_subject (subject);
	ping.mutable_market_data_header()->set_msg_type (example::MarketDataHeader::MSG_VERIFY);
	ping.mutable_market_data_header()->set_rec_type (example::MarketDataHeader::PING);
	ping.mutable_market_data_header()->set_rec_status (example::MarketDataHeader::STATUS_OK);
	ping.set_time (last);

	last = now = pgm_time_update_now();
	do {
		if (g_msg_sent && g_latency_seqno + 1 == g_msg_sent)
			latency = g_latency_current;
		else
			latency = g_odata_interval;

		ping.set_seqno (g_msg_sent);
		ping.set_latency (latency);
		ping.set_payload (payload, sizeof(payload));

		const size_t header_size = pgm_pkt_offset (FALSE, g_pgmcc_family);
		const size_t apdu_size = ping.ByteSize();
		struct pgm_sk_buff_t* skb = pgm_alloc_skb (g_max_tpdu);
		pgm_skb_reserve (skb, header_size);
		pgm_skb_put (skb, apdu_size);

/* wait on packet rate limit */
		if ((last + g_odata_interval) > now) {
#ifndef _WIN32
			const unsigned int usec = g_odata_interval - (now - last);
			usleep (usec);
#else
#	define usecs_to_msecs(t)	( ((t) + 999) / 1000 )
			const DWORD msec = (DWORD)usecs_to_msecs (g_odata_interval - (now - last));
/* Avoid yielding on Windows XP/2000 */ 
			if (msec > 0)
				Sleep (msec);
#endif
			now = pgm_time_update_now();
		}
		last += g_odata_interval;
		ping.set_time (now);
		ping.SerializeToArray (skb->data, skb->len);

		struct timeval tv;
#if defined(CONFIG_HAVE_EPOLL) || defined(CONFIG_HAVE_POLL)
		int timeout;
#elif defined(CONFIG_HAVE_WSAPOLL)
		DWORD dwTimeout;
#elif defined(CONFIG_WSA_WAIT)
		DWORD dwTimeout, dwEvents;
#else
		int n_fds;
		fd_set readfds, writefds;
#endif
		size_t bytes_written;
		int status;
again:
		status = pgm_send_skbv (tx_sock, &skb, 1, TRUE, &bytes_written);
		switch (status) {
/* rate control */
		case PGM_IO_STATUS_RATE_LIMITED:
		{
			socklen_t optlen = sizeof (tv);
			const gboolean status = pgm_getsockopt (tx_sock, IPPROTO_PGM, PGM_RATE_REMAIN, &tv, &optlen);
			if (G_UNLIKELY(!status)) {
				g_error ("getting PGM_RATE_REMAIN failed");
				break;
			}
#if defined(CONFIG_HAVE_EPOLL) || defined(CONFIG_HAVE_POLL)
			timeout = (tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000);
/* busy wait under 2ms */
#	ifdef CONFIG_BUSYWAIT
			if (timeout < 2)
				goto again;
#	else
			if (0 == timeout)
				goto again;
#	endif
#elif defined(CONFIG_HAVE_WSAPOLL) || defined(CONFIG_WSA_WAIT)
/* round up wait */
#	ifdef CONFIG_BUSYWAIT
			dwTimeout = (DWORD)(tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000);
#	else
			dwTimeout = (DWORD)(tv.tv_sec * 1000) + ((tv.tv_usec + 999) / 1000);
#	endif
			if (0 == dwTimeout)
				goto again;
#endif
#if defined(CONFIG_HAVE_EPOLL)
			const int ready = epoll_wait (efd_again, events, G_N_ELEMENTS(events), timeout /* ms */);
#elif defined(CONFIG_HAVE_POLL)
			memset (fds, 0, sizeof(fds));
			fds[0].fd = g_quit_pipe[0];
			fds[0].events = POLLIN;
			n_fds = G_N_ELEMENTS(fds) - 1;
			pgm_poll_info (tx_sock, &fds[1], &n_fds, POLLIN);
			poll (fds, 1 + n_fds, timeout /* ms */);
#elif defined(CONFIG_HAVE_WSAPOLL)
			ZeroMemory (fds, sizeof(WSAPOLLFD) * (n_fds + 1));
			fds[0].fd = g_quit_socket[0];
			fds[0].events = POLLRDNORM;
			n_fds = G_N_ELEMENTS(fds) - 1;
			pgm_poll_info (tx_sock, &fds[1], &n_fds, POLLRDNORM);
			WSAPoll (fds, 1 + n_fds, dwTimeout /* ms */);
#elif defined(CONFIG_WSA_WAIT)
/* only wait for quit or timeout events */
			cEvents = 1;
			dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, dwTimeout, FALSE);
			switch (dwEvents) {
			case WSA_WAIT_EVENT_0+1: WSAResetEvent (waitEvents[1]); break;
			default: break;
			}
#else
			FD_ZERO(&readfds);
#	ifndef _WIN32
			FD_SET(g_quit_pipe[0], &readfds);
			n_fds = g_quit_pipe[0] + 1;		/* highest fd + 1 */
#	else
			FD_SET(g_quit_socket[0], &readfds);
			n_fds = 1;				/* count of fds */
#	endif
			pgm_select_info (g_sock, &readfds, NULL, &n_fds);
			n_fds = select (n_fds, &readfds, NULL, NULL, PGM_IO_STATUS_WOULD_BLOCK == status ? NULL : &tv);
#endif /* !CONFIG_HAVE_EPOLL */
			if (G_UNLIKELY(g_quit))
				break;
			goto again;
		}
/* congestion control */
		case PGM_IO_STATUS_CONGESTION:
/* kernel feedback */
		case PGM_IO_STATUS_WOULD_BLOCK:
		{
#ifdef CONFIG_HAVE_EPOLL
#	if 1
/* re-enable write event for one-shot */
			if (pgm_epoll_ctl (tx_sock, efd_again, EPOLL_CTL_MOD, EPOLLOUT | EPOLLONESHOT) < 0)
			{
				g_error ("pgm_epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
				g_main_loop_quit (g_loop);
				return NULL;
			}
			const int ready = epoll_wait (efd_again, events, G_N_ELEMENTS(events), -1 /* ms */);
			if (G_UNLIKELY(g_quit))
				break;
#	else
			const int ready = epoll_wait (efd_again, events, G_N_ELEMENTS(events), -1 /* ms */);
			if (G_UNLIKELY(g_quit))
				break;
			if (ready > 0 &&
			    pgm_epoll_ctl (tx_sock, efd_again, EPOLL_CTL_MOD, EPOLLOUT | EPOLLONESHOT) < 0)
			{
				g_error ("pgm_epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
				g_main_loop_quit (g_loop);
				return NULL;
			}
#	endif
#elif defined(CONFIG_HAVE_POLL)
			memset (fds, 0, sizeof(fds));
			fds[0].fd = g_quit_pipe[0];
			fds[0].events = POLLIN;
			n_fds = G_N_ELEMENTS(fds) - 1;
			pgm_poll_info (g_sock, &fds[1], &n_fds, POLLOUT);
			poll (fds, 1 + n_fds, -1 /* ms */);
#elif defined(CONFIG_HAVE_WSAPOLL)
			ZeroMemory (fds, sizeof(WSAPOLLFD) * (n_fds + 1));
			fds[0].fd = g_quit_socket[0];
			fds[0].events = POLLRDNORM;
			n_fds = G_N_ELEMENTS(fds) - 1;
			pgm_poll_info (tx_sock, &fds[1], &n_fds, POLLWRNORM);
			WSAPoll (fds, 1 + n_fds, -1 /* ms */);
#elif defined(CONFIG_WSA_WAIT)
			cEvents = PGM_BUS_SOCKET_WRITE_COUNT + 1;
			dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, WSA_INFINITE, FALSE);
			switch (dwEvents) {
			case WSA_WAIT_EVENT_0+1: WSAResetEvent (waitEvents[1]); break;
			default: break;
			}
#else
			FD_ZERO(&readfds);
#	ifndef _WIN32
			FD_SET(g_quit_pipe[0], &readfds);
			n_fds = g_quit_pipe[0] + 1;		/* highest fd + 1 */
#	else
			FD_SET(g_quit_socket[0], &readfds);
			n_fds = 1;				/* count of fds */
#	endif
			pgm_select_info (g_sock, &readfds, &writefds, &n_fds);
			n_fds = select (n_fds, &readfds, &writefds, NULL, NULL);
			
#endif /* !CONFIG_HAVE_EPOLL */
			goto again;
		}
/* successful delivery */
		case PGM_IO_STATUS_NORMAL:
//			g_message ("sent payload: %s", ping.DebugString().c_str());
//			g_message ("sent %u bytes", (unsigned)bytes_written);
			break;
		default:
			g_warning ("pgm_send_skbv failed, status:%i", status);
			g_main_loop_quit (g_loop);
			return NULL;
		}
		g_out_total += bytes_written;
		g_msg_sent++;
	} while (G_LIKELY(!g_quit));

#if defined(CONFIG_HAVE_EPOLL)
	close (efd_again);
#elif defined(CONFIG_WSA_WAIT)
	WSACloseEvent (waitEvents[0]);
	WSACloseEvent (waitEvents[1]);
#endif
	return NULL;
}

static
gpointer
receiver_thread (
	gpointer	data
	)
{
	pgm_sock_t* rx_sock = (pgm_sock_t*)data;
	const long iov_len = 20;
	struct pgm_msgv_t msgv[iov_len];
	pgm_time_t lost_tstamp = 0;
	pgm_tsi_t  lost_tsi;
	guint32	   lost_count = 0;

#ifdef CONFIG_HAVE_EPOLL
	const long ev_len = 1;
	struct epoll_event events[ev_len];

	int efd = epoll_create (IP_MAX_MEMBERSHIPS);
	if (efd < 0) {
		g_error ("epoll_create failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
	if (pgm_epoll_ctl (rx_sock, efd, EPOLL_CTL_ADD, EPOLLIN) < 0) {
		g_error ("pgm_epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
	struct epoll_event event;
	memset (&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = g_quit_pipe[0];
	if (epoll_ctl (efd, EPOLL_CTL_ADD, g_quit_pipe[0], &event) < 0) {
		g_error ("epoll_ctl failed errno %i: \"%s\"", errno, strerror(errno));
		g_main_loop_quit (g_loop);
		return NULL;
	}
#elif defined(CONFIG_HAVE_POLL)
	int n_fds = PGM_BUS_SOCKET_READ_COUNT;
	struct pollfd fds[ PGM_BUS_SOCKET_READ_COUNT + 1 ];
#elif defined(CONFIG_HAVE_WSAPOLL)
	ULONG n_fds = PGM_BUS_SOCKET_READ_COUNT;
	WSAPOLLFD fds[ PGM_BUS_SOCKET_READ_COUNT + 1 ];
#elif defined(CONFIG_WSA_WAIT)
	SOCKET recv_sock, repair_sock, pending_sock;
	DWORD cEvents = PGM_BUS_SOCKET_READ_COUNT + 1;
	WSAEVENT waitEvents[ PGM_BUS_SOCKET_READ_COUNT + 1 ];
	socklen_t socklen = sizeof (SOCKET);

	waitEvents[0] = WSACreateEvent();;
	waitEvents[1] = WSACreateEvent();
	waitEvents[2] = WSACreateEvent();
	waitEvents[3] = WSACreateEvent();
	g_assert (3 == PGM_BUS_SOCKET_READ_COUNT);
	WSAEventSelect (g_quit_socket[0], waitEvents[0], FD_READ);
	pgm_getsockopt (rx_sock, IPPROTO_PGM, PGM_RECV_SOCK, &recv_sock, &socklen);
	WSAEventSelect (recv_sock, waitEvents[1], FD_READ);
	pgm_getsockopt (rx_sock, IPPROTO_PGM, PGM_REPAIR_SOCK, &repair_sock, &socklen);
	WSAEventSelect (repair_sock, waitEvents[2], FD_READ);
	pgm_getsockopt (rx_sock, IPPROTO_PGM, PGM_PENDING_SOCK, &pending_sock, &socklen);
	WSAEventSelect (pending_sock, waitEvents[3], FD_READ);
#endif /* !CONFIG_HAVE_EPOLL */

#ifdef CONFIG_HAVE_SCHEDPARAM
/* realtime scheduling */
	pthread_t thread_id = pthread_self ();
	int policy;
	struct sched_param param;

	if (0 == pthread_getschedparam (thread_id, &policy, &param)) {
		policy = SCHED_FIFO;
		param.sched_priority = 50;
		if (0 != pthread_setschedparam (thread_id, policy, &param))
			g_warning ("Cannot set thread scheduling parameters.");
	} else
		g_warning ("Cannot get thread scheduling parameters.");
#endif

	memset (&lost_tsi, 0, sizeof(lost_tsi));

	do {
		struct timeval tv;
#if defined(CONFIG_HAVE_EPOLL) || defined(CONFIG_HAVE_POLL)
		int timeout;
#elif defined(CONFIG_HAVE_WSAPOLL)
		DWORD dwTimeout;
#elif defined(CONFIG_WSA_WAIT)
		DWORD dwTimeout, dwEvents;
#else
		int n_fds;
		fd_set readfds;
#endif
		size_t len;
		pgm_error_t* pgm_err;
		int status;

again:
		pgm_err = NULL;
		status = pgm_recvmsgv (rx_sock,
				       msgv,
				       G_N_ELEMENTS(msgv),
				       MSG_ERRQUEUE,
				       &len,
				       &pgm_err);
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
//			g_message ("recv %u bytes", (unsigned)len);
			on_msgv (msgv, len);
			break;
		case PGM_IO_STATUS_TIMER_PENDING:
			{
				socklen_t optlen = sizeof (tv);
				const gboolean status = pgm_getsockopt (g_sock, IPPROTO_PGM, PGM_TIME_REMAIN, &tv, &optlen);
//g_message ("timer pending %d", ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));
				if (G_UNLIKELY(!status)) {
					g_error ("getting PGM_TIME_REMAIN failed");
					break;
				}
			}
			goto block;
		case PGM_IO_STATUS_RATE_LIMITED:
			{
				socklen_t optlen = sizeof (tv);
				const gboolean status = pgm_getsockopt (g_sock, IPPROTO_PGM, PGM_RATE_REMAIN, &tv, &optlen);
//g_message ("rate limited %d", ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));
				if (G_UNLIKELY(!status)) {
					g_error ("getting PGM_RATE_REMAIN failed");
					break;
				}
			}
/* fall through */
		case PGM_IO_STATUS_WOULD_BLOCK:
//g_message ("would block");
block:
#if defined(CONFIG_HAVE_EPOLL) || defined(CONFIG_HAVE_POLL)
			timeout = PGM_IO_STATUS_WOULD_BLOCK == status ? -1 : ((tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000));
/* busy wait under 2ms */
#	ifdef CONFIG_BUSYWAIT
			if (timeout >= 0 && timeout < 2)
				goto again;
#	else
			if (0 == timeout)
				goto again;
#	endif
#elif defined(CONFIG_HAVE_WSAPOLL) || defined(CONFIG_WSA_WAIT)
/* round up wait */
#	ifdef CONFIG_BUSYWAIT
			dwTimeout = PGM_IO_STATUS_WOULD_BLOCK == status ? WSA_INFINITE : (DWORD)((tv.tv_sec * 1000) + ((tv.tv_usec + 500) / 1000));
#	else
			dwTimeout = PGM_IO_STATUS_WOULD_BLOCK == status ? WSA_INFINITE : (DWORD)((tv.tv_sec * 1000) + ((tv.tv_usec + 999) / 1000));
#	endif
			if (0 == dwTimeout)
				goto again;
#endif
#ifdef CONFIG_HAVE_EPOLL
			epoll_wait (efd, events, G_N_ELEMENTS(events), timeout /* ms */);
#elif defined(CONFIG_HAVE_POLL)
			memset (fds, 0, sizeof(fds));
			fds[0].fd = g_quit_pipe[0];
			fds[0].events = POLLIN;
			pgm_poll_info (g_sock, &fds[1], &n_fds, POLLIN);
			poll (fds, 1 + n_fds, timeout /* ms */);
#elif defined(CONFIG_HAVE_WSAPOLL)
			ZeroMemory (fds, sizeof(fds));
			fds[0].fd = g_quit_socket[0];
			fds[0].events = POLLRDNORM;
			pgm_poll_info (g_sock, &fds[1], &n_fds, POLLRDNORM);
			WSAPoll (fds, 1 + n_fds, dwTimeout /* ms */);
#elif defined(CONFIG_WSA_WAIT)
#	ifdef CONFIG_HAVE_IOCP
			dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, dwTimeout, TRUE);
			if (WSA_WAIT_IO_COMPLETION == dwEvents)
#	endif
			dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, dwTimeout, FALSE);
			if ((WSA_WAIT_FAILED != dwEvents) && (WSA_WAIT_TIMEOUT != dwEvents)) {
				const DWORD Index = dwEvents - WSA_WAIT_EVENT_0;
/* Do not reset quit event */
				if (Index > 0) WSAResetEvent (waitEvents[Index]);
			}
#else
			FD_ZERO(&readfds);
#	ifndef _WIN32
			FD_SET(g_quit_pipe[0], &readfds);
			n_fds = g_quit_pipe[0] + 1;		/* highest fd + 1 */
#	else
			FD_SET(g_quit_socket[0], &readfds);
			n_fds = 1;				/* count of fds */
#	endif
			pgm_select_info (g_sock, &readfds, NULL, &n_fds);
			n_fds = select (n_fds, &readfds, NULL, NULL, PGM_IO_STATUS_WOULD_BLOCK == status ? NULL : &tv);
#endif /* !CONFIG_HAVE_EPOLL */
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
			if (pgm_err) {
				g_warning ("%s", pgm_err->message);
				pgm_error_free (pgm_err);
				pgm_err = NULL;
			}
			break;
		}
	} while (G_LIKELY(!g_quit));

#if defined(CONFIG_HAVE_EPOLL)
	close (efd);
#elif defined(CONFIG_WSA_WAIT)
	WSACloseEvent (waitEvents[0]);
	WSACloseEvent (waitEvents[1]);
	WSACloseEvent (waitEvents[2]);
	WSACloseEvent (waitEvents[3]);
#endif
	return NULL;
}

static
int
on_msgv (
	struct pgm_msgv_t*	msgv,		/* an array of msgvs */
	size_t			len
	)
{
	example::Ping ping;
	guint i = 0;
	static pgm_time_t last_time = pgm_time_update_now();

	while (len)
	{
		const struct pgm_sk_buff_t* pskb = msgv[i].msgv_skb[0];
		gsize apdu_len = 0;
		for (unsigned j = 0; j < msgv[i].msgv_len; j++)
			apdu_len += msgv[i].msgv_skb[j]->len;

		if (PGMPING_MODE_REFLECTOR == g_mode)
		{
			int status;
again:
			status = pgm_send (g_sock, pskb->data, pskb->len, NULL);
			switch (status) {
			case PGM_IO_STATUS_RATE_LIMITED:
//g_message ("reflector ratelimit");
				goto again;
			case PGM_IO_STATUS_CONGESTION:
g_message ("reflector congestion");
				goto again;
			case PGM_IO_STATUS_WOULD_BLOCK:
//g_message ("reflector would block");
/* busy wait always as reflector */
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
			const pgm_time_t recv_time	= pskb->tstamp;
			const guint64 seqno		= ping.seqno();
			const guint64 latency		= ping.latency();

			if (seqno < g_latency_seqno) {
				g_message ("seqno replay?");
				goto next_msg;
			}

			g_in_total += pskb->len;
			g_msg_received++;

/* handle ping */
			const pgm_time_t now = pgm_time_update_now();
			if (send_time > now)
				g_warning ("send time %" PGM_TIME_FORMAT " newer than now %" PGM_TIME_FORMAT,
					   send_time, now);
			if (recv_time > now)
				g_warning ("recv time %" PGM_TIME_FORMAT " newer than now %" PGM_TIME_FORMAT,
					   recv_time, now);
			if (send_time > recv_time){
				g_message ("timer mismatch, send time = recv time + %.3f ms (last time + %.3f ms)",
					   pgm_to_msecsf(send_time - recv_time),
					   pgm_to_msecsf(last_time - send_time));
				goto next_msg;
			}
			g_latency_current	= pgm_to_secs (recv_time - send_time);
			g_latency_seqno		= seqno;

			const double elapsed    = pgm_to_usecsf (recv_time - send_time);
			g_latency_total	       += elapsed;
			g_latency_square_total += elapsed * elapsed;

			if (elapsed > g_latency_max)
				g_latency_max = elapsed;
			if (elapsed < g_latency_min)
				g_latency_min = elapsed;

			g_latency_running_average += elapsed;
			g_latency_count++;
			last_time = recv_time;

#ifdef CONFIG_WITH_HEATMAP
/* update heatmap slice */
			if (NULL != g_heatmap_file) {
				const guintptr key = (guintptr)((pgm_to_usecs (recv_time - send_time) + (g_heatmap_resolution - 1)) / g_heatmap_resolution) * g_heatmap_resolution;
				g_mutex_lock (g_heatmap_lock);
				guint32* value = (guint32*)g_hash_table_lookup (g_heatmap_slice, (const void*)key);
				if (NULL == value) {
					value = g_slice_new (guint32);
					*value = 1;
					g_hash_table_insert (g_heatmap_slice, (void*)key, (void*)value);
				} else
					(*value)++;
				g_mutex_unlock (g_heatmap_lock);
			}
#endif
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
#ifdef INFINITY
		g_latency_min		= INFINITY;
#else
		g_latency_min		= (double)INT64_MAX;
#endif
		g_latency_max		= 0.0;
		g_out_total		= 0;
		g_in_total		= 0;

#ifdef CONFIG_WITH_HEATMAP
/* serialize heatmap slice */
		if (NULL != g_heatmap_file) {
			GHashTableIter iter;
			gpointer key, value;
			guint32 slice_size;

			g_mutex_lock (g_heatmap_lock);
			slice_size = g_htonl ((guint32)g_hash_table_size (g_heatmap_slice));
			fwrite (&slice_size, sizeof (slice_size), 1, g_heatmap_file);
			g_hash_table_iter_init (&iter, g_heatmap_slice);
			while (g_hash_table_iter_next (&iter, &key, &value)) {
				guint32 words[2];
				words[0] = g_htonl ((guint32)(guintptr)key);
				words[1] = g_htonl (*(guint32*)value);
				fwrite (words, sizeof (guint32), 2, g_heatmap_file);
				g_slice_free (guint32, value);
				g_hash_table_iter_remove (&iter);
			}
			g_mutex_unlock (g_heatmap_lock);
		}
#endif
	}

	return TRUE;
}

/* eof */
