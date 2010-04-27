/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Transport recv API.
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

#define _GNU_SOURCE
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>		/* _GNU_SOURCE for in6_pktinfo */
#include <pgm/i18n.h>
#include <pgm/framework.h>
#include "pgm/recv.h"
#include "pgm/source.h"
#include "pgm/packet_parse.h"
#include "pgm/timer.h"


//#define RECV_DEBUG

#ifndef RECV_DEBUG
#	define PGM_DISABLE_ASSERT
#endif

#ifdef _WIN32
#	ifndef WSAID_WSARECVMSG
/* http://cvs.winehq.org/cvsweb/wine/include/mswsock.h */
#		define WSAID_WSARECVMSG {0xf689d7c8,0x6f1f,0x436b,{0x8a,0x53,0xe5,0x4f,0xe3,0x51,0xc3,0x22}}
#	endif
#	define cmsghdr wsacmsghdr
#	define CMSG_FIRSTHDR(msg)	WSA_CMSG_FIRSTHDR(msg)
#	define CMSG_NXTHDR(msg, cmsg)	WSA_CMSG_NXTHDR(msg, cmsg)
#	define CMSG_DATA(cmsg)		WSA_CMSG_DATA(cmsg)
#	define CMSG_SPACE(len)		WSA_CMSG_SPACE(len)
#	define CMSG_LEN(len)		WSA_CMSG_LEN(len)
#endif


/* read a packet into a PGM skbuff
 * on success returns packet length, on closed socket returns 0,
 * on error returns -1.
 */

static
ssize_t
recvskb (
	pgm_transport_t*      const restrict transport,
	struct pgm_sk_buff_t* const restrict skb,
	const int			     flags,
	struct sockaddr*      const restrict src_addr,
	const socklen_t			     src_addrlen,
	struct sockaddr*      const restrict dst_addr,
	const socklen_t			     dst_addrlen
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);
	pgm_assert (NULL != src_addr);
	pgm_assert (src_addrlen > 0);
	pgm_assert (NULL != dst_addr);
	pgm_assert (dst_addrlen > 0);

	pgm_debug ("recvskb (transport:%p skb:%p flags:%d src-addr:%p src-addrlen:%d dst-addr:%p dst-addrlen:%d)",
		(void*)transport, (void*)skb, flags, (void*)src_addr, (int)src_addrlen, (void*)dst_addr, (int)dst_addrlen);

	if (PGM_UNLIKELY(transport->is_destroyed))
		return 0;

#ifdef CONFIG_TARGET_WINE
	socklen_t fromlen = src_addrlen;
	const ssize_t len = recvfrom (transport->recv_sock, skb->head, transport->max_tpdu, 0, src_addr, &fromlen);
	if (len <= 0)
		return len;
#else
	struct pgm_iovec iov = {
		.iov_base	= skb->head,
		.iov_len	= transport->max_tpdu
	};
	char aux[ 1024 ];
#	ifndef _WIN32
	struct msghdr msg = {
		.msg_name	= src_addr,
		.msg_namelen	= src_addrlen,
		.msg_iov	= (void*)&iov,
		.msg_iovlen	= 1,
		.msg_control	= aux,
		.msg_controllen = sizeof(aux),
		.msg_flags	= 0
	};

	ssize_t len = recvmsg (transport->recv_sock, &msg, flags);
	if (len <= 0)
		return len;
#	else /* !_WIN32 */
	WSAMSG msg = {
		.name		= (LPSOCKADDR)src_addr,
		.namelen	= src_addrlen,
		.lpBuffers	= (LPWSABUF)&iov,
		.dwBufferCount	= 1,
		.dwFlags	= 0
	};
	msg.Control.buf		= aux;
	msg.Control.len		= sizeof(aux);

	LPFN_WSARECVMSG WSARecvMsg_ = NULL;
	if (PGM_UNLIKELY(!WSARecvMsg_)) {
		GUID WSARecvMsg_GUID = WSAID_WSARECVMSG;
		DWORD cbBytesReturned;
		if (SOCKET_ERROR == WSAIoctl (transport->recv_sock,
					      SIO_GET_EXTENSION_FUNCTION_POINTER,
				 	      &WSARecvMsg_GUID, sizeof(WSARecvMsg_GUID),
		 			      &WSARecvMsg_, sizeof(WSARecvMsg_),
		 			      &cbBytesReturned,
					      NULL,
					      NULL))
		{
			pgm_fatal (_("WSARecvMsg function not found."));
			abort ();
			return -1;
		}
	}

	DWORD len;
	if (SOCKET_ERROR == WSARecvMsg_ (transport->recv_sock, &msg, &len, NULL, NULL))
		return -1;
#	endif /* !_WIN32 */
#endif /* !CONFIG_TARGET_WINE */

	skb->transport		= transport;
	skb->tstamp		= pgm_time_update_now();
	skb->data		= skb->head;
	skb->len		= len;
	skb->zero_padded	= 0;
	skb->tail		= (char*)skb->data + len;

#ifdef CONFIG_TARGET_WINE
	pgm_assert (pgm_sockaddr_len (&transport->recv_gsr[0].gsr_group) <= dst_addrlen);
	memcpy (dst_addr, &transport->recv_gsr[0].gsr_group, pgm_sockaddr_len (&transport->recv_gsr[0].gsr_group));
#else
	if (transport->udp_encap_ucast_port ||
	    AF_INET6 == pgm_sockaddr_family (src_addr))
	{
#ifdef CONFIG_HAVE_WSACMSGHDR
		WSACMSGHDR* cmsg;
#else
		struct cmsghdr* cmsg;
#endif
		for (cmsg = CMSG_FIRSTHDR(&msg);
		     cmsg != NULL;
		     cmsg = CMSG_NXTHDR(&msg, cmsg))
		{
#ifdef IP_PKTINFO
			if (IPPROTO_IP == cmsg->cmsg_level && 
			    IP_PKTINFO == cmsg->cmsg_type)
			{
				const void* pktinfo		= CMSG_DATA(cmsg);
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
#elif defined(IP_RECVDSTADDR)
			if (IPPROTO_IP == cmsg->cmsg_level &&
			    IP_RECVDSTADDR == cmsg->cmsg_type)
			{
				const void* recvdstaddr = CMSG_DATA(cmsg);
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

			if (IPPROTO_IPV6 == cmsg->cmsg_level && 
			    IPV6_PKTINFO == cmsg->cmsg_type)
			{
				const void* pktinfo		= CMSG_DATA(cmsg);
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
#endif
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
	pgm_transport_t*      const restrict transport,
	struct pgm_sk_buff_t* const restrict skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);
	pgm_assert_cmpuint (skb->pgm_header->pgm_dport, ==, transport->tsi.sport);

	pgm_debug ("on_upstream (transport:%p skb:%p)",
		(const void*)transport, (const void*)skb);

	if (PGM_UNLIKELY(!transport->can_send_data)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet for muted source."));
		goto out_discarded;
	}

/* unicast upstream message, note that dport & sport are reversed */
	if (PGM_UNLIKELY(skb->pgm_header->pgm_sport != transport->dport)) {
/* its upstream/peer-to-peer for another session */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet on data-destination port mismatch."));
		goto out_discarded;
	}

	if (PGM_UNLIKELY(!pgm_gsi_equal (&skb->tsi.gsi, &transport->tsi.gsi))) {
/* its upstream/peer-to-peer for another session */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet on data-destination port mismatch."));
		goto out_discarded;
	}

/* advance SKB pointer to PGM type header */
	skb->data	= (char*)skb->data + sizeof(struct pgm_header);
	skb->len       -= sizeof(struct pgm_header);

	switch (skb->pgm_header->pgm_type) {
	case PGM_NAK:
		if (PGM_UNLIKELY(!pgm_on_nak (transport, skb)))
			goto out_discarded;
		break;

	case PGM_NNAK:
		if (PGM_UNLIKELY(!pgm_on_nnak (transport, skb)))
			goto out_discarded;
		break;

	case PGM_SPMR:
		if (PGM_UNLIKELY(!pgm_on_spmr (transport, NULL, skb)))
			goto out_discarded;
		break;

	case PGM_POLR:
	default:
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded unsupported PGM type packet."));
		goto out_discarded;
	}

	return TRUE;
out_discarded:
	transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
	return FALSE;
}

/* peer to peer message, either multicast NAK or multicast SPMR.
 *
 * returns TRUE on valid processed packet, returns FALSE on discarded packet.
 */

static
bool
on_peer (
	pgm_transport_t*      const restrict transport,
	struct pgm_sk_buff_t* const restrict skb,
	pgm_peer_t**		    restrict source
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);
	pgm_assert_cmpuint (skb->pgm_header->pgm_dport, !=, transport->tsi.sport);
	pgm_assert (NULL != source);

	pgm_debug ("on_peer (transport:%p skb:%p source:%p)",
		(const void*)transport, (const void*)skb, (const void*)source);

/* we are not the source */
	if (PGM_UNLIKELY(!transport->can_recv_data)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet for muted receiver."));
		goto out_discarded;
	}

/* unicast upstream message, note that dport & sport are reversed */
	if (PGM_UNLIKELY(skb->pgm_header->pgm_sport != transport->dport)) {
/* its upstream/peer-to-peer for another session */
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet on data-destination port mismatch."));
		goto out_discarded;
	}

/* check to see the source this peer-to-peer message is about is in our peer list */
	pgm_tsi_t upstream_tsi;
	memcpy (&upstream_tsi.gsi, &skb->tsi.gsi, sizeof(pgm_gsi_t));
	upstream_tsi.sport = skb->pgm_header->pgm_dport;

	pgm_rwlock_reader_lock (&transport->peers_lock);
	*source = pgm_hashtable_lookup (transport->peers_hashtable, &upstream_tsi);
	pgm_rwlock_reader_unlock (&transport->peers_lock);
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
		if (PGM_UNLIKELY(!pgm_on_peer_nak (transport, *source, skb)))
			goto out_discarded;
		break;

	case PGM_SPMR:
		if (PGM_UNLIKELY(!pgm_on_spmr (transport, *source, skb)))
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
	else if (transport->can_send_data)
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
	return FALSE;
}

/* source to receiver message
 *
 * returns TRUE on valid processed packet, returns FALSE on discarded packet.
 */

static
bool
on_downstream (
	pgm_transport_t*      const restrict transport,
	struct pgm_sk_buff_t* const restrict skb,
	struct sockaddr*      const restrict src_addr,
	struct sockaddr*      const restrict dst_addr,
	pgm_peer_t**	  	    restrict source
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);
	pgm_assert (NULL != src_addr);
	pgm_assert (NULL != dst_addr);
	pgm_assert (NULL != source);

#ifdef RECV_DEBUG
	char saddr[INET6_ADDRSTRLEN], daddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (src_addr, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (dst_addr, daddr, sizeof(daddr));
	pgm_debug ("on_downstream (transport:%p skb:%p src-addr:%s dst-addr:%s source:%p)",
		(const void*)transport, (const void*)skb, saddr, daddr, (const void*)source);
#endif

	if (PGM_UNLIKELY(!transport->can_recv_data)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet for muted receiver."));
		goto out_discarded;
	}

/* pgm packet DPORT contains our transport DPORT */
	if (PGM_UNLIKELY(skb->pgm_header->pgm_dport != transport->dport)) {
		pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded packet on data-destination port mismatch."));
		goto out_discarded;
	}

/* search for TSI peer context or create a new one */
	if (PGM_LIKELY(pgm_tsi_hash (&skb->tsi) == transport->last_hash_key &&
			NULL != transport->last_hash_value))
	{
		*source = transport->last_hash_value;
	}
	else
	{
		pgm_rwlock_reader_lock (&transport->peers_lock);
		*source = pgm_hashtable_lookup_extended (transport->peers_hashtable, &skb->tsi, &transport->last_hash_key);
		pgm_rwlock_reader_unlock (&transport->peers_lock);
		if (PGM_UNLIKELY(NULL == *source)) {
			*source = pgm_new_peer (transport,
					       &skb->tsi,
					       (struct sockaddr*)src_addr, pgm_sockaddr_len(src_addr),
					       (struct sockaddr*)dst_addr, pgm_sockaddr_len(dst_addr),
						skb->tstamp);
		}
		transport->last_hash_value = *source;
	}

	(*source)->cumulative_stats[PGM_PC_RECEIVER_BYTES_RECEIVED] += skb->len;
	(*source)->last_packet = skb->tstamp;

	skb->data       = (void*)( skb->pgm_header + 1 );
	skb->len       -= sizeof(struct pgm_header);

/* handle PGM packet type */
	switch (skb->pgm_header->pgm_type) {
	case PGM_ODATA:
	case PGM_RDATA:
		if (PGM_UNLIKELY(!pgm_on_data (transport, *source, skb)))
			goto out_discarded;
		transport->rx_buffer = pgm_alloc_skb (transport->max_tpdu);
		break;

	case PGM_NCF:
		if (PGM_UNLIKELY(!pgm_on_ncf (transport, *source, skb)))
			goto out_discarded;
		break;

	case PGM_SPM:
		if (PGM_UNLIKELY(!pgm_on_spm (transport, *source, skb)))
			goto out_discarded;

/* update group NLA if appropriate */
		if (PGM_LIKELY(pgm_sockaddr_is_addr_multicast ((struct sockaddr*)dst_addr)))
			memcpy (&(*source)->group_nla, dst_addr, pgm_sockaddr_len(dst_addr));
		break;

#ifdef CONFIG_PGM_POLLING
	case PGM_POLL:
		if (PGM_UNLIKELY(!pgm_on_poll (transport, *source, skb)))
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
	else if (transport->can_send_data)
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
	return FALSE;
}

/* process a pgm packet
 *
 * returns TRUE on valid processed packet, returns FALSE on discarded packet.
 */
static
bool
on_pgm (
	pgm_transport_t*      const restrict transport,
	struct pgm_sk_buff_t* const restrict skb,
	struct sockaddr*      const restrict src_addr,
	struct sockaddr*      const restrict dst_addr,
	pgm_peer_t**		    restrict source
	)
{
/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (NULL != skb);
	pgm_assert (NULL != src_addr);
	pgm_assert (NULL != dst_addr);
	pgm_assert (NULL != source);

#ifdef RECV_DEBUG
	char saddr[INET6_ADDRSTRLEN], daddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (src_addr, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (dst_addr, daddr, sizeof(daddr));
	pgm_debug ("on_pgm (transport:%p skb:%p src-addr:%s dst-addr:%s source:%p)",
		(const void*)transport, (const void*)skb, saddr, daddr, (const void*)source);
#endif

	if (pgm_is_downstream (skb->pgm_header->pgm_type))
		return on_downstream (transport, skb, src_addr, dst_addr, source);
	if (skb->pgm_header->pgm_dport == transport->tsi.sport)
	{
		if (pgm_is_upstream (skb->pgm_header->pgm_type) ||
		    pgm_is_peer (skb->pgm_header->pgm_type))
		{
			return on_upstream (transport, skb);
		}
	}
	else if (pgm_is_peer (skb->pgm_header->pgm_type))
		return on_peer (transport, skb, source);

	pgm_trace (PGM_LOG_ROLE_NETWORK,_("Discarded unknown PGM packet."));
	if (transport->can_send_data)
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
	return FALSE;
}

/* block on receiving socket whilst holding transport::waiting-mutex
 * returns EAGAIN for waiting data, returns EINTR for waiting timer event,
 * returns ENOENT on closed transport, and returns EFAULT for libc error.
 */

static
int
wait_for_event (
	pgm_transport_t* const	transport
	)
{
	int n_fds = 3;

/* pre-conditions */
	pgm_assert (NULL != transport);

	pgm_debug ("wait_for_event (transport:%p)", (const void*)transport);

	do {
		if (PGM_UNLIKELY(transport->is_destroyed))
			return ENOENT;

		if (transport->can_send_data && !pgm_txw_retransmit_is_empty (transport->window))
/* tight loop on blocked send */
			pgm_on_deferred_nak (transport);

#ifdef CONFIG_HAVE_POLL
		struct pollfd fds[ n_fds ];
		memset (fds, 0, sizeof(fds));
		const int status = pgm_transport_poll_info (transport, fds, &n_fds, POLLIN);
		pgm_assert (-1 != status);
#else
		fd_set readfds;
		FD_ZERO(&readfds);
		const int status = pgm_transport_select_info (transport, &readfds, NULL, &n_fds);
		pgm_assert (-1 != status);
#endif /* CONFIG_HAVE_POLL */

/* flush any waiting notifications */
		if (transport->is_pending_read) {
			pgm_notify_clear (&transport->pending_notify);
			transport->is_pending_read = FALSE;
		}

		int timeout;
		if (transport->can_send_data && !pgm_txw_retransmit_is_empty (transport->window))
			timeout = 0;
		else
			timeout = pgm_timer_expiration (transport);
		
#ifdef CONFIG_HAVE_POLL
		const int ready = poll (fds, n_fds, timeout /* μs */ / 1000 /* to ms */);
#else
		struct timeval tv_timeout = {
			.tv_sec		= timeout > 1000000UL ? timeout / 1000000UL : 0,
			.tv_usec	= timeout > 1000000UL ? timeout % 1000000UL : timeout
		};
		const int ready = select (n_fds, &readfds, NULL, NULL, &tv_timeout);
#endif
		if (PGM_UNLIKELY(-1 == ready)) {
			pgm_debug ("block returned errno=%i",errno);
			return EFAULT;
		} else if (ready > 0) {
			pgm_debug ("recv again on empty");
			return EAGAIN;
		}
	} while (pgm_timer_check (transport));
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
	pgm_transport_t*   const restrict transport,
	struct pgm_msgv_t* const restrict msg_start,
	const size_t			  msg_len,
	const int			  flags,	/* MSG_DONTWAIT for non-blocking */
	size_t*			 restrict _bytes_read,	/* may be NULL */
	pgm_error_t**		 restrict error
	)
{
	int status = PGM_IO_STATUS_WOULD_BLOCK;

	pgm_debug ("pgm_recvmsgv (transport:%p msg-start:%p msg-len:%zu flags:%d bytes-read:%p error:%p)",
		(void*)transport, (void*)msg_start, msg_len, flags, (void*)_bytes_read, (void*)error);

/* parameters */
	pgm_return_val_if_fail (NULL != transport, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(msg_len)) pgm_return_val_if_fail (NULL != msg_start, PGM_IO_STATUS_ERROR);

/* shutdown */
	if (PGM_UNLIKELY(!pgm_rwlock_reader_trylock (&transport->lock)))
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);

/* state */
	if (PGM_UNLIKELY(!transport->is_bound ||
	    transport->is_destroyed))
	{
		pgm_rwlock_reader_unlock (&transport->lock);
		pgm_return_val_if_reached (PGM_IO_STATUS_ERROR);
	}

/* pre-conditions */
	pgm_assert (NULL != transport->rx_buffer);
	pgm_assert (transport->max_tpdu > 0);
	if (transport->can_recv_data) {
		pgm_assert (NULL != transport->peers_hashtable);
		pgm_assert_cmpuint (transport->nak_bo_ivl, >, 1);
		pgm_assert (pgm_notify_is_valid (&transport->pending_notify));
	}

/* receiver */
	pgm_mutex_lock (&transport->receiver_mutex);

	if (PGM_UNLIKELY(transport->is_reset)) {
		pgm_assert (NULL != transport->peers_pending);
		pgm_assert (NULL != transport->peers_pending->data);
		pgm_peer_t* peer = transport->peers_pending->data;
		if (flags & MSG_ERRQUEUE)
			pgm_set_reset_error (transport, peer, msg_start);
		else if (error) {
			char tsi[PGM_TSISTRLEN];
			pgm_tsi_print_r (&peer->tsi, tsi, sizeof(tsi));
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_RECV,
				     PGM_ERROR_CONNRESET,
				     _("Transport has been reset on unrecoverable loss from %s."),
				     tsi);
		}
		if (!transport->is_abort_on_reset)
			transport->is_reset = !transport->is_reset;
		pgm_mutex_unlock (&transport->receiver_mutex);
		pgm_rwlock_reader_unlock (&transport->lock);
		return PGM_IO_STATUS_RESET;
	}

/* timer status */
	if (pgm_timer_check (transport) &&
	    !pgm_timer_dispatch (transport))
	{
/* block on send-in-recv */
		status = PGM_IO_STATUS_RATE_LIMITED;
	}
/* NAK status */
	else if (transport->can_send_data)
	{
		if (!pgm_txw_retransmit_is_empty (transport->window))
		{
			if (!pgm_on_deferred_nak (transport))
				status = PGM_IO_STATUS_RATE_LIMITED;
		}
		else
			pgm_notify_clear (&transport->rdata_notify);
	}

	size_t bytes_read = 0;
	unsigned data_read = 0;
	struct pgm_msgv_t* pmsg = msg_start;
	const struct pgm_msgv_t* msg_end = msg_start + msg_len - 1;

	if (PGM_UNLIKELY(0 == ++(transport->last_commit)))
		++(transport->last_commit);

	/* second, flush any remaining contiguous messages from previous call(s) */
	if (transport->peers_pending) {
		if (0 != pgm_flush_peers_pending (transport, &pmsg, msg_end, &bytes_read, &data_read))
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

	len = recvskb (transport,
		       transport->rx_buffer,		/* PGM skbuff */
		       0,
		       (struct sockaddr*)&src,
		       sizeof(src),
		       (struct sockaddr*)&dst,
		       sizeof(dst));
	if (len < 0)
	{
#ifndef _WIN32
		const int save_errno = errno;
		if (PGM_LIKELY(EAGAIN == save_errno)) {
			goto check_for_repeat;
		}
		status = PGM_IO_STATUS_ERROR;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_RECV,
			     pgm_error_from_errno (save_errno),
			     _("Transport socket error: %s"),
			     strerror (save_errno));
#else
		const int save_wsa_errno = WSAGetLastError ();
		if (PGM_LIKELY(WSAEWOULDBLOCK == save_wsa_errno)) {
			goto check_for_repeat;
		}
		status = PGM_IO_STATUS_ERROR;
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_RECV,
			     pgm_error_from_wsa_errno (save_wsa_errno),
			     _("Transport socket error: %s"),
			     wsastrerror (save_wsa_errno));
#endif /* !_WIN32 */
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
	const bool is_valid = (transport->udp_encap_ucast_port || AF_INET6 == src.ss_family) ?
					pgm_parse_udp_encap (transport->rx_buffer, &err) :
					pgm_parse_raw (transport->rx_buffer, (struct sockaddr*)&dst, &err);
	if (PGM_UNLIKELY(!is_valid))
	{
/* inherently cannot determine PGM_PC_RECEIVER_CKSUM_ERRORS unless only one receiver */
		pgm_trace (PGM_LOG_ROLE_NETWORK,
				_("Discarded invalid packet: %s"),
				(err && err->message) ? err->message : "(null)");
		pgm_error_free (err);
		if (transport->can_send_data) {
			if (err && PGM_ERROR_CKSUM == err->code)
				transport->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS]++;
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		}
		goto recv_again;
	}

	pgm_peer_t* source = NULL;
	if (PGM_UNLIKELY(!on_pgm (transport, transport->rx_buffer, (struct sockaddr*)&src, (struct sockaddr*)&dst, &source)))
		goto recv_again;

/* check whether this source has waiting data */
	if (source && pgm_peer_has_pending (source)) {
		pgm_trace (PGM_LOG_ROLE_RX_WINDOW,_("New pending data."));
		pgm_peer_set_pending (transport, source);
	}

flush_pending:
/* flush any congtiguous packets generated by the receipt of this packet */
	if (transport->peers_pending)
	{
		if (0 != pgm_flush_peers_pending (transport, &pmsg, msg_end, &bytes_read, &data_read))
		{
/* recv vector is now full */
			goto out;
		}
	}

check_for_repeat:
/* repeat if non-blocking and not full */
	if (transport->is_nonblocking ||
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
			const int wait_status = wait_for_event (transport);
			switch (wait_status) {
			case EAGAIN:
				goto recv_again;
			case EINTR:
				if (!pgm_timer_dispatch (transport))
					goto check_for_repeat;
				goto flush_pending;
			case ENOENT:
				pgm_mutex_unlock (&transport->receiver_mutex);
				pgm_rwlock_reader_unlock (&transport->lock);
				return PGM_IO_STATUS_EOF;
			case EFAULT:
				pgm_set_error (error,
						PGM_ERROR_DOMAIN_RECV,
						pgm_error_from_errno (errno),
						_("Waiting for event: %s"),
#ifndef _WIN32
						strerror (errno)
#else
						wsa_strerror (WSAGetLastError())	/* from select() */
#endif
						);
				pgm_mutex_unlock (&transport->receiver_mutex);
				pgm_rwlock_reader_unlock (&transport->lock);
				return PGM_IO_STATUS_ERROR;
			default:
				pgm_assert_not_reached();
			}
		}
	}

out:
	if (0 == data_read)
	{
/* clear event notification */
		if (transport->is_pending_read) {
			pgm_notify_clear (&transport->pending_notify);
			transport->is_pending_read = FALSE;
		}
/* report data loss */
		if (PGM_UNLIKELY(transport->is_reset)) {
			pgm_assert (NULL != transport->peers_pending);
			pgm_assert (NULL != transport->peers_pending->data);
			pgm_peer_t* peer = transport->peers_pending->data;
			if (flags & MSG_ERRQUEUE)
				pgm_set_reset_error (transport, peer, msg_start);
			else if (error) {
				char tsi[PGM_TSISTRLEN];
				pgm_tsi_print_r (&peer->tsi, tsi, sizeof(tsi));
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_RECV,
					     PGM_ERROR_CONNRESET,
					     _("Transport has been reset on unrecoverable loss from %s."),
					     tsi);
			}
			if (!transport->is_abort_on_reset)
				transport->is_reset = !transport->is_reset;
			pgm_mutex_unlock (&transport->receiver_mutex);
			pgm_rwlock_reader_unlock (&transport->lock);
			return PGM_IO_STATUS_RESET;
		}
		pgm_mutex_unlock (&transport->receiver_mutex);
		pgm_rwlock_reader_unlock (&transport->lock);
		if (PGM_IO_STATUS_WOULD_BLOCK == status &&
		    ( transport->can_send_data ||
		      ( transport->can_recv_data && NULL != transport->peers_list )))
		{
			status = PGM_IO_STATUS_TIMER_PENDING;
		}
		return status;
	}

	if (transport->peers_pending)
	{
/* set event notification for additional available data */
		if (transport->is_pending_read && transport->is_edge_triggered_recv)
		{
/* empty pending-pipe */
			pgm_notify_clear (&transport->pending_notify);
			transport->is_pending_read = FALSE;
		}
		else if (!transport->is_pending_read && !transport->is_edge_triggered_recv)
		{
/* fill pending-pipe */
			pgm_notify_send (&transport->pending_notify);
			transport->is_pending_read = TRUE;
		}
	}

	if (NULL != _bytes_read)
		*_bytes_read = bytes_read;
	pgm_mutex_unlock (&transport->receiver_mutex);
	pgm_rwlock_reader_unlock (&transport->lock);
	return PGM_IO_STATUS_NORMAL;
}

/* read one contiguous apdu and return as a IO scatter/gather array.  msgv is owned by
 * the caller, tpdu contents are owned by the receive window.
 *
 * on success, returns PGM_IO_STATUS_NORMAL.
 */

int
pgm_recvmsg (
	pgm_transport_t*   const restrict transport,
	struct pgm_msgv_t* const restrict msgv,
	const int			  flags,	/* MSG_DONTWAIT for non-blocking */
	size_t*			 restrict bytes_read,	/* may be NULL */
	pgm_error_t**		 restrict error
	)
{
	pgm_return_val_if_fail (NULL != transport, PGM_IO_STATUS_ERROR);
	pgm_return_val_if_fail (NULL != msgv, PGM_IO_STATUS_ERROR);

	pgm_debug ("pgm_recvmsg (transport:%p msgv:%p flags:%d bytes_read:%p error:%p)",
		(const void*)transport, (const void*)msgv, flags, (const void*)bytes_read, (const void*)error);

	return pgm_recvmsgv (transport, msgv, 1, flags, bytes_read, error);
}

/* vanilla read function.  copies from the receive window to the provided buffer
 * location.  the caller must provide an adequately sized buffer to store the largest
 * expected apdu or else it will be truncated.
 *
 * on success, returns PGM_IO_STATUS_NORMAL.
 */

int
pgm_recvfrom (
	pgm_transport_t* const restrict	transport,
	void*		       restrict	buf,
	const size_t			buflen,
	const int			flags,		/* MSG_DONTWAIT for non-blocking */
	size_t*		       restrict	_bytes_read,	/* may be NULL */
	pgm_tsi_t*	       restrict	from,		/* may be NULL */
	pgm_error_t**	       restrict	error
	)
{
	struct pgm_msgv_t msgv;
	size_t bytes_read = 0;

	pgm_return_val_if_fail (NULL != transport, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(buflen)) pgm_return_val_if_fail (NULL != buf, PGM_IO_STATUS_ERROR);

	pgm_debug ("pgm_recvfrom (transport:%p buf:%p buflen:%zu flags:%d bytes-read:%p from:%p error:%p)",
		(void*)transport, buf, buflen, flags, (void*)_bytes_read, (void*)from, (void*)error);

	const int status = pgm_recvmsg (transport, &msgv, flags & ~(MSG_ERRQUEUE), &bytes_read, error);
	if (PGM_IO_STATUS_NORMAL != status)
		return status;

	size_t bytes_copied = 0;
	struct pgm_sk_buff_t* skb = msgv.msgv_skb[0];

	if (from) {
		memcpy (&from->gsi, &skb->tsi.gsi, sizeof(pgm_gsi_t));
		from->sport = ntohs (skb->tsi.sport);
	}

	while (bytes_copied < bytes_read) {
		size_t copy_len = skb->len;
		if (bytes_copied + copy_len > buflen) {
			pgm_warn (_("APDU truncated, original length %zu bytes."),
				bytes_read);
			copy_len = buflen - bytes_copied;
			bytes_read = buflen;
		}
		memcpy ((char*)buf + bytes_copied, skb->data, copy_len);
		bytes_copied += copy_len;
		skb++;
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
	pgm_transport_t* const restrict transport,
	void*		       restrict buf,
	const size_t			buflen,
	const int			flags,		/* MSG_DONTWAIT for non-blocking */
	size_t*		 const restrict bytes_read,	/* may be NULL */
	pgm_error_t**	       restrict error
	)
{
	pgm_return_val_if_fail (NULL != transport, PGM_IO_STATUS_ERROR);
	if (PGM_LIKELY(buflen)) pgm_return_val_if_fail (NULL != buf, PGM_IO_STATUS_ERROR);

	pgm_debug ("pgm_recv (transport:%p buf:%p buflen:%zu flags:%d bytes-read:%p error:%p)",
		(const void*)transport, buf, buflen, flags, (const void*)bytes_read, (const void*)error);

	return pgm_recvfrom (transport, buf, buflen, flags, bytes_read, NULL, error);
}

/* eof */
