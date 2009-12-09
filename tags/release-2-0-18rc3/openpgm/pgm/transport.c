/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM transport: manage incoming & outgoing sockets with ambient SPMs, 
 * transmit & receive windows.
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
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#ifdef CONFIG_HAVE_POLL
#	include <poll.h>
#endif
#ifdef CONFIG_HAVE_EPOLL
#	include <sys/epoll.h>
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>

#ifdef G_OS_UNIX
#	include <netdb.h>
#	include <net/if.h>
#	include <netinet/in.h>
#	include <netinet/ip.h>
#	include <netinet/udp.h>
#	include <sys/socket.h>
#	include <sys/time.h>
#	include <arpa/inet.h>
#else
#	include <ws2tcpip.h>
#endif

#include "pgm/transport.h"
#include "pgm/source.h"
#include "pgm/receiver.h"
#include "pgm/if.h"
#include "pgm/getifaddrs.h"
#include "pgm/getnodeaddr.h"
#include "pgm/indextoaddr.h"
#include "pgm/indextoname.h"
#include "pgm/nametoindex.h"
#include "pgm/ip.h"
#include "pgm/packet.h"
#include "pgm/net.h"
#include "pgm/txwi.h"
#include "pgm/rxwi.h"
#include "pgm/rate_control.h"
#include "pgm/sn.h"
#include "pgm/time.h"
#include "pgm/timer.h"
#include "pgm/checksum.h"
#include "pgm/reed_solomon.h"


//#define TRANSPORT_DEBUG
//#define TRANSPORT_SPM_DEBUG

#ifndef TRANSPORT_DEBUG
#	define g_trace(m,...)		while (0)
#else
#include <ctype.h>
#	ifdef TRANSPORT_SPM_DEBUG
#		define g_trace(m,...)		g_debug(__VA_ARGS__)
#	else
#		define g_trace(m,...)		do { if (strcmp((m),"SPM")) { g_debug(__VA_ARGS__); } } while (0)
#	endif
#endif

#ifndef GROUP_FILTER_SIZE
#	define GROUP_FILTER_SIZE(numsrc) (sizeof (struct group_filter) \
					  - sizeof (struct sockaddr_storage)         \
					  + ((numsrc)                                \
					     * sizeof (struct sockaddr_storage)))
#endif


/* global locals */
GStaticRWLock pgm_transport_list_lock = G_STATIC_RW_LOCK_INIT;		/* list of all transports for admin interfaces */
GSList* pgm_transport_list = NULL;


gsize
pgm_transport_pkt_offset (
	gboolean		can_fragment
	)
{
	return can_fragment ? ( sizeof(struct pgm_header)
			      + sizeof(struct pgm_data)
			      + sizeof(struct pgm_opt_length)
	                      + sizeof(struct pgm_opt_header)
			      + sizeof(struct pgm_opt_fragment) )
			    : ( sizeof(struct pgm_header) + sizeof(struct pgm_data) );
}

/* destroy a pgm_transport object and contents, if last transport also destroy
 * associated event loop
 *
 * outstanding locks:
 * 1) pgm_transport_t::lock
 * 2) pgm_transport_t::receiver_mutex
 * 3) pgm_transport_t::source_mutex
 * 4) pgm_transport_t::txw_mutex
 * 5) pgm_transport_t::timer_mutex
 *
 * If application calls a function on the transport after destroy() it is a
 * programmer error: segv likely to occur on unlock.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

gboolean
pgm_transport_destroy (
	pgm_transport_t*	transport,
	gboolean		flush
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	g_return_val_if_fail (!transport->is_destroyed, FALSE);
	g_trace ("INFO","pgm_transport_destroy (transport:%p flush:%s)",
		(gpointer)transport,
		flush ? "TRUE":"FALSE");
/* flag existing calls */
	transport->is_destroyed = TRUE;
/* cancel running blocking operations */
	if (-1 != transport->recv_sock) {
		g_trace ("INFO","closing receive socket.");
		close (transport->recv_sock);
		transport->recv_sock = -1;
	}
	if (-1 != transport->recv_sock2) {
		g_trace ("INFO","closing receive socket2.");
		close (transport->recv_sock2);
		transport->recv_sock2 = -1;
	}
	if (-1 != transport->send_sock) {
		g_trace ("INFO","closing send socket.");
		close (transport->send_sock);
		transport->send_sock = -1;
	}
	g_static_rw_lock_reader_unlock (&transport->lock);
	g_trace ("INFO","blocking on destroy lock ...");
	g_static_rw_lock_writer_lock (&transport->lock);

	g_trace ("INFO","removing transport from inventory.");
	g_static_rw_lock_writer_lock (&pgm_transport_list_lock);
	pgm_transport_list = g_slist_remove (pgm_transport_list, transport);
	g_static_rw_lock_writer_unlock (&pgm_transport_list_lock);

/* flush source side by sending heartbeat SPMs */
	if (transport->can_send_data &&
	    transport->is_bound && 
	    flush)
	{
		g_trace ("INFO","flushing PGM source with session finish option broadcast SPMs.");
		if (!pgm_send_spm (transport, PGM_OPT_FIN) ||
		    !pgm_send_spm (transport, PGM_OPT_FIN) ||
		    !pgm_send_spm (transport, PGM_OPT_FIN))
		{
			g_trace ("INFO","failed to send flushing SPMs.");
		}
	}

	if (transport->peers_hashtable) {
		g_trace ("INFO","destroying peer lookup table.");
		g_hash_table_destroy (transport->peers_hashtable);
		transport->peers_hashtable = NULL;
	}
	if (transport->peers_list) {
		g_trace ("INFO","destroying peer list.");
		do {
			GList* next = transport->peers_list->next;
			pgm_peer_unref ((pgm_peer_t*)transport->peers_list->data);

			transport->peers_list = next;
		} while (transport->peers_list);
	}

	if (transport->window) {
		g_trace ("INFO","destroying transmit window.");
		pgm_txw_shutdown (transport->window);
		transport->window = NULL;
	}
	if (transport->rate_control) {
		g_trace ("INFO","destroying rate control.");
		pgm_rate_destroy (transport->rate_control);
		transport->rate_control = NULL;
	}
	if (transport->send_with_router_alert_sock) {
		g_trace ("INFO","closing send with router alert socket.");
		close(transport->send_with_router_alert_sock);
		transport->send_with_router_alert_sock = 0;
	}
	if (transport->spm_heartbeat_interval) {
		g_trace ("INFO","freeing SPM heartbeat interval data.");
		g_free (transport->spm_heartbeat_interval);
		transport->spm_heartbeat_interval = NULL;
	}
	if (transport->rand_) {
		g_trace ("INFO","freeing randomization data.");
		g_rand_free (transport->rand_);
		transport->rand_ = NULL;
	}
	if (transport->rx_buffer) {
		g_trace ("INFO","freeing receive buffer.");
		pgm_free_skb (transport->rx_buffer);
		transport->rx_buffer = NULL;
	}
	g_trace ("INFO","destroying notification channel.");
	pgm_notify_destroy (&transport->pending_notify);
	g_trace ("INFO","freeing transport locks.");
	g_static_rw_lock_free (&transport->peers_lock);
	g_static_mutex_free (&transport->txw_mutex);
	g_static_mutex_free (&transport->send_mutex);
	g_static_mutex_free (&transport->timer_mutex);
	g_static_mutex_free (&transport->source_mutex);
	g_static_mutex_free (&transport->receiver_mutex);
	g_static_rw_lock_writer_unlock (&transport->lock);
	g_static_rw_lock_free (&transport->lock);
	g_trace ("INFO","freeing transport data.");
	g_free (transport);
	g_trace ("INFO","finished.");
	return TRUE;
}

/* create a pgm_transport object.  create sockets that require superuser priviledges, if this is
 * the first instance also create a real-time priority receiving thread.  if interface ports
 * are specified then UDP encapsulation will be used instead of raw protocol.
 *
 * if send == recv only two sockets need to be created iff ip headers are not required (IPv6).
 *
 * all receiver addresses must be the same family.
 * interface and multiaddr must be the same family.
 * family cannot be AF_UNSPEC!
 *
 * returns 0 on success, or -1 on error and sets errno appropriately.
 */

#if ( AF_INET != PF_INET ) || ( AF_INET6 != PF_INET6 )
#error AF_INET and PF_INET are different values, the bananas are jumping in their pyjamas!
#endif

gboolean
pgm_transport_create (
	pgm_transport_t**		transport,
	struct pgm_transport_info_t*	tinfo,
	GError**			error
	)
{
	pgm_transport_t* new_transport;

	g_return_val_if_fail (NULL != transport, FALSE);
	g_return_val_if_fail (NULL != tinfo, FALSE);
	if (tinfo->ti_sport) g_return_val_if_fail (tinfo->ti_sport != tinfo->ti_dport, FALSE);
	if (tinfo->ti_udp_encap_ucast_port)
		g_return_val_if_fail (tinfo->ti_udp_encap_mcast_port, FALSE);
	else if (tinfo->ti_udp_encap_mcast_port)
		g_return_val_if_fail (tinfo->ti_udp_encap_ucast_port, FALSE);
	g_return_val_if_fail (tinfo->ti_recv_addrs_len > 0, FALSE);
	g_return_val_if_fail (tinfo->ti_recv_addrs_len <= IP_MAX_MEMBERSHIPS, FALSE);
	g_return_val_if_fail (NULL != tinfo->ti_recv_addrs, FALSE);
	g_return_val_if_fail (1 == tinfo->ti_send_addrs_len, FALSE);
	g_return_val_if_fail (NULL != tinfo->ti_send_addrs, FALSE);
	for (unsigned i = 0; i < tinfo->ti_recv_addrs_len; i++)
	{
		g_return_val_if_fail (pgm_sockaddr_family (&tinfo->ti_recv_addrs[i].gsr_group) == pgm_sockaddr_family (&tinfo->ti_recv_addrs[0].gsr_group), -FALSE);
		g_return_val_if_fail (pgm_sockaddr_family (&tinfo->ti_recv_addrs[i].gsr_group) == pgm_sockaddr_family (&tinfo->ti_recv_addrs[i].gsr_source), -FALSE);
	}
	g_return_val_if_fail (pgm_sockaddr_family (&tinfo->ti_send_addrs[0].gsr_group) == pgm_sockaddr_family (&tinfo->ti_send_addrs[0].gsr_source), -FALSE);

	new_transport = g_malloc0 (sizeof(pgm_transport_t));
	new_transport->can_send_data = TRUE;
	new_transport->can_send_nak  = TRUE;
	new_transport->can_recv_data = TRUE;

/* source-side */
	g_static_mutex_init (&new_transport->source_mutex);
/* transmit window */
	g_static_mutex_init (&new_transport->txw_mutex);
/* send socket */
	g_static_mutex_init (&new_transport->send_mutex);
/* next timer & spm expiration */
	g_static_mutex_init (&new_transport->timer_mutex);
/* receiver-side */
	g_static_mutex_init (&new_transport->receiver_mutex);
/* peer hash map & list lock */
	g_static_rw_lock_init (&new_transport->peers_lock);
/* destroy lock */
	g_static_rw_lock_init (&new_transport->lock);

	memcpy (&new_transport->tsi.gsi, &tinfo->ti_gsi, sizeof(pgm_gsi_t));
	new_transport->dport = g_htons (tinfo->ti_dport);
	if (tinfo->ti_sport) {
		new_transport->tsi.sport = g_htons (tinfo->ti_sport);
	} else {
		do {
			new_transport->tsi.sport = g_htons (g_random_int_range (0, UINT16_MAX));
		} while (new_transport->tsi.sport == new_transport->dport);
	}

/* network data ports */
	new_transport->udp_encap_ucast_port = tinfo->ti_udp_encap_ucast_port;
	new_transport->udp_encap_mcast_port = tinfo->ti_udp_encap_mcast_port;

/* copy network parameters */
	memcpy (&new_transport->send_gsr, &tinfo->ti_send_addrs[0], sizeof(struct group_source_req));
	((struct sockaddr_in*)&new_transport->send_gsr.gsr_group)->sin_port = g_htons (new_transport->udp_encap_mcast_port);
	for (unsigned i = 0; i < tinfo->ti_recv_addrs_len; i++)
	{
		memcpy (&new_transport->recv_gsr[i], &tinfo->ti_recv_addrs[i], sizeof(struct group_source_req));
	}
	new_transport->recv_gsr_len = tinfo->ti_recv_addrs_len;

/* open sockets to implement PGM */
	int socket_type, protocol;
	if (new_transport->udp_encap_ucast_port) {
		g_trace ("INFO", "opening UDP encapsulated sockets.");
		socket_type = SOCK_DGRAM;
		protocol = IPPROTO_UDP;
	} else {
		g_trace ("INFO", "opening raw sockets.");
		socket_type = SOCK_RAW;
		protocol = ipproto_pgm;
	}

	if ((new_transport->recv_sock = socket (pgm_sockaddr_family (&new_transport->recv_gsr[0].gsr_group),
						socket_type,
						protocol)) < 0)
	{
#ifdef G_OS_UNIX
		const int save_errno = errno;
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     pgm_transport_error_from_errno (errno),
			     _("Creating receive socket: %s"),
			     g_strerror (errno));
		if (EPERM == save_errno) {
			g_warning ("PGM protocol requires CAP_NET_RAW capability, e.g. sudo execcap 'cap_net_raw=ep'");
		}
#else
		const int save_errno = WSAGetLastError();
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     pgm_transport_error_from_wsa_errno (save_errno),
			     _("Creating receive socket: %s"),
			     pgm_wsastrerror (save_errno));
#endif
		goto err_destroy;
	}

	new_transport->recv_sock2 = -1;
	if (new_transport->udp_encap_ucast_port != new_transport->udp_encap_mcast_port)
	{
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     PGM_TRANSPORT_ERROR_INVAL,
			     _("Creating receive socket2: unsupported feature."));
		goto err_destroy;
	}

	if ((new_transport->send_sock = socket (pgm_sockaddr_family (&new_transport->send_gsr.gsr_group),
						socket_type,
						protocol)) < 0)
	{
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     pgm_transport_error_from_errno (errno),
			     _("Creating send socket: %s"),
			     g_strerror (errno));
		goto err_destroy;
	}

	if ((new_transport->send_with_router_alert_sock = socket (pgm_sockaddr_family (&new_transport->send_gsr.gsr_group),
						socket_type,
						protocol)) < 0)
	{
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     pgm_transport_error_from_errno (errno),
			     _("Creating IP Router Alert (RFC 2113) send socket: %s"),
			     g_strerror (errno));
		goto err_destroy;
	}

	*transport = new_transport;

	g_static_rw_lock_writer_lock (&pgm_transport_list_lock);
	pgm_transport_list = g_slist_append (pgm_transport_list, *transport);
	g_static_rw_lock_writer_unlock (&pgm_transport_list_lock);
	return TRUE;

err_destroy:
	if (-1 != new_transport->recv_sock) {
		close (new_transport->recv_sock);
		new_transport->recv_sock = -1;
	}
	if (-1 != new_transport->recv_sock2) {
		close (new_transport->recv_sock2);
		new_transport->recv_sock2 = -1;
	}
	if (-1 != new_transport->send_sock) {
		close (new_transport->send_sock);
		new_transport->send_sock = -1;
	}
	if (-1 != new_transport->send_with_router_alert_sock) {
		close (new_transport->send_with_router_alert_sock);
		new_transport->send_with_router_alert_sock = -1;
	}
	g_free (new_transport);
	return FALSE;
}

/* helper to drop out of setuid 0 after creating PGM sockets
 */
void
pgm_drop_superuser (void)
{
#ifdef G_OS_UNIX
	if (0 == getuid()) {
		setuid((gid_t)65534);
		setgid((uid_t)65534);
	}
#endif
}

/* 0 < tpdu < 65536 by data type (guint16)
 *
 * IPv4:   68 <= tpdu < 65536		(RFC 2765)
 * IPv6: 1280 <= tpdu < 65536		(RFC 2460)
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

gboolean
pgm_transport_set_max_tpdu (
	pgm_transport_t* const	transport,
	const guint16		max_tpdu
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (max_tpdu >= (sizeof(struct pgm_ip) + sizeof(struct pgm_header)), FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		return FALSE;
	}
	transport->max_tpdu = max_tpdu;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* TRUE = enable multicast loopback and for UDP encapsulation SO_REUSEADDR,
 * FALSE = default, to disable.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

gboolean
pgm_transport_set_multicast_loop (
	pgm_transport_t* const	transport,
	const gboolean		use_multicast_loop
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		return FALSE;
	}
	transport->use_multicast_loop = use_multicast_loop;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < hops < 256, hops == -1 use kernel default (ignored).
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

gboolean
pgm_transport_set_hops (
	pgm_transport_t* const	transport,
	const gint		hops
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (hops > 0, FALSE);
	g_return_val_if_fail (hops < 256, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		return FALSE;
	}
	transport->hops = hops;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < wmem < wmem_max (user)
 *
 * operating system and sysctl dependent maximum, minimum on Linux 256 (doubled).
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

gboolean
pgm_transport_set_sndbuf (
	pgm_transport_t* const	transport,
	const int		size		/* not gsize/gssize as we propogate to setsockopt() */
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (size > 0, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		return FALSE;
	}
#ifdef G_OS_UNIX
	int wmem_max;
	FILE* fp;

	fp = fopen ("/proc/sys/net/core/wmem_max", "r");
	if (fp) {
		fscanf (fp, "%d", &wmem_max);
		fclose (fp);
		if (size > wmem_max) {
			g_static_rw_lock_reader_unlock (&transport->lock);
			return FALSE;
		}
	} else {
		g_warning ("cannot open /proc/sys/net/core/wmem_max");
		g_static_rw_lock_reader_unlock (&transport->lock);
		return FALSE;
	}
#endif
	transport->sndbuf = size;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < rmem < rmem_max (user)
 *
 * minimum on Linux is 2048 (doubled).
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

gboolean
pgm_transport_set_rcvbuf (
	pgm_transport_t* const	transport,
	const int		size		/* not gsize/gssize */
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (size > 0, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		return FALSE;
	}
#ifdef G_OS_UNIX
	int rmem_max;
	FILE* fp;

	fp = fopen ("/proc/sys/net/core/rmem_max", "r");
	if (fp) {
		fscanf (fp, "%d", &rmem_max);
		fclose (fp);
		if (size > rmem_max) {
			g_static_rw_lock_reader_unlock (&transport->lock);
			return FALSE;
		}
	} else {
		g_warning ("cannot open /proc/sys/net/core/rmem_max");
		g_static_rw_lock_reader_unlock (&transport->lock);
		return FALSE;
	}
#endif
	transport->rcvbuf = size;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* bind the sockets to the link layer to start receiving data.
 *
 * returns 0 on success, or -1 on error and sets errno appropriately,
 *			 or -2 on NS lookup error and sets h_errno appropriately.
 */

gboolean
pgm_transport_bind (
	pgm_transport_t*	transport,
	GError**		error
	)
{
	g_return_val_if_fail (NULL != transport, FALSE);
	if (!g_static_rw_lock_writer_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_writer_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}

	g_trace ("INFO", "bind (transport:%p error:%p)",
		 (gpointer)transport, (gpointer)error);

	transport->rand_ = g_rand_new();
	g_assert (transport->rand_);

	if (transport->can_send_data) {
		if (0 != pgm_notify_init (&transport->rdata_notify)) {
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_errno (errno),
				     _("Creating RDATA notification channel: %s"),
				     g_strerror (errno));
			g_static_rw_lock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}
	if (transport->can_recv_data) {
		if (0 != pgm_notify_init (&transport->pending_notify)) {
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_errno (errno),
				     _("Creating waiting peer notification channel: %s"),
				     g_strerror (errno));
			g_static_rw_lock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}

/* determine IP header size for rate regulation engine & stats */
	transport->iphdr_len = (AF_INET == pgm_sockaddr_family (&transport->send_gsr.gsr_group)) ? sizeof(struct pgm_ip) : sizeof(struct pgm_ip6_hdr);
	g_trace ("INFO","assuming IP header size of %" G_GSIZE_FORMAT " bytes", transport->iphdr_len);

	if (transport->udp_encap_ucast_port) {
		const guint udphdr_len = sizeof(struct pgm_udphdr);
		g_trace ("INFO","assuming UDP header size of %i bytes", udphdr_len);
		transport->iphdr_len += udphdr_len;
	}

	transport->max_tsdu = transport->max_tpdu - transport->iphdr_len - pgm_transport_pkt_offset (FALSE);
	transport->max_tsdu_fragment = transport->max_tpdu - transport->iphdr_len - pgm_transport_pkt_offset (TRUE);
	transport->max_apdu = (transport->txw_sqns ? 
				MIN(PGM_MAX_FRAGMENTS, transport->txw_sqns) :
				PGM_MAX_FRAGMENTS) * transport->max_tsdu_fragment;

	if (transport->can_send_data) {
		g_trace ("INFO","construct transmit window.");
		transport->window = transport->txw_sqns ?
					pgm_txw_create (&transport->tsi, 0, transport->txw_sqns, 0, 0, transport->use_ondemand_parity || transport->use_proactive_parity, transport->rs_n, transport->rs_k) :
					pgm_txw_create (&transport->tsi, transport->max_tpdu, 0, transport->txw_secs, transport->txw_max_rte, transport->use_ondemand_parity || transport->use_proactive_parity, transport->rs_n, transport->rs_k);
		g_assert (transport->window);
	}

/* create peer list */
	if (transport->can_recv_data) {
		transport->peers_hashtable = g_hash_table_new (pgm_tsi_hash, pgm_tsi_equal);
		g_assert (transport->peers_hashtable);
	}

	if (transport->udp_encap_ucast_port)
	{
		g_trace ("INFO","set socket sharing.");
		gboolean v = TRUE;
		if (0 != setsockopt (transport->recv_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v)) ||
		     (-1 != transport->recv_sock2 &&
		       0 != setsockopt (transport->recv_sock2, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v))) ||
		    0 != setsockopt (transport->send_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v)) ||
		    0 != setsockopt (transport->send_with_router_alert_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v)))
		{
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_errno (errno),
				     _("Enabling reuse of socket local address: %s"),
				     g_strerror (errno));
			g_static_rw_lock_writer_unlock (&transport->lock);
			return FALSE;
		}

/* request extra packet information to determine destination address on each packet */
		g_trace ("INFO","request socket packet-info.");
		const int recv_family = pgm_sockaddr_family (&transport->recv_gsr[0].gsr_group);
		if (0 != pgm_sockaddr_pktinfo (transport->recv_sock, recv_family, TRUE) ||
		     (-1 != transport->recv_sock2 &&
		       0 != pgm_sockaddr_pktinfo (transport->recv_sock2, recv_family, TRUE)))
		{
#ifdef G_OS_UNIX
			int save_errno = errno;
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_errno (save_errno),
				     _("Enabling receipt of ancillary information per incoming packet: %s"),
				     g_strerror (save_errno));
#else
			int save_errno = WSAGetLastError();
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_wsa_errno (save_errno),
				     _("Enabling receipt of ancillary information per incoming packet: %s"),
				     pgm_wsastrerror (save_errno));
#endif
			g_static_rw_lock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}
	else
	{
		const int recv_family = pgm_sockaddr_family(&transport->recv_gsr[0].gsr_group);
		if (AF_INET == recv_family)
		{
/* include IP header only for incoming data, only works for IPv4 */
			g_trace ("INFO","request IP headers.");
			if (0 != pgm_sockaddr_hdrincl (transport->recv_sock, recv_family, TRUE) ||
			     (-1 != transport->recv_sock2 &&
			       0 != pgm_sockaddr_hdrincl (transport->recv_sock2, recv_family, TRUE)))
			{
				g_set_error (error,
					     PGM_TRANSPORT_ERROR,
					     pgm_transport_error_from_errno (errno),
					     _("Enabling IP header in front of user data: %s"),
					     g_strerror (errno));
				g_static_rw_lock_writer_unlock (&transport->lock);
				return FALSE;
			}
		}
		else
		{
			g_assert (AF_INET6 == recv_family);
			g_trace ("INFO","request socket packet-info.");
			if (0 != pgm_sockaddr_pktinfo (transport->recv_sock, recv_family, TRUE) ||
			     (-1 != transport->recv_sock2 &&
			       0 != pgm_sockaddr_pktinfo (transport->recv_sock2, recv_family, TRUE)))
			{
				g_set_error (error,
					     PGM_TRANSPORT_ERROR,
					     pgm_transport_error_from_errno (errno),
					     _("Enabling receipt of control message per incoming datagram: %s"),
					     g_strerror (errno));
				g_static_rw_lock_writer_unlock (&transport->lock);
				return FALSE;
			}
		}
	}

/* buffers, set size first then re-read to confirm actual value */
	if (transport->rcvbuf)
	{
		g_trace ("INFO","set receive socket buffer size.");
		if (0 != setsockopt (transport->recv_sock, SOL_SOCKET, SO_RCVBUF, (const char*)&transport->rcvbuf, sizeof(transport->rcvbuf)) ||
		     (-1 != transport->recv_sock2 &&
		       0 != setsockopt (transport->recv_sock2, SOL_SOCKET, SO_RCVBUF, (const char*)&transport->rcvbuf, sizeof(transport->rcvbuf))))
		{
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_errno (errno),
				     _("Setting maximum socket receive buffer in bytes: %s"),
				     g_strerror (errno));
			g_static_rw_lock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}
	if (transport->sndbuf)
	{
		g_trace ("INFO","set send socket buffer size.");
		if (0 != setsockopt (transport->send_sock, SOL_SOCKET, SO_SNDBUF, (const char*)&transport->sndbuf, sizeof(transport->sndbuf)) ||
		    0 != setsockopt (transport->send_with_router_alert_sock, SOL_SOCKET, SO_SNDBUF, (const char*)&transport->sndbuf, sizeof(transport->sndbuf)))
		{
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_errno (errno),
				     _("Setting maximum socket send buffer in bytes: %s"),
				     g_strerror (errno));
			g_static_rw_lock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}

/* bind udp unicast sockets to interfaces, note multicast on a bound interface is
 * fruity on some platforms so callee should specify any interface.
 *
 * after binding default interfaces (0.0.0.0) are resolved
 */
/* TODO: different ports requires a new bound socket */

	struct sockaddr_storage recv_addr, recv_addr2;
	memset (&recv_addr, 0, sizeof(recv_addr));

#ifdef CONFIG_BIND_INADDR_ANY

/* force default interface for bind-only, source address is still valid for multicast membership.
 * effectively same as running getaddrinfo(hints = {ai_flags = AI_PASSIVE})
 */
	((struct sockaddr*)&recv_addr)->sa_family = pgm_sockaddr_family (&transport->recv_gsr[0].gsr_group);
	if (AF_INET == pgm_sockaddr_family(&recv_addr))
		((struct sockaddr_in*)&recv_addr)->sin_addr.s_addr = INADDR_ANY;
	else
		((struct sockaddr_in6*)&recv_addr)->sin6_addr = in6addr_any;
#else
	if (!_pgm_indextoaddr (transport->recv_gsr[0].gsr_interface,
			       pgm_sockaddr_family (&transport->recv_gsr[0].gsr_group),
			       pgm_sockaddr_scope_id (&transport->recv_gsr[0].gsr_group),
			       &recv_addr,
			       error))
	{
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}
	g_trace ("INFO","binding receive socket to interface index %i", transport->recv_gsr[0].gsr_interface);

#endif /* CONFIG_BIND_INADDR_ANY */

	memcpy (&recv_addr2, &recv_addr, pgm_sockaddr_len (&recv_addr));
	((struct sockaddr_in*)&recv_addr)->sin_port = g_htons (transport->udp_encap_mcast_port);
	if (0 != bind (transport->recv_sock, (struct sockaddr*)&recv_addr, pgm_sockaddr_len (&recv_addr)))
	{
		int save_errno = errno;
		if (error) {
			char addr[INET6_ADDRSTRLEN];
			pgm_sockaddr_ntop (&recv_addr, addr, sizeof(addr));
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_errno (save_errno),
				     _("Binding receive socket to address %s: %s"),
				     addr,
				     g_strerror (save_errno));
		}
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}

#ifdef TRANSPORT_DEBUG
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&recv_addr, s, sizeof(s));
		g_trace ("INFO","bind succeeded on recv_gsr[0] interface %s", s);
	}
#endif

	if (-1 != transport->recv_sock2) {
		((struct sockaddr_in*)&recv_addr2)->sin_port = g_htons (transport->udp_encap_ucast_port);
		if (0 != bind (transport->recv_sock2, (struct sockaddr*)&recv_addr2, pgm_sockaddr_len (&recv_addr2)))
		{
			int save_errno = errno;
			if (error) {
				char addr[INET6_ADDRSTRLEN];
				pgm_sockaddr_ntop (&recv_addr2, addr, sizeof(addr));
				g_set_error (error,
					     PGM_TRANSPORT_ERROR,
					     pgm_transport_error_from_errno (save_errno),
					     _("Binding receive socket2 to address %s: %s"),
					     addr,
					     g_strerror (save_errno));
			}
			g_static_rw_lock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}

/* keep a copy of the original address source to re-use for router alert bind */
	struct sockaddr_storage send_addr, send_with_router_alert_addr;
	memset (&send_addr, 0, sizeof(send_addr));

	if (!pgm_if_indextoaddr (transport->send_gsr.gsr_interface,
				  pgm_sockaddr_family (&transport->send_gsr.gsr_group),
				  pgm_sockaddr_scope_id (&transport->send_gsr.gsr_group),
				  (struct sockaddr*)&send_addr,
				  error))
	{
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}
#ifdef TRANSPORT_DEBUG
	else
	{
		g_trace ("INFO","binding send socket to interface index %u", (unsigned)transport->send_gsr.gsr_interface);
	}
#endif

	memcpy (&send_with_router_alert_addr, &send_addr, pgm_sockaddr_len (&send_addr));
	if (0 != bind (transport->send_sock, (struct sockaddr*)&send_addr, pgm_sockaddr_len (&send_addr)))
	{
		int save_errno = errno;
		char addr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&send_addr, addr, sizeof(addr));
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     pgm_transport_error_from_errno (save_errno),
			     _("Binding send socket to address %s: %s"),
			     addr,
			     g_strerror (save_errno));
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}

/* resolve bound address if 0.0.0.0 */
	if (AF_INET == pgm_sockaddr_family (&send_addr))
	{
		if ((INADDR_ANY == ((struct sockaddr_in*)&send_addr)->sin_addr.s_addr) &&
		    !pgm_if_getnodeaddr (AF_INET, (struct sockaddr*)&send_addr, sizeof(send_addr), error))
		{
			g_static_rw_lock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}
	else if ((memcmp (&in6addr_any, &((struct sockaddr_in6*)&send_addr)->sin6_addr, sizeof(in6addr_any)) == 0) &&
		 !pgm_if_getnodeaddr (AF_INET6, (struct sockaddr*)&send_addr, sizeof(send_addr), error))
	{
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}

#ifdef TRANSPORT_DEBUG
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&send_addr, s, sizeof(s));
		g_trace ("INFO","bind succeeded on send_gsr interface %s", s);
	}
#endif

	if (0 != bind (transport->send_with_router_alert_sock,
			(struct sockaddr*)&send_with_router_alert_addr,
			pgm_sockaddr_len(&send_with_router_alert_addr)))
	{
		int save_errno = errno;
		if (error) {
			char addr[INET6_ADDRSTRLEN];
			pgm_sockaddr_ntop (&send_with_router_alert_addr, addr, sizeof(addr));
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_errno (save_errno),
				     _("Binding IP Router Alert (RFC 2113) send socket to address %s: %s"),
				     addr,
				     g_strerror (save_errno));
		}
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}

#ifdef TRANSPORT_DEBUG
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&send_with_router_alert_addr, s, sizeof(s));
		g_trace ("INFO","bind (router alert) succeeded on send_gsr interface %s", s);
	}
#endif

/* save send side address for broadcasting as source nla */
	memcpy (&transport->send_addr, &send_addr, pgm_sockaddr_len (&send_addr));

/* receiving groups (multiple) */
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		const struct group_source_req* p = &transport->recv_gsr[i];

/* ASM */
		if (0 == pgm_sockaddr_cmp ((const struct sockaddr*)&p->gsr_group, (const struct sockaddr*)&p->gsr_source))
		{
			if (0 != pgm_sockaddr_join_group (transport->recv_sock,
							  pgm_sockaddr_family (&p->gsr_group),
							  (const struct group_req*)p))
			{
				const int save_errno = errno;
				char group_addr[INET_ADDRSTRLEN];
				char ifname[IF_NAMESIZE];
				pgm_sockaddr_ntop (&p->gsr_group, group_addr, sizeof(group_addr));
				if (0 == p->gsr_interface)
					g_set_error (error,
						     PGM_TRANSPORT_ERROR,
						     pgm_transport_error_from_errno (save_errno),
						     _("Joining multicast group %s: %s"),
						     group_addr,
						     g_strerror (save_errno));
				else
					g_set_error (error,
						     PGM_TRANSPORT_ERROR,
						     pgm_transport_error_from_errno (save_errno),
						     _("Joining multicast group %s on interface %s: %s"),
						     group_addr,
						     if_indextoname (p->gsr_interface, ifname),
						     g_strerror (save_errno));
				g_static_rw_lock_writer_unlock (&transport->lock);
				return FALSE;
			}
#ifdef TRANSPORT_DEBUG
			else
			{
				char s1[INET6_ADDRSTRLEN];
				pgm_sockaddr_ntop (&p->gsr_group, s1, sizeof(s1));
				g_trace ("INFO","MCAST_JOIN_GROUP succeeded on recv_gsr[%i] interface %u group %s",
					i, (unsigned)p->gsr_interface, s1);
			}
#endif /* TRANSPORT_DEBUG */
		}
		else /* source != group, i.e. SSM */
		{
			if (0 != pgm_sockaddr_join_source_group (transport->recv_sock,
								 pgm_sockaddr_family (&p->gsr_group),
								 p))
			{
				const int save_errno = errno;
				char source_addr[INET_ADDRSTRLEN];
				char group_addr[INET_ADDRSTRLEN];
				pgm_sockaddr_ntop (&p->gsr_source, source_addr, sizeof(source_addr));
				pgm_sockaddr_ntop (&p->gsr_group, group_addr, sizeof(group_addr));
				g_set_error (error,
					     PGM_TRANSPORT_ERROR,
					     pgm_transport_error_from_errno (save_errno),
					     _("Joining multicast group %s from source %s: %s"),
					     group_addr,
					     source_addr,
					     g_strerror (save_errno));
				g_static_rw_lock_writer_unlock (&transport->lock);
				return FALSE;
			}
#ifdef TRANSPORT_DEBUG
			else
			{
				char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
				pgm_sockaddr_ntop (&p->gsr_group, s1, sizeof(s1));
				pgm_sockaddr_ntop (&p->gsr_source, s2, sizeof(s2));
				g_trace ("INFO","MCAST_JOIN_SOURCE_GROUP succeeded on recv_gsr[%i] interface %u group %s source %s",
					i, (unsigned)p->gsr_interface, s1, s2);
			}
#endif /* TRANSPORT_DEBUG */
		}
	}

/* send group (singular) */
	if (0 != pgm_sockaddr_multicast_if (transport->send_sock,
					    (struct sockaddr*)&transport->send_addr,
					    transport->send_gsr.gsr_interface))
	{
		const int save_errno = errno;
		char ifname[IF_NAMESIZE];
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     pgm_transport_error_from_errno (save_errno),
			     _("Setting device %s for multicast send socket: %s"),
			     if_indextoname (transport->send_gsr.gsr_interface, ifname),
			     g_strerror (save_errno));
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}
#ifdef TRANSPORT_DEBUG
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_addr, s, sizeof(s));
		g_trace ("INFO","pgm_sockaddr_multicast_if succeeded on send_gsr address %s interface %u",
					s, (unsigned)transport->send_gsr.gsr_interface);
	}
#endif
	if (0 != pgm_sockaddr_multicast_if (transport->send_with_router_alert_sock,
					    (struct sockaddr*)&transport->send_addr,
					    transport->send_gsr.gsr_interface))
	{
		const int save_errno = errno;
		char ifname[IF_NAMESIZE];
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     pgm_transport_error_from_errno (save_errno),
			     _("Setting device %s for multicast IP Router Alert (RFC 2113) send socket: %s"),
			     if_indextoname (transport->send_gsr.gsr_interface, ifname),
			     g_strerror (save_errno));
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}
#ifdef TRANSPORT_DEBUG
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop (&transport->send_addr, s, sizeof(s));
		g_trace ("INFO","pgm_sockaddr_multicast_if (router alert) succeeded on send_gsr address %s interface %u",
					s, (unsigned)transport->send_gsr.gsr_interface);
	}
#endif

/* multicast loopback */
	g_trace ("INFO","set multicast loopback.");
	if (0 != pgm_sockaddr_multicast_loop (transport->send_sock,
					      pgm_sockaddr_family (&transport->send_gsr.gsr_group),
					      transport->use_multicast_loop) ||
	    0 != pgm_sockaddr_multicast_loop (transport->send_with_router_alert_sock,
					      pgm_sockaddr_family (&transport->send_gsr.gsr_group),
					      transport->use_multicast_loop))
	{
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     pgm_transport_error_from_errno (errno),
			     _("Setting multicast loopback: %s"),
			     g_strerror (errno));
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}

/* multicast ttl: many crappy network devices go CPU ape with TTL=1, 16 is a popular alternative */
	g_trace ("INFO","set multicast hop limit.");
	if (0 != pgm_sockaddr_multicast_hops (transport->send_sock,
					      pgm_sockaddr_family (&transport->send_gsr.gsr_group),
					      transport->hops) ||
	    0 != pgm_sockaddr_multicast_hops (transport->send_with_router_alert_sock,
					      pgm_sockaddr_family (&transport->send_gsr.gsr_group),
					      transport->hops))
	{
		g_set_error (error,
			     PGM_TRANSPORT_ERROR,
			     pgm_transport_error_from_errno (errno),
			     _("Setting multicast hop limit to %i: %s"),
			     transport->hops,
			     g_strerror (errno));
		g_static_rw_lock_writer_unlock (&transport->lock);
		return FALSE;
	}

/* set Expedited Forwarding PHB for network elements, no ECN.
 * 
 * codepoint 101110 (RFC 3246)
 */
	g_trace ("INFO","set packet differentiated services field to expedited forwarding.");
	const int dscp = 0x2e << 2;
	if (0 != pgm_sockaddr_tos (transport->send_sock,
				   pgm_sockaddr_family (&transport->send_gsr.gsr_group),
				   dscp) ||
	    0 != pgm_sockaddr_tos (transport->send_with_router_alert_sock,
				   pgm_sockaddr_family (&transport->send_gsr.gsr_group),
				   dscp))
	{
		g_warning ("DSCP setting requires CAP_NET_ADMIN or ADMIN capability.");
		goto no_cap_net_admin;
	}

no_cap_net_admin:

/* rx to nak processor notify channel */
	if (transport->can_send_data)
	{
/* setup rate control */
		if (transport->txw_max_rte)
		{
			g_trace ("INFO","setting rate regulation to %i bytes per second.",
					transport->txw_max_rte);
	
			pgm_rate_create (&transport->rate_control, transport->txw_max_rte, transport->iphdr_len, transport->max_tpdu);
			g_assert (NULL != transport->rate_control);
		}

/* announce new transport by sending out SPMs */
		if (!pgm_send_spm (transport, PGM_OPT_SYN) ||
		    !pgm_send_spm (transport, PGM_OPT_SYN) ||
		    !pgm_send_spm (transport, PGM_OPT_SYN))
		{
			g_set_error (error,
				     PGM_TRANSPORT_ERROR,
				     pgm_transport_error_from_errno (errno),
				     _("Sending SPM broadcast: %s"),
				     g_strerror (errno));
			g_static_rw_lock_writer_unlock (&transport->lock);
			return FALSE;
		}

		transport->next_poll = transport->next_ambient_spm = pgm_time_update_now() + transport->spm_ambient_interval;
	}
	else
	{
		g_assert (transport->can_recv_data);
		transport->next_poll = pgm_time_update_now() + pgm_secs( 30 );
	}

/* non-blocking sockets */
	g_trace ("INFO","set %s sockets",
		transport->is_nonblocking ? "non-blocking" : "blocking");
	pgm_sockaddr_nonblocking (transport->recv_sock, transport->is_nonblocking);
	if (-1 != transport->recv_sock2)
		pgm_sockaddr_nonblocking (transport->recv_sock2, transport->is_nonblocking);
	pgm_sockaddr_nonblocking (transport->send_sock, transport->is_nonblocking);
	pgm_sockaddr_nonblocking (transport->send_with_router_alert_sock, transport->is_nonblocking);

/* allocate first incoming packet buffer */
	transport->rx_buffer = pgm_alloc_skb (transport->max_tpdu);

/* cleanup */
	g_trace ("INFO","preparing dynamic timer");
	pgm_timer_prepare (transport);

	transport->is_bound = TRUE;
	g_static_rw_lock_writer_unlock (&transport->lock);
	g_trace ("INFO","transport successfully created.");
	return TRUE;
}

/* returns timeout for pending timer.
 */

gboolean
pgm_transport_get_timer_pending (
	pgm_transport_t* const	transport,
	struct timeval*		tv
	)
{
	g_return_val_if_fail (NULL != transport, FALSE);
	g_return_val_if_fail (NULL != tv, FALSE);
	const long usecs = pgm_timer_expiration (transport);
	tv->tv_sec  = usecs / 1000000UL;
	tv->tv_usec = usecs % 1000000UL;
	return TRUE;
}

/* returns timeout for blocking sends, PGM_IO_STATUS_AGAIN2 on recv()
 * or PGM_IO_STATUS_AGAIN on send().
 */

gboolean
pgm_transport_get_rate_remaining (
	pgm_transport_t* const	transport,
	struct timeval*		tv
	)
{
	g_return_val_if_fail (NULL != transport, FALSE);
	g_return_val_if_fail (NULL != tv, FALSE);
	const pgm_time_t pgm_time = pgm_rate_remaining (transport->rate_control, transport->blocklen);
	tv->tv_sec  = pgm_time / 1000000UL;
	tv->tv_usec = pgm_time % 1000000UL;
	return TRUE;
}

/* add select parameters for the transports receive socket(s)
 *
 * returns highest file descriptor used plus one.
 */

int
pgm_transport_select_info (
	pgm_transport_t* const	transport,
	fd_set* const		readfds,	/* blocking recv fds */
	fd_set* const		writefds,	/* blocking send fds */
	int* const		n_fds		/* in: max fds, out: max (in:fds, transport:fds) */
	)
{
	int fds = 0;

	g_assert (transport);
	g_assert (n_fds);

	if (!transport->is_bound ||
	    transport->is_destroyed)
	{
		errno = EBADF;
		return -1;
	}

	if (readfds)
	{
		FD_SET(transport->recv_sock, readfds);
		fds = transport->recv_sock + 1;
		if (-1 != transport->recv_sock2) {
			FD_SET(transport->recv_sock2, readfds);
			fds = MAX(fds, transport->recv_sock2 + 1);
		}
		if (transport->can_send_data) {
			const int rdata_fd = pgm_notify_get_fd (&transport->rdata_notify);
			FD_SET(rdata_fd, readfds);
			fds = MAX(fds, rdata_fd + 1);
		}
		if (transport->can_recv_data) {
			const int pending_fd = pgm_notify_get_fd (&transport->pending_notify);
			FD_SET(pending_fd, readfds);
			fds = MAX(fds, pending_fd + 1);
		}
	}

	if (transport->can_send_data && writefds)
	{
		FD_SET(transport->send_sock, writefds);
		fds = MAX(transport->send_sock + 1, fds);
	}

	return *n_fds = MAX(fds, *n_fds);
}

#ifdef CONFIG_HAVE_POLL
/* add poll parameters for this transports receive socket(s)
 *
 * returns number of pollfd structures filled.
 */

int
pgm_transport_poll_info (
	pgm_transport_t* const	transport,
	struct pollfd* const	fds,
	int* const		n_fds,		/* in: #fds, out: used #fds */
	const int		events		/* POLLIN, POLLOUT */
	)
{
	g_assert (transport);
	g_assert (fds);
	g_assert (n_fds);

	if (!transport->is_bound ||
	    transport->is_destroyed)
	{
		errno = EBADF;
		return -1;
	}

	int moo = 0;

/* we currently only support one incoming socket */
	if (events & POLLIN)
	{
		g_assert ( (1 + moo) <= *n_fds );
		fds[moo].fd = transport->recv_sock;
		fds[moo].events = POLLIN;
		moo++;
		if (-1 != transport->recv_sock2) {
			g_assert ( (1 + moo) <= *n_fds );
			fds[moo].fd = transport->recv_sock2;
			fds[moo].events = POLLIN;
			moo++;
		}
		if (transport->can_send_data) {
			g_assert ( (1 + moo) <= *n_fds );
			fds[moo].fd = pgm_notify_get_fd (&transport->rdata_notify);
			fds[moo].events = POLLIN;
			moo++;
		}
		if (transport->can_recv_data) {
			g_assert ( (1 + moo) <= *n_fds );
			fds[moo].fd = pgm_notify_get_fd (&transport->pending_notify);
			fds[moo].events = POLLIN;
			moo++;
		}
	}

/* ODATA only published on regular socket, no need to poll router-alert sock */
	if (transport->can_send_data && events & POLLOUT)
	{
		g_assert ( (1 + moo) <= *n_fds );
		fds[moo].fd = transport->send_sock;
		fds[moo].events = POLLOUT;
		moo++;
	}

	return *n_fds = moo;
}
#endif /* CONFIG_HAVE_POLL */

/* add epoll parameters for this transports recieve socket(s), events should
 * be set to EPOLLIN to wait for incoming events (data), and EPOLLOUT to wait
 * for non-blocking write.
 *
 * returns 0 on success, -1 on failure and sets errno appropriately.
 */
#ifdef CONFIG_HAVE_EPOLL
int
pgm_transport_epoll_ctl (
	pgm_transport_t* const	transport,
	const int		epfd,
	const int		op,		/* EPOLL_CTL_ADD, ... */
	const int		events		/* EPOLLIN, EPOLLOUT */
	)
{
	if (!(op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD))
	{
		errno = EINVAL;
		return -1;
	}
	else if (!transport->is_bound || transport->is_destroyed)
	{
		errno = EBADF;
		return -1;
	}

	struct epoll_event event;
	int retval = 0;

	if (events & EPOLLIN)
	{
		event.events = events & (EPOLLIN | EPOLLET | EPOLLONESHOT);
		event.data.ptr = transport;
		retval = epoll_ctl (epfd, op, transport->recv_sock, &event);
		if (retval)
			goto out;

		if (-1 != transport->recv_sock2) {
			retval = epoll_ctl (epfd, op, transport->recv_sock2, &event);
			if (retval)
				goto out;
		}
		if (transport->can_send_data) {
			retval = epoll_ctl (epfd, op, pgm_notify_get_fd (&transport->rdata_notify), &event);
			if (retval)
				goto out;
		}
		if (transport->can_recv_data) {
			retval = epoll_ctl (epfd, op, pgm_notify_get_fd (&transport->pending_notify), &event);
			if (retval)
				goto out;
		}

		if (events & EPOLLET)
			transport->is_edge_triggered_recv = TRUE;
	}

	if (transport->can_send_data && events & EPOLLOUT)
	{
		event.events = events & (EPOLLOUT | EPOLLET | EPOLLONESHOT);
		event.data.ptr = transport;
		retval = epoll_ctl (epfd, op, transport->send_sock, &event);
	}
out:
	return retval;
}
#endif

/* Enable FEC for this transport, specifically Reed Solmon encoding RS(n,k), common
 * setting is RS(255, 223).
 *
 * inputs:
 *
 * n = FEC Block size = [k+1, 255]
 * k = original data packets == transmission group size = [2, 4, 8, 16, 32, 64, 128]
 * m = symbol size = 8 bits
 *
 * outputs:
 *
 * h = 2t = n - k = parity packets
 *
 * when h > k parity packets can be lost.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

gboolean
pgm_transport_set_fec (
	pgm_transport_t* const	transport,
	const guint		proactive_h,		/* 0 == no pro-active parity */
	const gboolean		use_ondemand_parity,
	const gboolean		use_varpkt_len,
	const guint		default_n,
	const guint		default_k
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail ((default_k & (default_k -1)) == 0, FALSE);
	g_return_val_if_fail (default_k >= 2 && default_k <= 128, FALSE);
	g_return_val_if_fail (default_n >= default_k + 1 && default_n <= 255, FALSE);

	const guint default_h = default_n - default_k;

	g_return_val_if_fail (proactive_h <= default_h, FALSE);

/* check validity of parameters */
	if ( default_k > 223 &&
		( (default_h * 223.0) / default_k ) < 1.0 )
	{
		g_error ("k/h ratio too low to generate parity data.");
		return FALSE;
	}

	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}

	transport->use_proactive_parity	= proactive_h > 0;
	transport->use_ondemand_parity	= use_ondemand_parity;
	transport->use_varpkt_len	= use_varpkt_len;
	transport->rs_n			= default_n;
	transport->rs_k			= default_k;
	transport->rs_proactive_h	= proactive_h;

	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* declare transport only for sending, discard any incoming SPM, ODATA,
 * RDATA, etc, packets.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */
gboolean
pgm_transport_set_send_only (
	pgm_transport_t* const	transport,
	const gboolean		send_only
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->can_recv_data	= !send_only;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* declare transport only for receiving, no transmit window will be created
 * and no SPM broadcasts sent.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */
gboolean
pgm_transport_set_recv_only (
	pgm_transport_t* const	transport,
	const gboolean		recv_only,
	const gboolean		is_passive	/* don't send any request or responses */
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->can_send_data	= !recv_only;
	transport->can_send_nak		= !is_passive;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* on unrecoverable data loss stop transport from further transmission and
 * receiving.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */
gboolean
pgm_transport_set_abort_on_reset (
	pgm_transport_t* const	transport,
	const gboolean		abort_on_reset
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->is_abort_on_reset = abort_on_reset;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* default non-blocking operation on send and receive sockets.
 */
gboolean
pgm_transport_set_nonblocking (
	pgm_transport_t* const	transport,
	const gboolean		nonblocking
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->is_nonblocking = nonblocking;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}


#define SOCKADDR_TO_LEVEL(sa)	( (AF_INET == pgm_sockaddr_family((sa))) ? IPPROTO_IP : IPPROTO_IPV6 )
#define TRANSPORT_TO_LEVEL(t)	SOCKADDR_TO_LEVEL( &(t)->recv_gsr[0].gsr_group )


/* for any-source applications (ASM), join a new group
 */

gboolean
pgm_transport_join_group (
	pgm_transport_t*	transport,
	struct group_req*	gr,
	gsize			len
	)
{
	int status;

	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (gr != NULL, FALSE);
	g_return_val_if_fail (sizeof(struct group_req) == len, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed ||
	    transport->recv_gsr_len > IP_MAX_MEMBERSHIPS)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}

/* verify not duplicate group/interface pairing */
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((struct sockaddr*)&gr->gr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_group)  == 0 &&
		    pgm_sockaddr_cmp ((struct sockaddr*)&gr->gr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_source) == 0 &&
			(gr->gr_interface == transport->recv_gsr[i].gsr_interface ||
			                0 == transport->recv_gsr[i].gsr_interface    )
                   )
		{
#ifdef TRANSPORT_DEBUG
			char s[INET6_ADDRSTRLEN];
			pgm_sockaddr_ntop (&gr->gr_group, s, sizeof(s));
			if (transport->recv_gsr[i].gsr_interface) {
				g_trace("INFO", "transport has already joined group %s on interface %u", s, (unsigned)gr->gr_interface);
			} else {
				g_trace("INFO", "transport has already joined group %s on all interfaces.", s);
			}
#endif
			g_static_rw_lock_reader_unlock (&transport->lock);
			return FALSE;
		}
	}

	transport->recv_gsr[transport->recv_gsr_len].gsr_interface = 0;
	memcpy (&transport->recv_gsr[transport->recv_gsr_len].gsr_group, &gr->gr_group, pgm_sockaddr_len(&gr->gr_group));
	memcpy (&transport->recv_gsr[transport->recv_gsr_len].gsr_source, &gr->gr_group, pgm_sockaddr_len(&gr->gr_group));
	transport->recv_gsr_len++;
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_JOIN_GROUP, (const char*)gr, len);
	g_static_rw_lock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for any-source applications (ASM), leave a joined group.
 */

gboolean
pgm_transport_leave_group (
	pgm_transport_t*	transport,
	struct group_req*	gr,
	gsize			len
	)
{
	int status;

	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (gr != NULL, FALSE);
	g_return_val_if_fail (sizeof(struct group_req) == len, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed ||
	    transport->recv_gsr_len == 0)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}

	for (unsigned i = 0; i < transport->recv_gsr_len;)
	{
		if ((pgm_sockaddr_cmp ((struct sockaddr*)&gr->gr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_group) == 0) &&
/* drop all matching receiver entries */
		            (gr->gr_interface == 0 ||
/* drop all sources with matching interface */
			     gr->gr_interface == transport->recv_gsr[i].gsr_interface) )
		{
			transport->recv_gsr_len--;
			if (i < (IP_MAX_MEMBERSHIPS-1))
			{
				memmove (&transport->recv_gsr[i], &transport->recv_gsr[i+1], (transport->recv_gsr_len - i) * sizeof(struct group_source_req));
				continue;
			}
		}
		i++;
	}
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_LEAVE_GROUP, (const char*)gr, len);
	g_static_rw_lock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for any-source applications (ASM), turn off a given source
 */

gboolean
pgm_transport_block_source (
	pgm_transport_t*	transport,
	struct group_source_req* gsr,
	gsize			len
	)
{
	int status;

	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (gsr != NULL, FALSE);
	g_return_val_if_fail (sizeof(struct group_source_req) == len, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_BLOCK_SOURCE, (const char*)gsr, len);
	g_static_rw_lock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for any-source applications (ASM), re-allow a blocked source
 */

gboolean
pgm_transport_unblock_source (
	pgm_transport_t*	transport,
	struct group_source_req* gsr,
	gsize			len
	)
{
	int status;

	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (gsr != NULL, FALSE);
	g_return_val_if_fail (sizeof(struct group_source_req) == len, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_UNBLOCK_SOURCE, (const char*)gsr, len);
	g_static_rw_lock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for controlled-source applications (SSM), join each group/source pair.
 *
 * SSM joins are allowed on top of ASM in order to merge a remote source onto the local segment.
 */

gboolean
pgm_transport_join_source_group (
	pgm_transport_t*	transport,
	struct group_source_req* gsr,
	gsize			len
	)
{
	int status;

	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (gsr != NULL, FALSE);
	g_return_val_if_fail (sizeof(struct group_source_req) == len, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed ||
	    transport->recv_gsr_len > IP_MAX_MEMBERSHIPS)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}

/* verify if existing group/interface pairing */
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_group) == 0 &&
			(gsr->gsr_interface == transport->recv_gsr[i].gsr_interface ||
			                  0 == transport->recv_gsr[i].gsr_interface    )
                   )
		{
			if (pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_source, (struct sockaddr*)&transport->recv_gsr[i].gsr_source) == 0)
			{
#ifdef TRANSPORT_DEBUG
				char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
				pgm_sockaddr_ntop (&gsr->gsr_group, s1, sizeof(s1));
				pgm_sockaddr_ntop (&gsr->gsr_source, s2, sizeof(s2));
				if (transport->recv_gsr[i].gsr_interface) {
					g_trace("INFO", "transport has already joined group %s from source %s on interface %u",
						s1, s2, (unsigned)gsr->gsr_interface);
				} else {
					g_trace("INFO", "transport has already joined group %s from source %s on all interfaces",
						s1, s2);
				}
#endif
				g_static_rw_lock_reader_unlock (&transport->lock);
				return FALSE;
			}
			break;
		}
	}

	memcpy (&transport->recv_gsr[transport->recv_gsr_len], &gsr, sizeof(struct group_source_req));
	transport->recv_gsr_len++;
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_JOIN_SOURCE_GROUP, (const char*)gsr, len);
	g_static_rw_lock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for controlled-source applications (SSM), leave each group/source pair
 */

gboolean
pgm_transport_leave_source_group (
	pgm_transport_t*	transport,
	struct group_source_req* gsr,
	gsize			len
	)
{
	int status;

	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (gsr != NULL, FALSE);
	g_return_val_if_fail (sizeof(struct group_source_req) == len, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed ||
	    transport->recv_gsr_len == 0)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}

/* verify if existing group/interface pairing */
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_group)   == 0 &&
		    pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_source, (struct sockaddr*)&transport->recv_gsr[i].gsr_source) == 0 &&
		    gsr->gsr_interface == transport->recv_gsr[i].gsr_interface)
		{
			transport->recv_gsr_len--;
			if (i < (IP_MAX_MEMBERSHIPS-1))
			{
				memmove (&transport->recv_gsr[i], &transport->recv_gsr[i+1], (transport->recv_gsr_len - i) * sizeof(struct group_source_req));
				break;
			}
		}
	}

	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_LEAVE_SOURCE_GROUP, (const char*)gsr, len);
	g_static_rw_lock_reader_unlock (&transport->lock);
	return (0 == status);
}

gboolean
pgm_transport_msfilter (
	pgm_transport_t*	transport,
	struct group_filter*	gf_list,
	gsize			len
	)
{
	int status;

	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (gf_list != NULL, -EINVAL);
	g_return_val_if_fail (len > 0, -EINVAL);
	g_return_val_if_fail (GROUP_FILTER_SIZE(gf_list->gf_numsrc) == len, -EINVAL);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_MSFILTER, (const char*)gf_list, len);
	g_static_rw_lock_reader_unlock (&transport->lock);
	return (0 == status);
}

GQuark
pgm_transport_error_quark (void)
{
	return g_quark_from_static_string ("pgm-transport-error-quark");
}

PGMTransportError
pgm_transport_error_from_errno (
	gint		err_no
        )
{
	switch (err_no) {
#ifdef EFAULT
	case EFAULT:
		return PGM_TRANSPORT_ERROR_FAULT;
		break;
#endif

#ifdef EINVAL
	case EINVAL:
		return PGM_TRANSPORT_ERROR_INVAL;
		break;
#endif

#ifdef EPERM
	case EPERM:
		return PGM_TRANSPORT_ERROR_PERM;
		break;
#endif

#ifdef EMFILE
	case EMFILE:
		return PGM_TRANSPORT_ERROR_MFILE;
		break;
#endif

#ifdef ENFILE
	case ENFILE:
		return PGM_TRANSPORT_ERROR_NFILE;
		break;
#endif

#ifdef ENODEV
	case ENODEV:
		return PGM_TRANSPORT_ERROR_NODEV;
		break;
#endif

#ifdef ENOMEM
	case ENOMEM:
		return PGM_TRANSPORT_ERROR_NOMEM;
		break;
#endif

#ifdef ENOPROTOOPT
	case ENOPROTOOPT:
		return PGM_TRANSPORT_ERROR_NOPROTOOPT;
		break;
#endif

	default :
		return PGM_TRANSPORT_ERROR_FAILED;
		break;
	}
}

PGMTransportError
pgm_transport_error_from_eai_errno (
	gint		err_no
        )
{
	switch (err_no) {
#ifdef EAI_ADDRFAMILY
	case EAI_ADDRFAMILY:
		return PGM_TRANSPORT_ERROR_ADDRFAMILY;
		break;
#endif

#ifdef EAI_AGAIN
	case EAI_AGAIN:
		return PGM_TRANSPORT_ERROR_AGAIN;
		break;
#endif

#ifdef EAI_BADFLAGS
	case EAI_BADFLAGS:
		return PGM_TRANSPORT_ERROR_BADFLAGS;
		break;
#endif

#ifdef EAI_FAIL
	case EAI_FAIL:
		return PGM_TRANSPORT_ERROR_FAIL;
		break;
#endif

#ifdef EAI_FAMILY
	case EAI_FAMILY:
		return PGM_TRANSPORT_ERROR_FAMILY;
		break;
#endif

#ifdef EAI_MEMORY
	case EAI_MEMORY:
		return PGM_TRANSPORT_ERROR_MEMORY;
		break;
#endif

#ifdef EAI_NODATA
	case EAI_NODATA:
		return PGM_TRANSPORT_ERROR_NODATA;
		break;
#endif

#ifdef EAI_NONAME
	case EAI_NONAME:
		return PGM_TRANSPORT_ERROR_NONAME;
		break;
#endif
#ifdef EAI_SERVICE
	case EAI_SERVICE:
		return PGM_TRANSPORT_ERROR_SERVICE;
		break;
#endif

#ifdef EAI_SOCKTYPE
	case EAI_SOCKTYPE:
		return PGM_TRANSPORT_ERROR_SOCKTYPE;
		break;
#endif

#ifdef EAI_SYSTEM
	case EAI_SYSTEM:
		return pgm_if_error_from_errno (errno);
		break;
#endif

	default :
		return PGM_TRANSPORT_ERROR_FAILED;
		break;
	}
}

PGMTransportError
pgm_transport_error_from_wsa_errno (
	gint		err_no
        )
{
	switch (err_no) {
#ifdef WSAEINVAL
	case WSAEINVAL:
		return PGM_TRANSPORT_ERROR_INVAL;
		break;
#endif
#ifdef WSAEMFILE
	case WSAEMFILE:
		return PGM_TRANSPORT_ERROR_MFILE;
		break;
#endif
#ifdef WSA_NOT_ENOUGH_MEMORY
	case WSA_NOT_ENOUGH_MEMORY:
		return PGM_TRANSPORT_ERROR_NOMEM;
		break;
#endif
#ifdef WSAENOPROTOOPT
	case WSAENOPROTOOPT:
		return PGM_TRANSPORT_ERROR_NOPROTOOPT;
		break;
#endif
#ifdef WSAECONNRESET
	case WSAECONNRESET:
		return PGM_TRANSPORT_ERROR_CONNRESET;
		break;
#endif

	default :
		return PGM_TRANSPORT_ERROR_FAILED;
		break;
	}
}

/* eof */
