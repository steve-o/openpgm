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
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <glib.h>

#include <pgm/if.h>
#include <pgm/backtrace.h>
#include <pgm/log.h>
#include <pgm/packet.h>
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
	pgm_gsi_t	gsi;
	pgm_transport_t* transport;
	gboolean	transport_is_fake;
};

/* globals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN	"sim"

static int g_port = 7500;
static char* g_network = ";239.192.0.1";

static int g_max_tpdu = 1500;
static int g_sqns = 100 * 1000;

static GHashTable* g_sessions = NULL;
static GMainLoop* g_loop = NULL;
static GIOChannel* g_stdin_channel = NULL;


static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static void destroy_session (gpointer, gpointer, gpointer);

static gboolean on_stdin_data (GIOChannel*, GIOCondition, gpointer);

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
	pgm_signal_install (SIGINT, on_signal);
	pgm_signal_install (SIGTERM, on_signal);
	pgm_signal_install (SIGHUP, SIG_IGN);

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

int
fake_pgm_transport_create (
	pgm_transport_t**	transport_,
	pgm_gsi_t*		gsi,
	guint16			dport,
	struct pgm_sock_mreq*	recv_smr,	/* receive port, multicast group & interface address */
	int			recv_len,
	struct pgm_sock_mreq*	send_smr	/* send ... */
	)
{
	guint16 udp_encap_port = ((struct sockaddr_in*)&send_smr->smr_multiaddr)->sin_port;

	g_return_val_if_fail (transport_ != NULL, -EINVAL);
	g_return_val_if_fail (recv_smr != NULL, -EINVAL);
	g_return_val_if_fail (recv_len > 0, -EINVAL);
	g_return_val_if_fail (recv_len <= IP_MAX_MEMBERSHIPS, -EINVAL);
	g_return_val_if_fail (send_smr != NULL, -EINVAL);
	for (int i = 0; i < recv_len; i++)
	{
		g_return_val_if_fail (pgm_sockaddr_family(&recv_smr[i].smr_multiaddr) == pgm_sockaddr_family(&recv_smr[0].smr_multiaddr), -EINVAL);
		g_return_val_if_fail (pgm_sockaddr_family(&recv_smr[i].smr_multiaddr) == pgm_sockaddr_family(&recv_smr[i].smr_interface), -EINVAL);
	}
	g_return_val_if_fail (pgm_sockaddr_family(&send_smr->smr_multiaddr) == pgm_sockaddr_family(&send_smr->smr_interface), -EINVAL);

	int retval = 0;
	pgm_transport_t* transport;

/* create transport object */
	transport = g_malloc0 (sizeof(pgm_transport_t));

	memcpy (&transport->tsi.gsi, gsi, 6);
	transport->dport = g_htons (dport);
	do {
		transport->tsi.sport = g_htons (g_random_int_range (0, UINT16_MAX));
	} while (transport->tsi.sport == transport->dport);

/* network data ports */
	transport->udp_encap_port = udp_encap_port;

/* copy network parameters */
	memcpy (&transport->send_smr, send_smr, sizeof(struct pgm_sock_mreq));
	for (int i = 0; i < recv_len; i++)
	{
		memcpy (&transport->recv_smr[i], &recv_smr[i], sizeof(struct pgm_sock_mreq));
	}

/* open sockets to implement PGM */
	int socket_type, protocol;
	if (transport->udp_encap_port) {
                puts ("opening UDP encapsulated sockets.");
                socket_type = SOCK_DGRAM;
                protocol = IPPROTO_UDP;
        } else {
                puts ("opening raw sockets.");
                socket_type = SOCK_RAW;
                protocol = IPPROTO_PGM;
        }

        if ((transport->send_sock = socket(pgm_sockaddr_family(&send_smr->smr_interface),
                                                socket_type,
                                                protocol)) < 0)
        {
                retval = errno;
                goto err_destroy;
        }

        if ((transport->send_with_router_alert_sock = socket(pgm_sockaddr_family(&send_smr->smr_interface),
                                                socket_type,
                                                protocol)) < 0)
        {
                retval = errno;
                goto err_destroy;
        }

	*transport_ = transport;
	return retval;

err_destroy:
        if (transport->send_sock) {
                close(transport->send_sock);
                transport->send_sock = 0;
        }
        if (transport->send_with_router_alert_sock) {
                close(transport->send_with_router_alert_sock);
                transport->send_with_router_alert_sock = 0;
        }

        g_free (transport);
        transport = NULL;

        return retval;
}

int
fake_pgm_transport_bind (
	pgm_transport_t*	transport
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->bound, -EINVAL);

	int retval = 0;

/* bind udp unicast sockets to interfaces, note multicast on a bound interface is
 * fruity on some platforms so callee should specify any interface.
 *
 * after binding default interfaces (0.0.0.0) are resolved
 */
	retval = bind (transport->send_sock,
                        (struct sockaddr*)&transport->send_smr.smr_interface,
                        pgm_sockaddr_len(&transport->send_smr.smr_interface));
        if (retval < 0) {
                retval = errno;
		goto out;
        }

/* resolve bound address if 0.0.0.0 */
        if (((struct sockaddr_in*)&transport->send_smr.smr_interface)->sin_addr.s_addr == INADDR_ANY)
        {
                char hostname[NI_MAXHOST + 1];
                gethostname (hostname, sizeof(hostname));
                struct hostent *he = gethostbyname (hostname);
                if (he == NULL) {
                        retval = errno;
                        puts ("gethostbyname failed on local hostname");
                        goto out;
                }

                ((struct sockaddr_in*)&transport->send_smr.smr_interface)->sin_addr.s_addr = ((struct in_addr*)(he->h_addr_list[0]))->s_addr;
        }

	retval = bind (transport->send_with_router_alert_sock,
                        (struct sockaddr*)&transport->send_smr.smr_interface,
                        pgm_sockaddr_len(&transport->send_smr.smr_interface));
        if (retval < 0) {
                retval = errno;
		goto out;
        }

/* send group (singular) */
        retval = pgm_sockaddr_multicast_if (transport->send_sock, &transport->send_smr);
        if (retval < 0) {
                retval = errno;
                goto out;
        }

	retval = pgm_sockaddr_multicast_if (transport->send_with_router_alert_sock, &transport->send_smr);
        if (retval < 0) {
                retval = errno;
		goto out;
        }

/* multicast loopback */
        retval = pgm_sockaddr_multicast_loop (transport->send_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), FALSE);
        if (retval < 0) {
                retval = errno;
                goto out;
        }
        retval = pgm_sockaddr_multicast_loop (transport->send_with_router_alert_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), FALSE);
        if (retval < 0) {
                retval = errno;
                goto out;
        }

/* multicast ttl: many crappy network devices go CPU ape with TTL=1, 16 is a popular alternative */
        retval = pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), transport->hops);
        if (retval < 0) {
                retval = errno;
                goto out;
        }
        retval = pgm_sockaddr_multicast_hops (transport->send_with_router_alert_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), transport->hops);
        if (retval < 0) {
                retval = errno;
                goto out;
        }

/* set low packet latency preference for network elements */
        int tos = IPTOS_LOWDELAY;
        retval = pgm_sockaddr_tos (transport->send_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), tos);
        if (retval < 0) {
                retval = errno;
                goto out;
        }
        retval = pgm_sockaddr_tos (transport->send_with_router_alert_sock, pgm_sockaddr_family(&transport->send_smr.smr_interface), tos);
        if (retval < 0) {
                retval = errno;
                goto out;
        }

/* cleanup */
	transport->bound = TRUE;

out:
	return retval;
}

int
fake_pgm_transport_destroy (
	pgm_transport_t*	transport,
	gboolean		flush
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);

	if (transport->send_sock) {
                puts ("closing send socket.");
                close(transport->send_sock);
                transport->send_sock = 0;
        }
        if (transport->send_with_router_alert_sock) {
                puts ("closing send with router alert socket.");
                close(transport->send_with_router_alert_sock);
                transport->send_with_router_alert_sock = 0;
        }

	g_free (transport);
	return 0;
}

void
session_create (
	char*		name,
	gboolean	is_fake
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
	int e = pgm_create_md5_gsi (&sess->gsi);
	if (e != 0) {
		puts ("FAILED: pgm_create_md5_gsi()");
		goto err_free;
	}

/* temp fixed addresses */
	struct pgm_sock_mreq recv_smr, send_smr;
	int smr_len = 1;
	e = pgm_if_parse_transport (g_network, AF_INET, &recv_smr, &send_smr, &smr_len);
	g_assert (e == 0);
	g_assert (smr_len == 1);

	if (is_fake) {
		sess->transport_is_fake = TRUE;
		e = fake_pgm_transport_create (&sess->transport, &sess->gsi, g_port, &recv_smr, 1, &send_smr);
	} else {
		e = pgm_transport_create (&sess->transport, &sess->gsi, g_port, &recv_smr, 1, &send_smr);
	}
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
	pgm_transport_set_ambient_spm (sess->transport, pgm_secs(30));
	guint spm_heartbeat[] = { pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(1300), pgm_secs(7), pgm_secs(16), pgm_secs(25), pgm_secs(30) };
	pgm_transport_set_heartbeat_spm (sess->transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat));
	pgm_transport_set_peer_expiry (sess->transport, pgm_secs(300));
	pgm_transport_set_spmr_expiry (sess->transport, pgm_msecs(250));
	pgm_transport_set_nak_rb_ivl (sess->transport, pgm_msecs(50));
	pgm_transport_set_nak_rpt_ivl (sess->transport, pgm_secs(2));
	pgm_transport_set_nak_rdata_ivl (sess->transport, pgm_secs(2));
	pgm_transport_set_nak_data_retries (sess->transport, 50);
	pgm_transport_set_nak_ncf_retries (sess->transport, 50);

	int e = 0;
	if (sess->transport_is_fake)
	{
		e = fake_pgm_transport_bind (sess->transport);
	}
	else
	{
		e = pgm_transport_bind (sess->transport);
	}

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
        int e = pgm_write_copy_ex (sess->transport, string, strlen(string) + 1);
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

/* remove from hash table */
	g_hash_table_remove (g_sessions, name);

	if (sess->transport_is_fake)
	{
		fake_pgm_transport_destroy (sess->transport, TRUE);
	}
	else
	{
		pgm_transport_destroy (sess->transport, TRUE);
	}
	sess->transport = NULL;
	g_free (sess->name);
	sess->name = NULL;
	g_free (sess);

	puts ("READY");
}

void
net_send_data (
	char*		name,
	guint8		pgm_type,		/* PGM_ODATA or PGM_RDATA */
	guint32		data_sqn,
	guint32		txw_trail,
	char*		string
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	pgm_transport_t* transport = sess->transport;

/* payload is string including terminating null. */
	int count = strlen(string) + 1;

/* send */
        int retval = 0;
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + count;

	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_data *data = (struct pgm_data*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = transport->tsi.sport;
	header->pgm_dport       = transport->dport;
	header->pgm_type        = pgm_type;
	header->pgm_options     = 0;
	header->pgm_tsdu_length = g_htons (count);

/* O/RDATA */
	data->data_sqn		= g_htonl (data_sqn);
	data->data_trail	= g_htonl (txw_trail);

	memcpy (data + 1, string, count);

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_checksum((char*)header, tpdu_length, 0);

	g_static_mutex_lock (&transport->send_mutex);
        retval = sendto (transport->send_sock,
                                header,
                                tpdu_length,
                                MSG_CONFIRM,            /* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				pgm_sockaddr_len(&transport->send_smr.smr_multiaddr));
	g_static_mutex_unlock (&transport->send_mutex);

	puts ("READY");
}

void
net_send_spm (
	char*		name,
	guint32		spm_sqn,
	guint32		txw_trail,
	guint32		txw_lead
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	pgm_transport_t* transport = sess->transport;

/* send */
        int retval = 0;
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_spm);

	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_spm *spm = (struct pgm_spm*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = transport->tsi.sport;
	header->pgm_dport       = transport->dport;
	header->pgm_type        = PGM_SPM;
	header->pgm_options	= 0;
	header->pgm_tsdu_length	= 0;

/* SPM */
	spm->spm_sqn		= g_htonl (spm_sqn);
	spm->spm_trail		= g_htonl (txw_trail);
	spm->spm_lead		= g_htonl (txw_lead);
	pgm_sockaddr_to_nla ((struct sockaddr*)&transport->send_smr.smr_interface, (char*)&spm->spm_nla_afi);

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_checksum((char*)header, tpdu_length, 0);

	g_static_mutex_lock (&transport->send_with_router_alert_mutex);
        retval = sendto (transport->send_sock,
                                header,
                                tpdu_length,
                                MSG_CONFIRM,            /* not expecting a reply */
				(struct sockaddr*)&transport->send_smr.smr_multiaddr,
				pgm_sockaddr_len(&transport->send_smr.smr_multiaddr));
	g_static_mutex_unlock (&transport->send_with_router_alert_mutex);

	puts ("READY");
}

/* Send a NAK on a valid transport.  A fake transport would need to specify the senders NLA,
 * we use the peer list to bypass extracting it from the monitor output.
 */

void
net_send_nak (
	char*		name,
	pgm_tsi_t*	tsi,
	pgm_sqn_list_t*	sqn_list	/* list of sequence numbers */
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

/* check that the peer exists */
	pgm_transport_t* transport = sess->transport;
	pgm_peer_t* peer = g_hash_table_lookup (transport->peers, tsi);
	if (peer == NULL) {
		printf ("FAILED: peer \"%s\" not found\n", pgm_print_tsi(tsi));
		return;
	}

/* send */
        int retval = 0;
        int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);

	if (sqn_list->len > 1) {
		tpdu_length += sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length) +
				sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list) +
				( (sqn_list->len-1) * sizeof(guint32) );
	}

	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
        struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
        memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));

	guint16 peer_sport = peer->tsi.sport;
	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->nla, sizeof(struct sockaddr_storage));

/* dport & sport swap over for a nak */
        header->pgm_sport       = transport->dport;
        header->pgm_dport       = peer_sport;
        header->pgm_type        = PGM_NAK;
        header->pgm_options     = (sqn_list->len > 1) ? PGM_OPT_PRESENT : 0;
        header->pgm_tsdu_length = 0;

/* NAK */
        nak->nak_sqn            = g_htonl (sqn_list->sqn[0]);

/* source nla */
        pgm_sockaddr_to_nla ((struct sockaddr*)&peer_nla, (char*)&nak->nak_src_nla_afi);

/* group nla */
        pgm_sockaddr_to_nla ((struct sockaddr*)&transport->recv_smr[0].smr_multiaddr, (char*)&nak->nak_grp_nla_afi);

/* OPT_NAK_LIST */
	if (sqn_list->len > 1)
	{
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(nak + 1);
		opt_header->opt_type    = PGM_OPT_LENGTH;
		opt_header->opt_length  = sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length);
		struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);
		opt_length->opt_total_length = g_htons (sizeof(struct pgm_opt_header) +
							sizeof(struct pgm_opt_length) +
							sizeof(struct pgm_opt_header) +
							sizeof(struct pgm_opt_nak_list) +
							( (sqn_list->len-1) * sizeof(guint32) ));
		opt_header = (struct pgm_opt_header*)(opt_length + 1);
		opt_header->opt_type    = PGM_OPT_NAK_LIST | PGM_OPT_END;
		opt_header->opt_length  = sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
						+ ( (sqn_list->len-1) * sizeof(guint32) );
		struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
		opt_nak_list->opt_reserved = 0;
		for (int i = 1; i < sqn_list->len; i++) {
			opt_nak_list->opt_sqn[i-1] = g_htonl (sqn_list->sqn[i]);
		}
	}

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_checksum((char*)header, tpdu_length, 0);

	g_static_mutex_lock (&transport->send_with_router_alert_mutex);
        retval = sendto (transport->send_with_router_alert_sock,
                                header,
                                tpdu_length,
                                MSG_CONFIRM,            /* not expecting a reply */
                                (struct sockaddr*)&peer_nla,
                                pgm_sockaddr_len(&peer_nla));
	g_static_mutex_unlock (&transport->send_with_router_alert_mutex);

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

/* quit */
                if (strcmp(str, "quit") == 0)
		{
                        g_main_loop_quit(g_loop);
			goto out;
		}

		regex_t preg;
		regmatch_t pmatch[10];
		char *re;

/* endpoint simulator specific: */

/* send odata or rdata */
		re = "^net[[:space:]]+send[[:space:]]+([or])data[[:space:]]+"
			"([[:alnum:]]+)[[:space:]]+"	/* transport */
			"([0-9]+)[[:space:]]+"		/* sequence number */
			"([0-9]+)[[:space:]]+"		/* txw_trail */
			"([[:alnum:]]+)$";		/* payload */
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			guint8 pgm_type = *(str + pmatch[1].rm_so) == 'o' ? PGM_ODATA : PGM_RDATA;

			char *name = g_memdup (str + pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so + 1 );
			name[ pmatch[2].rm_eo - pmatch[2].rm_so ] = 0;

			char* p = str + pmatch[3].rm_so;
			guint32 data_sqn = strtoul (p, &p, 10);

			p = str + pmatch[4].rm_so;
			guint txw_trail = strtoul (p, &p, 10);

			char *string = g_memdup (str + pmatch[5].rm_so, pmatch[5].rm_eo - pmatch[5].rm_so + 1 );
			string[ pmatch[5].rm_eo - pmatch[5].rm_so ] = 0;

			net_send_data (name, pgm_type, data_sqn, txw_trail, string);

			g_free (name);
			g_free (string);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* send spm */
		re = "^net[[:space:]]+send[[:space:]]+spm[[:space:]]+"
			"([[:alnum:]]+)[[:space:]]+"	/* transport */
			"([0-9]+)[[:space:]]+"		/* spm sequence number */
			"([0-9]+)[[:space:]]+"		/* txw_trail */
			"([0-9]+)$";			/* txw_lead */
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char* p = str + pmatch[2].rm_so;
			guint32 spm_sqn = strtoul (p, &p, 10);

			p = str + pmatch[3].rm_so;
			guint txw_trail = strtoul (p, &p, 10);

			p = str + pmatch[4].rm_so;
			guint txw_lead = strtoul (p, &p, 10);

			net_send_spm (name, spm_sqn, txw_trail, txw_lead);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* send nak */
		re = "^net +send +nak +([0-9a-z]+) +([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+) +([0-9,]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			pgm_tsi_t tsi;
			char *p = str + pmatch[2].rm_so;
			tsi.gsi.identifier[0] = strtol (p, &p, 10);
			++p;
			tsi.gsi.identifier[1] = strtol (p, &p, 10);
			++p;
			tsi.gsi.identifier[2] = strtol (p, &p, 10);
			++p;
			tsi.gsi.identifier[3] = strtol (p, &p, 10);
			++p;
			tsi.gsi.identifier[4] = strtol (p, &p, 10);
			++p;
			tsi.gsi.identifier[5] = strtol (p, &p, 10);
			++p;
			tsi.sport = g_htons ( strtol (p, NULL, 10) );

/* parse list of sequence numbers */
			pgm_sqn_list_t sqn_list;
			sqn_list.len = 0;
			{
				char* saveptr;
				for (p = str + pmatch[3].rm_so; ; p = NULL) {
					char* token = strtok_r (p, ",", &saveptr);
					if (!token) break;
					sqn_list.sqn[sqn_list.len++] = strtoul (token, NULL, 10);
				}
			}

			net_send_nak (name, &tsi, &sqn_list);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/** same as test application: **/

/* create transport */
		re = "^create[[:space:]]+(fake[[:space:]]+)?([[:alnum:]]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so + 1 );
			name[ pmatch[2].rm_eo - pmatch[2].rm_so ] = 0;

			session_create (name, (pmatch[1].rm_eo > pmatch[1].rm_so));

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

		re = "^send[[:space:]]+([[:alnum:]]+)[[:space:]]+([[:alnum:]]+)[[:space:]]+x[[:space:]]([0-9]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
			name[ pmatch[1].rm_eo - pmatch[1].rm_so ] = 0;

			char* p = str + pmatch[3].rm_so;
			int factor = strtol (p, &p, 10);
			int src_len = pmatch[2].rm_eo - pmatch[2].rm_so;
			char *string = g_malloc ( (factor * src_len) + 1 );
			for (int i = 0; i < factor; i++)
			{
				memcpy (string + (i * src_len), str + pmatch[2].rm_so, src_len);
			}
			string[ factor * src_len ] = 0;

			session_send (name, string);

			g_free (name);
			g_free (string);
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
