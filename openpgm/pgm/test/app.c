/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM conformance test application.
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
#include <getopt.h>
#include <netdb.h>
#include <regex.h>
#include <sched.h>
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

#include <pgm/backtrace.h>
#include <pgm/log.h>
#include <pgm/transport.h>
#include <pgm/source.h>
#include <pgm/receiver.h>
#include <pgm/gsi.h>
#include <pgm/signal.h>
#include <pgm/timer.h>
#include <pgm/if.h>
#include <pgm/async.h>


/* typedefs */

struct idle_source {
	GSource			source;
	guint64			expiration;
};

struct app_session {
	char*			name;
	pgm_transport_t*	transport;
	pgm_async_t*		async;
};

/* globals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN	"app"

static int g_port = 7500;
static const char* g_network = ";239.192.0.1";

static guint g_max_tpdu = 1500;
static guint g_sqns = 100 * 1000;

static GHashTable* g_sessions = NULL;
static GMainLoop* g_loop = NULL;
static GIOChannel* g_stdin_channel = NULL;


static void on_signal (int, gpointer);
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
	g_message ("app");

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

	log_init ();
	pgm_init ();

	g_loop = g_main_loop_new (NULL, FALSE);

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
	signal (SIGHUP, SIG_IGN);
	pgm_signal_install (SIGINT, on_signal, g_loop);
	pgm_signal_install (SIGTERM, on_signal, g_loop);

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

	g_message ("finished.");
	return 0;
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

	g_message ("destroying transport \"%s\"", (char*)key);
	pgm_transport_destroy (sess->transport, TRUE);
	sess->transport = NULL;

	if (sess->async) {
		g_message ("destroying asynchronous session on \"%s\"", (char*)key);
		pgm_async_destroy (sess->async);
		sess->async = NULL;
	}

	g_free (sess->name);
	sess->name = NULL;
	g_free (sess);
}

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

static
gboolean
on_startup (
	G_GNUC_UNUSED gpointer data
	)
{
	g_message ("startup.");

	g_sessions = g_hash_table_new (g_str_hash, g_str_equal);

/* add stdin to event manager */
	g_stdin_channel = g_io_channel_unix_new (fileno(stdin));
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
	char*		name
	)
{
	struct pgm_transport_info_t hints = {
		.ti_family = AF_INET
	}, *res = NULL;
	GError* err = NULL;

/* check for duplicate */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess != NULL) {
		puts ("FAILED: duplicate session");
		return;
	}

/* create new and fill in bits */
	sess = g_malloc0(sizeof(struct app_session));
	sess->name = g_memdup (name, strlen(name)+1);

	if (!pgm_if_get_transport_info (g_network, &hints, &res, &err)) {
		printf ("FAILED: pgm_if_get_transport_info(): %s\n", err->message);
		g_error_free (err);
		goto err_free;
	}

	if (!pgm_gsi_create_from_hostname (&res->ti_gsi, &err)) {
		printf ("FAILED: pgm_gsi_create_from_hostname(): %s\n", err->message);
		g_error_free (err);
		pgm_if_free_transport_info (res);
		goto err_free;
	}

	res->ti_sport = g_port;
	res->ti_dport = 0;
	if (!pgm_transport_create (&sess->transport, res, &err)) {
		printf ("FAILED: pgm_transport_create(): %s\n", err->message);
		g_error_free (err);
		pgm_if_free_transport_info (res);
		goto err_free;
	}

	pgm_if_free_transport_info (res);

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
	char*		name,
	guint		nak_bo_ivl		/* milliseconds */
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	if (!pgm_transport_set_nak_bo_ivl (sess->transport, pgm_msecs(nak_bo_ivl)))
		puts ("FAILED: pgm_transport_set_nak_bo_ivl");
	else
		puts ("READY");
}

static
void
session_set_nak_rpt_ivl (
	char*		name,
	guint		nak_rpt_ivl		/* milliseconds */
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	if (!pgm_transport_set_nak_rpt_ivl (sess->transport, pgm_msecs(nak_rpt_ivl)))
		puts ("FAILED: pgm_transport_set_nak_rpt_ivl");
	else
		puts ("READY");
}

static
void
session_set_nak_rdata_ivl (
	char*		name,
	guint		nak_rdata_ivl		/* milliseconds */
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	if (!pgm_transport_set_nak_rdata_ivl (sess->transport, pgm_msecs(nak_rdata_ivl)))
		puts ("FAILED: pgm_transport_set_nak_rdata_ivl");
	else
		puts ("READY");
}

static
void
session_set_nak_ncf_retries (
	char*		name,
	guint		nak_ncf_retries
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	if (!pgm_transport_set_nak_ncf_retries (sess->transport, nak_ncf_retries))
		puts ("FAILED pgm_transport_set_nak_ncf_retries");
	else
		puts ("READY");
}

static
void
session_set_nak_data_retries (
	char*		name,
	guint		nak_data_retries
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	if (!pgm_transport_set_nak_data_retries (sess->transport, nak_data_retries))
		puts ("FAILED: pgm_transport_set_nak_data_retries");
	else
		puts ("READY");
}

static
void
session_set_txw_max_rte (
	char*		name,
	guint		txw_max_rte
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	if (!pgm_transport_set_txw_max_rte (sess->transport, txw_max_rte))
		puts ("FAILED:pgm_transport_set_txw_max_rte");
	else
		puts ("READY");
}

static
void
session_set_fec (
	char*		name,
	guint		default_n,
	guint		default_k
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	if (!pgm_transport_set_fec (sess->transport, FALSE /* pro-active */, TRUE /* on-demand */, TRUE /* varpkt-len */, default_n, default_k))
		puts ("FAILED: pgm_transport_set_fec");
	else
		puts ("READY");
}

static
void
session_bind (
	char*		name
	)
{
	const guint spm_heartbeat[] = { pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(1300), pgm_secs(7), pgm_secs(16), pgm_secs(25), pgm_secs(30) };
	GError* err = NULL;

/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	if (!pgm_transport_set_max_tpdu (sess->transport, g_max_tpdu))
		puts ("FAILED: pgm_transport_set_max_tpdu");
	if (!pgm_transport_set_txw_sqns (sess->transport, g_sqns))
		puts ("FAILED: pgm_transport_set_txw_sqns");
	if (!pgm_transport_set_rxw_sqns (sess->transport, g_sqns))
		puts ("FAILED: pgm_transport_set_rxw_sqns");
	if (!pgm_transport_set_hops (sess->transport, 16))
		puts ("FAILED: pgm_transport_set_hops");
	if (!pgm_transport_set_ambient_spm (sess->transport, pgm_secs(30)))
		puts ("FAILED: pgm_transport_set_ambient_spm");
	if (!pgm_transport_set_heartbeat_spm (sess->transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat)))
		puts ("FAILED: pgm_transport_set_heartbeat_spm");
	if (!pgm_transport_set_peer_expiry (sess->transport, pgm_secs(300)))
		puts ("FAILED: pgm_transport_set_peer_expiry");
	if (!pgm_transport_set_spmr_expiry (sess->transport, pgm_msecs(250)))
		puts ("FAILED: pgm_transport_set_spmr_expiry");
	if (!sess->transport->nak_bo_ivl && !pgm_transport_set_nak_bo_ivl (sess->transport, pgm_msecs(50)))
		puts ("FAILED: pgm_transport_set_nak_bo_ivl");
	if (!sess->transport->nak_rpt_ivl && !pgm_transport_set_nak_rpt_ivl (sess->transport, pgm_secs(2)))
		puts ("FAILED: pgm_transport_set_nak_rpt_ivl");
	if (!sess->transport->nak_rdata_ivl && !pgm_transport_set_nak_rdata_ivl (sess->transport, pgm_secs(2)))
		puts ("FAILED: pgm_transport_set_nak_rdata_ivl");
	if (!sess->transport->nak_data_retries && !pgm_transport_set_nak_data_retries (sess->transport, 50))
		puts ("FAILED: pgm_transport_set_nak_data_retries");
	if (!sess->transport->nak_ncf_retries && !pgm_transport_set_nak_ncf_retries (sess->transport, 50))
		puts ("FAILED: pgm_transport_set_nak_ncf_retries");

	if (!pgm_transport_bind (sess->transport, &err)) {
		printf ("FAILED: pgm_transport_bind(): %s\n", err->message);
		g_error_free (err);
	} else 
		puts ("READY");
}

static
void
session_send (
	char*		name,
	char*		string
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

/* send message */
	if (G_IO_STATUS_NORMAL != pgm_send (sess->transport, string, strlen(string) + 1, NULL))
		puts ("FAILED: pgm_transport_send()");
	else
		puts ("READY");
}

static
void
session_listen (
	char*		name
	)
{
	GError* err = NULL;

/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

/* listen */
	if (!pgm_async_create (&sess->async, sess->transport, &err)) {
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
	char*		name
	)
{
/* check that session exists */
	struct app_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

/* remove from hash table */
	g_hash_table_remove (g_sessions, name);

/* stop any async thread */
	if (sess->async) {
		pgm_async_destroy (sess->async);
		sess->async = NULL;
	}

	pgm_transport_destroy (sess->transport, TRUE);
	sess->transport = NULL;
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

/* create transport */
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

/* bind transport */
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
