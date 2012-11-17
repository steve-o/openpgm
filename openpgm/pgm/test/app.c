/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM conformance test application.
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

#ifdef HAVE_CONFIG_H
#       include <config.h>
#endif

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#ifndef _WIN32
#	include <netdb.h>
#	include <sched.h>
#	include <unistd.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
#include <getopt.h>
#include <regex.h>
#include <glib.h>
#include <pgm/pgm.h>
#include <pgm/backtrace.h>
#include <pgm/log.h>
#include <pgm/gsi.h>
#include <pgm/signal.h>

#include "async.h"


/* typedefs */

struct idle_source {
	GSource		source;
	guint64		expiration;
};

struct app_session {
	char*		name;
	pgm_sock_t*	sock;
	pgm_async_t*	async;
};

/* globals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN	"app"

static int		g_port = 7500;
static const char*	g_network = ";239.192.0.1";

static guint		g_max_tpdu = 1500;
static guint		g_sqns = 100 * 1000;

static GHashTable*	g_sessions = NULL;
static GMainLoop*	g_loop = NULL;
static GIOChannel*	g_stdin_channel = NULL;

#ifndef _WIN32
static void on_signal (int, gpointer);
#else
static BOOL on_console_ctrl (DWORD);
#endif
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);
static void destroy_session (gpointer, gpointer, gpointer);
static int on_data (gpointer, guint, gpointer);
static gboolean on_stdin_data (GIOChannel*, GIOCondition, gpointer);


G_GNUC_NORETURN static
void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
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
	pgm_error_t* err = NULL;

/* pre-initialise PGM messages module to add hook for GLib logging */
	pgm_messages_init();
	log_init ();
	g_message ("app");

	if (!pgm_init (&err)) {
		g_error ("Unable to start PGM engine: %s", (err && err->message) ? err->message : "(null)");
		pgm_error_free (err);
		pgm_messages_shutdown();
		return EXIT_FAILURE;
	}

/* parse program arguments */
#ifdef _WIN32
	const char* binary_name = strrchr (argv[0], '\\');
#else
	const char* binary_name = strrchr (argv[0], '/');
#endif
	if (NULL == binary_name)	binary_name = argv[0];
	else				binary_name++;

	int c;
	while ((c = getopt (argc, argv, "s:n:h")) != -1)
	{
		switch (c) {
		case 'n':	g_network = optarg; break;
		case 's':	g_port = atoi (optarg); break;

		case 'h':
		case '?':
				pgm_messages_shutdown();
				usage (binary_name);
		}
	}

	g_loop = g_main_loop_new (NULL, FALSE);

/* setup signal handlers */
#ifndef _WIN32
	signal (SIGSEGV, on_sigsegv);
	signal (SIGHUP,  SIG_IGN);
	pgm_signal_install (SIGINT,  on_signal, g_loop);
	pgm_signal_install (SIGTERM, on_signal, g_loop);
#else
	SetConsoleCtrlHandler ((PHANDLER_ROUTINE)on_console_ctrl, TRUE);
	setvbuf (stdout, (char *) NULL, _IONBF, 0);
#endif /* !_WIN32 */

/* delayed startup */
	g_message ("scheduling startup.");
	g_timeout_add (0, (GSourceFunc)on_startup, NULL);

/* dispatch loop */
	g_message ("entering main event loop ... ");
	g_main_loop_run (g_loop);

	g_message ("event loop terminated, cleaning up.");

/* cleanup */
	g_main_loop_unref(g_loop);
	g_loop = NULL;

	if (g_sessions) {
		g_message ("destroying sessions.");
		g_hash_table_foreach_remove (g_sessions, (GHRFunc)destroy_session, NULL);
		g_hash_table_unref (g_sessions);
		g_sessions = NULL;
	}

	if (g_stdin_channel) {
		puts ("unbinding stdin.");
		g_io_channel_unref (g_stdin_channel);
		g_stdin_channel = NULL;
	}

	g_message ("PGM engine shutdown.");
	pgm_shutdown();
	g_message ("finished.");
	pgm_messages_shutdown();
	return EXIT_SUCCESS;
}

static
void
destroy_session (
                gpointer        key,		/* session name */
                gpointer        value,		/* transport_session object */
                G_GNUC_UNUSED gpointer        user_data
                )
{
	struct app_session* sess = (struct app_session*)value;

	g_message ("closing socket \"%s\"", (char*)key);
	pgm_close (sess->sock, TRUE);
	sess->sock = NULL;

	if (sess->async) {
		g_message ("destroying asynchronous session on \"%s\"", (char*)key);
		pgm_async_destroy (sess->async);
		sess->async = NULL;
	}

	g_free (sess->name);
	sess->name = NULL;
	g_free (sess);
}

#ifndef _WIN32
static
void
on_signal (
	int		signum,
	gpointer	user_data
	)
{
	GMainLoop* loop = (GMainLoop*)user_data;
	g_message ("on_signal (signum:%d user-data:%p)", signum, user_data);
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
#endif /* !_WIN32 */

static
gboolean
on_startup (
	G_GNUC_UNUSED gpointer data
	)
{
	g_message ("startup.");

	g_sessions = g_hash_table_new (g_str_hash, g_str_equal);

/* add stdin to event manager */
#ifndef G_OS_WIN32
	g_stdin_channel = g_io_channel_unix_new (fileno(stdin));
#else
	g_stdin_channel = g_io_channel_win32_new_fd (fileno(stdin));
#endif
	printf ("binding stdin with encoding %s.\n", g_io_channel_get_encoding(g_stdin_channel));

	g_io_add_watch (g_stdin_channel, G_IO_IN | G_IO_PRI, on_stdin_data, NULL);

/* period timer to indicate some form of life */
// TODO: Gnome 2.14: replace with g_timeout_add_seconds()
	g_timeout_add(10 * 1000, (GSourceFunc)on_mark, NULL);

	puts ("READY");
	fflush (stdout);
	return FALSE;
}

static
int
on_data (
	gpointer	data,
	G_GNUC_UNUSED guint		len,
	G_GNUC_UNUSED gpointer	user_data
	)
{
	printf ("DATA: %s\n", (char*)data);
	fflush (stdout);
	return 0;
}

static
void
session_create (
	char*		session_name
	)
{
	pgm_error_t* pgm_err = NULL;

/* check for duplicate */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess != NULL) {
		printf ("FAILED: duplicate session name '%s'\n", session_name);
		return;
	}

/* create new and fill in bits */
	sess = g_new0(struct app_session, 1);
	sess->name = g_memdup (session_name, strlen(session_name)+1);

	if (!pgm_socket (&sess->sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &pgm_err)) {
		printf ("FAILED: pgm_socket(): %s\n", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
		pgm_error_free (pgm_err);
		goto err_free;
	}

/* success */
	g_hash_table_insert (g_sessions, sess->name, sess);
	printf ("created new session \"%s\"\n", sess->name);
	puts ("READY");

	return;

err_free:
	g_free(sess->name);
	g_free(sess);
}

static
void
session_set_nak_bo_ivl (
	char*		session_name,
	guint		milliseconds
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	if (pgm_msecs (milliseconds) > INT_MAX) {
		puts ("FAILED: value out of bounds");
		return;
	}

	const int nak_bo_ivl = pgm_msecs (milliseconds);
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_BO_IVL, &nak_bo_ivl, sizeof(nak_bo_ivl)))
		printf ("FAILED: set NAK_BO_IVL = %dms\n", milliseconds);
	else
		puts ("READY");
}

static
void
session_set_nak_rpt_ivl (
	char*		session_name,
	guint		milliseconds
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	if (pgm_msecs (milliseconds) > INT_MAX) {
		puts ("FAILED: value out of bounds");
		return;
	}

	const int nak_rpt_ivl = pgm_msecs (milliseconds);
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_RPT_IVL, &nak_rpt_ivl, sizeof(nak_rpt_ivl)))
		printf ("FAILED: set NAK_RPT_IVL = %dms\n", milliseconds);
	else
		puts ("READY");
}

static
void
session_set_nak_rdata_ivl (
	char*		session_name,
	guint		milliseconds
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	if (pgm_msecs (milliseconds) > INT_MAX) {
		puts ("FAILED: value out of bounds");
		return;
	}

	const int nak_rdata_ivl = pgm_msecs (milliseconds);
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_RDATA_IVL, &nak_rdata_ivl, sizeof(nak_rdata_ivl)))
		printf ("FAILED: set NAK_RDATA_IVL = %dms\n", milliseconds);
	else
		puts ("READY");
}

static
void
session_set_nak_ncf_retries (
	char*		session_name,
	guint		retry_count
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	if (retry_count > INT_MAX) {
		puts ("FAILED: value out of bounds");
		return;
	}

	const int nak_ncf_retries = retry_count;
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_NCF_RETRIES, &nak_ncf_retries, sizeof(nak_ncf_retries)))
		printf ("FAILED: set NAK_NCF_RETRIES = %d\n", retry_count);
	else
		puts ("READY");
}

static
void
session_set_nak_data_retries (
	char*		session_name,
	guint		retry_count
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	if (retry_count > INT_MAX) {
		puts ("FAILED: value out of bounds");
		return;
	}

	const int nak_data_retries = retry_count;
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_DATA_RETRIES, &nak_data_retries, sizeof(nak_data_retries)))
		printf ("FAILED: set NAK_DATA_RETRIES = %d\n", retry_count);
	else
		puts ("READY");
}

static
void
session_set_txw_max_rte (
	char*		session_name,
	guint		bitrate
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	if (bitrate > INT_MAX) {
		puts ("FAILED: value out of bounds");
		return;
	}

	const int txw_max_rte = bitrate;
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_TXW_MAX_RTE, &txw_max_rte, sizeof(txw_max_rte)))
		printf ("FAILED: set TXW_MAX_RTE = %d\n", bitrate);
	else
		puts ("READY");
}

static
void
session_set_fec (
	char*		session_name,
	guint		block_size,
	guint		group_size
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	if (block_size > UINT8_MAX ||
	    group_size > UINT8_MAX)
	{
		puts ("FAILED: value out of bounds");
		return;
	}

	const struct pgm_fecinfo_t fecinfo = {
		.block_size			= block_size,
		.proactive_packets		= 0,
		.group_size			= group_size,
		.ondemand_parity_enabled	= TRUE,
		.var_pktlen_enabled		= TRUE
	};
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_USE_FEC, &fecinfo, sizeof(fecinfo)))
		printf ("FAILED: set FEC = RS(%d, %d)\n", block_size, group_size);
	else
		puts ("READY");
}

static
void
session_bind (
	char*		session_name
	)
{
	pgm_error_t* pgm_err = NULL;

/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

/* Use RFC 2113 tagging for PGM Router Assist */
	const int no_router_assist = 0;
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_IP_ROUTER_ALERT, &no_router_assist, sizeof(no_router_assist)))
		puts ("FAILED: disable IP_ROUTER_ALERT");

/* set PGM parameters */
	const int send_and_receive = 0,
		  active = 0,
		  mtu = g_max_tpdu,
		  txw_sqns = g_sqns,
		  rxw_sqns = g_sqns,
		  ambient_spm = pgm_secs (30),
		  heartbeat_spm[] = { pgm_msecs (100),
				      pgm_msecs (100),
                                      pgm_msecs (100),
				      pgm_msecs (100),
				      pgm_msecs (1300),
				      pgm_secs  (7),
				      pgm_secs  (16),
				      pgm_secs  (25),
				      pgm_secs  (30) },
		  peer_expiry = pgm_secs (300),
		  spmr_expiry = pgm_msecs (250),
		  nak_bo_ivl = pgm_msecs (50),
		  nak_rpt_ivl = pgm_secs (2),
		  nak_rdata_ivl = pgm_secs (2),
		  nak_data_retries = 50,
		  nak_ncf_retries = 50;

	g_assert (G_N_ELEMENTS(heartbeat_spm) > 0);

	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_SEND_ONLY, &send_and_receive, sizeof(send_and_receive)))
		puts ("FAILED: set bi-directional transport");
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_RECV_ONLY, &send_and_receive, sizeof(send_and_receive)))
		puts ("FAILED: set bi-directional transport");
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_PASSIVE, &active, sizeof(active)))
		puts ("FAILED: set active transport");
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_MTU, &mtu, sizeof(mtu)))
		printf ("FAILED: set MAX_TPDU = %d bytes\n", mtu);
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_TXW_SQNS, &txw_sqns, sizeof(txw_sqns)))
		printf ("FAILED: set TXW_SQNS = %d\n", txw_sqns);
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_RXW_SQNS, &rxw_sqns, sizeof(rxw_sqns)))
		printf ("FAILED: set RXW_SQNS = %d\n", rxw_sqns);
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_AMBIENT_SPM, &ambient_spm, sizeof(ambient_spm)))
		printf ("FAILED: set AMBIENT_SPM = %ds\n", (int)pgm_to_secs (ambient_spm));
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_HEARTBEAT_SPM, &heartbeat_spm, sizeof(heartbeat_spm)))
	{
		char buffer[1024];
		sprintf (buffer, "%d", heartbeat_spm[0]);
		for (unsigned i = 1; i < G_N_ELEMENTS(heartbeat_spm); i++) {
			char t[1024];
			sprintf (t, ", %d", heartbeat_spm[i]);
			strcat (buffer, t);
		}
		printf ("FAILED: set HEARTBEAT_SPM = { %s }\n", buffer);
	}
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_PEER_EXPIRY, &peer_expiry, sizeof(peer_expiry)))
		printf ("FAILED: set PEER_EXPIRY = %ds\n",(int) pgm_to_secs (peer_expiry));
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_SPMR_EXPIRY, &spmr_expiry, sizeof(spmr_expiry)))
		printf ("FAILED: set SPMR_EXPIRY = %dms\n", (int)pgm_to_msecs (spmr_expiry));
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_BO_IVL, &nak_bo_ivl, sizeof(nak_bo_ivl)))
		printf ("FAILED: set NAK_BO_IVL = %dms\n", (int)pgm_to_msecs (nak_bo_ivl));
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_RPT_IVL, &nak_rpt_ivl, sizeof(nak_rpt_ivl)))
		printf ("FAILED: set NAK_RPT_IVL = %dms\n", (int)pgm_to_msecs (nak_rpt_ivl));
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_RDATA_IVL, &nak_rdata_ivl, sizeof(nak_rdata_ivl)))
		printf ("FAILED: set NAK_RDATA_IVL = %dms\n", (int)pgm_to_msecs (nak_rdata_ivl));
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_DATA_RETRIES, &nak_data_retries, sizeof(nak_data_retries)))
		printf ("FAILED: set NAK_DATA_RETRIES = %d\n", nak_data_retries);
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NAK_NCF_RETRIES, &nak_ncf_retries, sizeof(nak_ncf_retries)))
		printf ("FAILED: set NAK_NCF_RETRIES = %d\n", nak_ncf_retries);

/* create global session identifier */
	struct pgm_sockaddr_t addr;
	memset (&addr, 0, sizeof(addr));
	addr.sa_port = g_port;
	addr.sa_addr.sport = 0;
	if (!pgm_gsi_create_from_hostname (&addr.sa_addr.gsi, &pgm_err)) {
		printf ("FAILED: pgm_gsi_create_from_hostname(): %s\n", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
	}

{
	char buffer[1024];
	pgm_tsi_print_r (&addr.sa_addr, buffer, sizeof(buffer));
	printf ("pgm_bind (sock:%p addr:{port:%d tsi:%s} err:%p)\n",
		(gpointer)sess->sock,
		addr.sa_port, buffer,
		(gpointer)&pgm_err);
}
	if (!pgm_bind (sess->sock, &addr, sizeof(addr), &pgm_err)) {
		printf ("FAILED: pgm_bind(): %s\n", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
		pgm_error_free (pgm_err);
	} else 
		puts ("READY");
}

static
void
session_connect (
	char*		session_name
	)
{
	struct pgm_addrinfo_t hints = {
                .ai_family = AF_INET
        }, *res = NULL;
        pgm_error_t* pgm_err = NULL;

/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	if (!pgm_getaddrinfo (g_network, &hints, &res, &pgm_err)) {
                printf ("FAILED: pgm_getaddrinfo(): %s\n", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
                pgm_error_free (pgm_err);
		return;
        }

/* join IP multicast groups */
	for (unsigned i = 0; i < res->ai_recv_addrs_len; i++)
		if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_JOIN_GROUP, &res->ai_recv_addrs[i], sizeof(struct group_req)))
		{
			char group[INET6_ADDRSTRLEN];
			getnameinfo ((struct sockaddr*)&res->ai_recv_addrs[i].gsr_group, sizeof(struct sockaddr_in),
					group, sizeof(group),
					NULL, 0,
					NI_NUMERICHOST);
			printf ("FAILED: join group (#%u %s)\n", (unsigned)res->ai_recv_addrs[i].gsr_interface, group);
		}
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_SEND_GROUP, &res->ai_send_addrs[0], sizeof(struct group_req)))
		{
			char group[INET6_ADDRSTRLEN];
			getnameinfo ((struct sockaddr*)&res->ai_send_addrs[0].gsr_group, sizeof(struct sockaddr_in),
					group, sizeof(group),
					NULL, 0,
					NI_NUMERICHOST);
			printf ("FAILED: send group (#%u %s)\n", (unsigned)res->ai_send_addrs[0].gsr_interface, group);
		}
	pgm_freeaddrinfo (res);

/* set IP parameters */
	const int non_blocking = 1,
		  no_multicast_loop = 0,
		  multicast_hops = 16,
		  dscp = 0x2e << 2;		/* Expedited Forwarding PHB for network elements, no ECN. */

	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_MULTICAST_LOOP, &no_multicast_loop, sizeof(no_multicast_loop)))
		puts ("FAILED: disable multicast loop");
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops)))
		printf ("FAILED: set TTL = %d\n", multicast_hops);
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_TOS, &dscp, sizeof(dscp)))
		printf ("FAILED: set TOS = 0x%x\n", dscp);
	if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NOBLOCK, &non_blocking, sizeof(non_blocking)))
		puts ("FAILED: set non-blocking sockets");

	if (!pgm_connect (sess->sock, &pgm_err)) {
		printf ("FAILED: pgm_connect(): %s\n", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
	} else
		puts ("READY");
}

static
void
session_send (
	char*		session_name,
	char*		string
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

/* send message */
	int status;
	gsize stringlen = strlen(string) + 1;
	struct timeval tv;
#ifdef HAVE_POLL
	int n_fds = 1;
	struct pollfd fds[ n_fds ];
	int timeout;
#else
	int send_sock;
	DWORD cEvents = 1;
	WSAEVENT waitEvents[ cEvents ];
	DWORD timeout, dwEvents;
        socklen_t socklen = sizeof(int);

        waitEvents[0] = WSACreateEvent ();
        pgm_getsockopt (sess->sock, IPPROTO_PGM, PGM_SEND_SOCK, &send_sock, &socklen);
        WSAEventSelect (send_sock, waitEvents[0], FD_WRITE);
#endif
again:
printf ("pgm_send (sock:%p string:\"%s\" stringlen:%" G_GSIZE_FORMAT " NULL)\n", (gpointer)sess->sock, string, stringlen);
	status = pgm_send (sess->sock, string, stringlen, NULL);
	switch (status) {
	case PGM_IO_STATUS_NORMAL:
		puts ("READY");
		break;
	case PGM_IO_STATUS_TIMER_PENDING:
		{
			socklen_t optlen = sizeof (tv);
			pgm_getsockopt (sess->sock, IPPROTO_PGM, PGM_TIME_REMAIN, &tv, &optlen);
		}
		goto block;
	case PGM_IO_STATUS_RATE_LIMITED:
		{
			socklen_t optlen = sizeof (tv);
			pgm_getsockopt (sess->sock, IPPROTO_PGM, PGM_RATE_REMAIN, &tv, &optlen);
		}
/* fall through */
	case PGM_IO_STATUS_WOULD_BLOCK:
block:
#ifdef HAVE_POLL
		timeout = PGM_IO_STATUS_WOULD_BLOCK == status ? -1 : ((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
		memset (fds, 0, sizeof(fds));
		pgm_poll_info (sess->sock, fds, &n_fds, POLLOUT);
		poll (fds, n_fds, timeout /* ms */);
#else
		timeout = PGM_IO_STATUS_WOULD_BLOCK == status ? WSA_INFINITE : (DWORD)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
		dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, timeout, FALSE);
		switch (dwEvents) {
		case WSA_WAIT_EVENT_0+1: WSAResetEvent (waitEvents[0]); break;
		default: break;
		}
#endif
		goto again;
	default:
		puts ("FAILED: pgm_send()");
		break;
	}

#ifndef HAVE_POLL
	WSACloseEvent (waitEvents[0]);
#endif
}

static
void
session_listen (
	char*		session_name
	)
{
	GError* err = NULL;

/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

/* listen */
printf ("pgm_async_create (async:%p sock:%p err:%p)\n", (gpointer)&sess->async, (gpointer)sess->sock, (gpointer)&err);
	if (!pgm_async_create (&sess->async, sess->sock, &err)) {
		printf ("FAILED: pgm_async_create(): %s", err->message);
		g_error_free (err);
		return;
	}
	pgm_async_add_watch (sess->async, on_data, sess);
	puts ("READY");
}

static
void
session_destroy (
	char*		session_name
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

/* remove from hash table */
	g_hash_table_remove (g_sessions, session_name);

/* stop any async thread */
	if (sess->async) {
		pgm_async_destroy (sess->async);
		sess->async = NULL;
	}

	pgm_close (sess->sock, TRUE);
	sess->sock = NULL;
	g_free (sess->name);
	sess->name = NULL;
	g_free (sess);

	puts ("READY");
}

/* process input commands from stdin/fd 
 */

static
gboolean
on_stdin_data (
	GIOChannel* source,
	G_GNUC_UNUSED GIOCondition condition,
	G_GNUC_UNUSED gpointer data
	)
{
	gchar* str = NULL;
        gsize len = 0;
        gsize term = 0;
        GError* err = NULL;

        g_io_channel_read_line (source, &str, &len, &term, &err);
        if (len > 0) {
                if (term) str[term] = 0;

/* quit */
                if (strcmp(str, "quit") == 0)
		{
                        g_main_loop_quit(g_loop);
			goto out;
		}

		regex_t preg;
		regmatch_t pmatch[10];

/* create socket */
		const char *re = "^create[[:space:]]+([[:alnum:]]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			session_create (name);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* set NAK_BO_IVL */
		re = "^set[[:space:]]+([[:alnum:]]+)[[:space:]]+NAK_BO_IVL[[:space:]]+([0-9]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char *p = str + pmatch[2].rm_so;
			guint nak_bo_ivl = strtol (p, &p, 10);

			session_set_nak_bo_ivl (name, nak_bo_ivl);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* set NAK_RPT_IVL */
		re = "^set[[:space:]]+([[:alnum:]]+)[[:space:]]+NAK_RPT_IVL[[:space:]]+([0-9]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char *p = str + pmatch[2].rm_so;
			guint nak_rpt_ivl = strtol (p, &p, 10);

			session_set_nak_rpt_ivl (name, nak_rpt_ivl);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* set NAK_RDATA_IVL */
		re = "^set[[:space:]]+([[:alnum:]]+)[[:space:]]+NAK_RDATA_IVL[[:space:]]+([0-9]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char *p = str + pmatch[2].rm_so;
			guint nak_rdata_ivl = strtol (p, &p, 10);

			session_set_nak_rdata_ivl (name, nak_rdata_ivl);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* set NAK_NCF_RETRIES */
		re = "^set[[:space:]]+([[:alnum:]]+)[[:space:]]+NAK_NCF_RETRIES[[:space:]]+([0-9]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char *p = str + pmatch[2].rm_so;
			guint nak_ncf_retries = strtol (p, &p, 10);

			session_set_nak_ncf_retries (name, nak_ncf_retries);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* set NAK_DATA_RETRIES */
		re = "^set[[:space:]]+([[:alnum:]]+)[[:space:]]+NAK_DATA_RETRIES[[:space:]]+([0-9]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char *p = str + pmatch[2].rm_so;
			guint nak_data_retries = strtol (p, &p, 10);

			session_set_nak_data_retries (name, nak_data_retries);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* set TXW_MAX_RTE */
		re = "^set[[:space:]]+([[:alnum:]]+)[[:space:]]+TXW_MAX_RTE[[:space:]]+([0-9]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char *p = str + pmatch[2].rm_so;
			guint txw_max_rte = strtol (p, &p, 10);

			session_set_txw_max_rte (name, txw_max_rte);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* enable Reed-Solomon Forward Error Correction */
		re = "^set[[:space:]]+([[:alnum:]]+)[[:space:]]+FEC[[:space:]]+RS[[:space:]]*\\([[:space:]]*([0-9]+)[[:space:]]*,[[:space:]]*([0-9]+)[[:space:]]*\\)$";
		regcomp (&preg, re, REG_EXTENDED);

		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char *p = str + pmatch[2].rm_so;
			*(str + pmatch[2].rm_eo) = 0;
			guint n = strtol (p, &p, 10);
			p = str + pmatch[3].rm_so;
			*(str + pmatch[3].rm_eo) = 0;
			guint k = strtol (p, &p, 10);
			session_set_fec (name, n, k);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* bind socket */
		re = "^bind[[:space:]]+([[:alnum:]]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			session_bind (name);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* connect socket */
		re = "^connect[[:space:]]+([[:alnum:]]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			session_connect (name);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* send packet */
		re = "^send[[:space:]]+([[:alnum:]]+)[[:space:]]+([[:alnum:]]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char *string = g_memdup (str + pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so + 1 );
			string[ pmatch[2].rm_eo - pmatch[2].rm_so ] = 0;

			session_send (name, string);

			g_free (name);
			g_free (string);
			regfree (&preg);
			goto out;
                }
		regfree (&preg);

/* listen */
		re = "^listen[[:space:]]+([[:alnum:]]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			session_listen (name);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* destroy transport */
		re = "^destroy[[:space:]]+([[:alnum:]]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			session_destroy (name);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* set PGM network */
		re = "^set[[:space:]]+network[[:space:]]+([[:print:]]*;[[:print:]]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char* pgm_network = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			pgm_network[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;
			g_network = pgm_network;
			puts ("READY");

			regfree (&preg);
			goto out;
		}
		regfree (&preg);

                printf ("unknown command: %s\n", str);
        }

out:
	fflush (stdout);
        g_free (str);
        return TRUE;
}

/* idle log notification
 */

static
gboolean
on_mark (
	G_GNUC_UNUSED gpointer data
	)
{
	g_message ("-- MARK --");
	return TRUE;
}

/* eof */
