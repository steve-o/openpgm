/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM conformance endpoint simulator.
 *
 * Copyright (c) 2006-2008 Miru Limited.
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
#include <pgm/checksum.h>
#include <pgm/reed_solomon.h>
#include <pgm/txwi.h>
#include <pgm/async.h>


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
	GIOChannel*	recv_channel;
	pgm_async_t*	async;
};

/* globals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN	"sim"

static int g_port = 7500;
static const char* g_network = ";239.192.0.1";

static int g_max_tpdu = 1500;
static int g_sqns = 100 * 1000;

static GHashTable* g_sessions = NULL;
static GMainLoop* g_loop = NULL;
static GIOChannel* g_stdin_channel = NULL;


static void on_signal (int);
static gboolean on_startup (gpointer);
static gboolean on_mark (gpointer);

static void destroy_session (gpointer, gpointer, gpointer);

static int on_data (gpointer, guint, gpointer);
static gboolean on_stdin_data (GIOChannel*, GIOCondition, gpointer);

void generic_net_send_nak (guint8, char*, pgm_tsi_t*, pgm_sqn_list_t*);
	

G_GNUC_NORETURN static void
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
                G_GNUC_UNUSED gpointer        user_data
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
	G_GNUC_UNUSED int	signum
	)
{
	g_message ("on_signal");

	g_main_loop_quit(g_loop);
}

static gboolean
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

static int
fake_pgm_transport_create (
	pgm_transport_t**		transport_,
	pgm_gsi_t*			gsi,
	guint16				sport,
	guint16				dport,
	struct group_source_req*	recv_gsr,	/* receive port, multicast group & interface address */
	gsize				recv_len,
	struct group_source_req*	send_gsr	/* send ... */
	)
{
	guint16 udp_encap_port = ((struct sockaddr_in*)&send_gsr->gsr_group)->sin_port;

	g_return_val_if_fail (transport_ != NULL, -EINVAL);
	g_return_val_if_fail (recv_gsr != NULL, -EINVAL);
	g_return_val_if_fail (recv_len > 0, -EINVAL);
	g_return_val_if_fail (recv_len <= IP_MAX_MEMBERSHIPS, -EINVAL);
	g_return_val_if_fail (send_gsr != NULL, -EINVAL);
	for (gsize i = 0; i < recv_len; i++)
	{
		g_return_val_if_fail (pgm_sockaddr_family(&recv_gsr[i].gsr_group) == pgm_sockaddr_family(&recv_gsr[0].gsr_group), -EINVAL);
		g_return_val_if_fail (pgm_sockaddr_family(&recv_gsr[i].gsr_group) == pgm_sockaddr_family(&recv_gsr[i].gsr_source), -EINVAL);
	}
	g_return_val_if_fail (pgm_sockaddr_family(&send_gsr->gsr_group) == pgm_sockaddr_family(&send_gsr->gsr_source), -EINVAL);

	int retval = 0;
	pgm_transport_t* transport;

/* create transport object */
	transport = g_malloc0 (sizeof(pgm_transport_t));

/* transport defaults */
	transport->can_send_data = TRUE;
	transport->can_send_nak  = FALSE;
	transport->can_recv = TRUE;

	memcpy (&transport->tsi.gsi, gsi, 6);
	transport->dport = g_htons (dport);
	if (sport) {
		transport->tsi.sport = sport;
	} else {
		do {
			transport->tsi.sport = g_htons (g_random_int_range (0, UINT16_MAX));
		} while (transport->tsi.sport == transport->dport);
	}

/* network data ports */
	transport->udp_encap_port = udp_encap_port;

/* copy network parameters */
	memcpy (&transport->send_gsr, send_gsr, sizeof(struct group_source_req));
	for (gsize i = 0; i < recv_len; i++)
	{
		memcpy (&transport->recv_gsr[i], &recv_gsr[i], sizeof(struct group_source_req));
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

	if ((transport->recv_sock = socket(pgm_sockaddr_family(&recv_gsr[0].gsr_source),
                                                socket_type,
                                                protocol)) < 0)
        {
                retval = errno;
                if (retval == EPERM && 0 != getuid()) {
                        g_critical ("PGM protocol requires this program to run as superuser.");
                }
                goto err_destroy;
        }

        if ((transport->send_sock = socket(pgm_sockaddr_family(&send_gsr->gsr_source),
                                                socket_type,
                                                protocol)) < 0)
        {
                retval = errno;
                goto err_destroy;
        }

        if ((transport->send_with_router_alert_sock = socket(pgm_sockaddr_family(&send_gsr->gsr_source),
                                                socket_type,
                                                protocol)) < 0)
        {
                retval = errno;
                goto err_destroy;
        }

	*transport_ = transport;
	return retval;

err_destroy:
	if (transport->recv_sock) {
                close(transport->recv_sock);
                transport->recv_sock = 0;
        }
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

static gboolean
on_io_data (
        GIOChannel* source,
        G_GNUC_UNUSED GIOCondition condition,
        gpointer data
        )
{
	pgm_transport_t* transport = data;

        int fd = g_io_channel_unix_get_fd(source);
	char buffer[transport->max_tpdu];
	struct sockaddr_storage src_addr;
	socklen_t src_addr_len = sizeof(src_addr);
        int len = recvfrom(fd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr*)&src_addr, &src_addr_len);

        printf ("%i bytes received from %s.\n", len, inet_ntoa(((struct sockaddr_in*)&src_addr)->sin_addr));

        monitor_packet (buffer, len);
        fflush (stdout);

/* parse packet to maintain peer database */
	struct sockaddr_storage dst_addr;
        socklen_t dst_addr_len = sizeof(dst_addr);
        struct pgm_header *pgm_header;
        gpointer packet;
        gsize packet_len;
	int e;

	if (transport->udp_encap_port) {
		if ((e = pgm_parse_udp_encap(buffer, len, (struct sockaddr*)&dst_addr, &dst_addr_len, &pgm_header, &packet, &packet_len)) < 0)
                {
                        goto out;
                }
        } else {
                if ((e = pgm_parse_raw(buffer, len, (struct sockaddr*)&dst_addr, &dst_addr_len, &pgm_header, &packet, &packet_len)) < 0)
                {
                        goto out;
                }
        }

/* calculate senders TSI */
        pgm_tsi_t tsi;
        memcpy (&tsi.gsi, pgm_header->pgm_gsi, sizeof(pgm_gsi_t));
        tsi.sport = pgm_header->pgm_sport;

	if (pgm_is_upstream (pgm_header->pgm_type) || pgm_is_peer (pgm_header->pgm_type))
	{
		goto out;	/* ignore */
	}

/* downstream = source to receivers */
	if (!pgm_is_downstream (pgm_header->pgm_type))
	{
		goto out;
	}

/* pgm packet DPORT contains our transport DPORT */
        if (pgm_header->pgm_dport != transport->dport) {
                goto out;
        }

/* search for TSI peer context or create a new one */
        pgm_peer_t* sender = g_hash_table_lookup (transport->peers_hashtable, &tsi);
        if (sender == NULL)
        {
		printf ("new peer, tsi %s, local nla %s\n", pgm_print_tsi (&tsi), inet_ntoa(((struct sockaddr_in*)&src_addr)->sin_addr));

		pgm_peer_t* peer = g_malloc0 (sizeof(pgm_peer_t));
		peer->transport = transport;
		memcpy (&peer->tsi, &tsi, sizeof(pgm_tsi_t));
		((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr = INADDR_ANY;
		memcpy (&peer->local_nla, &src_addr, src_addr_len);

		g_hash_table_insert (transport->peers_hashtable, &peer->tsi, peer);
		sender = peer;
        }

/* handle SPMs for advertised NLA */
	if (pgm_header->pgm_type == PGM_SPM)
	{
		char *pgm_data = (char*)(pgm_header + 1);
		struct pgm_spm* spm = (struct pgm_spm*)pgm_data;
		guint32 spm_sqn = g_ntohl (spm->spm_sqn);

		if ( pgm_uint32_gte (spm_sqn, sender->spm_sqn) 
			|| ( ((struct sockaddr*)&sender->nla)->sa_family == 0 ) )
		{
			pgm_nla_to_sockaddr (&spm->spm_nla_afi, (struct sockaddr*)&sender->nla);
			sender->spm_sqn = spm_sqn;
		}
	}
	
out:
        return TRUE;
}

static gboolean
on_io_error (
        GIOChannel* source,
        G_GNUC_UNUSED GIOCondition condition,
        G_GNUC_UNUSED gpointer data
        )
{
        puts ("on_error.");

        GError *err;
        g_io_channel_shutdown (source, FALSE, &err);

/* remove event */
        return FALSE;
}

static int
fake_pgm_transport_bind (
	pgm_transport_t*	transport
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);

	int retval = 0;

/* create peer list */
	transport->peers_hashtable = g_hash_table_new (pgm_tsi_hash, pgm_tsi_equal);

/* bind udp unicast sockets to interfaces, note multicast on a bound interface is
 * fruity on some platforms so callee should specify any interface.
 *
 * after binding default interfaces (0.0.0.0) are resolved
 */
	retval = bind (transport->recv_sock,
                        (struct sockaddr*)&transport->recv_gsr[0].gsr_source,
                        pgm_sockaddr_len(&transport->recv_gsr[0].gsr_source));
        if (retval < 0) {
                retval = errno;
                goto out;
        }

/* resolve bound address if 0.0.0.0 */
        if (((struct sockaddr_in*)&transport->recv_gsr[0].gsr_source)->sin_addr.s_addr == INADDR_ANY)
        {
                char hostname[NI_MAXHOST + 1];
                gethostname (hostname, sizeof(hostname));
                struct hostent *he = gethostbyname (hostname);
                if (he == NULL) {
                        retval = errno;
                        puts ("gethostbyname failed on local hostname");
                        goto out;
                }

                ((struct sockaddr_in*)(&transport->recv_gsr[0].gsr_source))->sin_addr.s_addr = ((struct in_addr*)(he->h_addr_list[0]))->s_addr;
        }

	retval = bind (transport->send_sock,
                        (struct sockaddr*)&transport->send_gsr.gsr_source,
                        pgm_sockaddr_len(&transport->send_gsr.gsr_source));
        if (retval < 0) {
                retval = errno;
		goto out;
        }

/* resolve bound address if 0.0.0.0 */
        if (((struct sockaddr_in*)&transport->send_gsr.gsr_source)->sin_addr.s_addr == INADDR_ANY)
        {
                char hostname[NI_MAXHOST + 1];
                gethostname (hostname, sizeof(hostname));
                struct hostent *he = gethostbyname (hostname);
                if (he == NULL) {
                        retval = errno;
                        puts ("gethostbyname failed on local hostname");
                        goto out;
                }

                ((struct sockaddr_in*)&transport->send_gsr.gsr_source)->sin_addr.s_addr = ((struct in_addr*)(he->h_addr_list[0]))->s_addr;
        }

	retval = bind (transport->send_with_router_alert_sock,
                        (struct sockaddr*)&transport->send_gsr.gsr_source,
                        pgm_sockaddr_len(&transport->send_gsr.gsr_source));
        if (retval < 0) {
                retval = errno;
		goto out;
        }

/* receiving groups (multiple) */
	struct group_source_req* p = transport->recv_gsr;
        int i = 1;
        do {
                retval = pgm_sockaddr_add_membership (transport->recv_sock, p);
                if (retval < 0) {
                        retval = errno;
			goto out;
                }
	} while ((i++) < IP_MAX_MEMBERSHIPS && pgm_sockaddr_family(&(++p)->gsr_group) != 0);

/* send group (singular) */
        retval = pgm_sockaddr_multicast_if (transport->send_sock, (struct sockaddr*)&transport->send_gsr.gsr_source, 0);
        if (retval < 0) {
                retval = errno;
                goto out;
        }

	retval = pgm_sockaddr_multicast_if (transport->send_with_router_alert_sock, (struct sockaddr*)&transport->send_gsr.gsr_source, 0);
        if (retval < 0) {
                retval = errno;
		goto out;
        }

/* multicast loopback */
	retval = pgm_sockaddr_multicast_loop (transport->recv_sock, pgm_sockaddr_family(&transport->recv_gsr[0].gsr_source), FALSE);
        if (retval < 0) {
                retval = errno;
                goto out;
        }
        retval = pgm_sockaddr_multicast_loop (transport->send_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_source), FALSE);
        if (retval < 0) {
                retval = errno;
                goto out;
        }
        retval = pgm_sockaddr_multicast_loop (transport->send_with_router_alert_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_source), FALSE);
        if (retval < 0) {
                retval = errno;
                goto out;
        }

/* multicast ttl: many crappy network devices go CPU ape with TTL=1, 16 is a popular alternative */
	retval = pgm_sockaddr_multicast_hops (transport->recv_sock, pgm_sockaddr_family(&transport->recv_gsr[0].gsr_source), transport->hops);
        if (retval < 0) {
                retval = errno;
                goto out;
        }
        retval = pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_source), transport->hops);
        if (retval < 0) {
                retval = errno;
                goto out;
        }
        retval = pgm_sockaddr_multicast_hops (transport->send_with_router_alert_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_source), transport->hops);
        if (retval < 0) {
                retval = errno;
                goto out;
        }

/* set low packet latency preference for network elements */
        int tos = IPTOS_LOWDELAY;
        retval = pgm_sockaddr_tos (transport->send_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_source), tos);
        if (retval < 0) {
                retval = errno;
                goto out;
        }
        retval = pgm_sockaddr_tos (transport->send_with_router_alert_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_source), tos);
        if (retval < 0) {
                retval = errno;
                goto out;
        }

/* parity buffer for odata/rdata transmission */
	if (transport->use_proactive_parity || transport->use_ondemand_parity) {
		pgm_rs_create (&transport->rs, transport->rs_n, transport->rs_k);
	}

/* cleanup */
	transport->is_bound = TRUE;

out:
	return retval;
}

static int
fake_pgm_transport_destroy (
	pgm_transport_t*	transport,
	G_GNUC_UNUSED gboolean		flush
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);

	if (transport->recv_sock) {
                puts ("closing receive socket.");
                close(transport->recv_sock);
                transport->recv_sock = 0;
        }
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
	if (transport->rs) {
		puts ("destroying Reed-Solomon engine.");
		pgm_rs_destroy (transport->rs);
		transport->rs = NULL;
	}

	g_free (transport);
	return 0;
}

static void
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
	struct group_source_req recv_gsr, send_gsr;
	gsize recv_len = 1;
	e = pgm_if_parse_transport (g_network, AF_INET, &recv_gsr, &recv_len, &send_gsr);
	g_assert (e == 0);
	g_assert (recv_len == 1);

	if (is_fake) {
		sess->transport_is_fake = TRUE;
		e = fake_pgm_transport_create (&sess->transport, &sess->gsi, 0, g_port, &recv_gsr, recv_len, &send_gsr);
	} else {
		e = pgm_transport_create (&sess->transport, &sess->gsi, 0, g_port, &recv_gsr, recv_len, &send_gsr);
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

static void
session_set_fec (
	char*		name,
	guint		default_n,
	guint		default_k
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	pgm_transport_set_fec (sess->transport, FALSE /* pro-active */, TRUE /* on-demand */, TRUE /* varpkt-len */, default_n, default_k);
	puts ("READY");
}

static void
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
	pgm_transport_set_nak_bo_ivl (sess->transport, pgm_msecs(50));
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

	if (sess->transport_is_fake)
	{
/* add receive socket(s) to event manager */
		sess->recv_channel = g_io_channel_unix_new (sess->transport->recv_sock);

		GSource *source;
		source = g_io_create_watch (sess->recv_channel, G_IO_IN);
		g_source_set_callback (source, (GSourceFunc)on_io_data, sess->transport, NULL);
		g_source_attach (source, NULL);
		g_source_unref (source);
	}
	else
	{
		pgm_async_create (&sess->async, sess->transport, 0);
		pgm_async_add_watch (sess->async, on_data, NULL);
	}

	puts ("READY");
}

static inline gssize
pgm_sendto (pgm_transport_t* transport, gboolean ra, const void* buf, gsize len, int flags, const struct sockaddr* to, socklen_t tolen)
{
        GStaticMutex* mutex = ra ? &transport->send_with_router_alert_mutex : &transport->send_mutex;
        int sock = ra ? transport->send_with_router_alert_sock : transport->send_sock;

        g_static_mutex_lock (mutex);
        ssize_t sent = sendto (sock, buf, len, flags, to, tolen);
        g_static_mutex_unlock (mutex);

        return sent > 0 ? (gssize)len : (gssize)sent;
}

static int
pgm_reset_heartbeat_spm (pgm_transport_t* transport)
{
        int retval = 0;

        g_static_mutex_lock (&transport->mutex);

/* re-set spm timer */
        transport->spm_heartbeat_state = 1;
        transport->next_heartbeat_spm = pgm_time_update_now() + transport->spm_heartbeat_interval[transport->spm_heartbeat_state++];

/* prod timer thread if sleeping */
        if (pgm_time_after( transport->next_poll, transport->next_heartbeat_spm ))
        {
                transport->next_poll = transport->next_heartbeat_spm;
		if (!pgm_notify_send (&transport->timer_notify)) {
                        g_critical ("send to timer notify channel failed :(");
                        retval = -EINVAL;
                }
        }

        g_static_mutex_unlock (&transport->mutex);

        return retval;
}

static inline gssize
brokn_send_apdu_unlocked (
        pgm_transport_t*        transport,
        const gchar*            buf,
        gsize                   count,  
        G_GNUC_UNUSED int                     flags           /* MSG_DONTWAIT = non-blocking */)
{
        int retval = 0;
        guint32 opt_sqn = pgm_txw_next_lead(transport->txw);
        guint packets = 0;
        guint bytes_sent = 0;
        guint data_bytes_sent = 0;

        g_static_rw_lock_writer_lock (&transport->txw_lock);

        do {
/* retrieve packet storage from transmit window */
                int header_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + 
                                sizeof(struct pgm_opt_length) +         /* includes header */
                                sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_fragment);
                int tsdu_length = MIN(transport->max_tpdu - transport->iphdr_len - header_length, count - data_bytes_sent);
                int tpdu_length = header_length + tsdu_length;

                char *pkt = pgm_txw_alloc(transport->txw);
                struct pgm_header *header = (struct pgm_header*)pkt;
                memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
                header->pgm_sport       = transport->tsi.sport;
                header->pgm_dport       = transport->dport;
                header->pgm_type        = PGM_ODATA;
                header->pgm_options     = PGM_OPT_PRESENT;
                header->pgm_tsdu_length = g_htons (tsdu_length);

/* ODATA */
                struct pgm_data *odata = (struct pgm_data*)(header + 1);
                odata->data_sqn         = g_htonl (pgm_txw_next_lead(transport->txw));
                odata->data_trail       = g_htonl (pgm_txw_trail(transport->txw));

/* OPT_LENGTH */
                struct pgm_opt_length* opt_len = (struct pgm_opt_length*)(odata + 1);
                opt_len->opt_type       = PGM_OPT_LENGTH;
                opt_len->opt_length     = sizeof(struct pgm_opt_length);
                opt_len->opt_total_length       = g_htons (     sizeof(struct pgm_opt_length) +
                                                                sizeof(struct pgm_opt_header) +
                                                                sizeof(struct pgm_opt_fragment) );
/* OPT_FRAGMENT */
                struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
                opt_header->opt_type    = PGM_OPT_FRAGMENT | PGM_OPT_END;
                opt_header->opt_length  = sizeof(struct pgm_opt_header) +
                                                sizeof(struct pgm_opt_fragment);
                struct pgm_opt_fragment* opt_fragment = (struct pgm_opt_fragment*)(opt_header + 1);
                opt_fragment->opt_reserved      = 0;
                opt_fragment->opt_sqn           = g_htonl (opt_sqn);
                opt_fragment->opt_frag_off      = g_htonl (data_bytes_sent);
                opt_fragment->opt_frag_len      = g_htonl (count);

/* TODO: the assembly checksum & copy routine is faster than memcpy & pgm_cksum on >= opteron hardware */
                header->pgm_checksum    = 0;

                int pgm_header_len      = (char*)(opt_fragment + 1) - (char*)header;
                guint32 unfolded_header = pgm_csum_partial ((const void*)header, pgm_header_len, 0);
                guint32 unfolded_odata  = pgm_csum_partial_copy ((const void*)(buf + data_bytes_sent), (void*)(opt_fragment + 1), tsdu_length, 0);
                header->pgm_checksum    = pgm_csum_fold (pgm_csum_block_add (unfolded_header, unfolded_odata, pgm_header_len));

/* add to transmit window */
                pgm_txw_push (transport->txw, pkt, tpdu_length);

/* do not send send packet */
		if (packets != 1)
                retval = pgm_sendto (transport,
                                        FALSE,
                                        header,
                                        tpdu_length,
                                        MSG_CONFIRM,            /* not expecting a reply */
                                        (struct sockaddr*)&transport->send_gsr.gsr_group,
                                        pgm_sockaddr_len(&transport->send_gsr.gsr_group));

/* save unfolded odata for retransmissions */
                *(guint32*)(void*)&header->pgm_sport   = unfolded_odata;

                packets++;
                bytes_sent += tpdu_length + transport->iphdr_len;

                data_bytes_sent += tsdu_length;

        } while (data_bytes_sent < count);

        if (data_bytes_sent > 0) {
                retval = data_bytes_sent;
        }

/* release txw lock here in order to allow spms to lock mutex */
        g_static_rw_lock_writer_unlock (&transport->txw_lock);

        pgm_reset_heartbeat_spm (transport);

        return retval;
}

static gssize
brokn_send (
        pgm_transport_t*        transport,      
        const gchar*            data,
        gsize                   len,
        int                     flags           /* MSG_DONTWAIT = non-blocking */
        )
{
        if ( len <= ( transport->max_tpdu - (  sizeof(struct pgm_header) +
                                               sizeof(struct pgm_data) ) ) )
        {
		puts ("FAILED: cannot send brokn single TPDU length APDU");
		return -1;
        }

        return brokn_send_apdu_unlocked (transport, data, len, flags);
}

static void
session_send (
	char*		name,
	char*		string,
	gboolean	is_brokn		/* send broken apdu */
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

/* send message */
        gssize e;
	if (is_brokn) {
		e = brokn_send (sess->transport, string, strlen(string) + 1, 0);
	} else {
		e = pgm_transport_send (sess->transport, string, strlen(string) + 1, 0);
	}
        if (e < 0) {
		puts ("FAILED: pgm_transport_send()");
        }

	puts ("READY");
}

static void
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

/* close down receive side first to stop new data incoming */
        if (sess->recv_channel) { 
                puts ("closing receive channel.");

                GError *err = NULL;
                g_io_channel_shutdown (sess->recv_channel, TRUE, &err);

                if (err) {
                        g_warning ("i/o shutdown error %i %s", err->code, err->message);
                }

/* TODO: flush GLib main loop with context specific to the recv channel */

                sess->recv_channel = NULL;
        }

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

static void
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
        header->pgm_checksum    = pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	g_static_mutex_lock (&transport->send_mutex);
        retval = sendto (transport->send_sock,
                                header,
                                tpdu_length,
                                MSG_CONFIRM,            /* not expecting a reply */
				(struct sockaddr*)&transport->send_gsr.gsr_group,
				pgm_sockaddr_len(&transport->send_gsr.gsr_group));
	g_static_mutex_unlock (&transport->send_mutex);

	puts ("READY");
}

/* differs to net_send_data in that the string parameters contains every payload
 * for the transmission group.  this is required to calculate the correct parity
 * as the fake transport does not own a transmission window.
 *
 * all payloads must be the same length unless variable TSDU support is enabled.
 */
static void
net_send_parity (
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

/* split string into individual payloads */
	guint16 parity_length = 0;
	gchar** src;
	src = g_strsplit (string, " ", transport->rs_k);

/* payload is string including terminating null. */
	parity_length = strlen(*src) + 1;

/* check length of payload array */
	gboolean is_var_pktlen = FALSE;
	guint i;
	for (i = 0; src[i]; i++)
	{
		guint tsdu_length = strlen(src[i]) + 1;
		if (tsdu_length != parity_length) {
			is_var_pktlen = TRUE;

			if (tsdu_length > parity_length)
				parity_length = tsdu_length;
		}
	}

	if ( i != transport->rs_k ) {
		printf ("FAILED: payload array length %u, whilst rs_k is %u.\n", i, transport->rs_k);
		return;
	}

/* add padding and append TSDU lengths */
	if (is_var_pktlen)
	{
		for (i = 0; src[i]; i++)
		{
			guint tsdu_length = strlen(src[i]) + 1;
			gchar* new_string = g_new0 (gchar, parity_length + 2);
			strncpy (new_string, src[i], parity_length);
			*(guint16*)(new_string + parity_length) = tsdu_length;
			g_free (src[i]);
			src[i] = new_string;
		}
		parity_length += 2;
	}

/* calculate FEC block offset */
	guint32 tg_sqn_mask = 0xffffffff << transport->tg_sqn_shift;
	guint rs_h = data_sqn & ~tg_sqn_mask;

/* send */
        int retval = 0;
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + parity_length;

	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_data *data = (struct pgm_data*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = transport->tsi.sport;
	header->pgm_dport       = transport->dport;
	header->pgm_type        = pgm_type;
	header->pgm_options     = is_var_pktlen ? (PGM_OPT_PARITY | PGM_OPT_VAR_PKTLEN) : PGM_OPT_PARITY;
	header->pgm_tsdu_length = g_htons (parity_length);

/* O/RDATA */
	data->data_sqn		= g_htonl (data_sqn);
	data->data_trail	= g_htonl (txw_trail);

	memset (data + 1, 0, parity_length);
	pgm_rs_encode (transport->rs, (const void**)src, transport->rs_k + rs_h, data + 1, parity_length);

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	g_static_mutex_lock (&transport->send_mutex);
        retval = sendto (transport->send_sock,
                                header,
                                tpdu_length,
                                MSG_CONFIRM,            /* not expecting a reply */
				(struct sockaddr*)&transport->send_gsr.gsr_group,
				pgm_sockaddr_len(&transport->send_gsr.gsr_group));
	g_static_mutex_unlock (&transport->send_mutex);

	g_strfreev (src);
	src = NULL;

	puts ("READY");
}

static void
net_send_spm (
	char*		name,
	guint32		spm_sqn,
	guint32		txw_trail,
	guint32		txw_lead,
	gboolean	proactive_parity,
	gboolean	ondemand_parity,
	guint		k
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

	if (proactive_parity || ondemand_parity) {
		tpdu_length +=	sizeof(struct pgm_opt_length) +
				sizeof(struct pgm_opt_header) +
				sizeof(struct pgm_opt_parity_prm);
	}

	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_spm *spm = (struct pgm_spm*)(header + 1);
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = transport->tsi.sport;
	header->pgm_dport       = transport->dport;
	header->pgm_type        = PGM_SPM;
	header->pgm_options	= (proactive_parity || ondemand_parity) ? (PGM_OPT_PRESENT | PGM_OPT_NETWORK) : 0;
	header->pgm_tsdu_length	= 0;

/* SPM */
	spm->spm_sqn		= g_htonl (spm_sqn);
	spm->spm_trail		= g_htonl (txw_trail);
	spm->spm_lead		= g_htonl (txw_lead);
	pgm_sockaddr_to_nla ((struct sockaddr*)&transport->send_gsr.gsr_source, (char*)&spm->spm_nla_afi);

	if (proactive_parity || ondemand_parity) {
		struct pgm_opt_length* opt_len = (struct pgm_opt_length*)(spm + 1);
		opt_len->opt_type	= PGM_OPT_LENGTH;
		opt_len->opt_length	= sizeof(struct pgm_opt_length);
		opt_len->opt_total_length = g_htons (	sizeof(struct pgm_opt_length) +
							sizeof(struct pgm_opt_header) +
							sizeof(struct pgm_opt_parity_prm) );
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type	= PGM_OPT_PARITY_PRM | PGM_OPT_END;
		opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_parity_prm);
		struct pgm_opt_parity_prm* opt_parity_prm = (struct pgm_opt_parity_prm*)(opt_header + 1);
		opt_parity_prm->opt_reserved = (proactive_parity ? PGM_PARITY_PRM_PRO : 0) |
					       (ondemand_parity ? PGM_PARITY_PRM_OND : 0);
		opt_parity_prm->parity_prm_tgs = g_htonl (k);
	}

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	g_static_mutex_lock (&transport->send_with_router_alert_mutex);
        retval = sendto (transport->send_sock,
                                header,
                                tpdu_length,
                                MSG_CONFIRM,            /* not expecting a reply */
				(struct sockaddr*)&transport->send_gsr.gsr_group,
				pgm_sockaddr_len(&transport->send_gsr.gsr_group));
	g_static_mutex_unlock (&transport->send_with_router_alert_mutex);

	puts ("READY");
}

static void
net_send_spmr (
	char*		name,
	pgm_tsi_t*	tsi
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, name);
	if (sess == NULL) {
		puts ("FAILED: session not found");
		return;
	}

	pgm_transport_t* transport = sess->transport;

/* check that the peer exists */
	pgm_peer_t* peer = g_hash_table_lookup (transport->peers_hashtable, tsi);
	struct sockaddr_storage peer_nla;
	guint16 peer_sport;

	if (peer == NULL) {
/* ourself */
		if (pgm_tsi_equal (tsi, &transport->tsi))
		{
			peer_sport = transport->tsi.sport;
		}
		else
		{
			printf ("FAILED: peer \"%s\" not found\n", pgm_print_tsi(tsi));
			return;
		}
	}
	else
	{
		memcpy (&peer_nla, &peer->local_nla, sizeof(struct sockaddr_storage));
		peer_sport = peer->tsi.sport;
	}

/* send */
        int retval = 0;
	int tpdu_length = sizeof(struct pgm_header);
	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
	memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = transport->dport;
	header->pgm_dport       = peer_sport;
	header->pgm_type        = PGM_SPMR;
	header->pgm_options     = 0;
	header->pgm_tsdu_length = 0;
        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	g_static_mutex_lock (&transport->send_mutex);
/* TTL 1 */
	pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_source), 1);
        retval = sendto (transport->send_sock,
                                header,
                                tpdu_length,
                                MSG_CONFIRM,            /* not expecting a reply */
				(struct sockaddr*)&transport->send_gsr.gsr_group,
				pgm_sockaddr_len(&transport->send_gsr.gsr_group));
/* default TTL */
	pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_source), transport->hops);

	if (!pgm_tsi_equal (tsi, &transport->tsi))
	{
	        retval = sendto (transport->send_sock,
	                                header,
	                                tpdu_length,
	                                MSG_CONFIRM,            /* not expecting a reply */
					(struct sockaddr*)&peer_nla,
					pgm_sockaddr_len(&peer_nla));
	}

	g_static_mutex_unlock (&transport->send_mutex);

	puts ("READY");
}

/* Send a NAK on a valid transport.  A fake transport would need to specify the senders NLA,
 * we use the peer list to bypass extracting it from the monitor output.
 */

static void
net_send_ncf (
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
	pgm_peer_t* peer = g_hash_table_lookup (transport->peers_hashtable, tsi);
	if (peer == NULL) {
		printf ("FAILED: peer \"%s\" not found\n", pgm_print_tsi(tsi));
		return;
	}

/* check for valid nla */
	if (((struct sockaddr*)&peer->nla)->sa_family == 0 ) {
		puts ("FAILED: peer NLA unknown, cannot send NCF.");
		return;
	}

/* send */
        int retval = 0;
        int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);

	if (sqn_list->len > 1) {
		tpdu_length += sizeof(struct pgm_opt_length) +		/* includes header */
				sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list) +
				( (sqn_list->len-1) * sizeof(guint32) );
	}

	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
        struct pgm_nak *ncf = (struct pgm_nak*)(header + 1);
        memcpy (header->pgm_gsi, &transport->tsi.gsi, sizeof(pgm_gsi_t));

	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->nla, sizeof(struct sockaddr_storage));

/* dport & sport swap over for a nak */
        header->pgm_sport       = transport->tsi.sport;
        header->pgm_dport       = transport->dport;
        header->pgm_type        = PGM_NCF;
        header->pgm_options     = (sqn_list->len > 1) ? (PGM_OPT_PRESENT | PGM_OPT_NETWORK) : 0;
        header->pgm_tsdu_length = 0;

/* NCF */
        ncf->nak_sqn            = g_htonl (sqn_list->sqn[0]);

/* source nla */
        pgm_sockaddr_to_nla ((struct sockaddr*)&peer_nla, (char*)&ncf->nak_src_nla_afi);

/* group nla */
        pgm_sockaddr_to_nla ((struct sockaddr*)&transport->recv_gsr[0].gsr_group, (char*)&ncf->nak_grp_nla_afi);

/* OPT_NAK_LIST */
	if (sqn_list->len > 1)
	{
		struct pgm_opt_length* opt_len = (struct pgm_opt_length*)(ncf + 1);
		opt_len->opt_type    = PGM_OPT_LENGTH;
		opt_len->opt_length  = sizeof(struct pgm_opt_length);
		opt_len->opt_total_length = g_htons (	sizeof(struct pgm_opt_length) +
							sizeof(struct pgm_opt_header) +
							sizeof(struct pgm_opt_nak_list) +
							( (sqn_list->len-1) * sizeof(guint32) ) );
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type    = PGM_OPT_NAK_LIST | PGM_OPT_END;
		opt_header->opt_length  = sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
						+ ( (sqn_list->len-1) * sizeof(guint32) );
		struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
		opt_nak_list->opt_reserved = 0;
		for (guint i = 1; i < sqn_list->len; i++) {
			opt_nak_list->opt_sqn[i-1] = g_htonl (sqn_list->sqn[i]);
		}
	}

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	g_static_mutex_lock (&transport->send_with_router_alert_mutex);
        retval = sendto (transport->send_with_router_alert_sock,
                                header,
                                tpdu_length,
                                MSG_CONFIRM,            /* not expecting a reply */
				(struct sockaddr*)&transport->send_gsr.gsr_group,
				pgm_sockaddr_len(&transport->send_gsr.gsr_group));
	g_static_mutex_unlock (&transport->send_with_router_alert_mutex);

	puts ("READY");
}

static void
net_send_nak (
	char*		name,
	pgm_tsi_t*	tsi,
	pgm_sqn_list_t*	sqn_list,	/* list of sequence numbers */
	gboolean	is_parity	/* TRUE = parity, FALSE = selective */
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
	pgm_peer_t* peer = g_hash_table_lookup (transport->peers_hashtable, tsi);
	if (peer == NULL) {
		printf ("FAILED: peer \"%s\" not found\n", pgm_print_tsi(tsi));
		return;
	}

/* send */
        int retval = 0;
        int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);

	if (sqn_list->len > 1) {
		tpdu_length += sizeof(struct pgm_opt_length) +		/* includes header */
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
	if (is_parity) {
	        header->pgm_options     = (sqn_list->len > 1) ? (PGM_OPT_PRESENT | PGM_OPT_NETWORK | PGM_OPT_PARITY)
							      : PGM_OPT_PARITY;
	} else {
	        header->pgm_options     = (sqn_list->len > 1) ? (PGM_OPT_PRESENT | PGM_OPT_NETWORK) : 0;
	}
        header->pgm_tsdu_length = 0;

/* NAK */
        nak->nak_sqn            = g_htonl (sqn_list->sqn[0]);

/* source nla */
        pgm_sockaddr_to_nla ((struct sockaddr*)&peer_nla, (char*)&nak->nak_src_nla_afi);

/* group nla */
        pgm_sockaddr_to_nla ((struct sockaddr*)&transport->recv_gsr[0].gsr_group, (char*)&nak->nak_grp_nla_afi);

/* OPT_NAK_LIST */
	if (sqn_list->len > 1)
	{
		struct pgm_opt_length* opt_len = (struct pgm_opt_length*)(nak + 1);
		opt_len->opt_type    = PGM_OPT_LENGTH;
		opt_len->opt_length  = sizeof(struct pgm_opt_length);
		opt_len->opt_total_length = g_htons (	sizeof(struct pgm_opt_length) +
							sizeof(struct pgm_opt_header) +
							sizeof(struct pgm_opt_nak_list) +
							( (sqn_list->len-1) * sizeof(guint32) ) );
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
		opt_header->opt_type    = PGM_OPT_NAK_LIST | PGM_OPT_END;
		opt_header->opt_length  = sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
						+ ( (sqn_list->len-1) * sizeof(guint32) );
		struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
		opt_nak_list->opt_reserved = 0;
		for (guint i = 1; i < sqn_list->len; i++) {
			opt_nak_list->opt_sqn[i-1] = g_htonl (sqn_list->sqn[i]);
		}
	}

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

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

static int
on_data (
        gpointer        data,
        G_GNUC_UNUSED guint           len,
        G_GNUC_UNUSED gpointer        user_data
        )
{
        printf ("DATA: %s\n", (char*)data);
        fflush (stdout);

        return 0;
}

/* process input commands from stdin/fd 
 */

static gboolean
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
		const char *re;

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

/* send parity odata or rdata */
		re = "^net[[:space:]]+send[[:space:]]+parity[[:space:]]+([or])data[[:space:]]+"
			"([[:alnum:]]+)[[:space:]]+"	/* transport */
			"([0-9]+)[[:space:]]+"		/* sequence number */
			"([0-9]+)[[:space:]]+"		/* txw_trail */
			"([a-z0-9 ]+)$";		/* payloads */
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

/* ideally confirm number of payloads matches sess->transport::rs_k ... */
			char *string = g_memdup (str + pmatch[5].rm_so, pmatch[5].rm_eo - pmatch[5].rm_so + 1 );
			string[ pmatch[5].rm_eo - pmatch[5].rm_so ] = 0;

			net_send_parity (name, pgm_type, data_sqn, txw_trail, string);

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
			"([0-9]+)"			/* txw_lead */
			"([[:space:]]+pro-active)?"	/* pro-active parity */
			"([[:space:]]+on-demand)?"	/* on-demand parity */
			"([[:space:]]+[0-9]+)?$";	/* transmission group size */
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

			gboolean proactive_parity = pmatch[5].rm_eo > pmatch[5].rm_so;
			gboolean ondemand_parity = pmatch[6].rm_eo > pmatch[6].rm_so;

			p = str + pmatch[7].rm_so;
			guint k = (pmatch[7].rm_eo > pmatch[7].rm_so) ? strtoul (p, &p, 10) : 0;

			net_send_spm (name, spm_sqn, txw_trail, txw_lead, proactive_parity, ondemand_parity, k);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* send spmr */
		re = "^net[[:space:]]+send[[:space:]]+spmr[[:space:]]+"
			"([[:alnum:]]+)[[:space:]]+"		/* transport */
			"([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)$";	/* TSI */
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

			net_send_spmr (name, &tsi);

			g_free (name);
			regfree (&preg);
			goto out;
		}
		regfree (&preg);

/* send nak/ncf */
		re = "^net[[:space:]]+send[[:space:]](parity[[:space:]])?n(ak|cf)[[:space:]]+"
			"([[:alnum:]]+)[[:space:]]+"	/* transport */
			"([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)[[:space:]]+"	/* TSI */
			"([0-9,]+)$";			/* sequence number or list */
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[3].rm_so, pmatch[3].rm_eo - pmatch[3].rm_so + 1 );
			name[ pmatch[3].rm_eo - pmatch[3].rm_so ] = 0;

			pgm_tsi_t tsi;
			char *p = str + pmatch[4].rm_so;
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
				char* saveptr = NULL;
				for (p = str + pmatch[5].rm_so; ; p = NULL) {
					char* token = strtok_r (p, ",", &saveptr);
					if (!token) break;
					sqn_list.sqn[sqn_list.len++] = strtoul (token, NULL, 10);
				}
			}

			if ( *(str + pmatch[2].rm_so) == 'a' )
			{
				net_send_nak (name, &tsi, &sqn_list, (pmatch[1].rm_eo > pmatch[1].rm_so));
			}
			else
			{
				net_send_ncf (name, &tsi, &sqn_list);
			}

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

			session_send (name, string, FALSE);

			g_free (name);
			g_free (string);
			regfree (&preg);
			goto out;
                }
		regfree (&preg);

		re = "^send[[:space:]]+(brokn[[:space:]]+)?([[:alnum:]]+)[[:space:]]+([[:alnum:]]+)[[:space:]]+x[[:space:]]([0-9]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *name = g_memdup (str + pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so + 1 );
			name[ pmatch[2].rm_eo - pmatch[2].rm_so ] = 0;

			char* p = str + pmatch[4].rm_so;
			int factor = strtol (p, &p, 10);
			int src_len = pmatch[3].rm_eo - pmatch[3].rm_so;
			char *string = g_malloc ( (factor * src_len) + 1 );
			for (int i = 0; i < factor; i++)
			{
				memcpy (string + (i * src_len), str + pmatch[3].rm_so, src_len);
			}
			string[ factor * src_len ] = 0;

			session_send (name, string, (pmatch[1].rm_eo > pmatch[1].rm_so));

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
	G_GNUC_UNUSED gpointer data
	)
{
	g_message ("-- MARK --");
	return TRUE;
}

/* eof */
