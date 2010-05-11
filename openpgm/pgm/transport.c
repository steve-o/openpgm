/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM transport: manage incoming & outgoing sockets with ambient SPMs, 
 * transmit & receive windows.
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

#include <errno.h>
#ifdef CONFIG_HAVE_POLL
#	include <poll.h>
#endif
#ifdef CONFIG_HAVE_EPOLL
#	include <sys/epoll.h>
#endif
#include <stdio.h>
#include <pgm/i18n.h>
#include <pgm/framework.h>
#include "pgm/transport.h"
#include "pgm/receiver.h"
#include "pgm/source.h"
#include "pgm/timer.h"


//#define TRANSPORT_DEBUG
//#define TRANSPORT_SPM_DEBUG


#ifndef GROUP_FILTER_SIZE
#	define GROUP_FILTER_SIZE(numsrc) (sizeof (struct group_filter) \
					  - sizeof (struct sockaddr_storage)         \
					  + ((numsrc)                                \
					     * sizeof (struct sockaddr_storage)))
#endif


/* global locals */
pgm_rwlock_t pgm_transport_list_lock;		/* list of all transports for admin interfaces */
pgm_slist_t* pgm_transport_list = NULL;


size_t
pgm_transport_pkt_offset2 (
	bool		can_fragment,
	bool		use_pgmcc
	)
{
	static const size_t data_size = sizeof(struct pgm_header) + sizeof(struct pgm_data);
	size_t pkt_size = data_size;
	if (can_fragment || use_pgmcc)
		pkt_size += sizeof(struct pgm_opt_length) + sizeof(struct pgm_opt_header);
	if (can_fragment)
		pkt_size += sizeof(struct pgm_opt_fragment);
	if (use_pgmcc)
		pkt_size += sizeof(struct pgm_opt_cc_data);
	return pkt_size;
}

/* destroy a pgm_transport object and contents, if last transport also destroy
 * associated event loop
 *
 * outstanding locks:
 * 1) pgm_transport_t::lock
 * 2) pgm_transport_t::receiver_mutex
 * 3) pgm_transport_t::source_mutex
 * 4) pgm_transport_t::txw_spinlock
 * 5) pgm_transport_t::timer_mutex
 *
 * If application calls a function on the transport after destroy() it is a
 * programmer error: segv likely to occur on unlock.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_destroy (
	pgm_transport_t*	transport,
	bool			flush
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	pgm_return_val_if_fail (!transport->is_destroyed, FALSE);
	pgm_debug ("pgm_transport_destroy (transport:%p flush:%s)",
		(const void*)transport,
		flush ? "TRUE":"FALSE");
/* flag existing calls */
	transport->is_destroyed = TRUE;
/* cancel running blocking operations */
	if (-1 != transport->recv_sock) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Closing receive socket."));
#ifndef _WIN32
		close (transport->recv_sock);
#else
		closesocket (transport->recv_sock);
#endif
		transport->recv_sock = -1;
	}
	if (-1 != transport->send_sock) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Closing send socket."));
#ifndef _WIN32
		close (transport->send_sock);
#else
		closesocket (transport->send_sock);
#endif
		transport->send_sock = -1;
	}
	pgm_rwlock_reader_unlock (&transport->lock);
	pgm_debug ("blocking on destroy lock ...");
	pgm_rwlock_writer_lock (&transport->lock);

	pgm_debug ("removing transport from inventory.");
	pgm_rwlock_writer_lock (&pgm_transport_list_lock);
	pgm_transport_list = pgm_slist_remove (pgm_transport_list, transport);
	pgm_rwlock_writer_unlock (&pgm_transport_list_lock);

/* flush source side by sending heartbeat SPMs */
	if (transport->can_send_data &&
	    transport->is_bound && 
	    flush)
	{
		pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Flushing PGM source with session finish option broadcast SPMs."));
		if (!pgm_send_spm (transport, PGM_OPT_FIN) ||
		    !pgm_send_spm (transport, PGM_OPT_FIN) ||
		    !pgm_send_spm (transport, PGM_OPT_FIN))
		{
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Failed to send flushing SPMs."));
		}
	}

	if (transport->peers_hashtable) {
		pgm_debug ("destroying peer lookup table.");
		pgm_hashtable_destroy (transport->peers_hashtable);
		transport->peers_hashtable = NULL;
	}
	if (transport->peers_list) {
		pgm_debug ("destroying peer list.");
		do {
			pgm_list_t* next = transport->peers_list->next;
			pgm_peer_unref ((pgm_peer_t*)transport->peers_list->data);

			transport->peers_list = next;
		} while (transport->peers_list);
	}

	if (transport->window) {
		pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Destroying transmit window."));
		pgm_txw_shutdown (transport->window);
		transport->window = NULL;
	}
	pgm_trace (PGM_LOG_ROLE_RATE_CONTROL,_("Destroying rate control."));
	pgm_rate_destroy (&transport->rate_control);
	if (transport->send_with_router_alert_sock) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Closing send with router alert socket."));
#ifndef _WIN32
		close (transport->send_with_router_alert_sock);
#else
		closesocket (transport->send_with_router_alert_sock);
#endif
		transport->send_with_router_alert_sock = 0;
	}
	if (transport->spm_heartbeat_interval) {
		pgm_debug ("freeing SPM heartbeat interval data.");
		pgm_free (transport->spm_heartbeat_interval);
		transport->spm_heartbeat_interval = NULL;
	}
	if (transport->rx_buffer) {
		pgm_debug ("freeing receive buffer.");
		pgm_free_skb (transport->rx_buffer);
		transport->rx_buffer = NULL;
	}
	pgm_debug ("destroying notification channel.");
	pgm_notify_destroy (&transport->pending_notify);
	pgm_debug ("freeing transport locks.");
	pgm_rwlock_free (&transport->peers_lock);
	pgm_spinlock_free (&transport->txw_spinlock);
	pgm_mutex_free (&transport->send_mutex);
	pgm_mutex_free (&transport->timer_mutex);
	pgm_mutex_free (&transport->source_mutex);
	pgm_mutex_free (&transport->receiver_mutex);
	pgm_rwlock_writer_unlock (&transport->lock);
	pgm_rwlock_free (&transport->lock);
	pgm_debug ("freeing transport data.");
	pgm_free (transport);
	pgm_debug ("finished.");
	return TRUE;
}

/* Create a pgm_transport object.  Create sockets that require superuser
 * priviledges.  If interface ports are specified then UDP encapsulation will
 * be used instead of raw protocol.
 *
 * If send == recv only two sockets need to be created iff ip headers are not
 * required (IPv6).
 *
 * All receiver addresses must be the same family.
 * interface and multiaddr must be the same family.
 * family cannot be AF_UNSPEC!
 *
 * returns TRUE on success, or FALSE on error and sets error appropriately.
 */

#if ( AF_INET != PF_INET ) || ( AF_INET6 != PF_INET6 )
#error AF_INET and PF_INET are different values, the bananas are jumping in their pyjamas!
#endif

bool
pgm_transport_create (
	pgm_transport_t**	     restrict transport,
	struct pgm_transport_info_t* restrict tinfo,
	pgm_error_t**		     restrict error
	)
{
	pgm_transport_t* new_transport;

	pgm_return_val_if_fail (NULL != transport, FALSE);
	pgm_return_val_if_fail (NULL != tinfo, FALSE);
	if (tinfo->ti_sport) pgm_return_val_if_fail (tinfo->ti_sport != tinfo->ti_dport, FALSE);
	if (tinfo->ti_udp_encap_ucast_port)
		pgm_return_val_if_fail (tinfo->ti_udp_encap_mcast_port, FALSE);
	else if (tinfo->ti_udp_encap_mcast_port)
		pgm_return_val_if_fail (tinfo->ti_udp_encap_ucast_port, FALSE);
	pgm_return_val_if_fail (tinfo->ti_recv_addrs_len > 0, FALSE);
#ifdef CONFIG_TARGET_WINE
	pgm_return_val_if_fail (tinfo->ti_recv_addrs_len == 1, FALSE);
#endif
	pgm_return_val_if_fail (tinfo->ti_recv_addrs_len <= IP_MAX_MEMBERSHIPS, FALSE);
	pgm_return_val_if_fail (NULL != tinfo->ti_recv_addrs, FALSE);
	pgm_return_val_if_fail (1 == tinfo->ti_send_addrs_len, FALSE);
	pgm_return_val_if_fail (NULL != tinfo->ti_send_addrs, FALSE);
	for (unsigned i = 0; i < tinfo->ti_recv_addrs_len; i++)
	{
		pgm_return_val_if_fail (tinfo->ti_recv_addrs[i].gsr_group.ss_family == tinfo->ti_recv_addrs[0].gsr_group.ss_family, -FALSE);
		pgm_return_val_if_fail (tinfo->ti_recv_addrs[i].gsr_group.ss_family == tinfo->ti_recv_addrs[i].gsr_source.ss_family, -FALSE);
	}
	pgm_return_val_if_fail (tinfo->ti_send_addrs[0].gsr_group.ss_family == tinfo->ti_send_addrs[0].gsr_source.ss_family, -FALSE);

	new_transport = pgm_new0 (pgm_transport_t, 1);
	new_transport->can_send_data = TRUE;
	new_transport->can_send_nak  = TRUE;
	new_transport->can_recv_data = TRUE;

/* source-side */
	pgm_mutex_init (&new_transport->source_mutex);
/* transmit window */
	pgm_spinlock_init (&new_transport->txw_spinlock);
/* send socket */
	pgm_mutex_init (&new_transport->send_mutex);
/* next timer & spm expiration */
	pgm_mutex_init (&new_transport->timer_mutex);
/* receiver-side */
	pgm_mutex_init (&new_transport->receiver_mutex);
/* peer hash map & list lock */
	pgm_rwlock_init (&new_transport->peers_lock);
/* destroy lock */
	pgm_rwlock_init (&new_transport->lock);

	memcpy (&new_transport->tsi.gsi, &tinfo->ti_gsi, sizeof(pgm_gsi_t));
	new_transport->dport = htons (tinfo->ti_dport);
	if (tinfo->ti_sport) {
		new_transport->tsi.sport = htons (tinfo->ti_sport);
	} else {
		do {
			new_transport->tsi.sport = htons (pgm_random_int_range (0, UINT16_MAX));
		} while (new_transport->tsi.sport == new_transport->dport);
	}

/* network data ports */
	new_transport->udp_encap_ucast_port = tinfo->ti_udp_encap_ucast_port;
	new_transport->udp_encap_mcast_port = tinfo->ti_udp_encap_mcast_port;

/* copy network parameters */
	memcpy (&new_transport->send_gsr, &tinfo->ti_send_addrs[0], sizeof(struct group_source_req));
	((struct sockaddr_in*)&new_transport->send_gsr.gsr_group)->sin_port = htons (new_transport->udp_encap_mcast_port);
	for (unsigned i = 0; i < tinfo->ti_recv_addrs_len; i++)
	{
		memcpy (&new_transport->recv_gsr[i], &tinfo->ti_recv_addrs[i], sizeof(struct group_source_req));
	}
	new_transport->recv_gsr_len = tinfo->ti_recv_addrs_len;

/* open sockets to implement PGM */
	int socket_type, protocol;
	if (new_transport->udp_encap_ucast_port) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Opening UDP encapsulated sockets."));
		socket_type = SOCK_DGRAM;
		protocol = IPPROTO_UDP;
	} else {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Opening raw sockets."));
		socket_type = SOCK_RAW;
		protocol = pgm_ipproto_pgm;
	}

	if ((new_transport->recv_sock = socket (new_transport->recv_gsr[0].gsr_group.ss_family,
						socket_type,
						protocol)) < 0)
	{
#ifndef _WIN32
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (errno),
			     _("Creating receive socket: %s"),
			     strerror (errno));
		if (EPERM == save_errno) {
			pgm_error (_("PGM protocol requires CAP_NET_RAW capability, e.g. sudo execcap 'cap_net_raw=ep'"));
		}
#else
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Creating receive socket: %s"),
			     pgm_wsastrerror (save_errno));
#endif
		goto err_destroy;
	}

	if (new_transport->udp_encap_ucast_port != new_transport->udp_encap_mcast_port)
	{
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     PGM_ERROR_INVAL,
			     _("Split unicast and multicast UDP encapsulation unsupported."));
		goto err_destroy;
	}

	if ((new_transport->send_sock = socket (new_transport->send_gsr.gsr_group.ss_family,
						socket_type,
						protocol)) < 0)
	{
#ifndef _WIN32
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (save_errno),
			     _("Creating send socket: %s"),
			     strerror (save_errno));
#else
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Creating send socket: %s"),
			     pgm_wsastrerror (save_errno));
#endif
		goto err_destroy;
	}

	if ((new_transport->send_with_router_alert_sock = socket (new_transport->send_gsr.gsr_group.ss_family,
						socket_type,
						protocol)) < 0)
	{
#ifndef _WIN32
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (save_errno),
			     _("Creating IP Router Alert (RFC 2113) send socket: %s"),
			     strerror (save_errno));
#else
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Creating IP Router Alert (RFC 2113) send socket: %s"),
			     pgm_wsastrerror (save_errno));
#endif
		goto err_destroy;
	}

	if (new_transport->use_router_alert)
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Request IP Router Alert (RFC 2113)."));
		if (0 != pgm_sockaddr_router_alert (new_transport->send_with_router_alert_sock, new_transport->send_gsr.gsr_group.ss_family, TRUE))
		{
#ifndef _WIN32
			const int save_errno = errno;
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       pgm_error_from_errno (save_errno),
				       _("Enabling IP Router Alert (RFC 2113): %s"),
				       strerror (save_errno));
#else
			const int save_errno = WSAGetLastError();
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       pgm_error_from_wsa_errno (save_errno),
				       _("Enabling IP Router Alert (RFC 2113): %s"),
				       pgm_wsastrerror (save_errno));
#endif
			goto err_destroy;
		}
	}

	*transport = new_transport;

	pgm_rwlock_writer_lock (&pgm_transport_list_lock);
	pgm_transport_list = pgm_slist_append (pgm_transport_list, *transport);
	pgm_rwlock_writer_unlock (&pgm_transport_list_lock);
	return TRUE;

err_destroy:
	if (-1 != new_transport->recv_sock) {
#ifndef _WIN32
		if (-1 == close (new_transport->recv_sock))
			pgm_warn (_("Close on receive socket failed: %s"), strerror (errno));
#else
		if (SOCKET_ERROR == closesocket (new_transport->recv_sock))
			pgm_warn (_("Close on receive socket failed: %s"), pgm_wsastrerror (WSAGetLastError()));
#endif
		new_transport->recv_sock = -1;
	}
	if (-1 != new_transport->send_sock) {
#ifndef _WIN32
		if (-1 == close (new_transport->send_sock))
			pgm_warn (_("Close on send socket failed: %s"), strerror (errno));
#else
		if (SOCKET_ERROR == closesocket (new_transport->send_sock)) 
			pgm_warn (_("Close on send socket failed: %s"), pgm_wsastrerror (WSAGetLastError()));
#endif
		new_transport->send_sock = -1;
	}
	if (-1 != new_transport->send_with_router_alert_sock) {
#ifndef _WIN32
		if (-1 == close (new_transport->send_with_router_alert_sock))
			pgm_warn (_("Close on IP Router Alert (RFC 2113) send socket failed: %s"), strerror (errno));
#else
		if (SOCKET_ERROR == closesocket (new_transport->send_with_router_alert_sock))
			pgm_warn (_("Close on IP Router Alert (RFC 2113) send socket failed: %s"), pgm_wsastrerror (WSAGetLastError()));
#endif
		new_transport->send_with_router_alert_sock = -1;
	}
	pgm_free (new_transport);
	return FALSE;
}

/* 0 < tpdu < 65536 by data type (guint16)
 *
 * IPv4:   68 <= tpdu < 65536		(RFC 2765)
 * IPv6: 1280 <= tpdu < 65536		(RFC 2460)
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_max_tpdu (
	pgm_transport_t* const	transport,
	const uint16_t		max_tpdu
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (max_tpdu >= (sizeof(struct pgm_ip) + sizeof(struct pgm_header)), FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
	transport->max_tpdu = max_tpdu;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* TRUE = enable multicast loopback and for UDP encapsulation SO_REUSEADDR,
 * FALSE = default, to disable.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_multicast_loop (
	pgm_transport_t* const	transport,
	const bool		use_multicast_loop
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
	transport->use_multicast_loop = use_multicast_loop;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < hops < 256, hops == -1 use kernel default (ignored).
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_hops (
	pgm_transport_t* const	transport,
	const unsigned		hops
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (hops > 0, FALSE);
	pgm_return_val_if_fail (hops < 256, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
	transport->hops = hops;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < wmem < wmem_max (user)
 *
 * operating system and sysctl dependent maximum, minimum on Linux 256 (doubled).
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_sndbuf (
	pgm_transport_t* const	transport,
	const size_t		size
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (size > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
#ifdef CONFIG_HAVE_PROC
	size_t wmem_max;
	FILE* fp;

	fp = fopen ("/proc/sys/net/core/wmem_max", "r");
	if (fp) {
		const int matches = fscanf (fp, "%zu", &wmem_max);
		pgm_assert (1 == matches);
		fclose (fp);
		if (size > wmem_max) {
			pgm_rwlock_reader_unlock (&transport->lock);
			return FALSE;
		}
	} else {
		pgm_warn (_("Cannot open /proc/sys/net/core/wmem_max: %s"), strerror(errno));
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
#endif
	transport->sndbuf = size;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < rmem < rmem_max (user)
 *
 * minimum on Linux is 2048 (doubled).
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_rcvbuf (
	pgm_transport_t* const	transport,
	const size_t		size
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (size > 0, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
#ifdef CONFIG_HAVE_PROC
	size_t rmem_max;
	FILE* fp;

	fp = fopen ("/proc/sys/net/core/rmem_max", "r");
	if (fp) {
		const int matches = fscanf (fp, "%zu", &rmem_max);
		pgm_assert (1 == matches);
		fclose (fp);
		if (size > rmem_max) {
			pgm_rwlock_reader_unlock (&transport->lock);
			return FALSE;
		}
	} else {
		pgm_warn (_("Cannot open /proc/sys/net/core/rmem_max: %s"), strerror(errno));
		pgm_rwlock_reader_unlock (&transport->lock);
		return FALSE;
	}
#endif
	transport->rcvbuf = size;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* bind the sockets to the link layer to start receiving data.
 *
 * returns TRUE on success, or FALSE on error and sets error appropriately,
 */

bool
pgm_transport_bind (
	pgm_transport_t* restrict transport,
	pgm_error_t**	 restrict error
	)
{
	pgm_return_val_if_fail (NULL != transport, FALSE);
	if (!pgm_rwlock_writer_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_writer_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}

/* sanity checks on state */
	if (PGM_UNLIKELY(0 == transport->max_tpdu)) {
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_TRANSPORT,
			       PGM_ERROR_FAILED,
			       _("Maximum TPDU size not configured."));
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}
	if (transport->can_send_data) {
		if (PGM_UNLIKELY(0 == transport->spm_ambient_interval)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("SPM ambient interval not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->spm_heartbeat_len)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("SPM heartbeat interval not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->txw_sqns && 0 == transport->txw_secs)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("TXW_SQNS not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->txw_sqns && 0 == transport->txw_max_rte)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("TXW_MAX_RTE not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}
	if (transport->can_recv_data) {
		if (PGM_UNLIKELY(0 == transport->rxw_sqns && 0 == transport->rxw_secs)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("RXW_SQNS not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->rxw_sqns && 0 == transport->rxw_max_rte)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("RXW_MAX_RTE not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->peer_expiry)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("Peer timeout not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->spmr_expiry)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("SPM-Request timeout not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->nak_bo_ivl)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("NAK_BO_IVL not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->nak_rpt_ivl)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("NAK_RPT_IVL not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->nak_rdata_ivl)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("NAK_RDATA_IVL not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->nak_data_retries)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("NAK_DATA_RETRIES not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == transport->nak_ncf_retries)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TRANSPORT,
				       PGM_ERROR_FAILED,
				       _("NAK_NCF_RETRIES not configured."));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}

	pgm_debug ("bind (transport:%p error:%p)",
		 (const void*)transport, (const void*)error);

	pgm_rand_create (&transport->rand_);

/* PGM Children support of POLLs requires 32-bit random node identifier RAND_NODE_ID */
	if (transport->can_recv_data) {
		transport->rand_node_id = pgm_rand_int (&transport->rand_);
	}

	if (transport->can_send_data) {
/* Windows notify call will raise an assertion on error, only Unix versions will return
 * a valid error.
 */
		if (0 != pgm_notify_init (&transport->rdata_notify)) {
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_errno (errno),
				     _("Creating RDATA notification channel: %s"),
				     strerror (errno));
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}
	if (0 != pgm_notify_init (&transport->pending_notify)) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (errno),
			     _("Creating waiting peer notification channel: %s"),
			     strerror (errno));
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}

/* determine IP header size for rate regulation engine & stats */
	transport->iphdr_len = (AF_INET == transport->send_gsr.gsr_group.ss_family) ? sizeof(struct pgm_ip) : sizeof(struct pgm_ip6_hdr);
	pgm_trace (PGM_LOG_ROLE_NETWORK,"assuming IP header size of %zu bytes", transport->iphdr_len);

	if (transport->udp_encap_ucast_port) {
		const size_t udphdr_len = sizeof(struct pgm_udphdr);
		pgm_trace (PGM_LOG_ROLE_NETWORK,"assuming UDP header size of %zu bytes", udphdr_len);
		transport->iphdr_len += udphdr_len;
	}

	transport->max_tsdu = transport->max_tpdu - transport->iphdr_len - pgm_transport_pkt_offset2 (FALSE, transport->use_pgmcc);
	transport->max_tsdu_fragment = transport->max_tpdu - transport->iphdr_len - pgm_transport_pkt_offset2 (TRUE, transport->use_pgmcc);
	const unsigned max_fragments = transport->txw_sqns ? MIN(PGM_MAX_FRAGMENTS, transport->txw_sqns) : PGM_MAX_FRAGMENTS;
	transport->max_apdu = MIN(PGM_MAX_APDU, max_fragments * transport->max_tsdu_fragment);

	if (transport->can_send_data) {
		pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Create transmit window."));
		transport->window = transport->txw_sqns ?
					pgm_txw_create (&transport->tsi, 0, transport->txw_sqns, 0, 0, transport->use_ondemand_parity || transport->use_proactive_parity, transport->rs_n, transport->rs_k) :
					pgm_txw_create (&transport->tsi, transport->max_tpdu, 0, transport->txw_secs, transport->txw_max_rte, transport->use_ondemand_parity || transport->use_proactive_parity, transport->rs_n, transport->rs_k);
		pgm_assert (NULL != transport->window);
	}

/* create peer list */
	if (transport->can_recv_data) {
		transport->peers_hashtable = pgm_hashtable_new (pgm_tsi_hash, pgm_tsi_equal);
		pgm_assert (NULL != transport->peers_hashtable);
	}

	if (transport->udp_encap_ucast_port)
	{
/* Stevens: "SO_REUSEADDR has datatype int."
 */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Set socket sharing."));
		const int v = 1;
		if (0 != setsockopt (transport->recv_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v)) ||
		    0 != setsockopt (transport->send_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v)) ||
		    0 != setsockopt (transport->send_with_router_alert_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v)))
		{
#ifndef _WIN32
			const int save_errno = errno;
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_errno (save_errno),
				     _("Enabling reuse of socket local address: %s"),
				     strerror (save_errno));
#else
			const int save_errno = WSAGetLastError();
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_wsa_errno (save_errno),
				     _("Enabling reuse of socket local address: %s"),
				     pgm_wsastrerror (save_errno));
#endif
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}

/* request extra packet information to determine destination address on each packet */
#ifndef CONFIG_TARGET_WINE
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Request socket packet-info."));
		const sa_family_t recv_family = transport->recv_gsr[0].gsr_group.ss_family;
		if (0 != pgm_sockaddr_pktinfo (transport->recv_sock, recv_family, TRUE))
		{
#ifndef _WIN32
			const int save_errno = errno;
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_errno (save_errno),
				     _("Enabling receipt of ancillary information per incoming packet: %s"),
				     strerror (save_errno));
#else
			const int save_errno = WSAGetLastError();
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_wsa_errno (save_errno),
				     _("Enabling receipt of ancillary information per incoming packet: %s"),
				     pgm_wsastrerror (save_errno));
#endif
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
#endif
	}
	else
	{
		const sa_family_t recv_family = transport->recv_gsr[0].gsr_group.ss_family;
		if (AF_INET == recv_family)
		{
/* include IP header only for incoming data, only works for IPv4 */
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Request IP headers."));
			if (0 != pgm_sockaddr_hdrincl (transport->recv_sock, recv_family, TRUE))
			{
#ifndef _WIN32
				const int save_errno = errno;
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_TRANSPORT,
					     pgm_error_from_errno (save_errno),
					     _("Enabling IP header in front of user data: %s"),
					     strerror (save_errno));
#else
				const int save_errno = WSAGetLastError();
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_TRANSPORT,
					     pgm_error_from_wsa_errno (save_errno),
					     _("Enabling IP header in front of user data: %s"),
					     pgm_wsastrerror (save_errno));
#endif
				pgm_rwlock_writer_unlock (&transport->lock);
				return FALSE;
			}
		}
		else
		{
			pgm_assert (AF_INET6 == recv_family);
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Request socket packet-info."));
			if (0 != pgm_sockaddr_pktinfo (transport->recv_sock, recv_family, TRUE))
			{
#ifndef _WIN32
				const int save_errno = errno;
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_TRANSPORT,
					     pgm_error_from_errno (save_errno),
					     _("Enabling receipt of control message per incoming datagram: %s"),
					     strerror (save_errno));
#else
				const int save_errno = WSAGetLastError();
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_TRANSPORT,
					     pgm_error_from_wsa_errno (save_errno),
					     _("Enabling receipt of control message per incoming datagram: %s"),
					     pgm_wsastrerror (save_errno));
#endif
				pgm_rwlock_writer_unlock (&transport->lock);
				return FALSE;
			}
		}
	}

/* buffers, set size first then re-read to confirm actual value */
	if (transport->rcvbuf)
	{
/* Stevens: "SO_RCVBUF has datatype int."
 */
		const int rcvbuf = transport->rcvbuf;	/* convert from size_t */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Set receive socket buffer size to %d bytes."), rcvbuf);
		if (0 != setsockopt (transport->recv_sock, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf)))
		{
#ifndef _WIN32
			const int save_errno = errno;
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_errno (save_errno),
				     _("Setting maximum socket receive buffer in bytes: %s"),
				     strerror (save_errno));
#else
			const int save_errno = WSAGetLastError();
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_wsa_errno (save_errno),
				     _("Setting maximum socket receive buffer in bytes: %s"),
				     pgm_wsastrerror (save_errno));
#endif
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}
	if (transport->sndbuf)
	{
/* Stevens: "SO_SNDBUF has datatype int."
 */
		const int sndbuf = transport->sndbuf;
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Set send socket buffer size to %d bytes."), sndbuf);
		if (0 != setsockopt (transport->send_sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf)) ||
		    0 != setsockopt (transport->send_with_router_alert_sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf)))
		{
#ifndef _WIN32
			const int save_errno = errno;
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_errno (save_errno),
				     _("Setting maximum socket send buffer in bytes: %s"),
				     strerror (save_errno));
#else
			const int save_errno = WSAGetLastError();
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_wsa_errno (save_errno),
				     _("Setting maximum socket send buffer in bytes: %s"),
				     pgm_wsastrerror (save_errno));
#endif
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}

/* Bind UDP sockets to interfaces, note multicast on a bound interface is
 * fruity on some platforms.  Roughly,  binding to INADDR_ANY provides all
 * data, binding to the multicast group provides only multicast traffic,
 * and binding to the interface address provides only unicast traffic.
 *
 * Multicast routing, IGMP & MLD require a link local address, for IPv4
 * this is provided through MULTICAST_IF and IPv6 through bind, and these
 * may be overridden by per packet scopes.
 *
 * After binding, default interfaces (0.0.0.0) are resolved.
 */
/* TODO: different ports requires a new bound socket */

	union {
		struct sockaddr		sa;
		struct sockaddr_in	s4;
		struct sockaddr_in6	s6;
		struct sockaddr_storage	ss;
	} recv_addr, recv_addr2, send_addr, send_with_router_alert_addr;

#ifdef CONFIG_BIND_INADDR_ANY
/* force default interface for bind-only, source address is still valid for multicast membership.
 * effectively same as running getaddrinfo(hints = {ai_flags = AI_PASSIVE})
 */
	if (AF_INET == transport->recv_gsr[0].gsr_group.ss_family) {
		memset (&recv_addr.s4, 0, sizeof(struct sockaddr_in));
		recv_addr.s4.sin_family = AF_INET;
		recv_addr.s4.sin_addr.s_addr = INADDR_ANY;
	} else {
		memset (&recv_addr.s6, 0, sizeof(struct sockaddr_in6));
		recv_addr.s6.sin6_family = AF_INET6;
		recv_addr.s6.sin6_addr = in6addr_any;
	}
	pgm_trace (PGM_LOG_ROLE_NETWORK,_("Binding receive socket to INADDR_ANY."));
#else
	if (!_pgm_indextoaddr (transport->recv_gsr[0].gsr_interface,
			       transport->recv_gsr[0].gsr_group.ss_family,
			       pgm_sockaddr_scope_id ((struct sockaddr*)&transport->recv_gsr[0].gsr_group),
			       &recv_addr.sa,
			       error))
	{
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}
	pgm_trace (PGM_LOG_ROLE_NETWORK,_("Binding receive socket to interface index %d"), transport->recv_gsr[0].gsr_interface);

#endif /* CONFIG_BIND_INADDR_ANY */

	memcpy (&recv_addr2.sa, &recv_addr.sa, pgm_sockaddr_len (&recv_addr.sa));
	((struct sockaddr_in*)&recv_addr)->sin_port = htons (transport->udp_encap_mcast_port);
	if (0 != bind (transport->recv_sock, &recv_addr.sa, pgm_sockaddr_len (&recv_addr.sa)))
	{
		char addr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&recv_addr, addr, sizeof(addr));
#ifndef _WIN32
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (save_errno),
			     _("Binding receive socket to address %s: %s"),
			     addr,
			     strerror (save_errno));
#else
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Binding receive socket to address %s: %s"),
			     addr,
			     pgm_wsastrerror (save_errno));
#endif
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}

	if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&recv_addr, s, sizeof(s));
		pgm_debug ("bind succeeded on recv_gsr[0] interface %s", s);
	}

/* keep a copy of the original address source to re-use for router alert bind */
	memset (&send_addr, 0, sizeof(send_addr));

	if (!pgm_if_indextoaddr (transport->send_gsr.gsr_interface,
				  transport->send_gsr.gsr_group.ss_family,
				  pgm_sockaddr_scope_id ((struct sockaddr*)&transport->send_gsr.gsr_group),
				  (struct sockaddr*)&send_addr,
				  error))
	{
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}
	else if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Binding send socket to interface index %u"), transport->send_gsr.gsr_interface);
	}

	memcpy (&send_with_router_alert_addr, &send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));
	if (0 != bind (transport->send_sock, (struct sockaddr*)&send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr)))
	{
		char addr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&send_addr, addr, sizeof(addr));
#ifndef _WIN32
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (save_errno),
			     _("Binding send socket to address %s: %s"),
			     addr,
			     strerror (save_errno));
#else
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Binding send socket to address %s: %s"),
			     addr,
			     pgm_wsastrerror (save_errno));
#endif
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}

/* resolve bound address if 0.0.0.0 */
	if (AF_INET == send_addr.ss.ss_family)
	{
		if ((INADDR_ANY == ((struct sockaddr_in*)&send_addr)->sin_addr.s_addr) &&
		    !pgm_if_getnodeaddr (AF_INET, (struct sockaddr*)&send_addr, sizeof(send_addr), error))
		{
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}
	}
	else if ((memcmp (&in6addr_any, &((struct sockaddr_in6*)&send_addr)->sin6_addr, sizeof(in6addr_any)) == 0) &&
		 !pgm_if_getnodeaddr (AF_INET6, (struct sockaddr*)&send_addr, sizeof(send_addr), error))
	{
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}

	if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&send_addr, s, sizeof(s));
		pgm_debug ("bind succeeded on send_gsr interface %s", s);
	}

	if (0 != bind (transport->send_with_router_alert_sock,
			(struct sockaddr*)&send_with_router_alert_addr,
			pgm_sockaddr_len((struct sockaddr*)&send_with_router_alert_addr)))
	{
		char addr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&send_with_router_alert_addr, addr, sizeof(addr));
#ifndef _WIN32
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (save_errno),
			     _("Binding IP Router Alert (RFC 2113) send socket to address %s: %s"),
			     addr,
			     strerror (save_errno));
#else
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Binding IP Router Alert (RFC 2113) send socket to address %s: %s"),
			     addr,
			     pgm_wsastrerror (save_errno));
#endif
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}

	if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&send_with_router_alert_addr, s, sizeof(s));
		pgm_debug ("bind (router alert) succeeded on send_gsr interface %s", s);
	}

/* save send side address for broadcasting as source nla */
	memcpy (&transport->send_addr, &send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));

/* receiving groups (multiple) */
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		const struct group_source_req* p = &transport->recv_gsr[i];

/* ASM */
		if (0 == pgm_sockaddr_cmp ((const struct sockaddr*)&p->gsr_group, (const struct sockaddr*)&p->gsr_source))
		{
			if (0 != pgm_sockaddr_join_group (transport->recv_sock,
							  p->gsr_group.ss_family,
							  (const struct group_req*)p))
			{
#ifndef _WIN32
				const int save_errno = errno;
#else
				const int save_errno = WSAGetLastError();
#endif
				char group_addr[INET6_ADDRSTRLEN];
				char ifname[IF_NAMESIZE];
				pgm_sockaddr_ntop ((const struct sockaddr*)&p->gsr_group, group_addr, sizeof(group_addr));
				if (0 == p->gsr_interface)
#ifndef _WIN32
					pgm_set_error (error,
						     PGM_ERROR_DOMAIN_TRANSPORT,
						     pgm_error_from_errno (save_errno),
						     _("Joining multicast group %s: %s"),
						     group_addr,
						     strerror (save_errno));
#else
					pgm_set_error (error,
						     PGM_ERROR_DOMAIN_TRANSPORT,
						     pgm_error_from_wsa_errno (save_errno),
						     _("Joining multicast group %s: %s"),
						     group_addr,
						     pgm_wsastrerror (save_errno));
#endif
				else
#ifndef _WIN32
					pgm_set_error (error,
						     PGM_ERROR_DOMAIN_TRANSPORT,
						     pgm_error_from_errno (save_errno),
						     _("Joining multicast group %s on interface %s: %s"),
						     group_addr,
						     if_indextoname (p->gsr_interface, ifname),
						     strerror (save_errno));
#else
					pgm_set_error (error,
						     PGM_ERROR_DOMAIN_TRANSPORT,
						     pgm_error_from_wsa_errno (save_errno),
						     _("Joining multicast group %s on interface %s: %s"),
						     group_addr,
						     pgm_if_indextoname (p->gsr_interface, ifname),
						     pgm_wsastrerror (save_errno));
#endif
				pgm_rwlock_writer_unlock (&transport->lock);
				return FALSE;
			}
			else if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
			{
				char s1[INET6_ADDRSTRLEN];
				pgm_sockaddr_ntop ((const struct sockaddr*)&p->gsr_group, s1, sizeof(s1));
				pgm_trace (PGM_LOG_ROLE_NETWORK,_("Multicast group join succeeded on recv_gsr[%u] interface %u group %s"),
					i, p->gsr_interface, s1);
			}
		}
		else /* source != group, i.e. SSM */
		{
			if (0 != pgm_sockaddr_join_source_group (transport->recv_sock,
								 p->gsr_group.ss_family,
								 p))
			{
				char source_addr[INET6_ADDRSTRLEN];
				char group_addr[INET6_ADDRSTRLEN];
				pgm_sockaddr_ntop ((const struct sockaddr*)&p->gsr_source, source_addr, sizeof(source_addr));
				pgm_sockaddr_ntop ((const struct sockaddr*)&p->gsr_group, group_addr, sizeof(group_addr));
#ifndef _WIN32
				const int save_errno = errno;
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_TRANSPORT,
					     pgm_error_from_errno (save_errno),
					     _("Joining multicast group %s from source %s: %s"),
					     group_addr,
					     source_addr,
					     strerror (save_errno));
#else
				const int save_errno = WSAGetLastError();
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_TRANSPORT,
					     pgm_error_from_wsa_errno (save_errno),
					     _("Joining multicast group %s from source %s: %s"),
					     group_addr,
					     source_addr,
					     pgm_wsastrerror (save_errno));
#endif
				pgm_rwlock_writer_unlock (&transport->lock);
				return FALSE;
			}
			else if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
			{
				char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
				pgm_sockaddr_ntop ((const struct sockaddr*)&p->gsr_group, s1, sizeof(s1));
				pgm_sockaddr_ntop ((const struct sockaddr*)&p->gsr_source, s2, sizeof(s2));
				pgm_trace (PGM_LOG_ROLE_NETWORK,_("Multicast join source group succeeded on recv_gsr[%u] interface %u group %s source %s"),
					i, p->gsr_interface, s1, s2);
			}
		}
	}

/* send group (singular) */
	if (0 != pgm_sockaddr_multicast_if (transport->send_sock,
					    (struct sockaddr*)&transport->send_addr,
					    transport->send_gsr.gsr_interface))
	{
		char ifname[IF_NAMESIZE];
#ifndef _WIN32
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (save_errno),
			     _("Setting device %s for multicast send socket: %s"),
			     if_indextoname (transport->send_gsr.gsr_interface, ifname),
			     strerror (save_errno));
#else
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Setting device %s for multicast send socket: %s"),
			     pgm_if_indextoname (transport->send_gsr.gsr_interface, ifname),
			     pgm_wsastrerror (save_errno));
#endif
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}
	else if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&transport->send_addr, s, sizeof(s));
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Set multicast local device succeeded on send_gsr address %s interface %u"),
					s, (unsigned)transport->send_gsr.gsr_interface);
	}

	if (0 != pgm_sockaddr_multicast_if (transport->send_with_router_alert_sock,
					    (struct sockaddr*)&transport->send_addr,
					    transport->send_gsr.gsr_interface))
	{
		char ifname[IF_NAMESIZE];
#ifndef _WIN32
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (save_errno),
			     _("Setting device %s for multicast IP Router Alert (RFC 2113) send socket: %s"),
			     if_indextoname (transport->send_gsr.gsr_interface, ifname),
			     strerror (save_errno));
#else
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Setting device %s for multicast IP Router Alert (RFC 2113) send socket: %s"),
			     pgm_if_indextoname (transport->send_gsr.gsr_interface, ifname),
			     pgm_wsastrerror (save_errno));
#endif
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}
	else if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&transport->send_addr, s, sizeof(s));
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Set multicast local device (router alert) succeeded on send_gsr address %s interface %u"),
					s, (unsigned)transport->send_gsr.gsr_interface);
	}

/* multicast loopback */
	pgm_trace (PGM_LOG_ROLE_NETWORK,transport->use_multicast_loop?_("Set multicast loopback.") : _("Unset multicast loopback."));
#ifndef _WIN32
	if (0 != pgm_sockaddr_multicast_loop (transport->send_sock,
					      transport->send_gsr.gsr_group.ss_family,
					      transport->use_multicast_loop) ||
	    0 != pgm_sockaddr_multicast_loop (transport->send_with_router_alert_sock,
					      transport->send_gsr.gsr_group.ss_family,
					      transport->use_multicast_loop))
	{
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (save_errno),
			     _("Setting multicast loopback: %s"),
			     strerror (save_errno));
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}
#else /* _WIN32 */
	if (0 != pgm_sockaddr_multicast_loop (transport->recv_sock,
					      transport->recv_gsr[0].gsr_group.ss_family,
					      transport->use_multicast_loop))
	{
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Setting multicast loopback: %s"),
			     pgm_wsastrerror (save_errno));
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}
#endif /* _WIN32 */

/* multicast ttl: many crappy network devices go CPU ape with TTL=1, 16 is a popular alternative */
#ifndef CONFIG_TARGET_WINE
	pgm_trace (PGM_LOG_ROLE_NETWORK,_("Set multicast hop limit to %d."), transport->hops);
	if (0 != pgm_sockaddr_multicast_hops (transport->send_sock,
					      transport->send_gsr.gsr_group.ss_family,
					      transport->hops) ||
	    0 != pgm_sockaddr_multicast_hops (transport->send_with_router_alert_sock,
					      transport->send_gsr.gsr_group.ss_family,
					      transport->hops))
	{
#ifndef _WIN32
		const int save_errno = errno;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_errno (save_errno),
			     _("Setting multicast hop limit to %i: %s"),
			     transport->hops,
			     strerror (save_errno));
#else
		const int save_errno = WSAGetLastError();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TRANSPORT,
			     pgm_error_from_wsa_errno (save_errno),
			     _("Setting multicast hop limit to %i: %s"),
			     transport->hops,
			     pgm_wsastrerror (save_errno));
#endif
		pgm_rwlock_writer_unlock (&transport->lock);
		return FALSE;
	}
#endif /* CONFIG_TARGET_WINE */

/* set Expedited Forwarding PHB for network elements, no ECN.
 * 
 * codepoint 101110 (RFC 3246)
 */
	pgm_trace (PGM_LOG_ROLE_NETWORK,_("Set packet differentiated services field to expedited forwarding."));
	const int dscp = 0x2e << 2;
	if (0 != pgm_sockaddr_tos (transport->send_sock,
				   transport->send_gsr.gsr_group.ss_family,
				   dscp) ||
	    0 != pgm_sockaddr_tos (transport->send_with_router_alert_sock,
				   transport->send_gsr.gsr_group.ss_family,
				   dscp))
	{
		pgm_warn (_("DSCP setting requires CAP_NET_ADMIN or ADMIN capability."));
		goto no_cap_net_admin;
	}

no_cap_net_admin:

/* rx to nak processor notify channel */
	if (transport->can_send_data)
	{
/* setup rate control */
		if (transport->txw_max_rte)
		{
			pgm_trace (PGM_LOG_ROLE_RATE_CONTROL,_("Setting rate regulation to %zd bytes per second."),
					transport->txw_max_rte);
	
			pgm_rate_create (&transport->rate_control, transport->txw_max_rte, transport->iphdr_len, transport->max_tpdu);
			transport->is_controlled_spm   = TRUE;	/* must always be set */
			transport->is_controlled_odata = TRUE;
			transport->is_controlled_rdata = TRUE;
		}
		else
		{
			transport->is_controlled_spm   = FALSE;
			transport->is_controlled_odata = FALSE;
			transport->is_controlled_rdata = FALSE;
		}

/* announce new transport by sending out SPMs */
		if (!pgm_send_spm (transport, PGM_OPT_SYN) ||
		    !pgm_send_spm (transport, PGM_OPT_SYN) ||
		    !pgm_send_spm (transport, PGM_OPT_SYN))
		{
#ifndef _WIN32
			const int save_errno = errno;
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_errno (save_errno),
				     _("Sending SPM broadcast: %s"),
				     strerror (save_errno));
#else
			const int save_errno = WSAGetLastError();
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_TRANSPORT,
				     pgm_error_from_wsa_errno (save_errno),
				     _("Sending SPM broadcast: %s"),
				     pgm_wsastrerror (save_errno));
#endif
			pgm_rwlock_writer_unlock (&transport->lock);
			return FALSE;
		}

		transport->next_poll = transport->next_ambient_spm = pgm_time_update_now() + transport->spm_ambient_interval;
	}
	else
	{
		pgm_assert (transport->can_recv_data);
		transport->next_poll = pgm_time_update_now() + pgm_secs( 30 );
	}

/* non-blocking sockets */
	pgm_trace (PGM_LOG_ROLE_NETWORK,transport->is_nonblocking ? _("Set non-blocking sockets") : _("Set blocking sockets"));
	pgm_sockaddr_nonblocking (transport->recv_sock, transport->is_nonblocking);
	pgm_sockaddr_nonblocking (transport->send_sock, transport->is_nonblocking);
	pgm_sockaddr_nonblocking (transport->send_with_router_alert_sock, transport->is_nonblocking);

/* allocate first incoming packet buffer */
	transport->rx_buffer = pgm_alloc_skb (transport->max_tpdu);

/* cleanup */
	pgm_debug ("preparing dynamic timer");
	pgm_timer_prepare (transport);

	transport->is_bound = TRUE;
	pgm_rwlock_writer_unlock (&transport->lock);
	pgm_debug ("transport successfully created.");
	return TRUE;
}

/* returns timeout for pending timer.
 */

bool
pgm_transport_get_timer_pending (
	pgm_transport_t* const restrict	transport,
	struct timeval*	       restrict	tv
	)
{
	pgm_return_val_if_fail (NULL != transport, FALSE);
	pgm_return_val_if_fail (NULL != tv, FALSE);
	const pgm_time_t usecs = pgm_timer_expiration (transport);
	tv->tv_sec  = usecs / 1000000UL;
	tv->tv_usec = usecs % 1000000UL;
	return TRUE;
}

/* returns timeout for blocking sends, PGM_IO_STATUS_AGAIN2 on recv()
 * or PGM_IO_STATUS_AGAIN on send().
 */

bool
pgm_transport_get_rate_remaining (
	pgm_transport_t* const restrict	transport,
	struct timeval*	       restrict	tv
	)
{
	pgm_return_val_if_fail (NULL != transport, FALSE);
	pgm_return_val_if_fail (NULL != tv, FALSE);
	const pgm_time_t usecs = pgm_rate_remaining (&transport->rate_control, transport->blocklen);
	tv->tv_sec  = usecs / 1000000UL;
	tv->tv_usec = usecs % 1000000UL;
	return TRUE;
}

/* add select parameters for the transports receive socket(s)
 *
 * returns highest file descriptor used plus one.
 */

int
pgm_transport_select_info (
	pgm_transport_t* const restrict	transport,
	fd_set*		 const restrict	readfds,	/* blocking recv fds */
	fd_set*		 const restrict	writefds,	/* blocking send fds */
	int*		 const restrict	n_fds		/* in: max fds, out: max (in:fds, transport:fds) */
	)
{
	int fds = 0;

	pgm_assert (NULL != transport);
	pgm_assert (NULL != n_fds);

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
		if (transport->can_send_data) {
			const int rdata_fd = pgm_notify_get_fd (&transport->rdata_notify);
			FD_SET(rdata_fd, readfds);
			fds = MAX(fds, rdata_fd + 1);
		}
		const int pending_fd = pgm_notify_get_fd (&transport->pending_notify);
		FD_SET(pending_fd, readfds);
		fds = MAX(fds, pending_fd + 1);
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
	pgm_transport_t* const restrict	transport,
	struct pollfd*   const restrict	fds,
	int*		 const restrict	n_fds,		/* in: #fds, out: used #fds */
	const int			events		/* POLLIN, POLLOUT */
	)
{
	pgm_assert (NULL != transport);
	pgm_assert (NULL != fds);
	pgm_assert (NULL != n_fds);

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
		pgm_assert ( (1 + moo) <= *n_fds );
		fds[moo].fd = transport->recv_sock;
		fds[moo].events = POLLIN;
		moo++;
		if (transport->can_send_data) {
			pgm_assert ( (1 + moo) <= *n_fds );
			fds[moo].fd = pgm_notify_get_fd (&transport->rdata_notify);
			fds[moo].events = POLLIN;
			moo++;
		}
		pgm_assert ( (1 + moo) <= *n_fds );
		fds[moo].fd = pgm_notify_get_fd (&transport->pending_notify);
		fds[moo].events = POLLIN;
		moo++;
	}

/* ODATA only published on regular socket, no need to poll router-alert sock */
	if (transport->can_send_data && events & POLLOUT)
	{
		pgm_assert ( (1 + moo) <= *n_fds );
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
		if (transport->can_send_data) {
			retval = epoll_ctl (epfd, op, pgm_notify_get_fd (&transport->rdata_notify), &event);
			if (retval)
				goto out;
		}
		retval = epoll_ctl (epfd, op, pgm_notify_get_fd (&transport->pending_notify), &event);
		if (retval)
			goto out;

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

bool
pgm_transport_set_fec (
	pgm_transport_t* const	transport,
	const uint8_t		proactive_h,		/* 0 == no pro-active parity */
	const bool		use_ondemand_parity,
	const bool		use_varpkt_len,
	const uint8_t		default_n,
	const uint8_t		default_k
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail ((default_k & (default_k -1)) == 0, FALSE);
	pgm_return_val_if_fail (default_k >= 2 && default_k <= 128, FALSE);
	pgm_return_val_if_fail (default_n >= default_k + 1, FALSE);

	const uint8_t default_h = default_n - default_k;

	pgm_return_val_if_fail (proactive_h <= default_h, FALSE);

/* check validity of parameters */
	if ( default_k > 223 &&
		( (default_h * 223.0) / default_k ) < 1.0 )
	{
		pgm_error (_("k/h ratio too low to generate parity data."));
		return FALSE;
	}

	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}

	transport->use_proactive_parity	= proactive_h > 0;
	transport->use_ondemand_parity	= use_ondemand_parity;
	transport->use_varpkt_len	= use_varpkt_len;
	transport->rs_n			= default_n;
	transport->rs_k			= default_k;
	transport->rs_proactive_h	= proactive_h;

	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

bool
pgm_transport_set_congestion_reports (
	pgm_transport_t* const	transport,
	const bool		use_cr,
	const unsigned		crqst_ivl	/* in milliseconds */
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->use_cr	= use_cr;
	transport->crqst_ivl	= pgm_msecs (crqst_ivl);
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* declare whether congestion control should be enabled for this transport.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_congestion_control (
	pgm_transport_t* const	transport,
	const bool		use_pgmcc,
	const unsigned		acker_ivl	/* in milliseconds */
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->use_pgmcc	= use_pgmcc;
	transport->acker_ivl	= pgm_msecs (acker_ivl);
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* send IP Router Alert on IP/PGM packets as required, some networking infrastructure
 * may silently consume RFC 2113 packets breaking PGM.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_router_alert (
	pgm_transport_t* const	transport,
	const bool		use_ip_router_alert
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->use_router_alert	= use_ip_router_alert;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* declare transport only for sending, discard any incoming SPM, ODATA,
 * RDATA, etc, packets.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_send_only (
	pgm_transport_t* const	transport,
	const bool		send_only
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->can_recv_data	= !send_only;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* declare transport only for receiving, no transmit window will be created
 * and no SPM broadcasts sent.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_recv_only (
	pgm_transport_t* const	transport,
	const bool		recv_only,
	const bool		is_passive	/* don't send any request or responses */
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->can_send_data	= !recv_only;
	transport->can_send_nak		= !is_passive;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* on unrecoverable data loss stop transport from further transmission and
 * receiving.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_transport_set_abort_on_reset (
	pgm_transport_t* const	transport,
	const bool		abort_on_reset
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->is_abort_on_reset = abort_on_reset;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}

/* default non-blocking operation on send and receive sockets.
 */

bool
pgm_transport_set_nonblocking (
	pgm_transport_t* const	transport,
	const bool		nonblocking
	)
{
	pgm_return_val_if_fail (transport != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	transport->is_nonblocking = nonblocking;
	pgm_rwlock_reader_unlock (&transport->lock);
	return TRUE;
}


#define SOCKADDR_TO_LEVEL(sa)	( (AF_INET == pgm_sockaddr_family((struct sockaddr*)(sa))) ? IPPROTO_IP : IPPROTO_IPV6 )
#define TRANSPORT_TO_LEVEL(t)	( (AF_INET == (t)->recv_gsr[0].gsr_group.ss_family) ? IPPROTO_IP : IPPROTO_IPV6 )


/* for any-source applications (ASM), join a new group
 */

bool
pgm_transport_join_group (
	pgm_transport_t*  	restrict transport,
	const struct group_req* restrict gr,
	socklen_t			 len
	)
{
	int status;

	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (gr != NULL, FALSE);
	pgm_return_val_if_fail (sizeof(struct group_req) == len, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed ||
	    transport->recv_gsr_len >= IP_MAX_MEMBERSHIPS)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}

/* verify not duplicate group/interface pairing */
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((const struct sockaddr*)&gr->gr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_group)  == 0 &&
		    pgm_sockaddr_cmp ((const struct sockaddr*)&gr->gr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_source) == 0 &&
			(gr->gr_interface == transport->recv_gsr[i].gsr_interface ||
			                0 == transport->recv_gsr[i].gsr_interface    )
                   )
		{
#ifdef TRANSPORT_DEBUG
			char s[INET6_ADDRSTRLEN];
			pgm_sockaddr_ntop ((const struct sockaddr*)&gr->gr_group, s, sizeof(s));
			if (transport->recv_gsr[i].gsr_interface) {
				pgm_warn(_("Transport has already joined group %s on interface %u"), s, gr->gr_interface);
			} else {
				pgm_warn(_("Transport has already joined group %s on all interfaces."), s);
			}
#endif
			pgm_rwlock_reader_unlock (&transport->lock);
			return FALSE;
		}
	}

	transport->recv_gsr[transport->recv_gsr_len].gsr_interface = 0;
	memcpy (&transport->recv_gsr[transport->recv_gsr_len].gsr_group, &gr->gr_group, pgm_sockaddr_len ((const struct sockaddr*)&gr->gr_group));
	memcpy (&transport->recv_gsr[transport->recv_gsr_len].gsr_source, &gr->gr_group, pgm_sockaddr_len ((const struct sockaddr*)&gr->gr_group));
	transport->recv_gsr_len++;
	status = setsockopt (transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_JOIN_GROUP, (const char*)gr, len);
	pgm_rwlock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for any-source applications (ASM), leave a joined group.
 */

bool
pgm_transport_leave_group (
	pgm_transport_t*	restrict transport,
	const struct group_req* restrict gr,
	socklen_t			 len
	)
{
	int status;

	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (gr != NULL, FALSE);
	pgm_return_val_if_fail (sizeof(struct group_req) == len, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed ||
	    transport->recv_gsr_len == 0)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}

	for (unsigned i = 0; i < transport->recv_gsr_len;)
	{
		if ((pgm_sockaddr_cmp ((const struct sockaddr*)&gr->gr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_group) == 0) &&
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
	pgm_rwlock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for any-source applications (ASM), turn off a given source
 */

bool
pgm_transport_block_source (
	pgm_transport_t*	       restrict transport,
	const struct group_source_req* restrict gsr,
	socklen_t				len
	)
{
	int status;

	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (gsr != NULL, FALSE);
	pgm_return_val_if_fail (sizeof(struct group_source_req) == len, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_BLOCK_SOURCE, (const char*)gsr, len);
	pgm_rwlock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for any-source applications (ASM), re-allow a blocked source
 */

bool
pgm_transport_unblock_source (
	pgm_transport_t*	       restrict transport,
	const struct group_source_req* restrict gsr,
	socklen_t				len
	)
{
	int status;

	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (gsr != NULL, FALSE);
	pgm_return_val_if_fail (sizeof(struct group_source_req) == len, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_UNBLOCK_SOURCE, (const char*)gsr, len);
	pgm_rwlock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for controlled-source applications (SSM), join each group/source pair.
 *
 * SSM joins are allowed on top of ASM in order to merge a remote source onto the local segment.
 */

bool
pgm_transport_join_source_group (
	pgm_transport_t*	       restrict transport,
	const struct group_source_req* restrict gsr,
	socklen_t				len
	)
{
	int status;

	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (gsr != NULL, FALSE);
	pgm_return_val_if_fail (sizeof(struct group_source_req) == len, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed ||
	    transport->recv_gsr_len >= IP_MAX_MEMBERSHIPS)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}

/* verify if existing group/interface pairing */
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_group) == 0 &&
			(gsr->gsr_interface == transport->recv_gsr[i].gsr_interface ||
			                  0 == transport->recv_gsr[i].gsr_interface    )
                   )
		{
			if (pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_source, (struct sockaddr*)&transport->recv_gsr[i].gsr_source) == 0)
			{
#ifdef TRANSPORT_DEBUG
				char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
				pgm_sockaddr_ntop ((const struct sockaddr*)&gsr->gsr_group, s1, sizeof(s1));
				pgm_sockaddr_ntop ((const struct sockaddr*)&gsr->gsr_source, s2, sizeof(s2));
				if (transport->recv_gsr[i].gsr_interface) {
					pgm_warn(_("Transport has already joined group %s from source %s on interface %d"),
						s1, s2, (unsigned)gsr->gsr_interface);
				} else {
					pgm_warn(_("Transport has already joined group %s from source %s on all interfaces"),
						s1, s2);
				}
#endif
				pgm_rwlock_reader_unlock (&transport->lock);
				return FALSE;
			}
			break;
		}
	}

	memcpy (&transport->recv_gsr[transport->recv_gsr_len], gsr, sizeof(struct group_source_req));
	transport->recv_gsr_len++;
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_JOIN_SOURCE_GROUP, (const char*)gsr, len);
	pgm_rwlock_reader_unlock (&transport->lock);
	return (0 == status);
}

/* for controlled-source applications (SSM), leave each group/source pair
 */

bool
pgm_transport_leave_source_group (
	pgm_transport_t*	       restrict transport,
	const struct group_source_req* restrict gsr,
	socklen_t				len
	)
{
	int status;

	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (gsr != NULL, FALSE);
	pgm_return_val_if_fail (sizeof(struct group_source_req) == len, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed ||
	    transport->recv_gsr_len == 0)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}

/* verify if existing group/interface pairing */
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_group, (struct sockaddr*)&transport->recv_gsr[i].gsr_group)   == 0 &&
		    pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_source, (struct sockaddr*)&transport->recv_gsr[i].gsr_source) == 0 &&
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
	pgm_rwlock_reader_unlock (&transport->lock);
	return (0 == status);
}

#if defined(MCAST_MSFILTER) || defined(SIOCSMSFILTER)
bool
pgm_transport_msfilter (
	pgm_transport_t*	   restrict transport,
	const struct group_filter* restrict gf_list,
	socklen_t		   	    len
	)
{
	int status;

	pgm_return_val_if_fail (transport != NULL, FALSE);
	pgm_return_val_if_fail (gf_list != NULL, FALSE);
	pgm_return_val_if_fail (len > 0, FALSE);
	pgm_return_val_if_fail (GROUP_FILTER_SIZE(gf_list->gf_numsrc) == len, FALSE);
	if (!pgm_rwlock_reader_trylock (&transport->lock))
		pgm_return_val_if_reached (FALSE);
	if (!transport->is_bound ||
	    transport->is_destroyed)
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (FALSE);
	}

#ifdef MCAST_MSFILTER	
	status = setsockopt(transport->recv_sock, TRANSPORT_TO_LEVEL(transport), MCAST_MSFILTER, (const char*)gf_list, len);
#elif defined(SIOCSMSFILTER)
	status = ioctl (transport->recv_sock, SIOCSMSFILTER, (const char*)gf_list);
#else
/* operation unsupported */
	status = -1;
#endif
	pgm_rwlock_reader_unlock (&transport->lock);
	return (0 == status);
}
#endif

/* eof */
