/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM socket: manage incoming & outgoing sockets with ambient SPMs, 
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
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/socket.h>
#include <impl/receiver.h>
#include <impl/source.h>
#include <impl/timer.h>


//#define SOCK_DEBUG
//#define SOCK_SPM_DEBUG


/* global locals */
pgm_rwlock_t pgm_sock_list_lock;		/* list of all sockets for admin interfaces */
pgm_slist_t* pgm_sock_list = NULL;


static const char* pgm_family_string (const int) PGM_GNUC_CONST;
static const char* pgm_sock_type_string (const int) PGM_GNUC_CONST;
static const char* pgm_protocol_string (const int) PGM_GNUC_CONST;


size_t
pgm_pkt_offset (
	bool		can_fragment,
	sa_family_t	pgmcc_family		/* 0 = disable */
	)
{
	static const size_t data_size = sizeof(struct pgm_header) + sizeof(struct pgm_data);
	size_t pkt_size = data_size;
	if (can_fragment || 0 != pgmcc_family)
		pkt_size += sizeof(struct pgm_opt_length) + sizeof(struct pgm_opt_header);
	if (can_fragment)
		pkt_size += sizeof(struct pgm_opt_fragment);
	if (AF_INET == pgmcc_family)
		pkt_size += sizeof(struct pgm_opt_pgmcc_data);
	else if (AF_INET6 == pgmcc_family)
		pkt_size += sizeof(struct pgm_opt6_pgmcc_data);
	return pkt_size;
}

/* destroy a pgm_sock object and contents, if last sock also destroy
 * associated event loop
 *
 * outstanding locks:
 * 1) pgm_sock_t::lock
 * 2) pgm_sock_t::receiver_mutex
 * 3) pgm_sock_t::source_mutex
 * 4) pgm_sock_t::txw_spinlock
 * 5) pgm_sock_t::timer_mutex
 *
 * If application calls a function on the sock after destroy() it is a
 * programmer error: segv likely to occur on unlock.
 *
 * on success, returns TRUE, on failure returns FALSE.
 */

bool
pgm_close (
	pgm_sock_t*	sock,
	bool		flush
	)
{
	pgm_return_val_if_fail (sock != NULL, FALSE);
	if (!pgm_rwlock_reader_trylock (&sock->lock))
		pgm_return_val_if_reached (FALSE);
	pgm_return_val_if_fail (!sock->is_destroyed, FALSE);
	pgm_debug ("pgm_sock_destroy (sock:%p flush:%s)",
		(const void*)sock,
		flush ? "TRUE":"FALSE");
/* flag existing calls */
	sock->is_destroyed = TRUE;
/* cancel running blocking operations */
	if (PGM_INVALID_SOCKET != sock->recv_sock) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Closing receive socket."));
		pgm_closesocket (sock->recv_sock);
		sock->recv_sock = PGM_INVALID_SOCKET;
	}
	if (PGM_INVALID_SOCKET != sock->send_sock) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Closing send socket."));
		pgm_closesocket (sock->send_sock);
		sock->send_sock = PGM_INVALID_SOCKET;
	}
	pgm_rwlock_reader_unlock (&sock->lock);
	pgm_debug ("blocking on destroy lock ...");
	pgm_rwlock_writer_lock (&sock->lock);

	pgm_debug ("removing sock from inventory.");
	pgm_rwlock_writer_lock (&pgm_sock_list_lock);
	pgm_sock_list = pgm_slist_remove (pgm_sock_list, sock);
	pgm_rwlock_writer_unlock (&pgm_sock_list_lock);

/* flush source side by sending heartbeat SPMs */
	if (sock->can_send_data &&
	    sock->is_connected && 
	    flush)
	{
		pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Flushing PGM source with session finish option broadcast SPMs."));
		if (!pgm_send_spm (sock, PGM_OPT_FIN) ||
		    !pgm_send_spm (sock, PGM_OPT_FIN) ||
		    !pgm_send_spm (sock, PGM_OPT_FIN))
		{
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Failed to send flushing SPMs."));
		}
	}

	if (sock->peers_hashtable) {
		pgm_debug ("destroying peer lookup table.");
		pgm_hashtable_destroy (sock->peers_hashtable);
		sock->peers_hashtable = NULL;
	}
	if (sock->peers_list) {
		pgm_debug ("destroying peer list.");
		do {
			pgm_list_t* next = sock->peers_list->next;
			pgm_peer_unref ((pgm_peer_t*)sock->peers_list->data);

			sock->peers_list = next;
		} while (sock->peers_list);
	}

	if (sock->window) {
		pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Destroying transmit window."));
		pgm_txw_shutdown (sock->window);
		sock->window = NULL;
	}
	pgm_trace (PGM_LOG_ROLE_RATE_CONTROL,_("Destroying rate control."));
	pgm_rate_destroy (&sock->rate_control);
	if (PGM_INVALID_SOCKET != sock->send_with_router_alert_sock) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Closing send with router alert socket."));
		pgm_closesocket (sock->send_with_router_alert_sock);
		sock->send_with_router_alert_sock = PGM_INVALID_SOCKET;
	}
	if (sock->spm_heartbeat_interval) {
		pgm_debug ("freeing SPM heartbeat interval data.");
		pgm_free (sock->spm_heartbeat_interval);
		sock->spm_heartbeat_interval = NULL;
	}
	if (sock->rx_buffer) {
		pgm_debug ("freeing receive buffer.");
		pgm_free_skb (sock->rx_buffer);
		sock->rx_buffer = NULL;
	}
	pgm_debug ("destroying notification channel.");
	pgm_notify_destroy (&sock->pending_notify);
	pgm_debug ("freeing sock locks.");
	pgm_rwlock_free (&sock->peers_lock);
	pgm_spinlock_free (&sock->txw_spinlock);
	pgm_mutex_free (&sock->send_mutex);
	pgm_mutex_free (&sock->timer_mutex);
	pgm_mutex_free (&sock->source_mutex);
	pgm_mutex_free (&sock->receiver_mutex);
	pgm_rwlock_writer_unlock (&sock->lock);
	pgm_rwlock_free (&sock->lock);
	pgm_debug ("freeing sock data.");
	pgm_free (sock);
	pgm_debug ("finished.");
	return TRUE;
}

/* Create a pgm_sock object.  Create sockets that require superuser
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
pgm_socket (
	pgm_sock_t**	     restrict sock,
	const sa_family_t	      family,		/* communications domain */
	const int		      pgm_sock_type,
	const int		      protocol,
	pgm_error_t**	     restrict error
	)
{
	pgm_sock_t* new_sock;

	pgm_return_val_if_fail (NULL != sock, FALSE);
	pgm_return_val_if_fail (AF_INET == family || AF_INET6 == family, FALSE);
	pgm_return_val_if_fail (SOCK_SEQPACKET == pgm_sock_type, FALSE);
	pgm_return_val_if_fail (IPPROTO_UDP == protocol || IPPROTO_PGM == protocol, FALSE);

	pgm_debug ("socket (sock:%p family:%s sock-type:%s protocol:%s error:%p)",
		 (const void*)sock, pgm_family_string(family), pgm_sock_type_string(pgm_sock_type), pgm_protocol_string(protocol), (const void*)error);

	new_sock = pgm_new0 (pgm_sock_t, 1);
	new_sock->family	= family;
	new_sock->socket_type	= pgm_sock_type;
	new_sock->protocol	= protocol;
	new_sock->can_send_data = TRUE;
	new_sock->can_send_nak  = TRUE;
	new_sock->can_recv_data = TRUE;
	new_sock->dport		= DEFAULT_DATA_DESTINATION_PORT;
	new_sock->tsi.sport	= DEFAULT_DATA_SOURCE_PORT;

/* PGMCC */
	new_sock->acker_nla.ss_family = family;

/* source-side */
	pgm_mutex_init (&new_sock->source_mutex);
/* transmit window */
	pgm_spinlock_init (&new_sock->txw_spinlock);
/* send socket */
	pgm_mutex_init (&new_sock->send_mutex);
/* next timer & spm expiration */
	pgm_mutex_init (&new_sock->timer_mutex);
/* receiver-side */
	pgm_mutex_init (&new_sock->receiver_mutex);
/* peer hash map & list lock */
	pgm_rwlock_init (&new_sock->peers_lock);
/* destroy lock */
	pgm_rwlock_init (&new_sock->lock);

/* open sockets to implement PGM */
	int socket_type;
	if (IPPROTO_UDP == new_sock->protocol) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Opening UDP encapsulated sockets."));
		socket_type = SOCK_DGRAM;
		new_sock->udp_encap_ucast_port = DEFAULT_UDP_ENCAP_UCAST_PORT;
		new_sock->udp_encap_mcast_port = DEFAULT_UDP_ENCAP_MCAST_PORT;
	} else {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Opening raw sockets."));
		socket_type = SOCK_RAW;
	}

	if ((new_sock->recv_sock = socket (new_sock->family,
					   socket_type,
					   new_sock->protocol)) == PGM_INVALID_SOCKET)
	{
		const int save_errno = pgm_sock_errno();
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_SOCKET,
			       pgm_error_from_sock_errno (save_errno),
			       _("Creating receive socket: %s"),
			       pgm_sock_strerror (save_errno));
#ifndef _WIN32
		if (EPERM == save_errno) {
			pgm_error (_("PGM protocol requires CAP_NET_RAW capability, e.g. sudo execcap 'cap_net_raw=ep'"));
		}
#endif
		goto err_destroy;
	}

	if ((new_sock->send_sock = socket (new_sock->family,
					   socket_type,
					   new_sock->protocol)) == PGM_INVALID_SOCKET)
	{
		const int save_errno = pgm_sock_errno();
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_SOCKET,
			       pgm_error_from_sock_errno (save_errno),
			       _("Creating send socket: %s"),
			       pgm_sock_strerror (save_errno));
		goto err_destroy;
	}

	if ((new_sock->send_with_router_alert_sock = socket (new_sock->family,
							     socket_type,
							     new_sock->protocol)) == PGM_INVALID_SOCKET)
	{
		const int save_errno = pgm_sock_errno();
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_SOCKET,
			       pgm_error_from_sock_errno (save_errno),
			       _("Creating IP Router Alert (RFC 2113) send socket: %s"),
			       pgm_sock_strerror (save_errno));
		goto err_destroy;
	}

	*sock = new_sock;

	pgm_rwlock_writer_lock (&pgm_sock_list_lock);
	pgm_sock_list = pgm_slist_append (pgm_sock_list, *sock);
	pgm_rwlock_writer_unlock (&pgm_sock_list_lock);
	pgm_debug ("PGM socket successfully created.");
	return TRUE;

err_destroy:
	if (PGM_INVALID_SOCKET != new_sock->recv_sock) {
		if (PGM_SOCKET_ERROR == pgm_closesocket (new_sock->recv_sock)) {
			const int save_errno = pgm_sock_errno();
			pgm_warn (_("Close on receive socket failed: %s"),
				  pgm_sock_strerror (save_errno));
		}
		new_sock->recv_sock = PGM_INVALID_SOCKET;
	}
	if (PGM_INVALID_SOCKET != new_sock->send_sock) {
		if (PGM_SOCKET_ERROR == pgm_closesocket (new_sock->send_sock)) {
			const int save_errno = pgm_sock_errno();
			pgm_warn (_("Close on send socket failed: %s"),
				  pgm_sock_strerror (save_errno));
		}
		new_sock->send_sock = PGM_INVALID_SOCKET;
	}
	if (PGM_INVALID_SOCKET != new_sock->send_with_router_alert_sock) {
		if (PGM_SOCKET_ERROR == pgm_closesocket (new_sock->send_with_router_alert_sock)) {
			const int save_errno = pgm_sock_errno();
			pgm_warn (_("Close on IP Router Alert (RFC 2113) send socket failed: %s"),
				  pgm_sock_strerror (save_errno));
		}
		new_sock->send_with_router_alert_sock = PGM_INVALID_SOCKET;
	}
	pgm_free (new_sock);
	return FALSE;
}

bool
pgm_getsockopt (
	pgm_sock_t* const restrict sock,
	const int		   optname,
	void*		  restrict optval,
	socklen_t*	  restrict optlen	/* required */
	)
{
	bool status = FALSE;
	pgm_return_val_if_fail (sock != NULL, status);
	pgm_return_val_if_fail (optval != NULL, status);
	pgm_return_val_if_fail (optlen != NULL, status);
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&sock->lock)))
		pgm_return_val_if_reached (status);
	if (PGM_UNLIKELY(sock->is_destroyed)) {
		pgm_rwlock_reader_unlock (&sock->lock);
		return status;
	}
	switch (optname) {
/* maximum TPDU size */
	case PGM_MTU:
		if (PGM_UNLIKELY(*optlen != sizeof (int)))
			break;
		*(int*restrict)optval = sock->max_tpdu;
		status = TRUE;
		break;

/* receive socket */
	case PGM_RECV_SOCK:
		if (PGM_UNLIKELY(!sock->is_connected))
			break;
		if (PGM_UNLIKELY(*optlen != sizeof (int)))
			break;
		*(int*)optval = sock->recv_sock;
		status = TRUE;
		break;

/* repair socket */
	case PGM_REPAIR_SOCK:
		if (PGM_UNLIKELY(!sock->is_connected))
			break;
		if (PGM_UNLIKELY(*optlen != sizeof (int)))
			break;
		*(int*)optval = pgm_notify_get_fd (&sock->rdata_notify);
		status = TRUE;
		break;

/* pending socket */
	case PGM_PENDING_SOCK:
		if (PGM_UNLIKELY(!sock->is_connected))
			break;
		if (PGM_UNLIKELY(*optlen != sizeof (int)))
			break;
		*(int*)optval = pgm_notify_get_fd (&sock->pending_notify);
		status = TRUE;
		break;

/* timeout for pending timer */
	case PGM_TIME_REMAIN:
		if (PGM_UNLIKELY(!sock->is_connected))
			break;
		if (PGM_UNLIKELY(*optlen != sizeof (struct timeval)))
			break;
		{
			struct timeval* tv = optval;
			const pgm_time_t usecs = pgm_timer_expiration (sock);
			tv->tv_sec  = usecs / 1000000UL;
			tv->tv_usec = usecs % 1000000UL;
		}
		status = TRUE;
		break;

/* timeout for blocking sends */
	case PGM_RATE_REMAIN:
		if (PGM_UNLIKELY(!sock->is_connected))
			break;
		if (PGM_UNLIKELY(*optlen != sizeof (struct timeval)))
			break;
		{
			struct timeval* tv = optval;
			const pgm_time_t usecs = pgm_rate_remaining (&sock->rate_control, sock->blocklen);
			tv->tv_sec  = usecs / 1000000UL;
			tv->tv_usec = usecs % 1000000UL;
		}
		status = TRUE;
		break;


	}
	pgm_rwlock_reader_unlock (&sock->lock);
	return status;
}

bool
pgm_setsockopt (
	pgm_sock_t* const	sock,
	const int		optname,
	const void*		optval,
	const socklen_t		optlen
	)
{
	bool status = FALSE;
	pgm_return_val_if_fail (sock != NULL, status);
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&sock->lock)))
		pgm_return_val_if_reached (status);
	if (PGM_UNLIKELY(sock->is_connected || sock->is_destroyed)) {
		pgm_rwlock_reader_unlock (&sock->lock);
		return status;
	}
	switch (optname) {

/* RFC2113 IP Router Alert 
 */
	case PGM_IP_ROUTER_ALERT:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		{
			const bool v = (0 != *(const int*)optval);
			if (PGM_SOCKET_ERROR == pgm_sockaddr_router_alert (sock->send_with_router_alert_sock, sock->family, v))
				break;
		}
		status = TRUE;
		break;

/* IPv4:   68 <= tpdu < 65536		(RFC 2765)
 * IPv6: 1280 <= tpdu < 65536		(RFC 2460)
 */
	case PGM_MTU:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval < (sizeof(struct pgm_ip) + sizeof(struct pgm_header))))
			break;
		if (PGM_UNLIKELY(*(const int*)optval > UINT16_MAX))
			break;
		sock->max_tpdu = *(const int*)optval;
		status = TRUE;
		break;

/* 1 = enable multicast loopback.
 * 0 = default, to disable.
 */
	case PGM_MULTICAST_LOOP:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		{
			const bool v = (0 != *(const int*)optval);
#ifndef _WIN32	/* loop on send */
			if (PGM_SOCKET_ERROR == pgm_sockaddr_multicast_loop (sock->send_sock, sock->family, v) ||
			    PGM_SOCKET_ERROR == pgm_sockaddr_multicast_loop (sock->send_with_router_alert_sock, sock->family, v))
				break;
#else		/* loop on receive */
			if (PGM_SOCKET_ERROR == pgm_sockaddr_multicast_loop (sock->recv_sock, sock->family, v))
				break;
#endif
		}
		status = TRUE;
		break;

/* 0 < hops < 256, hops == -1 use kernel default (ignored).
 */
	case PGM_MULTICAST_HOPS:
#ifndef CONFIG_TARGET_WINE
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		if (PGM_UNLIKELY(*(const int*)optval > UINT8_MAX))
			break;
		{
			const unsigned hops = *(const int*)optval;
			if (PGM_SOCKET_ERROR == pgm_sockaddr_multicast_hops (sock->send_sock, sock->family, hops) ||
			    PGM_SOCKET_ERROR == pgm_sockaddr_multicast_hops (sock->send_with_router_alert_sock, sock->family, hops))
				break;
		}
#endif
		status = TRUE;
		break;

/* IP Type of Service (ToS) or RFC 3246, differentiated services (DSCP)
 */
	case PGM_TOS:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_SOCKET_ERROR == pgm_sockaddr_tos (sock->send_sock, sock->family, *(const int*)optval) ||
		    PGM_SOCKET_ERROR == pgm_sockaddr_tos (sock->send_with_router_alert_sock, sock->family, *(const int*)optval))
		{
			pgm_warn (_("ToS/DSCP setting requires CAP_NET_ADMIN or ADMIN capability."));
			break;
		}
		status = TRUE;
		break;

/* 0 < wmem < wmem_max (user)
 *
 * operating system and sysctl dependent maximum, minimum on Linux 256 (doubled).
 */
	case PGM_SNDBUF:
		if (PGM_SOCKET_ERROR == setsockopt (sock->send_sock, SOL_SOCKET, SO_SNDBUF, (const char*)optval, optlen) ||
		    PGM_SOCKET_ERROR == setsockopt (sock->send_with_router_alert_sock, SOL_SOCKET, SO_SNDBUF, (const char*)optval, optlen))
			break;
		status = TRUE;
		break;

/* 0 < rmem < rmem_max (user)
 *
 * minimum on Linux is 2048 (doubled).
 */
	case PGM_RCVBUF:
		if (PGM_SOCKET_ERROR == setsockopt (sock->recv_sock, SOL_SOCKET, SO_RCVBUF, (const char*)optval, optlen))
			break;
		status = TRUE;
		break;

/* periodic ambient broadcast SPM interval in milliseconds.
 */
	case PGM_AMBIENT_SPM:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->spm_ambient_interval = *(const int*)optval;
		status = TRUE;
		break;

/* sequence of heartbeat broadcast SPMS to flush out original 
 */
	case PGM_HEARTBEAT_SPM:
		if (PGM_UNLIKELY(0 != optlen % sizeof (int)))
			break;
		{
			sock->spm_heartbeat_len = optlen / sizeof (int);
			sock->spm_heartbeat_interval = pgm_new (unsigned, sock->spm_heartbeat_len + 1);
			sock->spm_heartbeat_interval[0] = 0;
			for (unsigned i = 0; i < sock->spm_heartbeat_len; i++)
				sock->spm_heartbeat_interval[i + 1] = ((const int*)optval)[i];
		}
		status = TRUE;
		break;

/* size of transmit window in sequence numbers.
 * 0 < txw_sqns < one less than half sequence space
 */
	case PGM_TXW_SQNS:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		if (PGM_UNLIKELY(*(const int*)optval >= ((UINT32_MAX/2)-1)))
			break;
		sock->txw_sqns = *(const int*)optval;
		status = TRUE;
		break;

/* size of transmit window in seconds.
 * 0 < secs < ( txw_sqns / txw_max_rte )
 */
	case PGM_TXW_SECS:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->txw_secs = *(const int*)optval;
		status = TRUE;
		break;

/* maximum transmit rate.
 * 0 < txw_max_rte < interface capacity
 *  10mb :   1250000
 * 100mb :  12500000
 *   1gb : 125000000
 */
	case PGM_TXW_MAX_RTE:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->txw_max_rte = *(const int*)optval;
		status = TRUE;
		break;

/* timeout for peers.
 * 0 < 2 * spm_ambient_interval <= peer_expiry
 */
	case PGM_PEER_EXPIRY:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->peer_expiry = *(const int*)optval;
		status = TRUE;
		break;

/* maximum back off range for listening for multicast SPMR.
 * 0 < spmr_expiry < spm_ambient_interval
 */
	case PGM_SPMR_EXPIRY:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->spmr_expiry = *(const int*)optval;
		status = TRUE;

/* size of receive window in sequence numbers.
 * 0 < rxw_sqns < one less than half sequence space
 */
	case PGM_RXW_SQNS:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		if (PGM_UNLIKELY(*(const int*)optval >= ((UINT32_MAX/2)-1)))
			break;
		sock->rxw_sqns = *(const int*)optval;
		status = TRUE;
		break;

/* size of receive window in seconds.
 * 0 < secs < ( rxw_sqns / rxw_max_rte )
 */
	case PGM_RXW_SECS:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->rxw_secs = *(const int*)optval;
		status = TRUE;
		break;

/* maximum receive rate, for determining window size with txw_secs.
 * 0 < rxw_max_rte < interface capacity
 */
	case PGM_RXW_MAX_RTE:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->rxw_max_rte = *(const int*)optval;
		status = TRUE;
		break;

/* maximum NAK back-off value nak_rb_ivl in milliseconds.
 * 0 < nak_rb_ivl <= nak_bo_ivl
 */
	case PGM_NAK_BO_IVL:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->nak_bo_ivl = *(const int*)optval;
		status = TRUE;
		break;

/* repeat interval prior to re-sending a NAK, in milliseconds.
 */
	case PGM_NAK_RPT_IVL:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->nak_rpt_ivl = *(const int*)optval;
		status = TRUE;
		break;

/* interval waiting for repair data, in milliseconds.
 */
	case PGM_NAK_RDATA_IVL:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->nak_rdata_ivl = *(const int*)optval;
		status = TRUE;
		break;

/* limit for data.
 * 0 < nak_data_retries < 256
 */
	case PGM_NAK_DATA_RETRIES:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		if (PGM_UNLIKELY(*(const int*)optval > UINT8_MAX))
			break;
		sock->nak_data_retries = *(const int*)optval;
		status = TRUE;
		break;

/* limit for NAK confirms.
 * 0 < nak_ncf_retries < 256
 */
	case PGM_NAK_NCF_RETRIES:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		if (PGM_UNLIKELY(*(const int*)optval > UINT8_MAX))
			break;
		sock->nak_ncf_retries = *(const int*)optval;
		status = TRUE;
		break;

/* Enable FEC for this sock, specifically Reed Solmon encoding RS(n,k), common
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
 */
	case PGM_USE_FEC:
		if (PGM_UNLIKELY(optlen != sizeof (struct pgm_fecinfo_t)))
			break;
		{
			const struct pgm_fecinfo_t* fecinfo = optval;
			if (PGM_UNLIKELY(0 != (fecinfo->group_size & (fecinfo->group_size - 1))))
				break;
			if (PGM_UNLIKELY(fecinfo->group_size < 2 || fecinfo->group_size > 128))
				break;
			if (PGM_UNLIKELY((fecinfo->group_size + 1) < fecinfo->block_size))
				break;
			const uint8_t parity_packets = fecinfo->block_size - fecinfo->group_size;
/* technically could re-send previous packets */
			if (PGM_UNLIKELY(fecinfo->proactive_packets > parity_packets))
				break;
/* check validity of parameters */
			if (PGM_UNLIKELY(fecinfo->group_size > 223 && ((parity_packets * 223.0) / fecinfo->group_size) < 1.0))
			{
				pgm_error (_("k/h ratio too low to generate parity data."));
				break;
			}
			sock->use_proactive_parity	= (fecinfo->proactive_packets > 0);
			sock->use_ondemand_parity	= fecinfo->ondemand_parity_enabled;
			sock->use_var_pktlen		= fecinfo->var_pktlen_enabled;
			sock->rs_n			= fecinfo->block_size;
			sock->rs_k			= fecinfo->group_size;
			sock->rs_proactive_h		= fecinfo->proactive_packets;
		}
		status = TRUE;
		break;

/* congestion reporting */
	case PGM_USE_CR:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		if (PGM_UNLIKELY(*(const int*)optval <= 0))
			break;
		sock->crqst_ivl = *(const int*)optval;
		sock->use_cr    = (sock->crqst_ivl > 0);
		status = TRUE;
		break;

/* congestion control */
	case PGM_USE_PGMCC:
		if (PGM_UNLIKELY(optlen != sizeof (struct pgm_pgmccinfo_t)))
			break;
		{
			const struct pgm_pgmccinfo_t* pgmccinfo = optval;
			sock->ack_bo_ivl = pgmccinfo->ack_bo_ivl;
			sock->ack_c      = pgmccinfo->ack_c;
			sock->ack_c_p    = pgmccinfo->ack_c_p;
			sock->use_pgmcc  = (sock->ack_c > 0);
		}
		status = TRUE;
		break;

/* declare socket only for sending, discard any incoming SPM, ODATA,
 * RDATA, etc, packets.
 */
	case PGM_SEND_ONLY:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		sock->can_recv_data = (0 == *(const int*)optval);
		status = TRUE;
		break;

/* declare socket only for receiving, no transmit window will be created
 * and no SPM broadcasts sent.
 */
	case PGM_RECV_ONLY:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		sock->can_send_data = (0 == *(const int*)optval);
		status = TRUE;
		break;

/* passive receiving socket, i.e. no back channel to source
 */
	case PGM_PASSIVE:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		sock->can_send_nak = (0 == *(const int*)optval);
		status = TRUE;
		break;

/* on unrecoverable data loss stop socket from further transmission and
 * receiving.
 */
	case PGM_ABORT_ON_RESET:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		sock->is_abort_on_reset = (0 != *(const int*)optval);
		status = TRUE;
		break;

/* default non-blocking operation on send and receive sockets.
 */
	case PGM_NOBLOCK:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		sock->is_nonblocking = (0 != *(const int*)optval);
		pgm_sockaddr_nonblocking (sock->recv_sock, sock->is_nonblocking);
		pgm_sockaddr_nonblocking (sock->send_sock, sock->is_nonblocking);
		pgm_sockaddr_nonblocking (sock->send_with_router_alert_sock, sock->is_nonblocking);
		status = TRUE;
		break;

/* sending group, singular.
 */
	case PGM_SEND_GROUP:
		if (PGM_UNLIKELY(optlen != sizeof(struct group_req)))
			break;
		memcpy (&sock->send_gsr, optval, optlen);
		((struct sockaddr_in*)&sock->send_gsr.gsr_group)->sin_port = htons (sock->udp_encap_mcast_port);
		if (PGM_UNLIKELY(sock->family != sock->send_gsr.gsr_group.ss_family))
			break;
		if (PGM_SOCKET_ERROR == pgm_sockaddr_multicast_if (sock->send_sock, (const struct sockaddr*)&sock->send_gsr.gsr_group, sock->send_gsr.gsr_interface) ||
		    PGM_SOCKET_ERROR == pgm_sockaddr_multicast_if (sock->send_with_router_alert_sock, (const struct sockaddr*)&sock->send_gsr.gsr_group, sock->send_gsr.gsr_interface))
			break;
		status = TRUE;
		break;

/* for any-source applications (ASM), join a new group
 */
	case PGM_JOIN_GROUP:
		if (PGM_UNLIKELY(optlen != sizeof(struct group_req)))
			break;
		if (PGM_UNLIKELY(sock->recv_gsr_len >= IP_MAX_MEMBERSHIPS))
			break;
		{
			const struct group_req* gr = optval;
/* verify not duplicate group/interface pairing */
			for (unsigned i = 0; i < sock->recv_gsr_len; i++)
			{
				if (pgm_sockaddr_cmp ((const struct sockaddr*)&gr->gr_group, (struct sockaddr*)&sock->recv_gsr[i].gsr_group)  == 0 &&
				    pgm_sockaddr_cmp ((const struct sockaddr*)&gr->gr_group, (struct sockaddr*)&sock->recv_gsr[i].gsr_source) == 0 &&
					(gr->gr_interface == sock->recv_gsr[i].gsr_interface ||
					                0 == sock->recv_gsr[i].gsr_interface   ))
				{
#ifdef SOCKET_DEBUG
					char s[INET6_ADDRSTRLEN];
					pgm_sockaddr_ntop ((const struct sockaddr*)&gr->gr_group, s, sizeof(s));
					if (sock->recv_gsr[i].gsr_interface) {
						pgm_warn(_("Socket has already joined group %s on interface %u"), s, gr->gr_interface);
					} else {
						pgm_warn(_("Socket has already joined group %s on all interfaces."), s);
					}
#endif
					break;
				}
			}
			if (PGM_UNLIKELY(sock->family != gr->gr_group.ss_family))
				break;
			if (PGM_SOCKET_ERROR == pgm_sockaddr_join_group (sock->recv_sock, sock->family, gr))
				break;
			sock->recv_gsr[sock->recv_gsr_len].gsr_interface = gr->gr_interface;
			memcpy (&sock->recv_gsr[sock->recv_gsr_len].gsr_group, &gr->gr_group, pgm_sockaddr_len ((const struct sockaddr*)&gr->gr_group));
			memcpy (&sock->recv_gsr[sock->recv_gsr_len].gsr_source, &gr->gr_group, pgm_sockaddr_len ((const struct sockaddr*)&gr->gr_group));
			sock->recv_gsr_len++;
		}
		status = TRUE;
		break;

/* for any-source applications (ASM), leave a joined group.
 */
	case PGM_LEAVE_GROUP:
		if (PGM_UNLIKELY(optlen != sizeof(struct group_req)))
			break;
		if (PGM_UNLIKELY(0 == sock->recv_gsr_len))
			break;
		{
			const struct group_req* gr = optval;
			for (unsigned i = 0; i < sock->recv_gsr_len;)
			{
				if ((pgm_sockaddr_cmp ((const struct sockaddr*)&gr->gr_group, (struct sockaddr*)&sock->recv_gsr[i].gsr_group) == 0) &&
/* drop all matching receiver entries */
				            (gr->gr_interface == 0 ||
/* drop all sources with matching interface */
					     gr->gr_interface == sock->recv_gsr[i].gsr_interface) )
				{
					sock->recv_gsr_len--;
					if (i < (IP_MAX_MEMBERSHIPS - 1))
					{
						memmove (&sock->recv_gsr[i], &sock->recv_gsr[i+1], (sock->recv_gsr_len - i) * sizeof(struct group_source_req));
						continue;
					}
				}
				i++;
			}
			if (PGM_UNLIKELY(sock->family != gr->gr_group.ss_family))
				break;
			if (PGM_SOCKET_ERROR == pgm_sockaddr_leave_group (sock->recv_sock, sock->family, gr))
				break;
		}
		status = TRUE;
		break;

/* for any-source applications (ASM), turn off a given source
 */
	case PGM_BLOCK_SOURCE:
		if (PGM_UNLIKELY(optlen != sizeof(struct group_source_req)))
			break;
		{
			const struct group_source_req* gsr = optval;
			if (PGM_UNLIKELY(sock->family != gsr->gsr_group.ss_family))
				break;
			if (PGM_SOCKET_ERROR == pgm_sockaddr_block_source (sock->recv_sock, sock->family, gsr))
				break;
		}
		status = TRUE;
		break;

/* for any-source applications (ASM), re-allow a blocked source
 */
	case PGM_UNBLOCK_SOURCE:
		if (PGM_UNLIKELY(optlen != sizeof(struct group_source_req)))
			break;
		{
			const struct group_source_req* gsr = optval;
			if (PGM_UNLIKELY(sock->family != gsr->gsr_group.ss_family))
				break;
			if (PGM_SOCKET_ERROR == pgm_sockaddr_unblock_source (sock->recv_sock, sock->family, gsr))
				break;
		}
		status = TRUE;
		break;

/* for controlled-source applications (SSM), join each group/source pair.
 *
 * SSM joins are allowed on top of ASM in order to merge a remote source onto the local segment.
 */
	case PGM_JOIN_SOURCE_GROUP:
		if (PGM_UNLIKELY(optlen != sizeof(struct group_source_req)))
			break;
		if (PGM_UNLIKELY(sock->recv_gsr_len >= IP_MAX_MEMBERSHIPS))
			break;
		{
			const struct group_source_req* gsr = optval;
/* verify if existing group/interface pairing */
			for (unsigned i = 0; i < sock->recv_gsr_len; i++)
			{
				if (pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_group, (struct sockaddr*)&sock->recv_gsr[i].gsr_group) == 0 &&
					(gsr->gsr_interface == sock->recv_gsr[i].gsr_interface ||
					                  0 == sock->recv_gsr[i].gsr_interface   ))
				{
					if (pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_source, (struct sockaddr*)&sock->recv_gsr[i].gsr_source) == 0)
					{
#ifdef SOCKET_DEBUG
						char s1[INET6_ADDRSTRLEN], s2[INET6_ADDRSTRLEN];
						pgm_sockaddr_ntop ((const struct sockaddr*)&gsr->gsr_group, s1, sizeof(s1));
						pgm_sockaddr_ntop ((const struct sockaddr*)&gsr->gsr_source, s2, sizeof(s2));
						if (sock->recv_gsr[i].gsr_interface) {
							pgm_warn(_("Socket has already joined group %s from source %s on interface %d"),
								s1, s2, (unsigned)gsr->gsr_interface);
						} else {
							pgm_warn(_("Socket has already joined group %s from source %s on all interfaces"),
								s1, s2);
						}
#endif
						break;
					}
					break;
				}
			}
			if (PGM_UNLIKELY(sock->family != gsr->gsr_group.ss_family))
				break;
			if (PGM_UNLIKELY(sock->family != gsr->gsr_source.ss_family))
				break;
			if (PGM_SOCKET_ERROR == pgm_sockaddr_join_source_group (sock->recv_sock, sock->family, gsr))
				break;
			memcpy (&sock->recv_gsr[sock->recv_gsr_len], gsr, sizeof(struct group_source_req));
			sock->recv_gsr_len++;
		}
		status = TRUE;
		break;

/* for controlled-source applications (SSM), leave each group/source pair
 */
	case PGM_LEAVE_SOURCE_GROUP:
		if (PGM_UNLIKELY(optlen != sizeof(struct group_source_req)))
			break;
		if (PGM_UNLIKELY(0 == sock->recv_gsr_len))
			break;
		{
			const struct group_source_req* gsr = optval;
/* verify if existing group/interface pairing */
			for (unsigned i = 0; i < sock->recv_gsr_len; i++)
			{
				if (pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_group, (struct sockaddr*)&sock->recv_gsr[i].gsr_group)   == 0 &&
				    pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_source, (struct sockaddr*)&sock->recv_gsr[i].gsr_source) == 0 &&
				    gsr->gsr_interface == sock->recv_gsr[i].gsr_interface)
				{
					sock->recv_gsr_len--;
					if (i < (IP_MAX_MEMBERSHIPS - 1))
					{
						memmove (&sock->recv_gsr[i], &sock->recv_gsr[i+1], (sock->recv_gsr_len - i) * sizeof(struct group_source_req));
						break;
					}
				}
			}
			if (PGM_UNLIKELY(sock->family != gsr->gsr_group.ss_family))
				break;
			if (PGM_UNLIKELY(sock->family != gsr->gsr_source.ss_family))
				break;
			if (PGM_SOCKET_ERROR == pgm_sockaddr_leave_source_group (sock->recv_sock, sock->family, gsr))
				break;
		}
		status = TRUE;
		break;

/* batch block and unblock sources */
	case PGM_MSFILTER:
#if defined(MCAST_MSFILTER) || defined(SIOCSMSFILTER)
		if (PGM_UNLIKELY(optlen < sizeof(struct group_filter)))
			break;
		{
			const struct group_filter* gf_list = optval;
			if (GROUP_FILTER_SIZE( gf_list->gf_numsrc ) != optlen)
				break;
			if (PGM_UNLIKELY(sock->family != gf_list->gf_group.ss_family))
				break;
/* check only first */
			if (PGM_UNLIKELY(sock->family != gf_list->gf_slist[0].ss_family))
				break;
			if (PGM_SOCKET_ERROR == pgm_sockaddr_msfilter (sock->recv_sock, sock->family, gf_list))
				break;
		}
		status = TRUE;
#endif
		break;

/* UDP encapsulation ports */
	case PGM_UDP_ENCAP_UCAST_PORT:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		sock->udp_encap_ucast_port = *(const int*)optval;
		status = TRUE;
		break;

	case PGM_UDP_ENCAP_MCAST_PORT:
		if (PGM_UNLIKELY(optlen != sizeof (int)))
			break;
		sock->udp_encap_mcast_port = *(const int*)optval;
		status = TRUE;
		break;

	}

	pgm_rwlock_reader_unlock (&sock->lock);
	return status;
}

bool
pgm_bind (
	pgm_sock_t*                       restrict sock,
	const struct pgm_sockaddr_t*const restrict sockaddr,
	const socklen_t			           sockaddrlen,
	pgm_error_t**	                  restrict error
	)
{
	struct pgm_interface_req_t null_req;
	memset (&null_req, 0, sizeof(null_req));
	return pgm_bind3 (sock, sockaddr, sockaddrlen, &null_req, sizeof(null_req), &null_req, sizeof(null_req), error);
}

/* bind the sockets to the link layer to start receiving data.
 *
 * returns TRUE on success, or FALSE on error and sets error appropriately,
 */

bool
pgm_bind3 (
	pgm_sock_t*		      restrict sock,
	const struct pgm_sockaddr_t*  restrict sockaddr,
	const socklen_t			       sockaddrlen,
	const struct pgm_interface_req_t*      send_req,		/* only use gr_interface and gr_group::sin6_scope */
	const socklen_t			       send_req_len,
	const struct pgm_interface_req_t*      recv_req,
	const socklen_t			       recv_req_len,
	pgm_error_t**		      restrict error			/* maybe NULL */
	)
{
	pgm_return_val_if_fail (NULL != sock, FALSE);
	pgm_return_val_if_fail (NULL != sockaddr, FALSE);
	pgm_return_val_if_fail (0 != sockaddrlen, FALSE);
	if (sockaddr->sa_addr.sport) pgm_return_val_if_fail (sockaddr->sa_addr.sport != sockaddr->sa_port, FALSE);
	pgm_return_val_if_fail (NULL != send_req, FALSE);
	pgm_return_val_if_fail (sizeof(struct pgm_interface_req_t) == send_req_len, FALSE);
	pgm_return_val_if_fail (NULL != recv_req, FALSE);
	pgm_return_val_if_fail (sizeof(struct pgm_interface_req_t) == recv_req_len, FALSE);

	if (!pgm_rwlock_writer_trylock (&sock->lock))
		pgm_return_val_if_reached (FALSE);
	if (sock->is_bound ||
	    sock->is_destroyed)
	{
		pgm_rwlock_writer_unlock (&sock->lock);
		pgm_return_val_if_reached (FALSE);
	}

/* sanity checks on state */
	if (sock->max_tpdu < (sizeof(struct pgm_ip) + sizeof(struct pgm_header))) {
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_SOCKET,
			       PGM_ERROR_FAILED,
			       _("Invalid maximum TPDU size."));
		pgm_rwlock_writer_unlock (&sock->lock);
		return FALSE;
	}
	if (sock->can_send_data) {
		if (PGM_UNLIKELY(0 == sock->spm_ambient_interval)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("SPM ambient interval not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->spm_heartbeat_len)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("SPM heartbeat interval not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->txw_sqns && 0 == sock->txw_secs)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("TXW_SQNS not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->txw_sqns && 0 == sock->txw_max_rte)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("TXW_MAX_RTE not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
	}
	if (sock->can_recv_data) {
		if (PGM_UNLIKELY(0 == sock->rxw_sqns && 0 == sock->rxw_secs)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("RXW_SQNS not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->rxw_sqns && 0 == sock->rxw_max_rte)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("RXW_MAX_RTE not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->peer_expiry)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("Peer timeout not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->spmr_expiry)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("SPM-Request timeout not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->nak_bo_ivl)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("NAK_BO_IVL not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->nak_rpt_ivl)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("NAK_RPT_IVL not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->nak_rdata_ivl)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("NAK_RDATA_IVL not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->nak_data_retries)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("NAK_DATA_RETRIES not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
		if (PGM_UNLIKELY(0 == sock->nak_ncf_retries)) {
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       PGM_ERROR_FAILED,
				       _("NAK_NCF_RETRIES not configured."));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
	}

	pgm_debug ("bind3 (sock:%p sockaddr:%p sockaddrlen:%u send-req:%p send-req-len:%u recv-req:%p recv-req-len:%u error:%p)",
		 (const void*)sock, (const void*)sockaddr, (unsigned)sockaddrlen, (const void*)send_req, (unsigned)send_req_len, (const void*)recv_req, (unsigned)recv_req_len, (const void*)error);

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

	if (sock->can_send_data)
	{
/* Windows notify call will raise an assertion on error, only Unix versions will return
 * a valid error.
 */
		if (0 != pgm_notify_init (&sock->rdata_notify))
		{
			const int save_errno = errno;
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       pgm_error_from_errno (save_errno),
				       _("Creating RDATA notification channel: %s"),
				       strerror (save_errno));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
	}
	if (0 != pgm_notify_init (&sock->pending_notify))
	{
		const int save_errno = errno;
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_SOCKET,
			       pgm_error_from_errno (save_errno),
			       _("Creating waiting peer notification channel: %s"),
			       strerror (save_errno));
		pgm_rwlock_writer_unlock (&sock->lock);
		return FALSE;
	}

/* determine IP header size for rate regulation engine & stats */
	sock->iphdr_len = (AF_INET == sock->family) ? sizeof(struct pgm_ip) : sizeof(struct pgm_ip6_hdr);
	pgm_trace (PGM_LOG_ROLE_NETWORK,"Assuming IP header size of %zu bytes", sock->iphdr_len);

	if (sock->udp_encap_ucast_port) {
		const size_t udphdr_len = sizeof(struct pgm_udphdr);
		pgm_trace (PGM_LOG_ROLE_NETWORK,"Assuming UDP header size of %zu bytes", udphdr_len);
		sock->iphdr_len += udphdr_len;
	}

	const sa_family_t pgmcc_family = sock->use_pgmcc ? sock->family : 0;
	sock->max_tsdu = sock->max_tpdu - sock->iphdr_len - pgm_pkt_offset (FALSE, pgmcc_family);
	sock->max_tsdu_fragment = sock->max_tpdu - sock->iphdr_len - pgm_pkt_offset (TRUE, pgmcc_family);
	const unsigned max_fragments = sock->txw_sqns ? MIN( PGM_MAX_FRAGMENTS, sock->txw_sqns ) : PGM_MAX_FRAGMENTS;
	sock->max_apdu = MIN( PGM_MAX_APDU, max_fragments * sock->max_tsdu_fragment );

	if (sock->can_send_data)
	{
		pgm_trace (PGM_LOG_ROLE_TX_WINDOW,_("Create transmit window."));
		sock->window = sock->txw_sqns ?
					pgm_txw_create (&sock->tsi,
							0,			/* MAX_TPDU */
							sock->txw_sqns,		/* TXW_SQNS */
							0,			/* TXW_SECS */
							0,			/* TXW_MAX_RTE */
							sock->use_ondemand_parity || sock->use_proactive_parity,
							sock->rs_n,
							sock->rs_k) :
					pgm_txw_create (&sock->tsi,
							sock->max_tpdu,		/* MAX_TPDU */
							0,			/* TXW_SQNS */
							sock->txw_secs,		/* TXW_SECS */
							sock->txw_max_rte,	/* TXW_MAX_RTE */
							sock->use_ondemand_parity || sock->use_proactive_parity,
							sock->rs_n,
							sock->rs_k);
		pgm_assert (NULL != sock->window);
	}

/* create peer list */
	if (sock->can_recv_data) {
		sock->peers_hashtable = pgm_hashtable_new (pgm_tsi_hash, pgm_tsi_equal);
		pgm_assert (NULL != sock->peers_hashtable);
	}

	if (IPPROTO_UDP == sock->protocol)
	{
/* Stevens: "SO_REUSEADDR has datatype int."
 */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Set socket sharing."));
		const int v = 1;
		if (PGM_SOCKET_ERROR == setsockopt (sock->recv_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v)) ||
		    PGM_SOCKET_ERROR == setsockopt (sock->send_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v)) ||
		    PGM_SOCKET_ERROR == setsockopt (sock->send_with_router_alert_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v)))
		{
			const int save_errno = pgm_sock_errno();
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       pgm_error_from_sock_errno (save_errno),
				       _("Enabling reuse of socket local address: %s"),
				       pgm_sock_strerror (save_errno));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}

/* request extra packet information to determine destination address on each packet */
#ifndef CONFIG_TARGET_WINE
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Request socket packet-info."));
		const sa_family_t recv_family = sock->family;
		if (PGM_SOCKET_ERROR == pgm_sockaddr_pktinfo (sock->recv_sock, recv_family, TRUE))
		{
			const int save_errno = pgm_sock_errno();
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       pgm_error_from_sock_errno (save_errno),
				       _("Enabling receipt of ancillary information per incoming packet: %s"),
				       pgm_sock_strerror (save_errno));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
#endif
	}
	else
	{
		const sa_family_t recv_family = sock->family;
		if (AF_INET == recv_family)
		{
/* include IP header only for incoming data, only works for IPv4 */
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Request IP headers."));
			if (PGM_SOCKET_ERROR == pgm_sockaddr_hdrincl (sock->recv_sock, recv_family, TRUE))
			{
				const int save_errno = pgm_sock_errno();
				pgm_set_error (error,
					       PGM_ERROR_DOMAIN_SOCKET,
					       pgm_error_from_sock_errno (save_errno),
					       _("Enabling IP header in front of user data: %s"),
					       pgm_sock_strerror (save_errno));
				pgm_rwlock_writer_unlock (&sock->lock);
				return FALSE;
			}
		}
		else
		{
			pgm_assert (AF_INET6 == recv_family);
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Request socket packet-info."));
			if (PGM_SOCKET_ERROR == pgm_sockaddr_pktinfo (sock->recv_sock, recv_family, TRUE))
			{
				const int save_errno = pgm_sock_errno();
				pgm_set_error (error,
					       PGM_ERROR_DOMAIN_SOCKET,
					       pgm_error_from_sock_errno (save_errno),
					       _("Enabling receipt of control message per incoming datagram: %s"),
					       pgm_sock_strerror (save_errno));
				pgm_rwlock_writer_unlock (&sock->lock);
				return FALSE;
			}
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
	if (AF_INET == sock->family) {
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
	if (!pgm_if_indextoaddr (recv_req->ir_interface,
			         sock->family,
				 recv_req->ir_scope_id,
			         &recv_addr.sa,
			         error))
	{
		pgm_rwlock_writer_unlock (&sock->lock);
		return FALSE;
	}
	pgm_trace (PGM_LOG_ROLE_NETWORK,_("Binding receive socket to interface index %u scope %u"),
		   recv_req->ir_interface,
		   recv_req->ir_scope_id);

#endif /* CONFIG_BIND_INADDR_ANY */

	memcpy (&recv_addr2.sa, &recv_addr.sa, pgm_sockaddr_len (&recv_addr.sa));
	((struct sockaddr_in*)&recv_addr)->sin_port = htons (sock->udp_encap_mcast_port);
	if (PGM_SOCKET_ERROR == bind (sock->recv_sock,
				      &recv_addr.sa,
				      pgm_sockaddr_len (&recv_addr.sa)))
	{
		char addr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&recv_addr, addr, sizeof(addr));
		const int save_errno = pgm_sock_errno();
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_SOCKET,
			       pgm_error_from_sock_errno (save_errno),
			       _("Binding receive socket to address %s: %s"),
			       addr,
			       pgm_sock_strerror (save_errno));
		pgm_rwlock_writer_unlock (&sock->lock);
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

	if (!pgm_if_indextoaddr (send_req->ir_interface,
				 sock->family,
				 send_req->ir_scope_id,
				 (struct sockaddr*)&send_addr,
				 error))
	{
		pgm_rwlock_writer_unlock (&sock->lock);
		return FALSE;
	}
	else if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
	{
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Binding send socket to interface index %u scope %u"),
			   send_req->ir_interface,
			   send_req->ir_scope_id);
	}

	memcpy (&send_with_router_alert_addr, &send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));
	if (PGM_SOCKET_ERROR == bind (sock->send_sock,
				      (struct sockaddr*)&send_addr,
				      pgm_sockaddr_len ((struct sockaddr*)&send_addr)))
	{
		char addr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&send_addr, addr, sizeof(addr));
		const int save_errno = pgm_sock_errno();
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_SOCKET,
			       pgm_error_from_sock_errno (save_errno),
			       _("Binding send socket to address %s: %s"),
			       addr,
			       pgm_sock_strerror (save_errno));
		pgm_rwlock_writer_unlock (&sock->lock);
		return FALSE;
	}

/* resolve bound address if 0.0.0.0 */
	if (AF_INET == send_addr.ss.ss_family)
	{
		if ((INADDR_ANY == ((struct sockaddr_in*)&send_addr)->sin_addr.s_addr) &&
		    !pgm_if_getnodeaddr (AF_INET, (struct sockaddr*)&send_addr, sizeof(send_addr), error))
		{
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}
	}
	else if ((memcmp (&in6addr_any, &((struct sockaddr_in6*)&send_addr)->sin6_addr, sizeof(in6addr_any)) == 0) &&
		 !pgm_if_getnodeaddr (AF_INET6, (struct sockaddr*)&send_addr, sizeof(send_addr), error))
	{
		pgm_rwlock_writer_unlock (&sock->lock);
		return FALSE;
	}

	if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&send_addr, s, sizeof(s));
		pgm_debug ("bind succeeded on send_gsr interface %s", s);
	}

	if (PGM_SOCKET_ERROR == bind (sock->send_with_router_alert_sock,
				      (struct sockaddr*)&send_with_router_alert_addr,
				      pgm_sockaddr_len((struct sockaddr*)&send_with_router_alert_addr)))
	{
		char addr[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&send_with_router_alert_addr, addr, sizeof(addr));
		const int save_errno = pgm_sock_errno();
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_SOCKET,
			       pgm_error_from_sock_errno (save_errno),
			       _("Binding IP Router Alert (RFC 2113) send socket to address %s: %s"),
			       addr,
			       pgm_sock_strerror (save_errno));
		pgm_rwlock_writer_unlock (&sock->lock);
		return FALSE;
	}

	if (PGM_UNLIKELY(pgm_log_mask & PGM_LOG_ROLE_NETWORK))
	{
		char s[INET6_ADDRSTRLEN];
		pgm_sockaddr_ntop ((struct sockaddr*)&send_with_router_alert_addr, s, sizeof(s));
		pgm_debug ("bind (router alert) succeeded on send_gsr interface %s", s);
	}

/* save send side address for broadcasting as source nla */
	memcpy (&sock->send_addr, &send_addr, pgm_sockaddr_len ((struct sockaddr*)&send_addr));

/* rx to nak processor notify channel */
	if (sock->can_send_data)
	{
/* setup rate control */
		if (sock->txw_max_rte)
		{
			pgm_trace (PGM_LOG_ROLE_RATE_CONTROL,_("Setting rate regulation to %zd bytes per second."),
					sock->txw_max_rte);
	
			pgm_rate_create (&sock->rate_control, sock->txw_max_rte, sock->iphdr_len, sock->max_tpdu);
			sock->is_controlled_spm   = TRUE;	/* must always be set */
			sock->is_controlled_odata = TRUE;
			sock->is_controlled_rdata = TRUE;
		}
		else
		{
			sock->is_controlled_spm   = FALSE;
			sock->is_controlled_odata = FALSE;
			sock->is_controlled_rdata = FALSE;
		}
	}

/* allocate first incoming packet buffer */
	sock->rx_buffer = pgm_alloc_skb (sock->max_tpdu);

/* bind complete */
	sock->is_bound = TRUE;

/* cleanup */
	pgm_rwlock_writer_unlock (&sock->lock);
	pgm_debug ("PGM socket successfully bound.");
	return TRUE;
}

bool
pgm_connect (
	pgm_sock_t*   restrict sock,
	pgm_error_t** restrict error	/* maybe NULL */
	)
{
	pgm_return_val_if_fail (sock != NULL, FALSE);
	pgm_return_val_if_fail (sock->recv_gsr_len > 0, FALSE);
#ifdef CONFIG_TARGET_WINE
	pgm_return_val_if_fail (sock->recv_gsr_len == 1, FALSE);
#endif
	for (unsigned i = 0; i < sock->recv_gsr_len; i++)
	{
		pgm_return_val_if_fail (sock->recv_gsr[i].gsr_group.ss_family == sock->recv_gsr[0].gsr_group.ss_family, FALSE);
		pgm_return_val_if_fail (sock->recv_gsr[i].gsr_group.ss_family == sock->recv_gsr[i].gsr_source.ss_family, FALSE);
	}
	pgm_return_val_if_fail (sock->send_gsr.gsr_group.ss_family == sock->recv_gsr[0].gsr_group.ss_family, FALSE);
/* shutdown */
	if (PGM_UNLIKELY(!pgm_rwlock_writer_trylock (&sock->lock)))
		pgm_return_val_if_reached (FALSE);
/* state */
	if (PGM_UNLIKELY(sock->is_connected || !sock->is_bound || sock->is_destroyed)) {
		pgm_rwlock_writer_unlock (&sock->lock);
		pgm_return_val_if_reached (FALSE);
	}

	pgm_debug ("connect (sock:%p error:%p)",
		 (const void*)sock, (const void*)error);

/* rx to nak processor notify channel */
	if (sock->can_send_data)
	{
/* announce new sock by sending out SPMs */
		if (!pgm_send_spm (sock, PGM_OPT_SYN) ||
		    !pgm_send_spm (sock, PGM_OPT_SYN) ||
		    !pgm_send_spm (sock, PGM_OPT_SYN))
		{
			const int save_errno = pgm_sock_errno();
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_SOCKET,
				       pgm_error_from_sock_errno (save_errno),
				       _("Sending SPM broadcast: %s"),
				       pgm_sock_strerror (save_errno));
			pgm_rwlock_writer_unlock (&sock->lock);
			return FALSE;
		}

		sock->next_poll = sock->next_ambient_spm = pgm_time_update_now() + sock->spm_ambient_interval;

/* start PGMCC with one token */
		sock->tokens = sock->cwnd_size = pgm_fp8 (1);

/* slow start threshold */
		sock->ssthresh = pgm_fp8 (4);

/* ACK timeout, should be greater than first SPM heartbeat interval in order to be scheduled correctly */
		sock->ack_expiry_ivl = pgm_secs (3);

/* start full history */
		sock->ack_bitmap = 0xffffffff;
	}
	else
	{
		pgm_assert (sock->can_recv_data);
		sock->next_poll = pgm_time_update_now() + pgm_secs( 30 );
	}

	sock->is_connected = TRUE;

/* cleanup */
	pgm_rwlock_writer_unlock (&sock->lock);
	pgm_debug ("PGM socket successfully connected.");
	return TRUE;
}

/* return local endpoint address
 */

bool
pgm_getsockname (
	pgm_sock_t*      const restrict sock,
	struct pgm_sockaddr_t* restrict addr,
	socklen_t*             restrict addrlen
	)
{
	pgm_assert (NULL != sock);
	pgm_assert (NULL != addr);
	pgm_assert (NULL != addrlen);
	pgm_assert (sizeof(struct pgm_sockaddr_t) == *addrlen);

	if (!sock->is_bound) {
		errno = EBADF;
		return FALSE;
	}

	addr->sa_port = sock->dport;
	memcpy (&addr->sa_addr, &sock->tsi, sizeof(pgm_tsi_t));
	return TRUE;
}

/* add select parameters for the receive socket(s)
 *
 * returns highest file descriptor used plus one.
 */

int
pgm_select_info (
	pgm_sock_t* const restrict sock,
	fd_set*	    const restrict readfds,	/* blocking recv fds */
	fd_set*	    const restrict writefds,	/* blocking send fds */
	int*	    const restrict n_fds	/* in: max fds, out: max (in:fds, sock:fds) */
	)
{
	int fds = 0;

	pgm_assert (NULL != sock);
	pgm_assert (NULL != n_fds);

	if (!sock->is_bound || sock->is_destroyed)
	{
		errno = EBADF;
		return -1;
	}

	if (readfds)
	{
		FD_SET(sock->recv_sock, readfds);
		fds = sock->recv_sock + 1;
		if (sock->can_send_data) {
			const int rdata_fd = pgm_notify_get_fd (&sock->rdata_notify);
			FD_SET(rdata_fd, readfds);
			fds = MAX(fds, rdata_fd + 1);
		}
		const int pending_fd = pgm_notify_get_fd (&sock->pending_notify);
		FD_SET(pending_fd, readfds);
		fds = MAX(fds, pending_fd + 1);
	}

	if (sock->can_send_data && writefds)
	{
		FD_SET(sock->send_sock, writefds);
		fds = MAX(sock->send_sock + 1, fds);
	}

	return *n_fds = MAX(fds, *n_fds);
}

#ifdef CONFIG_HAVE_POLL
/* add poll parameters for the receive socket(s)
 *
 * returns number of pollfd structures filled.
 */

int
pgm_poll_info (
	pgm_sock_t*	 const restrict	sock,
	struct pollfd*   const restrict	fds,
	int*		 const restrict	n_fds,		/* in: #fds, out: used #fds */
	const int			events		/* POLLIN, POLLOUT */
	)
{
	pgm_assert (NULL != sock);
	pgm_assert (NULL != fds);
	pgm_assert (NULL != n_fds);

	if (!sock->is_bound || sock->is_destroyed)
	{
		errno = EBADF;
		return -1;
	}

	int moo = 0;

/* we currently only support one incoming socket */
	if (events & POLLIN)
	{
		pgm_assert ( (1 + moo) <= *n_fds );
		fds[moo].fd = sock->recv_sock;
		fds[moo].events = POLLIN;
		moo++;
		if (sock->can_send_data) {
			pgm_assert ( (1 + moo) <= *n_fds );
			fds[moo].fd = pgm_notify_get_fd (&sock->rdata_notify);
			fds[moo].events = POLLIN;
			moo++;
		}
		pgm_assert ( (1 + moo) <= *n_fds );
		fds[moo].fd = pgm_notify_get_fd (&sock->pending_notify);
		fds[moo].events = POLLIN;
		moo++;
	}

/* ODATA only published on regular socket, no need to poll router-alert sock */
	if (sock->can_send_data && events & POLLOUT)
	{
		pgm_assert ( (1 + moo) <= *n_fds );
		fds[moo].fd = sock->send_sock;
		fds[moo].events = POLLOUT;
		moo++;
	}

	return *n_fds = moo;
}
#endif /* CONFIG_HAVE_POLL */

/* add epoll parameters for the recieve socket(s), events should
 * be set to EPOLLIN to wait for incoming events (data), and EPOLLOUT to wait
 * for non-blocking write.
 *
 * returns 0 on success, -1 on failure and sets errno appropriately.
 */
#ifdef CONFIG_HAVE_EPOLL
int
pgm_epoll_ctl (
	pgm_sock_t* const	sock,
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
	else if (!sock->is_bound || sock->is_destroyed)
	{
		errno = EBADF;
		return -1;
	}

	struct epoll_event event;
	int retval = 0;

	if (events & EPOLLIN)
	{
		event.events = events & (EPOLLIN | EPOLLET | EPOLLONESHOT);
		event.data.ptr = sock;
		retval = epoll_ctl (epfd, op, sock->recv_sock, &event);
		if (retval)
			goto out;
		if (sock->can_send_data) {
			retval = epoll_ctl (epfd, op, pgm_notify_get_fd (&sock->rdata_notify), &event);
			if (retval)
				goto out;
		}
		retval = epoll_ctl (epfd, op, pgm_notify_get_fd (&sock->pending_notify), &event);
		if (retval)
			goto out;

		if (events & EPOLLET)
			sock->is_edge_triggered_recv = TRUE;
	}

	if (sock->can_send_data && events & EPOLLOUT)
	{
		event.events = events & (EPOLLOUT | EPOLLET | EPOLLONESHOT);
		event.data.ptr = sock;
		retval = epoll_ctl (epfd, op, sock->send_sock, &event);
	}
out:
	return retval;
}
#endif

static
const char*
pgm_family_string (
	const int	family
	)
{
	const char* c;

	switch (family) {
	case AF_UNSPEC:		c = "AF_UNSPEC"; break;
	case AF_INET:		c = "AF_INET"; break;
	case AF_INET6:		c = "AF_INET6"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

static
const char*
pgm_sock_type_string (
	const int	sock_type
	)
{
	const char* c;

	switch (sock_type) {
	case SOCK_SEQPACKET:	c = "SOCK_SEQPACKET"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

static
const char*
pgm_protocol_string (
	const int	protocol
	)
{
	const char* c;

	switch (protocol) {
	case IPPROTO_UDP:	c = "IPPROTO_UDP"; break;
	case IPPROTO_PGM:	c = "IPPROTO_PGM"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

/* eof */
