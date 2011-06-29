/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Transport recv API.
 *
 * Copyright (c) 2006-2011 Miru Limited.
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
#	include <config.h>
#endif

#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#include <errno.h>
#ifndef _WIN32
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>		/* _GNU_SOURCE for in6_pktinfo */
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/source.h>
#include <impl/packet_parse.h>
#include <impl/timer.h>
#include <impl/engine.h>


//#define RECV_DEBUG

#ifndef RECV_DEBUG
#	define PGM_DISABLE_ASSERT
#endif

#ifndef _WIN32
#	define PGM_CMSG_FIRSTHDR(msg)		CMSG_FIRSTHDR(msg)
#	define PGM_CMSG_NXTHDR(msg, cmsg)	CMSG_NXTHDR(msg, cmsg)
#	define PGM_CMSG_DATA(cmsg)		CMSG_DATA(cmsg)
#	define PGM_CMSG_SPACE(len)		CMSG_SPACE(len)
#	define PGM_CMSG_LEN(len)		CMSG_LEN(len)
#else
#	define PGM_CMSG_FIRSTHDR(msg)		WSA_CMSG_FIRSTHDR(msg)
#	define PGM_CMSG_NXTHDR(msg, cmsg)	WSA_CMSG_NXTHDR(msg, cmsg)
#	define PGM_CMSG_DATA(cmsg)		WSA_CMSG_DATA(cmsg)
#	define PGM_CMSG_SPACE(len)		WSA_CMSG_SPACE(len)
#	define PGM_CMSG_LEN(len)		WSA_CMSG_LEN(len)
#endif

#ifdef HAVE_WSACMSGHDR
#	ifdef __GNU__
/* as listed in MSDN */
#		define pgm_cmsghdr			wsacmsghdr
#	else
#		define pgm_cmsghdr			_WSACMSGHDR
#	endif
#else
#	define pgm_cmsghdr			cmsghdr
#endif


/* read a packet into a PGM skbuff
 * on success returns packet length, on closed socket returns 0,
 * on error returns -1.
 */

static
ssize_t
recvskb (
	pgm_sock_t*           const restrict sock,
	struct pgm_sk_buff_t* const restrict skb,
	const int			     flags,
	struct sockaddr*      const restrict src_addr,
	const socklen_t			     src_addrlen,
	struct sockaddr*      const restrict dst_addr,
	const socklen_t			     dst_addrlen
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);
	pgm_assert (NULL != src_addr);
	pgm_assert (src_addrlen > 0);
	pgm_assert (NULL != dst_addr);
	pgm_assert (dst_addrlen > 0);

	pgm_debug ("recvskb (sock:%p skb:%p flags:%d src-addr:%p src-addrlen:%d dst-addr:%p dst-addrlen:%d)",
		(void*)sock, (void*)skb, flags, (void*)src_addr, (int)src_addrlen, (void*)dst_addr, (int)dst_addrlen);

	if (PGM_UNLIKELY(sock->is_destroyed))
		return 0;

	struct pgm_iovec iov = {
		.iov_base	= skb->head,
		.iov_len	= sock->max_tpdu
	};
	char aux[ 1024 ];
#ifndef _WIN32
	struct msghdr msg = {
		.msg_name	= src_addr,
		.msg_namelen	= src_addrlen,
		.msg_iov	= (void*)&iov,
		.msg_iovlen	= 1,
		.msg_control	= aux,
		.msg_controllen = sizeof(aux),
		.msg_flags	= 0
	};
	ssize_t len = recvmsg (sock->recv_sock, &msg, flags);
	if (len <= 0)
		return len;
#else /* !_WIN32 */
	WSAMSG msg = {
		.name		= (LPSOCKADDR)src_addr,
		.namelen	= src_addrlen,
		.lpBuffers	= (LPWSABUF)&iov,
		.dwBufferCount	= 1,
		.dwFlags	= 0
	};
	msg.Control.buf		= aux;
	msg.Control.len		= sizeof(aux);
	DWORD len;
	if (SOCKET_ERROR == pgm_WSARecvMsg (sock->recv_sock, &msg, &len, NULL, NULL)) {
		return SOCKET_ERROR;
	}
#endif /* !_WIN32 */

#ifdef PGM_DEBUG
	if (PGM_UNLIKELY(pgm_loss_rate > 0)) {
		const unsigned percent = pgm_rand_int_range (&sock->rand_, 0, 100);
		if (percent <= pgm_loss_rate) {
			pgm_debug ("Simulated packet loss");
			pgm_set_last_sock_error (PGM_SOCK_EAGAIN);
			return SOCKET_ERROR;
		}
	}
#endif

	skb->sock		= sock;
	skb->tstamp		= pgm_time_update_now();
	skb->data		= skb->head;
	skb->len		= (uint16_t)len;
	skb->zero_padded	= 0;
	skb->tail		= (char*)skb->data + len;

	if (sock->udp_encap_ucast_port ||
	    AF_INET6 == pgm_sockaddr_family (src_addr))
	{
		struct pgm_cmsghdr* cmsg;
		for (cmsg = PGM_CMSG_FIRSTHDR(&msg);
		     cmsg != NULL;
		     cmsg = PGM_CMSG_NXTHDR(&msg, cmsg))
		{
/* both IP_PKTINFO and IP_RECVDSTADDR exist on OpenSolaris, so capture
 * each type if defined.
 */
#ifdef IP_PKTINFO
			if (IPPROTO_IP == cmsg->cmsg_level && 
			    IP_PKTINFO == cmsg->cmsg_type)
			{
				const void* pktinfo		= PGM_CMSG_DATA(cmsg);
/* discard on invalid address */
				if (PGM_UNLIKELY(NULL == pktinfo)) {
					pgm_debug ("in_pktinfo is NULL");
					return -1;
				}
				const struct in_pktinfo* in	= pktinfo;
				struct sockaddr_in s4;
				memset (&s4, 0, sizeof(s4));
				s4.sin_family			= AF_INET;
				s4.sin_addr.s_addr		= in->ipi_addr.s_addr;
				memcpy (dst_addr, &s4, sizeof(s4));
				break;
			}
#endif
#ifdef IP_RECVDSTADDR
			if (IPPROTO_IP == cmsg->cmsg_level &&
			    IP_RECVDSTADDR == cmsg->cmsg_type)
			{
				const void* recvdstaddr		= PGM_CMSG_DATA(cmsg);
/* discard on invalid address */
				if (PGM_UNLIKELY(NULL == recvdstaddr)) {
					pgm_debug ("in_recvdstaddr is NULL");
					return -1;
				}
				const struct in_addr* in	= recvdstaddr;
				struct sockaddr_in s4;
				memset (&s4, 0, sizeof(s4));
				s4.sin_family			= AF_INET;
				s4.sin_addr.s_addr		= in->s_addr;
				memcpy (dst_addr, &s4, sizeof(s4));
				break;
			}
#endif
#if !defined(IP_PKTINFO) && !defined(IP_RECVDSTADDR)
#	error "No defined CMSG type for IPv4 destination address."
#endif

			if (IPPROTO_IPV6 == cmsg->cmsg_level && 
			    IPV6_PKTINFO == cmsg->cmsg_type)
			{
				const void* pktinfo		= PGM_CMSG_DATA(cmsg);
/* discard on invalid address */
				if (PGM_UNLIKELY(NULL == pktinfo)) {
					pgm_debug ("in6_pktinfo is NULL");
					return -1;
				}
				const struct in6_pktinfo* in6	= pktinfo;
				struct sockaddr_in6 s6;
				memset (&s6, 0, sizeof(s6));
				s6.sin6_family			= AF_INET6;
				s6.sin6_addr			= in6->ipi6_addr;
				s6.sin6_scope_id		= in6->ipi6_ifindex;
				memcpy (dst_addr, &s6, sizeof(s6));
/* does not set flow id */
				break;
			}
		}
	}
	return len;
}

/* upstream = receiver to source, peer-to-peer = receive to receiver
 *
 * NB: SPMRs can be upstream or peer-to-peer, if the packet is multicast then its
 *     a peer-to-peer message, if its unicast its an upstream message.
 *
 * returns TRUE on valid processed packet, returns FALSE on discarded packet.
 */

static
bool
on_upstream (
	pgm_sock_t*           const restrict sock,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);
	pgm_assert_cmpuint (skb->pgm_header->pgm_dport, ==, sock->tsi.sport);

	pgm_debug ("on_upstream (sock:%p skb:%p)",
		(const void*)sock, (const void*)skb);

	if (PGM_UNLIKELY(!sock->can_send_data)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet for muted source."));
		goto out_discarded;
	}

/* unicast upstream message, note that dport & sport are reversed */
	if (PGM_UNLIKELY(skb->pgm_header->pgm_sport != sock->dport)) {
/* its upstream/peer-to-peer for another session */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet on data-destination port mismatch."));
		goto out_discarded;
	}

	if (PGM_UNLIKELY(!pgm_gsi_equal (&skb->tsi.gsi, &sock->tsi.gsi))) {
/* its upstream/peer-to-peer for another session */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet on data-destination port mismatch."));
		goto out_discarded;
	}

/* advance SKB pointer to PGM type header */
	skb->data	= (char*)skb->data + sizeof(struct pgm_header);
	skb->len       -= sizeof(struct pgm_header);

	switch (skb->pgm_header->pgm_type) {
	case PGM_NAK:
		if (PGM_UNLIKELY(!pgm_on_nak (sock, skb)))
			goto out_discarded;
		break;

	case PGM_NNAK:
		if (PGM_UNLIKELY(!pgm_on_nnak (sock, skb)))
			goto out_discarded;
		break;

	case PGM_SPMR:
		if (PGM_UNLIKELY(!pgm_on_spmr (sock, NULL, skb)))
			goto out_discarded;
		break;

	case PGM_ACK:
		if (PGM_UNLIKELY(!pgm_on_ack (sock, skb)))
			goto out_discarded;
		break;

	case PGM_POLR:
	default:
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded unsupported PGM type packet."));
		goto out_discarded;
	}

	return TRUE;
out_discarded:
	sock->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
	return FALSE;
}

/* peer to peer message, either multicast NAK or multicast SPMR.
 *
 * returns TRUE on valid processed packet, returns FALSE on discarded packet.
 */

static
bool
on_peer (
	pgm_sock_t*           const restrict sock,
	struct pgm_sk_buff_t* const restrict skb,
	pgm_peer_t**		    restrict source
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);
	pgm_assert_cmpuint (skb->pgm_header->pgm_dport, !=, sock->tsi.sport);
	pgm_assert (NULL != source);

	pgm_debug ("on_peer (sock:%p skb:%p source:%p)",
		(const void*)sock, (const void*)skb, (const void*)source);

/* we are not the source */
	if (PGM_UNLIKELY(!sock->can_recv_data)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet for muted receiver."));
		goto out_discarded;
	}

/* unicast upstream message, note that dport & sport are reversed */
	if (PGM_UNLIKELY(skb->pgm_header->pgm_sport != sock->dport)) {
/* its upstream/peer-to-peer for another session */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet on data-destination port mismatch."));
		goto out_discarded;
	}

/* check to see the source this peer-to-peer message is about is in our peer list */
	pgm_tsi_t upstream_tsi;
	memcpy (&upstream_tsi.gsi, &skb->tsi.gsi, sizeof(pgm_gsi_t));
	upstream_tsi.sport = skb->pgm_header->pgm_dport;

	pgm_rwlock_reader_lock (&sock->peers_lock);
	*source = pgm_hashtable_lookup (sock->peers_hashtable, &upstream_tsi);
	pgm_rwlock_reader_unlock (&sock->peers_lock);
	if (PGM_UNLIKELY(NULL == *source)) {
/* this source is unknown, we don't care about messages about it */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded peer packet about new source."));
		goto out_discarded;
	}

/* advance SKB pointer to PGM type header */
	skb->data	= (char*)skb->data + sizeof(struct pgm_header);
	skb->len       -= sizeof(struct pgm_header);

	switch (skb->pgm_header->pgm_type) {
	case PGM_NAK:
		if (PGM_UNLIKELY(!pgm_on_peer_nak (sock, *source, skb)))
			goto out_discarded;
		break;

	case PGM_SPMR:
		if (PGM_UNLIKELY(!pgm_on_spmr (sock, *source, skb)))
			goto out_discarded;
		break;

	case PGM_NNAK:
	case PGM_POLR:
	default:
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded unsupported PGM type packet."));
		goto out_discarded;
	}

	return TRUE;
out_discarded:
	if (*source)
		(*source)->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
	else if (sock->can_send_data)
		sock->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
	return FALSE;
}

/* source to receiver message
 *
 * returns TRUE on valid processed packet, returns FALSE on discarded packet.
 */

static
bool
on_downstream (
	pgm_sock_t*           const restrict sock,
	struct pgm_sk_buff_t* const restrict skb,
	struct sockaddr*      const restrict src_addr,
	struct sockaddr*      const restrict dst_addr,
	pgm_peer_t**	  	    restrict source
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);
	pgm_assert (NULL != src_addr);
	pgm_assert (NULL != dst_addr);
	pgm_assert (NULL != source);

#ifdef RECV_DEBUG
	char saddr[INET6_ADDRSTRLEN], daddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (src_addr, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (dst_addr, daddr, sizeof(daddr));
	pgm_debug ("on_downstream (sock:%p skb:%p src-addr:%s dst-addr:%s source:%p)",
		(const void*)sock, (const void*)skb, saddr, daddr, (const void*)source);
#endif

	if (PGM_UNLIKELY(!sock->can_recv_data)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet for muted receiver."));
		goto out_discarded;
	}

/* pgm packet DPORT contains our sock DPORT */
	if (PGM_UNLIKELY(skb->pgm_header->pgm_dport != sock->dport)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet on data-destination port mismatch."));
		goto out_discarded;
	}

/* search for TSI peer context or create a new one */
	if (PGM_LIKELY(pgm_tsi_hash (&skb->tsi) == sock->last_hash_key &&
			NULL != sock->last_hash_value))
	{
		*source = sock->last_hash_value;
	}
	else
	{
		pgm_rwlock_reader_lock (&sock->peers_lock);
		*source = pgm_hashtable_lookup_extended (sock->peers_hashtable, &skb->tsi, &sock->last_hash_key);
		pgm_rwlock_reader_unlock (&sock->peers_lock);
		if (PGM_UNLIKELY(NULL == *source)) {
			*source = pgm_new_peer (sock,
					       &skb->tsi,
					       (struct sockaddr*)src_addr, pgm_sockaddr_len(src_addr),
					       (struct sockaddr*)dst_addr, pgm_sockaddr_len(dst_addr),
						skb->tstamp);
		}
		sock->last_hash_value = *source;
	}

	(*source)->cumulative_stats[PGM_PC_RECEIVER_BYTES_RECEIVED] += skb->len;
	(*source)->last_packet = skb->tstamp;

	skb->data       = (void*)( skb->pgm_header + 1 );
	skb->len       -= sizeof(struct pgm_header);

/* handle PGM packet type */
	switch (skb->pgm_header->pgm_type) {
	case PGM_ODATA:
	case PGM_RDATA:
		if (PGM_UNLIKELY(!pgm_on_data (sock, *source, skb)))
			goto out_discarded;
		sock->rx_buffer = pgm_alloc_skb (sock->max_tpdu);
		break;

	case PGM_NCF:
		if (PGM_UNLIKELY(!pgm_on_ncf (sock, *source, skb)))
			goto out_discarded;
		break;

	case PGM_SPM:
		if (PGM_UNLIKELY(!pgm_on_spm (sock, *source, skb)))
			goto out_discarded;

/* update group NLA if appropriate */
		if (PGM_LIKELY(pgm_sockaddr_is_addr_multicast ((struct sockaddr*)dst_addr)))
			memcpy (&(*source)->group_nla, dst_addr, pgm_sockaddr_len(dst_addr));
		break;

#ifdef USE_PGM_PROTOCOL_POLL
	case PGM_POLL:
		if (PGM_UNLIKELY(!pgm_on_poll (sock, *source, skb)))
			goto out_discarded;
		break;
#endif

	default:
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded unsupported PGM type packet."));
		goto out_discarded;
	}

	return TRUE;
out_discarded:
	if (*source)
		(*source)->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
	else if (sock->can_send_data)
		sock->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
	return FALSE;
}

/* process a pgm packet
 *
 * returns TRUE on valid processed packet, returns FALSE on discarded packet.
 */
static
bool
on_pgm (
	pgm_sock_t*           const restrict sock,
	struct pgm_sk_buff_t* const restrict skb,
	struct sockaddr*      const restrict src_addr,
	struct sockaddr*      const restrict dst_addr,
	pgm_peer_t**		    restrict source
	)
{
/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (NULL != skb);
	pgm_assert (NULL != src_addr);
	pgm_assert (NULL != dst_addr);
	pgm_assert (NULL != source);

#ifdef RECV_DEBUG
	char saddr[INET6_ADDRSTRLEN], daddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (src_addr, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (dst_addr, daddr, sizeof(daddr));
	pgm_debug ("on_pgm (sock:%p skb:%p src-addr:%s dst-addr:%s source:%p)",
		(const void*)sock, (const void*)skb, saddr, daddr, (const void*)source);
#endif

	if (PGM_IS_DOWNSTREAM (skb->pgm_header->pgm_type))
		return on_downstream (sock, skb, src_addr, dst_addr, source);
	if (skb->pgm_header->pgm_dport == sock->tsi.sport)
	{
		if (PGM_IS_UPSTREAM (skb->pgm_header->pgm_type) ||
		    PGM_IS_PEER (skb->pgm_header->pgm_type))
		{
			return on_upstream (sock, skb);
		}
	}
	else if (PGM_IS_PEER (skb->pgm_header->pgm_type))
		return on_peer (sock, skb, source);

	pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded unknown PGM packet."));
	if (sock->can_send_data)
		sock->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
	return FALSE;
}

/* block on receiving socket whilst holding sock::waiting-mutex
 * returns EAGAIN for waiting data, returns EINTR for waiting timer event,
 * returns ENOENT on closed sock, and returns EFAULT for libc error.
 */

static
int
wait_for_event (
	pgm_sock_t* const	sock
	)
{
	int n_fds = 3;

/* pre-conditions */
	pgm_assert (NULL != sock);

	pgm_debug ("wait_for_event (sock:%p)", (const void*)sock);

	do {
		if (PGM_UNLIKELY(sock->is_destroyed))
			return ENOENT;

		if (sock->can_send_data && !pgm_txw_retransmit_is_empty (sock->window))
/* tight loop on blocked send */
			pgm_on_deferred_nak (sock);

#ifdef HAVE_POLL
		struct pollfd fds[ n_fds ];
		memset (fds, 0, sizeof(fds));
		const int status = pgm_poll_info (sock, fds, &n_fds, POLLIN);
		pgm_assert (-1 != status);
#else
		fd_set readfds;
		FD_ZERO(&readfds);
		const int status = pgm_select_info (sock, &readfds, NULL, &n_fds);
		pgm_assert (-1 != status);
#endif /* HAVE_POLL */

/* flush any waiting notifications */
		if (sock->is_pending_read) {
			pgm_notify_clear (&sock->pending_notify);
			sock->is_pending_read = FALSE;
		}

		int timeout;
		if (sock->can_send_data && !pgm_txw_retransmit_is_empty (sock->window))
			timeout = 0;
		else
			timeout = (int)pgm_timer_expiration (sock);
		
#ifdef HAVE_POLL
		const int ready = poll (fds, n_fds, timeout /* μs */ / 1000 /* to ms */);
#else
		struct timeval tv_timeout = {
			.tv_sec		= timeout > 1000000L ? (timeout / 1000000L) : 0,
			.tv_usec	= timeout > 1000000L ? (timeout % 1000000L) : timeout
		};
		const int ready = select (n_fds, &readfds, NULL, NULL, &tv_timeout);
#endif /* HAVE_POLL */
		if (PGM_UNLIKELY(SOCKET_ERROR == ready)) {
			pgm_debug ("block returned errno=%i",errno);
			return EFAULT;
		} else if (ready > 0) {
			pgm_debug ("recv again on empty");
			return EAGAIN;
		}
	} while (pgm_timer_check (sock));
	pgm_debug ("state generated event");
	return EINTR;
}

/* data incoming on receive sockets, can be from a sender or receiver, or simply bogus.
 * for IPv4 we receive the IP header to handle fragmentation, for IPv6 we cannot, but the
 * underlying stack handles this for us.
 *
 * recvmsgv reads a vector of apdus each contained in a IO scatter/gather array.
 *
 * can be called due to event from incoming socket(s) or timer induced data loss.
 *
 * On success, returns PGM_IO_STATUS_NORMAL and saves the count of bytes read
 * into _bytes_read.  With non-blocking sockets a block returns
 * PGM_IO_STATUS_WOULD_BLOCK.  When rate limited sending repair data, returns
 * PGM_IO_STATUS_RATE_LIMITED and caller should wait.  During recovery state,
 * returns PGM_IO_STATUS_TIMER_PENDING and caller should also wait.  On
 * unrecoverable dataloss, returns PGM_IO_STATUS_CONN_RESET.  If connection is
 * closed, returns PGM_IO_STATUS_EOF.  On error, returns PGM_IO_STATUS_ERROR.
 */

int
pgm_recvmsgv (
	pgm_sock_t*   	   const restrict sock,
	struct pgm_msgv_t* const restrict msg_start,
	const size_t			  msg_len,
	const int			  flags,	/* MSG_DONTWAIT for non-blocking */
	size_t*			 restrict _bytes_read,	/* may be NULL */
	pgm_error_t**		 restrict error
	)
{
	int status = PGM_IO_STATUS_WOULD_BLOCK;

	pgm_debug ("pgm_recvmsgv (sock:%p msg-start:%p msg-len:%" PRIzu " flags:%d bytes-read:%p error:%p)",
		(void*)sock, (void*)msg_start, msg_len, flags, (void*)_bytes_read, (void*)error);

/* parameters */
	pgm_return_val_if_fail (NULL != sock, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(msg_len)) pgm_return_val_if_fail (NULL != msg_start, PGM_IO_STATUS_ERROR);

/* shutdown */
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&sock->lock)))
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);

/* state */
	if (PGM_UNLIKELY(!sock->is_bound || sock->is_destroyed))
	{
		pgm_rwlock_reader_unlock (&sock->lock);
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	}

/* pre-conditions */
	pgm_assert (NULL != sock->rx_buffer);
	pgm_assert (sock->max_tpdu > 0);
	if (sock->can_recv_data) {
		pgm_assert (NULL != sock->peers_hashtable);
		pgm_assert_cmpuint (sock->nak_bo_ivl, >, 1);
		pgm_assert (pgm_notify_is_valid (&sock->pending_notify));
	}

/* receiver */
	pgm_mutex_lock (&sock->receiver_mutex);

	if (PGM_UNLIKELY(sock->is_reset)) {
		pgm_assert (NULL != sock->peers_pending);
		pgm_assert (NULL != sock->peers_pending->data);
		pgm_peer_t* peer = sock->peers_pending->data;
		if (flags & MSG_ERRQUEUE)
			pgm_set_reset_error (sock, peer, msg_start);
		else if (error) {
			char tsi[PGM_TSISTRLEN];
			pgm_tsi_print_r (&peer->tsi, tsi, sizeof(tsi));
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_RECV,
				     PGM_ERROR_CONNRESET,
				     _("Transport has been reset on unrecoverable loss from %s."),
				     tsi);
		}
		if (!sock->is_abort_on_reset)
			sock->is_reset = !sock->is_reset;
		pgm_mutex_unlock (&sock->receiver_mutex);
		pgm_rwlock_reader_unlock (&sock->lock);
		return PGM_IO_STATUS_RESET;
	}

/* timer status */
	if (pgm_timer_check (sock) &&
	    !pgm_timer_dispatch (sock))
	{
/* block on send-in-recv */
		status = PGM_IO_STATUS_RATE_LIMITED;
	}
/* NAK status */
	else if (sock->can_send_data)
	{
		if (!pgm_txw_retransmit_is_empty (sock->window))
		{
			if (!pgm_on_deferred_nak (sock))
				status = PGM_IO_STATUS_RATE_LIMITED;
		}
		else
			pgm_notify_clear (&sock->rdata_notify);
	}

	size_t bytes_read = 0;
	unsigned data_read = 0;
	struct pgm_msgv_t* pmsg = msg_start;
	const struct pgm_msgv_t* msg_end = msg_start + msg_len - 1;

	if (PGM_UNLIKELY(0 == ++(sock->last_commit)))
		++(sock->last_commit);

	/* second, flush any remaining contiguous messages from previous call(s) */
	if (sock->peers_pending) {
		if (0 != pgm_flush_peers_pending (sock, &pmsg, msg_end, &bytes_read, &data_read))
			goto out;
/* returns on: reset or full buffer */
	}

/* read the data:
 *
 * We cannot actually block here as packets pushed by the timers need to be addressed too.
 */
	struct sockaddr_storage src, dst;
	ssize_t len;
	size_t bytes_received = 0;

recv_again:

	len = recvskb (sock,
		       sock->rx_buffer,		/* PGM skbuff */
		       0,
		       (struct sockaddr*)&src,
		       sizeof(src),
		       (struct sockaddr*)&dst,
		       sizeof(dst));
	if (len < 0)
	{
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		if (PGM_LIKELY(PGM_SOCK_EAGAIN == save_errno)) {
			goto check_for_repeat;
		}
		status = PGM_IO_STATUS_ERROR;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_RECV,
			     pgm_error_from_sock_errno (save_errno),
			     _("Transport socket error: %s"),
			     pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
		goto out;
	}
	else if (0 == len)
	{
/* cannot return NORMAL/0 as that is valid payload with SKB */
		status = PGM_IO_STATUS_EOF;
		goto out;
	}
	else
	{
		bytes_received += len;
	}

	pgm_error_t* err = NULL;
	const bool is_valid = (sock->udp_encap_ucast_port || AF_INET6 == src.ss_family) ?
					pgm_parse_udp_encap (sock->rx_buffer, &err) :
					pgm_parse_raw (sock->rx_buffer, (struct sockaddr*)&dst, &err);
	if (PGM_UNLIKELY(!is_valid))
	{
/* inherently cannot determine PGM_PC_RECEIVER_CKSUM_ERRORS unless only one receiver */
		pgm_trace (PGM_LOG_ROLE_NETWORK,
				_("Discarded invalid packet: %s"),
				(err && err->message) ? err->message : "(null)");
		pgm_error_free (err);
		if (sock->can_send_data) {
			if (err && PGM_ERROR_CKSUM == err->code)
				sock->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS]++;
			sock->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		}
		goto recv_again;
	}

	pgm_peer_t* source = NULL;
	if (PGM_UNLIKELY(!on_pgm (sock, sock->rx_buffer, (struct sockaddr*)&src, (struct sockaddr*)&dst, &source)))
		goto recv_again;

/* check whether this source has waiting data */
	if (source && pgm_peer_has_pending (source)) {
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("New pending data."));
		pgm_peer_set_pending (sock, source);
	}

flush_pending:
/* flush any congtiguous packets generated by the receipt of this packet */
	if (sock->peers_pending)
	{
		if (0 != pgm_flush_peers_pending (sock, &pmsg, msg_end, &bytes_read, &data_read))
		{
/* recv vector is now full */
			goto out;
		}
	}

check_for_repeat:
/* repeat if non-blocking and not full */
	if (sock->is_nonblocking ||
	    flags & MSG_DONTWAIT)
	{
		if (len > 0 && pmsg <= msg_end) {
			pgm_trace (PGM_LOG_ROLE_NETWORK,_("Recv again on not-full"));
			goto recv_again;		/* \:D/ */
		}
	}
	else
	{
/* repeat if blocking and empty, i.e. received non data packet.
 */
		if (0 == data_read) {
			const int wait_status = wait_for_event (sock);
			switch (wait_status) {
			case EAGAIN:
				goto recv_again;
			case EINTR:
				if (!pgm_timer_dispatch (sock))
					goto check_for_repeat;
				goto flush_pending;
			case ENOENT:
				pgm_mutex_unlock (&sock->receiver_mutex);
				pgm_rwlock_reader_unlock (&sock->lock);
				return PGM_IO_STATUS_EOF;
			case EFAULT: {
				const int save_errno = pgm_get_last_sock_error();
				char errbuf[1024];
				pgm_set_error (error,
						PGM_ERROR_DOMAIN_RECV,
						pgm_error_from_sock_errno (save_errno),
						_("Waiting for event: %s"),
						pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno)
						);
				pgm_mutex_unlock (&sock->receiver_mutex);
				pgm_rwlock_reader_unlock (&sock->lock);
				return PGM_IO_STATUS_ERROR;
			}
			default:
				pgm_assert_not_reached();
			}
		}
	}

out:
	if (0 == data_read)
	{
/* clear event notification */
		if (sock->is_pending_read) {
			pgm_notify_clear (&sock->pending_notify);
			sock->is_pending_read = FALSE;
		}
/* report data loss */
		if (PGM_UNLIKELY(sock->is_reset)) {
			pgm_assert (NULL != sock->peers_pending);
			pgm_assert (NULL != sock->peers_pending->data);
			pgm_peer_t* peer = sock->peers_pending->data;
			if (flags & MSG_ERRQUEUE)
				pgm_set_reset_error (sock, peer, msg_start);
			else if (error) {
				char tsi[PGM_TSISTRLEN];
				pgm_tsi_print_r (&peer->tsi, tsi, sizeof(tsi));
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_RECV,
					     PGM_ERROR_CONNRESET,
					     _("Transport has been reset on unrecoverable loss from %s."),
					     tsi);
			}
			if (!sock->is_abort_on_reset)
				sock->is_reset = !sock->is_reset;
			pgm_mutex_unlock (&sock->receiver_mutex);
			pgm_rwlock_reader_unlock (&sock->lock);
			return PGM_IO_STATUS_RESET;
		}
		pgm_mutex_unlock (&sock->receiver_mutex);
		pgm_rwlock_reader_unlock (&sock->lock);
		if (PGM_IO_STATUS_WOULD_BLOCK == status &&
		    ( sock->can_send_data ||
		      ( sock->can_recv_data && NULL != sock->peers_list )))
		{
			status = PGM_IO_STATUS_TIMER_PENDING;
		}
		return status;
	}

	if (sock->peers_pending)
	{
/* set event notification for additional available data */
		if (sock->is_pending_read && sock->is_edge_triggered_recv)
		{
/* empty pending-pipe */
			pgm_notify_clear (&sock->pending_notify);
			sock->is_pending_read = FALSE;
		}
		else if (!sock->is_pending_read && !sock->is_edge_triggered_recv)
		{
/* fill pending-pipe */
			pgm_notify_send (&sock->pending_notify);
			sock->is_pending_read = TRUE;
		}
	}

	if (NULL != _bytes_read)
		*_bytes_read = bytes_read;
	pgm_mutex_unlock (&sock->receiver_mutex);
	pgm_rwlock_reader_unlock (&sock->lock);
	return PGM_IO_STATUS_NORMAL;
}

/* read one contiguous apdu and return as a IO scatter/gather array.  msgv is owned by
 * the caller, tpdu contents are owned by the receive window.
 *
 * on success, returns PGM_IO_STATUS_NORMAL.
 */

int
pgm_recvmsg (
	pgm_sock_t*   	   const restrict sock,
	struct pgm_msgv_t* const restrict msgv,
	const int			  flags,	/* MSG_DONTWAIT for non-blocking */
	size_t*			 restrict bytes_read,	/* may be NULL */
	pgm_error_t**		 restrict error
	)
{
	pgm_return_val_if_fail (NULL != sock, PGM_IO_STATUS_ERROR);
	pgm_return_val_if_fail (NULL != msgv, PGM_IO_STATUS_ERROR);

	pgm_debug ("pgm_recvmsg (sock:%p msgv:%p flags:%d bytes_read:%p error:%p)",
		(const void*)sock, (const void*)msgv, flags, (const void*)bytes_read, (const void*)error);

	return pgm_recvmsgv (sock, msgv, 1, flags, bytes_read, error);
}

/* vanilla read function.  copies from the receive window to the provided buffer
 * location.  the caller must provide an adequately sized buffer to store the largest
 * expected apdu or else it will be truncated.
 *
 * on success, returns PGM_IO_STATUS_NORMAL.
 */

int
pgm_recvfrom (
	pgm_sock_t*	 const restrict sock,
	void*			  restrict buf,
	const size_t			   buflen,
	const int			   flags,		/* MSG_DONTWAIT for non-blocking */
	size_t*			  restrict _bytes_read,	/* may be NULL */
	struct pgm_sockaddr_t*	  restrict from,		/* may be NULL */
	socklen_t*		  restrict fromlen,
	pgm_error_t**		  restrict error
	)
{
	struct pgm_msgv_t msgv;
	size_t bytes_read = 0;

	pgm_return_val_if_fail (NULL != sock, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(buflen)) pgm_return_val_if_fail (NULL != buf, PGM_IO_STATUS_ERROR);
	if (fromlen) {
		pgm_return_val_if_fail (NULL != from, PGM_IO_STATUS_ERROR);
		pgm_return_val_if_fail (sizeof (struct pgm_sockaddr_t) == *fromlen, PGM_IO_STATUS_ERROR);
	}

	pgm_debug ("pgm_recvfrom (sock:%p buf:%p buflen:%" PRIzu " flags:%d bytes-read:%p from:%p from:%p error:%p)",
		(const void*)sock, buf, buflen, flags, (const void*)_bytes_read, (const void*)from, (const void*)fromlen, (const void*)error);

	const int status = pgm_recvmsg (sock, &msgv, flags & ~(MSG_ERRQUEUE), &bytes_read, error);
	if (PGM_IO_STATUS_NORMAL != status)
		return status;

	size_t bytes_copied = 0;
	struct pgm_sk_buff_t** skb = msgv.msgv_skb;
	struct pgm_sk_buff_t* pskb = *skb;

	if (from) {
		from->sa_port = ntohs (sock->dport);
		from->sa_addr.sport = ntohs (pskb->tsi.sport);
		memcpy (&from->sa_addr.gsi, &pskb->tsi.gsi, sizeof(pgm_gsi_t));
	}

	while (bytes_copied < bytes_read) {
		size_t copy_len = pskb->len;
		if (bytes_copied + copy_len > buflen) {
			pgm_warn (_("APDU truncated, original length %" PRIzu " bytes."),
				bytes_read);
			copy_len = buflen - bytes_copied;
			bytes_read = buflen;
		}
		memcpy ((char*)buf + bytes_copied, pskb->data, copy_len);
		bytes_copied += copy_len;
		pskb = *(++skb);
	}
	if (_bytes_read)
		*_bytes_read = bytes_copied;
	return PGM_IO_STATUS_NORMAL;
}

/* Basic recv operation, copying data from window to application.
 *
 * on success, returns PGM_IO_STATUS_NORMAL.
 */

int
pgm_recv (
	pgm_sock_t* const restrict sock,
	void*		  restrict buf,
	const size_t		   buflen,
	const int		   flags,	/* MSG_DONTWAIT for non-blocking */
	size_t*	    const restrict bytes_read,	/* may be NULL */
	pgm_error_t**     restrict error
	)
{
	pgm_return_val_if_fail (NULL != sock, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(buflen)) pgm_return_val_if_fail (NULL != buf, PGM_IO_STATUS_ERROR);

	pgm_debug ("pgm_recv (sock:%p buf:%p buflen:%" PRIzu " flags:%d bytes-read:%p error:%p)",
		(const void*)sock, buf, buflen, flags, (const void*)bytes_read, (const void*)error);

	return pgm_recvfrom (sock, buf, buflen, flags, bytes_read, NULL, NULL, error);
}

/* eof */
