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

#ifdef HAVE_CONFIG_H
#       include <config.h>
#endif

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#	include <sched.h>
#	include <unistd.h>
#	include <netdb.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <sys/time.h>
#	include <netinet/in.h>
#	include <netinet/ip.h>
#	include <arpa/inet.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
#include <regex.h>
#include <glib.h>
#include <impl/framework.h>
#include <impl/socket.h>
#include <impl/sqn_list.h>
#include <impl/packet_parse.h>
#include <pgm/pgm.h>
#include <pgm/backtrace.h>
#include <pgm/log.h>
#include <pgm/signal.h>
#include "dump-json.h"
#include "async.h"


/* typedefs */

struct idle_source {
	GSource			source;
	guint64			expiration;
};

struct sim_session {
	char*			name;
	pgm_sock_t*	 	sock;
	gboolean		is_transport_fake;
	GIOChannel*		recv_channel;
	pgm_async_t*		async;
};

/* globals */
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN	"sim"

#ifndef SOL_IP
#	define SOL_IP			IPPROTO_IP
#endif
#ifndef SOL_IPV6
#	define SOL_IPV6			IPPROTO_IPV6
#endif


static int		g_port = 7500;
static const char*	g_network = ";239.192.0.1";

static int		g_max_tpdu = 1500;
static int		g_sqns = 100 * 1000;

static GList*		g_sessions_list = NULL;
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
static void destroy_session (struct sim_session*);
static int on_data (gpointer, guint, gpointer);
static gboolean on_stdin_data (GIOChannel*, GIOCondition, gpointer);
void generic_net_send_nak (guint8, char*, pgm_tsi_t*, struct pgm_sqn_list_t*);
	

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
	pgm_error_t* pgm_err = NULL;

/* pre-initialise PGM messages module to add hook for GLib logging */
	pgm_messages_init();
	log_init ();
	g_message ("sim");

	if (!pgm_init (&pgm_err)) {
		g_error ("Unable to start PGM engine: %s", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
		pgm_error_free (pgm_err);
		pgm_messages_shutdown();
		return EXIT_FAILURE;
	}

/* parse program arguments */
#ifdef _WIN32
        const char* binary_name = strrchr (argv[0], '\\');
#else
        const char* binary_name = strrchr (argv[0], '/');
#endif
        if (NULL == binary_name)        binary_name = argv[0];
        else                            binary_name++;

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
		while (g_sessions_list) {
			destroy_session (g_sessions_list->data);
			g_sessions_list = g_list_delete_link (g_sessions_list, g_sessions_list);
		}
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
		struct sim_session* sess
                )
{
	printf ("destroying socket \"%s\"\n", sess->name);
	pgm_close (sess->sock, TRUE);
	sess->sock = NULL;
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
        DWORD           dwCtrlType
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
#ifdef G_OS_UNIX
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
bool
fake_pgm_socket (
	pgm_sock_t**restrict	sock,
	const sa_family_t	family,
	const int		pgm_sock_type,
	const int		protocol,
	G_GNUC_UNUSED pgm_error_t**restrict	error
	)
{
	pgm_sock_t* new_sock;

	g_return_val_if_fail (NULL != sock, FALSE);
	g_return_val_if_fail (AF_INET == family || AF_INET6 == family, FALSE);
        g_return_val_if_fail (SOCK_SEQPACKET == pgm_sock_type, FALSE);
        g_return_val_if_fail (IPPROTO_UDP == protocol || IPPROTO_PGM == protocol, FALSE);

	new_sock = pgm_new0 (pgm_sock_t, 1);
        new_sock->family        = family;
        new_sock->socket_type   = pgm_sock_type;
        new_sock->protocol      = protocol;
        new_sock->can_send_data = TRUE;
        new_sock->can_send_nak  = TRUE;
        new_sock->can_recv_data = TRUE;
        new_sock->dport         = DEFAULT_DATA_DESTINATION_PORT;
        new_sock->tsi.sport     = DEFAULT_DATA_SOURCE_PORT;
        new_sock->adv_mode      = 0;    /* advance with time */

/* PGMCC */
        new_sock->acker_nla.ss_family = family;

/* open sockets to implement PGM */
        int socket_type;
        if (IPPROTO_UDP == new_sock->protocol) {
                puts ("Opening UDP encapsulated sockets.");
                socket_type = SOCK_DGRAM;
                new_sock->udp_encap_ucast_port = DEFAULT_UDP_ENCAP_UCAST_PORT;
                new_sock->udp_encap_mcast_port = DEFAULT_UDP_ENCAP_MCAST_PORT;
        } else {
                puts ("Opening raw sockets.");
                socket_type = SOCK_RAW;
        }

        if ((new_sock->recv_sock = socket (new_sock->family,
                                           socket_type,
                                           new_sock->protocol)) == INVALID_SOCKET)
        {
                const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
                pgm_set_error (error,
                               PGM_ERROR_DOMAIN_SOCKET,
                               pgm_error_from_sock_errno (save_errno),
                               "Creating receive socket: %s",
                               pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
#ifndef _WIN32
                if (EPERM == save_errno) {
                        g_critical ("PGM protocol requires CAP_NET_RAW capability, e.g. sudo execcap 'cap_net_raw=ep'");
                }
#endif
                goto err_destroy;
        }

	if ((new_sock->send_sock = socket (new_sock->family,
                                           socket_type,
                                           new_sock->protocol)) == INVALID_SOCKET)
        {
                const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
                pgm_set_error (error,
                               PGM_ERROR_DOMAIN_SOCKET,
                               pgm_error_from_sock_errno (save_errno),
                               "Creating send socket: %s",
                               pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                goto err_destroy;
        }

        if ((new_sock->send_with_router_alert_sock = socket (new_sock->family,
                                                             socket_type,
                                                             new_sock->protocol)) == INVALID_SOCKET)
        {
                const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
                pgm_set_error (error,
                               PGM_ERROR_DOMAIN_SOCKET,
                               pgm_error_from_sock_errno (save_errno),
                               "Creating IP Router Alert (RFC 2113) send socket: %s",
                               pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                goto err_destroy;
        }

        *sock = new_sock;

	puts ("PGM socket successfully created.");
        return TRUE;

err_destroy:
        if (INVALID_SOCKET != new_sock->recv_sock) {
                if (SOCKET_ERROR == closesocket (new_sock->recv_sock)) {
                        const int save_errno = pgm_get_last_sock_error();
			char errbuf[1024];
                        g_warning ("Close on receive socket failed: %s",
                                  pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                }
                new_sock->recv_sock = INVALID_SOCKET;
        }
        if (INVALID_SOCKET != new_sock->send_sock) {
                if (SOCKET_ERROR == closesocket (new_sock->send_sock)) {
                        const int save_errno = pgm_get_last_sock_error();
			char errbuf[1024];
                        g_warning ("Close on send socket failed: %s",
                                  pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                }
                new_sock->send_sock = INVALID_SOCKET;
        }
        if (INVALID_SOCKET != new_sock->send_with_router_alert_sock) {
                if (SOCKET_ERROR == closesocket (new_sock->send_with_router_alert_sock)) {
                        const int save_errno = pgm_get_last_sock_error();
			char errbuf[1024];
                        g_warning ("Close on IP Router Alert (RFC 2113) send socket failed: %s",
                                  pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                }
                new_sock->send_with_router_alert_sock = INVALID_SOCKET;
        }
        pgm_free (new_sock);
        return FALSE;
}

static
gboolean
on_io_data (
        GIOChannel*	source,
        G_GNUC_UNUSED GIOCondition condition,
        gpointer	data
        )
{
	pgm_sock_t* sock = data;

	struct pgm_sk_buff_t* skb = pgm_alloc_skb (sock->max_tpdu);
        int fd = g_io_channel_unix_get_fd(source);
	struct sockaddr_storage src_addr;
	socklen_t src_addr_len = sizeof(src_addr);
        skb->len = recvfrom(fd, skb->head, sock->max_tpdu, MSG_DONTWAIT, (struct sockaddr*)&src_addr, &src_addr_len);

        printf ("%i bytes received from %s.\n", skb->len, inet_ntoa(((struct sockaddr_in*)&src_addr)->sin_addr));

        monitor_packet (skb->data, skb->len);
        fflush (stdout);

/* parse packet to maintain peer database */
	if (sock->udp_encap_ucast_port) {
		if (!pgm_parse_udp_encap (skb, NULL))
			goto out;
        } else {
		struct sockaddr_storage addr;
                if (!pgm_parse_raw (skb, (struct sockaddr*)&addr, NULL))
                        goto out;
        }

	if (PGM_IS_UPSTREAM (skb->pgm_header->pgm_type) ||
	    PGM_IS_PEER (skb->pgm_header->pgm_type))
		goto out;	/* ignore */

/* downstream = source to receivers */
	if (!PGM_IS_DOWNSTREAM (skb->pgm_header->pgm_type))
		goto out;

/* pgm packet DPORT contains our transport DPORT */
        if (skb->pgm_header->pgm_dport != sock->dport)
                goto out;

/* search for TSI peer context or create a new one */
        pgm_peer_t* sender = pgm_hashtable_lookup (sock->peers_hashtable, &skb->tsi);
        if (sender == NULL)
        {
		printf ("new peer, tsi %s, local nla %s\n",
			pgm_tsi_print (&skb->tsi),
			inet_ntoa(((struct sockaddr_in*)&src_addr)->sin_addr));

		pgm_peer_t* peer = g_new0 (pgm_peer_t, 1);
		memcpy (&peer->tsi, &skb->tsi, sizeof(pgm_tsi_t));
		((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr = INADDR_ANY;
		memcpy (&peer->local_nla, &src_addr, src_addr_len);

		pgm_hashtable_insert (sock->peers_hashtable, &peer->tsi, peer);
		sender = peer;
        }

/* handle SPMs for advertised NLA */
	if (skb->pgm_header->pgm_type == PGM_SPM)
	{
		char *pgm_data = (char*)(skb->pgm_header + 1);
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

static
bool
fake_pgm_bind3 (
        pgm_sock_t*                   restrict sock,
        const struct pgm_sockaddr_t*const restrict sockaddr,
        const socklen_t                        sockaddrlen,
        const struct pgm_interface_req_t*const send_req,                /* only use gr_interface and gr_group::sin6_scope */
        const socklen_t                        send_req_len,
        const struct pgm_interface_req_t*const recv_req,
        const socklen_t                        recv_req_len,
        pgm_error_t**                 restrict error                    /* maybe NULL */
        )
{
	g_return_val_if_fail (NULL != sock, FALSE);
        g_return_val_if_fail (NULL != sockaddr, FALSE);
        g_return_val_if_fail (0 != sockaddrlen, FALSE);
        if (sockaddr->sa_addr.sport) pgm_return_val_if_fail (sockaddr->sa_addr.sport != sockaddr->sa_port, FALSE);
        g_return_val_if_fail (NULL != send_req, FALSE);
        g_return_val_if_fail (sizeof(struct pgm_interface_req_t) == send_req_len, FALSE);
        g_return_val_if_fail (NULL != recv_req, FALSE);
        g_return_val_if_fail (sizeof(struct pgm_interface_req_t) == recv_req_len, FALSE);

	if (sock->is_bound ||
            sock->is_destroyed)
        {
                pgm_return_val_if_reached (FALSE);
        }

	memcpy (&sock->tsi, &sockaddr->sa_addr, sizeof(pgm_tsi_t));
        sock->dport = htons (sockaddr->sa_port);
        if (sock->tsi.sport) {
                sock->tsi.sport = htons (sock->tsi.sport);
        } else {
                do {
                        sock->tsi.sport = htons (pgm_random_int_range (0, UINT16_MAX));
                } while (sock->tsi.sport == sock->dport);
        }

/* UDP encapsulation port */
        if (sock->udp_encap_mcast_port) {
                ((struct sockaddr_in*)&sock->send_gsr.gsr_group)->sin_port = htons (sock->udp_encap_mcast_port);
        }

/* pseudo-random number generator for back-off intervals */
        pgm_rand_create (&sock->rand_);

/* PGM Children support of POLLs requires 32-bit random node identifier RAND_NODE_ID */
        if (sock->can_recv_data) {
                sock->rand_node_id = pgm_rand_int (&sock->rand_);
        }

/* determine IP header size for rate regulation engine & stats */
        sock->iphdr_len = (AF_INET == sock->family) ? sizeof(struct pgm_ip) : sizeof(struct pgm_ip6_hdr);
        pgm_trace (PGM_LOG_ROLE_NETWORK,"Assuming IP header size of %" PRIzu " bytes", sock->iphdr_len);

        if (sock->udp_encap_ucast_port) {
                const size_t udphdr_len = sizeof(struct pgm_udphdr);
                printf ("Assuming UDP header size of %" PRIzu " bytes\n", udphdr_len);
                sock->iphdr_len += udphdr_len;
        }

        const sa_family_t pgmcc_family = sock->use_pgmcc ? sock->family : 0;
        sock->max_tsdu = sock->max_tpdu - sock->iphdr_len - pgm_pkt_offset (FALSE, pgmcc_family);
        sock->max_tsdu_fragment = sock->max_tpdu - sock->iphdr_len - pgm_pkt_offset (TRUE, pgmcc_family);
        const unsigned max_fragments = sock->txw_sqns ? MIN( PGM_MAX_FRAGMENTS, sock->txw_sqns ) : PGM_MAX_FRAGMENTS;
        sock->max_apdu = MIN( PGM_MAX_APDU, max_fragments * sock->max_tsdu_fragment );

/* create peer list */
        if (sock->can_recv_data) {
                sock->peers_hashtable = pgm_hashtable_new (pgm_tsi_hash, pgm_tsi_equal);
                pgm_assert (NULL != sock->peers_hashtable);
        }

/* IP/PGM only */
	{
		const sa_family_t recv_family = sock->family;
                if (AF_INET == recv_family)
                {
/* include IP header only for incoming data, only works for IPv4 */
                        puts ("Request IP headers.");
                        if (SOCKET_ERROR == pgm_sockaddr_hdrincl (sock->recv_sock, recv_family, TRUE))
                        {
                                const int save_errno = pgm_get_last_sock_error();
				char errbuf[1024];
                                pgm_set_error (error,
                                               PGM_ERROR_DOMAIN_SOCKET,
                                               pgm_error_from_sock_errno (save_errno),
                                               "Enabling IP header in front of user data: %s",
                                               pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                                return FALSE;
                        }
                }
                else
                {
                        pgm_assert (AF_INET6 == recv_family);
                        puts ("Request socket packet-info.");
                        if (SOCKET_ERROR == pgm_sockaddr_pktinfo (sock->recv_sock, recv_family, TRUE))
                        {
                                const int save_errno = pgm_get_last_sock_error();
				char errbuf[1024];
                                pgm_set_error (error,
                                               PGM_ERROR_DOMAIN_SOCKET,
                                               pgm_error_from_sock_errno (save_errno),
                                               "Enabling receipt of control message per incoming datagram: %s",
                                               pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                                return FALSE;
                        }
                }
	}

	union {
                struct sockaddr         sa;
                struct sockaddr_in      s4;
                struct sockaddr_in6     s6;
                struct sockaddr_storage ss;
        } recv_addr, recv_addr2, send_addr, send_with_router_alert_addr;

#ifdef USE_BIND_INADDR_ANY
/* force default interface for bind-only, source address is still valid for multicast membership.
 * effectively same as running getaddrinfo(hints = {ai_flags = AI_PASSIVE})
 */
        if (AF_INET == sock->family) {
                memset (&recv_addr.s4, 0, sizeof(struct sockaddr_in));
                recv_addr.s4.sin_family = AF_INET;
                recv_addr.s4.sin_addr.s_addr = INADDR_ANY;
        } else {
                memset (&recv_addr.s6, 0, sizeof(struct sockaddr_in6));
                recv_addr.s6.sin6_family = AF_INET6;
                recv_addr.s6.sin6_addr = in6addr_any;
        }
        puts ("Binding receive socket to INADDR_ANY.");
#else
        if (!pgm_if_indextoaddr (recv_req->ir_interface,
                                 sock->family,
                                 recv_req->ir_scope_id,
                                 &recv_addr.sa,
                                 error))
        {
                return FALSE;
        }
        printf ("Binding receive socket to interface index %u scope %u\n"),
                   recv_req->ir_interface,
                   recv_req->ir_scope_id);

#endif /* CONFIG_BIND_INADDR_ANY */

	memcpy (&recv_addr2.sa, &recv_addr.sa, pgm_sockaddr_len (&recv_addr.sa));
        ((struct sockaddr_in*)&recv_addr)->sin_port = htons (sock->udp_encap_mcast_port);
        if (SOCKET_ERROR == bind (sock->recv_sock,
                                      &recv_addr.sa,
                                      pgm_sockaddr_len (&recv_addr.sa)))
        {
                const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
                char addr[INET6_ADDRSTRLEN];
                pgm_sockaddr_ntop ((struct sockaddr*)&recv_addr, addr, sizeof(addr));
                pgm_set_error (error,
                               PGM_ERROR_DOMAIN_SOCKET,
                               pgm_error_from_sock_errno (save_errno),
                               "Binding receive socket to address %s: %s",
                               addr,
                               pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                return FALSE;
        }

        {
                char s[INET6_ADDRSTRLEN];
                pgm_sockaddr_ntop ((struct sockaddr*)&recv_addr, s, sizeof(s));
                printf ("bind succeeded on recv_gsr[0] interface %s\n", s);
        }

/* keep a copy of the original address source to re-use for router alert bind */
        memset (&send_addr, 0, sizeof(send_addr));

        if (!pgm_if_indextoaddr (send_req->ir_interface,
                                 sock->family,
                                 send_req->ir_scope_id,
                                 (struct sockaddr*)&send_addr,
                                 error))
        {
                return FALSE;
        }
	else
	{
		printf ("Binding send socket to interface index %u scope %u\n",
			send_req->ir_interface,
                        send_req->ir_scope_id);
        }

        memcpy (&send_with_router_alert_addr, &send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));
        if (SOCKET_ERROR == bind (sock->send_sock,
                                      (struct sockaddr*)&send_addr,
                                      pgm_sockaddr_len ((struct sockaddr*)&send_addr)))
        {
                const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
                char addr[INET6_ADDRSTRLEN];
                pgm_sockaddr_ntop ((struct sockaddr*)&send_addr, addr, sizeof(addr));
                pgm_set_error (error,
                               PGM_ERROR_DOMAIN_SOCKET,
                               pgm_error_from_sock_errno (save_errno),
                               "Binding send socket to address %s: %s",
                               addr,
                               pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                return FALSE;
        }

/* resolve bound address if 0.0.0.0 */
        if (AF_INET == send_addr.ss.ss_family)
        {
                if (INADDR_ANY == ((struct sockaddr_in*)&send_addr)->sin_addr.s_addr)
		{
			struct addrinfo *result;
			if (!pgm_getnodeaddr (AF_INET, &result, error))
				return FALSE;
			memcpy (&send_addr, result->ai_addr, result->ai_addrlen);
	                pgm_freenodeaddr (result);
		}
        }
        else if (memcmp (&in6addr_any, &((struct sockaddr_in6*)&send_addr)->sin6_addr, sizeof(in6addr_any)) == 0)
	{
		struct addrinfo *result;
		if (!pgm_getnodeaddr (AF_INET6, &result, error))
                	return FALSE;
		memcpy (&send_addr, result->ai_addr, result->ai_addrlen);
                pgm_freenodeaddr (result);
        }

        {
                char s[INET6_ADDRSTRLEN];
                pgm_sockaddr_ntop ((struct sockaddr*)&send_addr, s, sizeof(s));
                printf ("bind succeeded on send_gsr interface %s\n", s);
        }

	if (SOCKET_ERROR == bind (sock->send_with_router_alert_sock,
                                      (struct sockaddr*)&send_with_router_alert_addr,
                                      pgm_sockaddr_len((struct sockaddr*)&send_with_router_alert_addr)))
        {
                const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
                char addr[INET6_ADDRSTRLEN];
                pgm_sockaddr_ntop ((struct sockaddr*)&send_with_router_alert_addr, addr, sizeof(addr));
                pgm_set_error (error,
                               PGM_ERROR_DOMAIN_SOCKET,
                               pgm_error_from_sock_errno (save_errno),
                               "Binding IP Router Alert (RFC 2113) send socket to address %s: %s",
                               addr,
                               pgm_sock_strerror_s (errbuf, sizeof(errbuf), save_errno));
                return FALSE;
        }

        {
                char s[INET6_ADDRSTRLEN];
                pgm_sockaddr_ntop ((struct sockaddr*)&send_with_router_alert_addr, s, sizeof(s));
                printf ("bind (router alert) succeeded on send_gsr interface %s\n", s);
        }

/* save send side address for broadcasting as source nla */
        memcpy (&sock->send_addr, &send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));

	sock->is_controlled_spm   = FALSE;
	sock->is_controlled_odata = FALSE;
	sock->is_controlled_rdata = FALSE;

/* allocate first incoming packet buffer */
        sock->rx_buffer = pgm_alloc_skb (sock->max_tpdu);

/* bind complete */
        sock->is_bound = TRUE;

/* cleanup */
        puts ("PGM socket successfully bound.");
        return TRUE;
}

static
bool
fake_pgm_bind (
	pgm_sock_t*                       restrict sock,
        const struct pgm_sockaddr_t*const restrict sockaddr,
        const socklen_t                            sockaddrlen,
        pgm_error_t**                     restrict error
	)
{
	struct pgm_interface_req_t null_req;
        memset (&null_req, 0, sizeof(null_req));
        return fake_pgm_bind3 (sock, sockaddr, sockaddrlen, &null_req, sizeof(null_req), &null_req, sizeof(null_req), error);
}

static
bool
fake_pgm_connect (
        pgm_sock_t*   restrict sock,
        G_GNUC_UNUSED pgm_error_t** restrict error    /* maybe NULL */
        )
{
        g_return_val_if_fail (sock != NULL, FALSE);
        g_return_val_if_fail (sock->recv_gsr_len > 0, FALSE);
#ifdef CONFIG_TARGET_WINE
        g_return_val_if_fail (sock->recv_gsr_len == 1, FALSE);
#endif
        for (unsigned i = 0; i < sock->recv_gsr_len; i++)
        {
                g_return_val_if_fail (sock->recv_gsr[i].gsr_group.ss_family == sock->recv_gsr[0].gsr_group.ss_family, FALSE);
                g_return_val_if_fail (sock->recv_gsr[i].gsr_group.ss_family == sock->recv_gsr[i].gsr_source.ss_family, FALSE);
        }
        g_return_val_if_fail (sock->send_gsr.gsr_group.ss_family == sock->recv_gsr[0].gsr_group.ss_family, FALSE);
/* state */
        if (PGM_UNLIKELY(sock->is_connected || !sock->is_bound || sock->is_destroyed)) {
                g_return_val_if_reached (FALSE);
        }

	sock->next_poll = pgm_time_update_now() + pgm_secs( 30 );
	sock->is_connected = TRUE;

/* cleanup */
	puts ("PGM socket successfully connected.");
        return TRUE;
}


static
bool
fake_pgm_close (
	pgm_sock_t*	sock,
	G_GNUC_UNUSED bool	flush
	)
{
	g_return_val_if_fail (sock != NULL, FALSE);
	g_return_val_if_fail (!sock->is_destroyed, FALSE);
/* flag existing calls */
        sock->is_destroyed = TRUE;
/* cancel running blocking operations */
	if (INVALID_SOCKET != sock->recv_sock) {
                puts ("Closing receive socket.");
		closesocket (sock->recv_sock);
                sock->recv_sock = INVALID_SOCKET;
        }
	if (INVALID_SOCKET != sock->send_sock) {
                puts ("Closing send socket.");
                closesocket (sock->send_sock);
                sock->send_sock = INVALID_SOCKET;
        }
	if (sock->peers_hashtable) {
		pgm_hashtable_destroy (sock->peers_hashtable);
                sock->peers_hashtable = NULL;
        }
        if (sock->peers_list) {
		do {
                        pgm_list_t* next = sock->peers_list->next;
                        pgm_peer_unref ((pgm_peer_t*)sock->peers_list->data);

                        sock->peers_list = next;
                } while (sock->peers_list);
        }
	if (INVALID_SOCKET != sock->send_with_router_alert_sock) {
                puts ("Closing send with router alert socket.");
                closesocket (sock->send_with_router_alert_sock);
                sock->send_with_router_alert_sock = INVALID_SOCKET;
        }
        if (sock->spm_heartbeat_interval) {
                puts ("freeing SPM heartbeat interval data.");
                g_free (sock->spm_heartbeat_interval);
                sock->spm_heartbeat_interval = NULL;
        }
        if (sock->rx_buffer) {
                puts ("freeing receive buffer.");
                pgm_free_skb (sock->rx_buffer);
                sock->rx_buffer = NULL;
        }

	g_free (sock);
	return TRUE;
}

static
void
session_create (
	char*		session_name,
	gboolean	is_fake
	)
{
	pgm_error_t* pgm_err = NULL;
	gboolean status;

/* check for duplicate */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess != NULL) {
		printf ("FAILED: duplicate session name '%s'\n", session_name);
		return;
	}

/* create new and fill in bits */
	sess = g_new0(struct sim_session, 1);
	sess->name = g_memdup (session_name, strlen(session_name)+1);

	if (is_fake) {
		sess->is_transport_fake = TRUE;
		status = fake_pgm_socket (&sess->sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &pgm_err);
	} else {
		status = pgm_socket (&sess->sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &pgm_err);
	}
	if (!status) {
		printf ("FAILED: pgm_socket(): %s\n", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
		pgm_error_free (pgm_err);
		goto err_free;
	}

/* success */
	g_hash_table_insert (g_sessions, sess->name, sess);
	g_sessions_list = g_list_prepend (g_sessions_list, sess);
	printf ("created new session \"%s\"\n", sess->name);
	puts ("READY");
	return;

err_free:
	g_free(sess->name);
	g_free(sess);
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
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
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
                .block_size                     = block_size,
                .proactive_packets              = 0,
                .group_size                     = group_size,
                .ondemand_parity_enabled        = TRUE,
                .var_pktlen_enabled             = TRUE
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
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
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
	const bool status = sess->is_transport_fake ?
			fake_pgm_bind (sess->sock, &addr, sizeof(addr), &pgm_err) :
			pgm_bind (sess->sock, &addr, sizeof(addr), &pgm_err);
	if (!status) {
                printf ("FAILED: pgm_bind(): %s\n", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
                pgm_error_free (pgm_err);
        } else
		puts ("READY");
}

static
void
session_connect (
        char*           session_name
        )
{
        struct pgm_addrinfo_t hints = {
                .ai_family = AF_INET
        }, *res = NULL;
        pgm_error_t* pgm_err = NULL;

/* check that session exists */
        struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
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
                  dscp = 0x2e << 2;             /* Expedited Forwarding PHB for network elements, no ECN. */

        if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_MULTICAST_LOOP, &no_multicast_loop, sizeof(no_multicast_loop)))
                puts ("FAILED: disable multicast loop");
        if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_MULTICAST_HOPS, &multicast_hops, sizeof(multicast_hops)))
                printf ("FAILED: set TTL = %d\n", multicast_hops);
        if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_TOS, &dscp, sizeof(dscp)))
                printf ("FAILED: set TOS = 0x%x\n", dscp);
        if (!pgm_setsockopt (sess->sock, IPPROTO_PGM, PGM_NOBLOCK, &non_blocking, sizeof(non_blocking)))
                puts ("FAILED: set non-blocking sockets");

	const bool status = sess->is_transport_fake ?
				fake_pgm_connect (sess->sock, &pgm_err) :
				pgm_connect (sess->sock, &pgm_err);
        if (!status) {
                printf ("FAILED: pgm_connect(): %s\n", (pgm_err && pgm_err->message) ? pgm_err->message : "(null)");
		return;
	}

	if (sess->is_transport_fake)
	{
/* add receive socket(s) to event manager */
		sess->recv_channel = g_io_channel_unix_new (sess->sock->recv_sock);

		GSource *source;
		source = g_io_create_watch (sess->recv_channel, G_IO_IN);
		g_source_set_callback (source, (GSourceFunc)on_io_data, sess->sock, NULL);
		g_source_attach (source, NULL);
		g_source_unref (source);
	}
	else
	{
		pgm_async_create (&sess->async, sess->sock, 0);
		pgm_async_add_watch (sess->async, on_data, sess);
	}

	puts ("READY");
}

static inline
gssize
pgm_sendto_hops (
	pgm_sock_t*		sock,
	G_GNUC_UNUSED gboolean	rl,
	gboolean		ra,
	const int		hops,
	const void*		buf,
	gsize			len,
	const struct sockaddr*	to,
	socklen_t		tolen
	)
{
        const int send_sock = ra ? sock->send_with_router_alert_sock : sock->send_sock;
        pgm_mutex_lock (&sock->send_mutex);
        const ssize_t sent = sendto (send_sock, buf, len, 0, to, tolen);
        pgm_mutex_unlock (&sock->send_mutex);
        return sent > 0 ? (gssize)len : (gssize)sent;
}

static
int
pgm_reset_heartbeat_spm (
	pgm_sock_t*	sock
	)
{
        int retval = 0;

        pgm_mutex_lock (&sock->timer_mutex);

/* re-set spm timer */
        sock->spm_heartbeat_state = 1;
        sock->next_heartbeat_spm = pgm_time_update_now() + sock->spm_heartbeat_interval[sock->spm_heartbeat_state++];

/* prod timer thread if sleeping */
        if (pgm_time_after( sock->next_poll, sock->next_heartbeat_spm ))
                sock->next_poll = sock->next_heartbeat_spm;

        pgm_mutex_unlock (&sock->timer_mutex);

        return retval;
}

static inline
int
brokn_send_apdu_unlocked (
        pgm_sock_t*	sock,
        const gchar*    buf,
        gsize           count,
	gsize*		bytes_written
	)
{
        guint32 opt_sqn = pgm_txw_next_lead(sock->window);
        guint packets = 0;
        guint bytes_sent = 0;
        guint data_bytes_sent = 0;

	pgm_mutex_lock (&sock->source_mutex);

        do {
/* retrieve packet storage from transmit window */
                int header_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + 
                                sizeof(struct pgm_opt_length) +         /* includes header */
                                sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_fragment);
                int tsdu_length = MIN(sock->max_tpdu - sock->iphdr_len - header_length, count - data_bytes_sent);
                int tpdu_length = header_length + tsdu_length;

		struct pgm_sk_buff_t* skb = pgm_alloc_skb (tsdu_length);
		pgm_skb_put (skb, tpdu_length);

                skb->pgm_header = (struct pgm_header*)skb->data;
                memcpy (skb->pgm_header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
                skb->pgm_header->pgm_sport       = sock->tsi.sport;
                skb->pgm_header->pgm_dport       = sock->dport;
                skb->pgm_header->pgm_type        = PGM_ODATA;
                skb->pgm_header->pgm_options     = PGM_OPT_PRESENT;
                skb->pgm_header->pgm_tsdu_length = g_htons (tsdu_length);

/* ODATA */
                skb->pgm_data = (struct pgm_data*)(skb->pgm_header + 1);
                skb->pgm_data->data_sqn         = g_htonl (pgm_txw_next_lead(sock->window));
                skb->pgm_data->data_trail       = g_htonl (pgm_txw_trail(sock->window));

/* OPT_LENGTH */
                struct pgm_opt_length* opt_len = (struct pgm_opt_length*)(skb->pgm_data + 1);
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
                skb->pgm_opt_fragment = (struct pgm_opt_fragment*)(opt_header + 1);
                skb->pgm_opt_fragment->opt_reserved      = 0;
                skb->pgm_opt_fragment->opt_sqn           = g_htonl (opt_sqn);
                skb->pgm_opt_fragment->opt_frag_off      = g_htonl (data_bytes_sent);
                skb->pgm_opt_fragment->opt_frag_len      = g_htonl (count);

/* TODO: the assembly checksum & copy routine is faster than memcpy & pgm_cksum on >= opteron hardware */
                skb->pgm_header->pgm_checksum    = 0;

                int pgm_header_len      = (char*)(skb->pgm_opt_fragment + 1) - (char*)skb->pgm_header;
                guint32 unfolded_header = pgm_csum_partial ((const void*)skb->pgm_header, pgm_header_len, 0);
                guint32 unfolded_odata  = pgm_csum_partial_copy ((const void*)(buf + data_bytes_sent), (void*)(skb->pgm_opt_fragment + 1), tsdu_length, 0);
                skb->pgm_header->pgm_checksum    = pgm_csum_fold (pgm_csum_block_add (unfolded_header, unfolded_odata, pgm_header_len));

/* add to transmit window */
		pgm_spinlock_lock (&sock->txw_spinlock);
                pgm_txw_add (sock->window, skb);
		pgm_spinlock_unlock (&sock->txw_spinlock);

/* do not send send packet */
		if (packets != 1)
                	pgm_sendto_hops (sock,
				    TRUE,
                                    FALSE,
				    sock->hops,
                                    skb->data,
                                    tpdu_length,
                                    (struct sockaddr*)&sock->send_gsr.gsr_group,
                                    pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));

/* save unfolded odata for retransmissions */
		*(guint32*)&skb->cb = unfolded_odata;

                packets++;
                bytes_sent += tpdu_length + sock->iphdr_len;
                data_bytes_sent += tsdu_length;

        } while (data_bytes_sent < count);

        if (data_bytes_sent > 0 && bytes_written)
		*bytes_written = data_bytes_sent;

/* release txw lock here in order to allow spms to lock mutex */
	pgm_mutex_unlock (&sock->source_mutex);
	pgm_reset_heartbeat_spm (sock);
	return PGM_IO_STATUS_NORMAL;
}

static
int
brokn_send (
        pgm_sock_t*	sock,      
        const gchar*	data,
        gsize		len,
	gsize*		bytes_written
        )
{
        if ( len <= ( sock->max_tpdu - (  sizeof(struct pgm_header) +
                                          sizeof(struct pgm_data) ) ) )
        {
		puts ("FAILED: cannot send brokn single TPDU length APDU");
		return PGM_IO_STATUS_ERROR;
        }

        return brokn_send_apdu_unlocked (sock, data, len, bytes_written);
}

static
void
session_send (
	char*		session_name,
	char*		string,
	gboolean	is_brokn		/* send broken apdu */
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

/* send message */
	int status;
	gsize stringlen = strlen(string) + 1;
	struct timeval tv;
#ifndef _WIN32
	int fds = 0;
	fd_set writefds;
#else
	int send_sock;
	DWORD cEvents = 1;
	WSAEVENT waitEvents[ cEvents ];
	DWORD dwTimeout, dwEvents;
        socklen_t socklen = sizeof(int);

        waitEvents[0] = WSACreateEvent ();
        pgm_getsockopt (sess->sock, IPPROTO_PGM, PGM_SEND_SOCK, &send_sock, &socklen);
        WSAEventSelect (send_sock, waitEvents[0], FD_WRITE);
#endif
again:
	if (is_brokn)
		status = brokn_send (sess->sock, string, stringlen, NULL);
	else
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
#ifndef _WIN32
		FD_ZERO(&writefds);
		pgm_select_info (sess->sock, NULL, &writefds, &fds);
		const int ready = select (1, NULL, &writefds, NULL, &tv);
#else
		dwTimeout = PGM_IO_STATUS_WOULD_BLOCK == status ? WSA_INFINITE : (DWORD)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
                dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, dwTimeout, FALSE);
#endif
		goto again;
	default:
		puts ("FAILED: pgm_send()");
		break;
	}
}

static
void
session_destroy (
	char*		session_name
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

/* remove from hash table */
	g_hash_table_remove (g_sessions, session_name);

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

	if (sess->is_transport_fake)
	{
		fake_pgm_close (sess->sock, TRUE);
	}
	else
	{
		pgm_close (sess->sock, TRUE);
	}
	sess->sock = NULL;
	g_free (sess->name);
	sess->name = NULL;
	g_free (sess);

	puts ("READY");
}

static
void
net_send_data (
	char*		session_name,
	guint8		pgm_type,		/* PGM_ODATA or PGM_RDATA */
	guint32		data_sqn,
	guint32		txw_trail,
	char*		string
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	pgm_sock_t* sock = sess->sock;

/* payload is string including terminating null. */
	int count = strlen(string) + 1;

/* send */
        int retval = 0;
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + count;

	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_data *data = (struct pgm_data*)(header + 1);
	memcpy (header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = sock->tsi.sport;
	header->pgm_dport       = sock->dport;
	header->pgm_type        = pgm_type;
	header->pgm_options     = 0;
	header->pgm_tsdu_length = g_htons (count);

/* O/RDATA */
	data->data_sqn		= g_htonl (data_sqn);
	data->data_trail	= g_htonl (txw_trail);

	memcpy (data + 1, string, count);

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	pgm_mutex_lock (&sock->send_mutex);
        retval = sendto (sock->send_sock,
                                (const void*)header,
                                tpdu_length,
                                0,            /* not expecting a reply */
				(struct sockaddr*)&sock->send_gsr.gsr_group,
				pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	pgm_mutex_unlock (&sock->send_mutex);

	puts ("READY");
}

/* differs to net_send_data in that the string parameters contains every payload
 * for the transmission group.  this is required to calculate the correct parity
 * as the fake transport does not own a transmission window.
 *
 * all payloads must be the same length unless variable TSDU support is enabled.
 */
static
void
net_send_parity (
	char*		session_name,
	guint8		pgm_type,		/* PGM_ODATA or PGM_RDATA */
	guint32		data_sqn,
	guint32		txw_trail,
	char*		string
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	pgm_sock_t* sock = sess->sock;

/* split string into individual payloads */
	guint16 parity_length = 0;
	gchar** src;
	src = g_strsplit (string, " ", sock->rs_k);

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

	if ( i != sock->rs_k ) {
		printf ("FAILED: payload array length %u, whilst rs_k is %u.\n", i, sock->rs_k);
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
	guint32 tg_sqn_mask = 0xffffffff << sock->tg_sqn_shift;
	guint rs_h = data_sqn & ~tg_sqn_mask;

/* send */
        int retval = 0;
	int tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_data) + parity_length;

	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_data *data = (struct pgm_data*)(header + 1);
	memcpy (header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = sock->tsi.sport;
	header->pgm_dport       = sock->dport;
	header->pgm_type        = pgm_type;
	header->pgm_options     = is_var_pktlen ? (PGM_OPT_PARITY | PGM_OPT_VAR_PKTLEN) : PGM_OPT_PARITY;
	header->pgm_tsdu_length = g_htons (parity_length);

/* O/RDATA */
	data->data_sqn		= g_htonl (data_sqn);
	data->data_trail	= g_htonl (txw_trail);

	memset (data + 1, 0, parity_length);
	pgm_rs_t rs;
	pgm_rs_create (&rs, sock->rs_n, sock->rs_k);
	pgm_rs_encode (&rs, (const pgm_gf8_t**)src, sock->rs_k + rs_h, (pgm_gf8_t*)(data + 1), parity_length);
	pgm_rs_destroy (&rs);

        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	pgm_mutex_lock (&sock->send_mutex);
        retval = sendto (sock->send_sock,
                                (const void*)header,
                                tpdu_length,
                                0,            /* not expecting a reply */
				(struct sockaddr*)&sock->send_gsr.gsr_group,
				pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	pgm_mutex_unlock (&sock->send_mutex);

	g_strfreev (src);
	src = NULL;

	puts ("READY");
}

static
void
net_send_spm (
	char*		session_name,
	guint32		spm_sqn,
	guint32		txw_trail,
	guint32		txw_lead,
	gboolean	proactive_parity,
	gboolean	ondemand_parity,
	guint		k
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	pgm_sock_t* sock = sess->sock;

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
	memcpy (header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = sock->tsi.sport;
	header->pgm_dport       = sock->dport;
	header->pgm_type        = PGM_SPM;
	header->pgm_options	= (proactive_parity || ondemand_parity) ? (PGM_OPT_PRESENT | PGM_OPT_NETWORK) : 0;
	header->pgm_tsdu_length	= 0;

/* SPM */
	spm->spm_sqn		= g_htonl (spm_sqn);
	spm->spm_trail		= g_htonl (txw_trail);
	spm->spm_lead		= g_htonl (txw_lead);
	pgm_sockaddr_to_nla ((struct sockaddr*)&sock->send_addr, (char*)&spm->spm_nla_afi);

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

        retval = sendto (sock->send_sock,
                                (const void*)header,
                                tpdu_length,
                                0,            /* not expecting a reply */
				(struct sockaddr*)&sock->send_gsr.gsr_group,
				pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
	puts ("READY");
}

static
void
net_send_spmr (
	char*		session_name,
	pgm_tsi_t*	tsi
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

	pgm_sock_t* sock = sess->sock;

/* check that the peer exists */
	pgm_peer_t* peer = pgm_hashtable_lookup (sock->peers_hashtable, tsi);
	struct sockaddr_storage peer_nla;
	pgm_gsi_t* peer_gsi;
	guint16 peer_sport;

	if (peer == NULL) {
/* ourself */
		if (pgm_tsi_equal (tsi, &sock->tsi))
		{
			peer_gsi   = &sock->tsi.gsi;
			peer_sport = sock->tsi.sport;
		}
		else
		{
			printf ("FAILED: peer \"%s\" not found\n", pgm_tsi_print (tsi));
			return;
		}
	}
	else
	{
		memcpy (&peer_nla, &peer->local_nla, sizeof(struct sockaddr_storage));
		peer_gsi   = &peer->tsi.gsi;
		peer_sport = peer->tsi.sport;
	}

/* send */
        int retval = 0;
	int tpdu_length = sizeof(struct pgm_header);
	gchar buf[ tpdu_length ];

        struct pgm_header *header = (struct pgm_header*)buf;
	memcpy (header->pgm_gsi, peer_gsi, sizeof(pgm_gsi_t));
	header->pgm_sport       = sock->dport;
	header->pgm_dport       = peer_sport;
	header->pgm_type        = PGM_SPMR;
	header->pgm_options     = 0;
	header->pgm_tsdu_length = 0;
        header->pgm_checksum    = 0;
        header->pgm_checksum    = pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	pgm_mutex_lock (&sock->send_mutex);
/* TTL 1 */
	pgm_sockaddr_multicast_hops (sock->send_sock, sock->send_gsr.gsr_group.ss_family, 1);
        retval = sendto (sock->send_sock,
                                (const void*)header,
                                tpdu_length,
                                0,            /* not expecting a reply */
				(struct sockaddr*)&sock->send_gsr.gsr_group,
				pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));
/* default TTL */
	pgm_sockaddr_multicast_hops (sock->send_sock, sock->send_gsr.gsr_group.ss_family, sock->hops);

	if (!pgm_tsi_equal (tsi, &sock->tsi))
	{
	        retval = sendto (sock->send_sock,
	                                (const void*)header,
	                                tpdu_length,
	                                0,            /* not expecting a reply */
					(struct sockaddr*)&peer_nla,
					pgm_sockaddr_len((struct sockaddr*)&peer_nla));
	}

	pgm_mutex_unlock (&sock->send_mutex);

	puts ("READY");
}

/* Send a NAK on a valid transport.  A fake transport would need to specify the senders NLA,
 * we use the peer list to bypass extracting it from the monitor output.
 */

static
void
net_send_ncf (
	char*			session_name,
	pgm_tsi_t*		tsi,
	struct pgm_sqn_list_t*	sqn_list	/* list of sequence numbers */
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

/* check that the peer exists */
	pgm_sock_t* sock = sess->sock;
	pgm_peer_t* peer = pgm_hashtable_lookup (sock->peers_hashtable, tsi);
	if (peer == NULL) {
		printf ("FAILED: peer \"%s\" not found\n", pgm_tsi_print (tsi));
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
        memcpy (header->pgm_gsi, &sock->tsi.gsi, sizeof(pgm_gsi_t));

	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->nla, sizeof(struct sockaddr_storage));

/* dport & sport swap over for a nak */
        header->pgm_sport       = sock->tsi.sport;
        header->pgm_dport       = sock->dport;
        header->pgm_type        = PGM_NCF;
        header->pgm_options     = (sqn_list->len > 1) ? (PGM_OPT_PRESENT | PGM_OPT_NETWORK) : 0;
        header->pgm_tsdu_length = 0;

/* NCF */
        ncf->nak_sqn            = g_htonl (sqn_list->sqn[0]);

/* source nla */
        pgm_sockaddr_to_nla ((struct sockaddr*)&peer_nla, (char*)&ncf->nak_src_nla_afi);

/* group nla */
        pgm_sockaddr_to_nla ((struct sockaddr*)&sock->recv_gsr[0].gsr_group, (char*)&ncf->nak_grp_nla_afi);

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

        retval = sendto (sock->send_with_router_alert_sock,
                                (const void*)header,
                                tpdu_length,
                                0,            /* not expecting a reply */
				(struct sockaddr*)&sock->send_gsr.gsr_group,
				pgm_sockaddr_len((struct sockaddr*)&sock->send_gsr.gsr_group));

	puts ("READY");
}

static
void
net_send_nak (
	char*			session_name,
	pgm_tsi_t*		tsi,
	struct pgm_sqn_list_t*	sqn_list,	/* list of sequence numbers */
	gboolean		is_parity	/* TRUE = parity, FALSE = selective */
	)
{
/* check that session exists */
	struct sim_session* sess = g_hash_table_lookup (g_sessions, session_name);
	if (sess == NULL) {
		printf ("FAILED: session '%s' not found\n", session_name);
		return;
	}

/* check that the peer exists */
	pgm_sock_t* sock = sess->sock;
	pgm_peer_t* peer = pgm_hashtable_lookup (sock->peers_hashtable, tsi);
	if (peer == NULL) {
		printf ("FAILED: peer \"%s\" not found\n", pgm_tsi_print(tsi));
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
        memcpy (header->pgm_gsi, &peer->tsi.gsi, sizeof(pgm_gsi_t));

	guint16 peer_sport = peer->tsi.sport;
	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->nla, sizeof(struct sockaddr_storage));

/* dport & sport swap over for a nak */
        header->pgm_sport       = sock->dport;
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
        pgm_sockaddr_to_nla ((struct sockaddr*)&sock->recv_gsr[0].gsr_group, (char*)&nak->nak_grp_nla_afi);

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

        retval = sendto (sock->send_with_router_alert_sock,
                                (const void*)header,
                                tpdu_length,
                                0,            /* not expecting a reply */
                                (struct sockaddr*)&peer_nla,
                                pgm_sockaddr_len((struct sockaddr*)&peer_nla));

	puts ("READY");
}

static
int
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

static
gboolean
on_stdin_data (
	GIOChannel*	source,
	G_GNUC_UNUSED GIOCondition	condition,
	G_GNUC_UNUSED gpointer		data
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

/* ideally confirm number of payloads matches sess->sock::rs_k ... */
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
			struct pgm_sqn_list_t sqn_list;
			sqn_list.len = 0;
			{
#ifndef _WIN32
				char* saveptr = NULL;
				for (p = str + pmatch[5].rm_so; ; p = NULL) {
					char* token = strtok_r (p, ",", &saveptr);
					if (!token) break;
					sqn_list.sqn[sqn_list.len++] = strtoul (token, NULL, 10);
				}
#else
				for (p = str + pmatch[5].rm_so; ; p = NULL) {
					char* token = strtok (p, ",");
					if (!token) break;
					sqn_list.sqn[sqn_list.len++] = strtoul (token, NULL, 10);
				}
#endif
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

/* set PGM network */
		re = "^set[[:space:]]+network[[:space:]]+([[:print:]]*;[[:print:]]+)$";
		regcomp (&preg, re, REG_EXTENDED);
		if (0 == regexec (&preg, str, G_N_ELEMENTS(pmatch), pmatch, 0))
		{
			char *pgm_network = g_memdup (str + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so + 1 );
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
