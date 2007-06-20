/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM conformance endpoint simulator.
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
#include <pgm/gsi.h>
#include <pgm/signal.h>
#include <pgm/timer.h>


/* typedefs */

struct idle_source {
	GSource		source;
	guint64		expiration;
};

struct sim_session {
	char*		name;
	char		gsi[6];
	struct pgm_transport* transport;
};

/* globals */

static int g_port = 7500;
static char* g_network = ";226.0.0.1";

static int g_odata_interval = 1 * 100 * 1000;	/* 100 ms */
static int g_payload = 0;
static int g_max_tpdu = 1500;
static int g_sqns = 100 * 1000;

static GHashTable* g_sessions = NULL;
static GMainLoop* g_loop = NULL;
static GIOChannel* g_stdin_channel = NULL;


static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static void destroy_session (gpointer, gpointer, gpointer);

static void send_odata (void);
static gboolean on_odata_timer (gpointer);
static int on_data (gpointer, guint, gpointer);

static gboolean on_stdin_data (GIOChannel*, GIOCondition, gpointer);

static gboolean idle_prepare (GSource*, gint*);
static gboolean idle_check (GSource*);
static gboolean idle_dispatch (GSource*, GSourceFunc, gpointer);


static void
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
	g_message ("sim");

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
	signal_install (SIGINT, on_signal);
	signal_install (SIGTERM, on_signal);
	signal_install (SIGHUP, SIG_IGN);

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
		g_hash_table_foreach (g_sessions, (GHFunc)destroy_session, NULL);
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

static void
destroy_session (
                gpointer        key,		/* session name */
                gpointer        value,		/* transport_session object */
                gpointer        user_data
                )
{
	printf ("destroying transport \"%s\"\n", (char*)key);
	struct sim_session* sess = (struct sim_session*)value;
	pgm_transport_destroy (sess->transport, TRUE);
	sess->transport = NULL;
	g_free (sess->name);
	sess->name = NULL;
	g_free (sess);
}

static void
on_signal (
	int	signum
	)
{
	g_message ("on_signal");

	g_main_loop_quit(g_loop);
}

static gboolean
on_startup (
	gpointer data
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

static gboolean
idle_prepare (
	GSource*	source,
	gint*		timeout
	)
{
	struct idle_source* idle_source = (struct idle_source*)source;

	guint64 now = time_update_now();
	glong msec = ((gint64)idle_source->expiration - (gint64)now) / 1000;
	if (msec < 0)
		msec = 0;
	else
		msec = MIN (G_MAXINT, (guint)msec);
	*timeout = (gint)msec;
	return (msec == 0);
}

static gboolean
idle_check (
	GSource*	source
	)
{
	struct idle_source* idle_source = (struct idle_source*)source;
	guint64 now = time_update_now();
	return ( time_after_eq(now, idle_source->expiration) );
}

static gboolean
idle_dispatch (
	GSource*	source,
	GSourceFunc	callback,
	gpointer	user_data
	)
{
	struct idle_source* idle_source = (struct idle_source*)source;

//	send_odata ();
	idle_source->expiration += g_odata_interval;

	if ( time_after_eq(idle_source->expiration, time_now) )
		sched_yield();

	return TRUE;
}

static int
on_data (
	gpointer	data,
	guint		len,
	gpointer	user_data
	)
{
	printf ("DATA: %s\n", (char*)data);
	fflush (stdout);

	return 0;
}

void
session_create (
	char*		name
	)
{
/* check for duplicate */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess != NULL) {
		puts ("FAILED: duplicate session");
		return;
	}

/* create new and fill in bits */
	sess = g_malloc0(sizeof(struct sim_session));
	sess->name = g_memdup (name, strlen(name)+1);
	int e = gsi_create_md5_id (sess->gsi);
	if (e != 0) {
		puts ("FAILED: gsi_create_md5_id()");
		goto err_free;
	}

/* temp fixed addresses */
	struct sock_mreq recv_smr, send_smr;
	int smr_len = 1;
	e = if_parse_transport (g_network, AF_INET, &recv_smr, &send_smr, &smr_len);
	g_assert (e == 0);
	g_assert (smr_len == 1);

	e = pgm_transport_create (&sess->transport, sess->gsi, g_port, &recv_smr, 1, &send_smr);
	if (e != 0) {
		puts ("FAILED: pgm_transport_create()");
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

void
session_bind (
	char*		name
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	pgm_transport_set_max_tpdu (sess->transport, g_max_tpdu);
	pgm_transport_set_txw_sqns (sess->transport, g_sqns);
	pgm_transport_set_rxw_sqns (sess->transport, g_sqns);
	pgm_transport_set_hops (sess->transport, 16);
	pgm_transport_set_ambient_spm (sess->transport, 8192*1000);
	guint spm_heartbeat[] = { 1*1000, 1*1000, 2*1000, 4*1000, 8*1000, 16*1000, 32*1000, 64*1000, 128*1000, 256*1000, 512*1000, 1024*1000, 2048*1000, 4096*1000, 8192*1000 };
	pgm_transport_set_heartbeat_spm (sess->transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat));
	pgm_transport_set_peer_expiry (sess->transport, 5*8192*1000);
	pgm_transport_set_spmr_expiry (sess->transport, 250*1000);
	pgm_transport_set_nak_rb_ivl (sess->transport, 50*1000);
	pgm_transport_set_nak_rpt_ivl (sess->transport, 200*1000);
	pgm_transport_set_nak_rdata_ivl (sess->transport, 200*1000);
	pgm_transport_set_nak_data_retries (sess->transport, 5);
	pgm_transport_set_nak_ncf_retries (sess->transport, 2);

	int e = pgm_transport_bind (sess->transport);
	if (e != 0) {
		puts ("FAILED: pgm_transport_bind()");
		return;
	}

	puts ("READY");
}

void
session_send (
	char*		name,
	char*		string
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

/* send message */
        int e = pgm_write_copy (sess->transport, string, strlen(string) + 1);
        if (e < 0) {
		puts ("FAILED: pgm_write_copy()");
        }

	puts ("READY");
}

void
session_destroy (
	char*		name
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	pgm_transport_destroy (sess->transport, TRUE);
	sess->transport = NULL;
	g_free (sess->name);
	sess->name = NULL;
	g_free (sess);

	puts ("READY");
}

void
net_send_nak (
	char*		name,
	struct tsi*	tsi,
	guint32		sqn
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

/* check that the peer exists */
	struct pgm_transport* transport = sess->transport;
	struct pgm_peer* peer = g_hash_table_lookup (transport->peers, tsi);
	if (peer == NULL) {
		printf ("FAILED: peer \"%s\" not found\n", pgm_print_tsi(tsi));
		return;
	}

/* send */
        int retval = 0;
        gchar buf[ sizeof(struct pgm_header) + sizeof(struct pgm_nak) ];
        int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);

        struct pgm_header *header = (struct pgm_header*)buf;
        struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
        memcpy (&header->pgm_gsi, transport->tsi.gsi, 6);
/* dport & sport swap over for a nak */
        header->pgm_sport       = sockaddr_port(&transport->recv_smr[0].smr_multiaddr);
        header->pgm_dport       = peer->tsi.sport;
        header->pgm_type        = PGM_NAK;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = 0;

/* NAK */
        nak->nak_sqn            = g_htonl (sqn);

/* source nla */
        sockaddr_to_nla ((struct sockaddr*)&peer->nla, (char*)&nak->nak_src_nla_afi);

/* group nla */
        sockaddr_to_nla ((struct sockaddr*)&transport->recv_smr[0].smr_multiaddr, (char*)&nak->nak_grp_nla_afi);

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_cksum((char*)header, tpdu_length, 0);

        retval = sendto (transport->send_sock,
                                header,
                                tpdu_length,
                                MSG_CONFIRM,            /* not expecting a reply */
                                (struct sockaddr*)&peer->nla,
                                sockaddr_len(&peer->nla));

	puts ("READY");
}

/* process input commands from stdin/fd 
 */

static gboolean
on_stdin_data (
	GIOChannel* source,
	GIOCondition condition,
	gpointer data
	)
{
	gchar* str = NULL;
        gsize len = 0;
        gsize term = 0;
        GError* err = NULL;

        g_io_channel_read_line (source, &str, &len, &term, &err);
        if (len > 0) {
                if (term) str[term] = 0;

                if (strcmp(str, "quit") == 0)
		{
                        g_main_loop_quit(g_loop);
			goto out;
		}

		regex_t preg;
		regmatch_t pmatch[10];
		char *re;

/* endpoint simulator specific: */

		re = "^net +send +nak +([0-9a-z]+) +([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+) +([0-9]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			struct tsi tsi;
			char *p = str + pmatch[2].rm_so;
			tsi.gsi[0] = strtol (p, &p, 10);
			++p;
			tsi.gsi[1] = strtol (p, &p, 10);
			++p;
			tsi.gsi[2] = strtol (p, &p, 10);
			++p;
			tsi.gsi[3] = strtol (p, &p, 10);
			++p;
			tsi.gsi[4] = strtol (p, &p, 10);
			++p;
			tsi.gsi[5] = strtol (p, &p, 10);
			++p;
			tsi.sport = g_htons ( strtol (p, NULL, 10) );

			guint32 sqn = strtol (str + pmatch[3].rm_so, NULL, 10);

			net_send_nak (name, &tsi, sqn);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* same as test application: */

		re = "^create[[:space:]]+([[:alnum:]]+)$";
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

static gboolean
on_mark (
	gpointer data
	)
{
	static struct timeval tv;
	gettimeofday(&tv, NULL);
	printf ("MARK %s\n", ts_format((tv.tv_sec + g_timezone) % 86400, tv.tv_usec));
	fflush (stdout);

	return TRUE;
}

/* eof */
