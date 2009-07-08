/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM receiver transport.
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
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#ifdef CONFIG_HAVE_POLL
#	include <poll.h>
#endif
#ifdef CONFIG_HAVE_EPOLL
#	include <sys/epoll.h>
#endif
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <glib.h>

#include "pgm/transport.h"
#include "pgm/source.h"
#include "pgm/receiver.h"
#include "pgm/if.h"
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
#include "pgm/err.h"

#define RECEIVER_DEBUG
//#define SPM_DEBUG

#ifndef RECEIVER_DEBUG
#	define g_trace(m,...)		while (0)
#else
#include <ctype.h>
#	ifdef SPM_DEBUG
#		define g_trace(m,...)		g_debug(__VA_ARGS__)
#	else
#		define g_trace(m,...)		do { if (strcmp((m),"SPM")) { g_debug(__VA_ARGS__); } } while (0)
#	endif
#endif


static int send_spmr (pgm_peer_t*);
static int send_nak (pgm_peer_t*, guint32);
static int send_parity_nak (pgm_peer_t*, guint, guint);
static int send_nak_list (pgm_peer_t*, pgm_sqn_list_t*);
static void nak_rb_state (pgm_peer_t*);
static void nak_rpt_state (pgm_peer_t*);
static void nak_rdata_state (pgm_peer_t*);
static pgm_peer_t* new_peer (pgm_transport_t*, pgm_tsi_t*, struct sockaddr*, gsize, struct sockaddr*, gsize) G_GNUC_PURE;
static inline pgm_peer_t* pgm_peer_ref (pgm_peer_t*);
static int on_spm (pgm_peer_t*, struct pgm_header*, gpointer, gsize);
static int on_peer_nak (pgm_peer_t*, struct pgm_header*, gpointer, gsize);
static int on_ncf (pgm_peer_t*, struct pgm_header*, gpointer, gsize);
static gboolean on_data (pgm_peer_t*, struct pgm_sk_buff_t*);


/* helpers for pgm_peer_t */
static inline pgm_time_t next_nak_rb_expiry (gpointer window_)
{
	pgm_rxw_t* window = (pgm_rxw_t*)window_;
	g_assert (window);
	g_assert (window->backoff_queue.tail);
	struct pgm_sk_buff_t* skb = (struct pgm_sk_buff_t*)window->backoff_queue.tail;
	pgm_rxw_state_t* state = (pgm_rxw_state_t*)&skb->cb;
	return state->nak_rb_expiry;
}

static inline pgm_time_t next_nak_rpt_expiry (gpointer window_)
{
	pgm_rxw_t* window = (pgm_rxw_t*)window_;
	g_assert (window);
	g_assert (window->wait_ncf_queue.tail);
	struct pgm_sk_buff_t* skb = (struct pgm_sk_buff_t*)window->wait_ncf_queue.tail;
	pgm_rxw_state_t* state = (pgm_rxw_state_t*)&skb->cb;
	return state->nak_rpt_expiry;
}

static inline pgm_time_t next_nak_rdata_expiry (gpointer window_)
{
	pgm_rxw_t* window = (pgm_rxw_t*)window_;
	g_assert (window);
	g_assert (window->wait_data_queue.tail);
	struct pgm_sk_buff_t* skb = (struct pgm_sk_buff_t*)window->wait_data_queue.tail;
	pgm_rxw_state_t* state = (pgm_rxw_state_t*)&skb->cb;
	return state->nak_rdata_expiry;
}

/* calculate NAK_RB_IVL as random time interval 1 - NAK_BO_IVL.
 */
static inline guint32
nak_rb_ivl (
	pgm_transport_t* transport
	)
{
	return g_rand_int_range (transport->rand_, 1 /* us */, transport->nak_bo_ivl);
}

/* increase reference count for peer object
 *
 * on success, returns peer object.
 */

static inline
pgm_peer_t*
pgm_peer_ref (
	pgm_peer_t*	peer
	)
{
	g_return_val_if_fail (peer != NULL, NULL);

	g_atomic_int_inc (&peer->ref_count);

	return peer;
}

/* decrease reference count of peer object, destroying on last reference.
 */

void
_pgm_peer_unref (
	pgm_peer_t*	peer
	)
{
	g_return_if_fail (peer != NULL);

	gboolean is_zero = g_atomic_int_dec_and_test (&peer->ref_count);

	if (G_UNLIKELY (is_zero))
	{
/* peer lock */
		g_static_mutex_free (&peer->mutex);

/* receive window */
		pgm_rxw_shutdown (peer->rxw);
		peer->rxw = NULL;

/* reed solomon state */
		if (peer->rs) {
			_pgm_rs_destroy (peer->rs);
			peer->rs = NULL;
		}

/* object */
		g_free (peer);
	}
}

/* set interval timer & expiration timeout for peer expiration, very lax checking.
 *
 * 0 < 2 * spm_ambient_interval <= peer_expiry
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */

int
pgm_transport_set_peer_expiry (
	pgm_transport_t*	transport,
	guint			peer_expiry	/* in microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);
	g_return_val_if_fail (peer_expiry > 0, -EINVAL);
	g_return_val_if_fail (peer_expiry >= 2 * transport->spm_ambient_interval, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->peer_expiry = peer_expiry;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* set maximum back off range for listening for multicast SPMR
 *
 * 0 < spmr_expiry < spm_ambient_interval
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */

int
pgm_transport_set_spmr_expiry (
	pgm_transport_t*	transport,
	guint			spmr_expiry	/* in microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);
	g_return_val_if_fail (spmr_expiry > 0, -EINVAL);
	if (transport->can_send_data) {
		g_return_val_if_fail (transport->spm_ambient_interval > spmr_expiry, -EINVAL);
	}

	g_static_mutex_lock (&transport->mutex);
	transport->spmr_expiry = spmr_expiry;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < rxw_sqns < one less than half sequence space
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */

int
pgm_transport_set_rxw_sqns (
	pgm_transport_t*	transport,
	guint			sqns
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);
	g_return_val_if_fail (sqns < ((UINT32_MAX/2)-1), -EINVAL);
	g_return_val_if_fail (sqns > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->rxw_sqns = sqns;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < secs < ( rxw_sqns / rxw_max_rte )
 *
 * can only be enforced upon bind.
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */

int
pgm_transport_set_rxw_secs (
	pgm_transport_t*	transport,
	guint			secs
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);
	g_return_val_if_fail (secs > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->rxw_secs = secs;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* 0 < rxw_max_rte < interface capacity
 *
 *  10mb :   1250000
 * 100mb :  12500000
 *   1gb : 125000000
 *
 * no practical way to determine upper limit and enforce.
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */

int
pgm_transport_set_rxw_max_rte (
	pgm_transport_t*	transport,
	guint			max_rte
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);
	g_return_val_if_fail (max_rte > 0, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->rxw_max_rte = max_rte;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* Actual NAK back-off, NAK_RB_IVL, is random time interval 1 < NAK_BO_IVL,
 * randomized to reduce storms.
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */

int
pgm_transport_set_nak_bo_ivl (
	pgm_transport_t*	transport,
	guint			usec		/* microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_bo_ivl = usec;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* Set NAK_RPT_IVL, the repeat interval before re-sending a NAK.
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */
int
pgm_transport_set_nak_rpt_ivl (
	pgm_transport_t*	transport,
	guint			usec		/* microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_rpt_ivl = usec;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* Set NAK_RDATA_IVL, the interval waiting for data.
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */
int
pgm_transport_set_nak_rdata_ivl (
	pgm_transport_t*	transport,
	guint			usec		/* microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_rdata_ivl = usec;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* statistics are limited to guint8, i.e. 255 retries
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */
int
pgm_transport_set_nak_data_retries (
	pgm_transport_t*	transport,
	guint			cnt
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_data_retries = cnt;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* statistics are limited to guint8, i.e. 255 retries
 *
 * on success, returns 0.  on invalid setting, returns -EINVAL.
 */
int
pgm_transport_set_nak_ncf_retries (
	pgm_transport_t*	transport,
	guint			cnt
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);
	g_return_val_if_fail (!transport->is_bound, -EINVAL);

	g_static_mutex_lock (&transport->mutex);
	transport->nak_ncf_retries = cnt;
	g_static_mutex_unlock (&transport->mutex);

	return 0;
}

/* a peer in the context of the transport is another party on the network sending PGM
 * packets.  for each peer we need a receive window and network layer address (nla) to
 * which nak requests can be forwarded to.
 *
 * on success, returns new peer object.
 */

static pgm_peer_t*
new_peer (
	pgm_transport_t*	transport,
	pgm_tsi_t*		tsi,
	struct sockaddr*	src_addr,
	gsize			src_addr_len,
	struct sockaddr*	dst_addr,
	gsize			dst_addr_len
	)
{
	pgm_peer_t* peer;

#ifdef TRANSPORT_DEBUG
	char localnla[INET6_ADDRSTRLEN];
	char groupnla[INET6_ADDRSTRLEN];
	pgm_inet_ntop (	pgm_sockaddr_family( src_addr ),
			pgm_sockaddr_addr( src_addr ),
			localnla,
			sizeof(localnla) );
	pgm_inet_ntop (	pgm_sockaddr_family( dst_addr ),
			pgm_sockaddr_addr( dst_addr ),
			groupnla,
			sizeof(groupnla) );
	g_trace ("INFO","new peer, tsi %s, group nla %s, local nla %s", pgm_print_tsi (tsi), groupnla, localnla);
#endif

	peer = g_malloc0 (sizeof(pgm_peer_t));
	peer->last_packet = pgm_time_update_now();
	peer->expiry = peer->last_packet + transport->peer_expiry;
	g_static_mutex_init (&peer->mutex);
	peer->transport = transport;
	memcpy (&peer->tsi, tsi, sizeof(pgm_tsi_t));
	((struct sockaddr_in*)&peer->nla)->sin_port = transport->udp_encap_port;
	((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr = INADDR_ANY;
	memcpy (&peer->group_nla, dst_addr, dst_addr_len);
	memcpy (&peer->local_nla, src_addr, src_addr_len);

/* lock on rx window */
	peer->rxw = pgm_rxw_init (
				&peer->tsi,
				transport->max_tpdu,
				transport->rxw_sqns,
				transport->rxw_secs,
				transport->rxw_max_rte);

	memcpy (&((pgm_rxw_t*)peer->rxw)->pgm_sock_err.tsi, &peer->tsi, sizeof(pgm_tsi_t));
	peer->spmr_expiry = peer->last_packet + transport->spmr_expiry;

/* add peer to hash table and linked list */
	g_static_rw_lock_writer_lock (&transport->peers_lock);
	gpointer entry = pgm_peer_ref(peer);
	g_hash_table_insert (transport->peers_hashtable, &peer->tsi, entry);
/* there is no g_list_prepend_link(): */
	peer->link_.next = transport->peers_list;
	peer->link_.data = peer;
/* update next entries previous link */
	if (transport->peers_list)
		transport->peers_list->prev = &peer->link_;
/* update head */
	transport->peers_list = &peer->link_;
	g_static_rw_lock_writer_unlock (&transport->peers_lock);

/* prod timer thread if sleeping */
	g_static_mutex_lock (&transport->mutex);
	if (pgm_time_after( transport->next_poll, peer->spmr_expiry ))
	{
		transport->next_poll = peer->spmr_expiry;
		g_trace ("INFO","new_peer: prod timer thread");
		if (!pgm_notify_send (&transport->timer_notify)) {
			g_critical ("notify to timer channel failed :(");
			/* retval = -EINVAL; */
		}
	}
	g_static_mutex_unlock (&transport->mutex);

	return peer;
}

/* data incoming on receive sockets, can be from a sender or receiver, or simply bogus.
 * for IPv4 we receive the IP header to handle fragmentation, for IPv6 we cannot, but the
 * underlying stack handles this for us.
 *
 * recvmsgv reads a vector of apdus each contained in a IO scatter/gather array.
 *
 * can be called due to event from incoming socket(s) or timer induced data loss.
 *
 * on success, returns bytes read, on error returns -1.
 */

gssize
pgm_transport_recvmsgv (
	pgm_transport_t*	transport,
	pgm_msgv_t*		msg_start,
	gsize			msg_len,
	int			flags		/* MSG_DONTWAIT for non-blocking */
	)
{
	g_trace ("INFO", "pgm_transport_recvmsgv");
	g_assert( msg_len > 0 );

/* reject on closed transport */
	if (!transport->is_open) {
		errno = ENOTCONN;
		return -1;
	}
	if (transport->has_lost_data) {
		pgm_rxw_t* lost_rxw = transport->peers_waiting->data;
		msg_start[0].msgv_len = sizeof(lost_rxw->pgm_sock_err);
		msg_start[0].msgv_skb[0] = (gpointer)&lost_rxw->pgm_sock_err;
		if (transport->will_close_on_failure) {
			transport->is_open = FALSE;
		} else {
			transport->has_lost_data = !transport->has_lost_data;
		}
		errno = ECONNRESET;
		return -1;
	}

	gsize bytes_read = 0;
	guint data_read = 0;
	pgm_msgv_t* pmsg = msg_start;
	const pgm_msgv_t* msg_end = msg_start + msg_len;

/* lock waiting so extra events are not generated during call */
	g_static_mutex_lock (&transport->waiting_mutex);

/* second, flush any remaining contiguous messages from previous call(s) */
	while (transport->peers_waiting)
	{
		pgm_rxw_t* waiting_rxw = transport->peers_waiting->data;
		const gssize peer_bytes_read = pgm_rxw_readv (waiting_rxw, &pmsg, msg_end - pmsg);

		if (waiting_rxw->ack_cumulative_losses != waiting_rxw->cumulative_losses)
		{
			transport->has_lost_data = TRUE;
			waiting_rxw->pgm_sock_err.lost_count = waiting_rxw->cumulative_losses - waiting_rxw->ack_cumulative_losses;
			waiting_rxw->ack_cumulative_losses = waiting_rxw->cumulative_losses;
		}
	
		if (peer_bytes_read >= 0)
		{
			bytes_read += peer_bytes_read;
			data_read++;

			if (pgm_rxw_is_full (waiting_rxw)) 		/* window full */
			{
				goto out;
			}
	
			if (pmsg == msg_end)	/* commit full */
			{
				goto out;
			}
		}
		if (transport->has_lost_data)
		{
			goto out;
		}

/* next */
		transport->peers_waiting->data = NULL;
		transport->peers_waiting->next = NULL;
		transport->peers_waiting = transport->peers_waiting->next;
	}

/* read the data:
 *
 * We cannot actually block here as packets pushed by the timers need to be addressed too.
 */
	struct pgm_sk_buff_t* skb = transport->rx_buffer;
	struct sockaddr_storage src, dst;
	ssize_t len;
	gsize bytes_received = 0;
	struct iovec iov = {
		.iov_base	= skb->head,
		.iov_len	= 0
	};
	size_t aux[1024 / sizeof(size_t)];
	struct msghdr msg = {
		.msg_name	= &src,
		.msg_namelen	= 0,
		.msg_iov	= &iov,
		.msg_iovlen	= 0,
		.msg_control	= aux,
		.msg_controllen = 0,
		.msg_flags	= 0
	};

recv_again:

/* reset msghdr */
	msg.msg_namelen		= sizeof(src);
	msg.msg_iov[0].iov_len	= transport->max_tpdu;
	msg.msg_iovlen		= 1;
	msg.msg_controllen	= sizeof(aux);

	len = recvmsg (transport->recv_sock, &msg, MSG_DONTWAIT);
	if (len < 0) {
		if (bytes_received) {
			goto flush_waiting;
		} else {
			goto check_for_repeat;
		}
	} else if (len == 0) {
		goto out;
	} else {
		bytes_received += len;
	}

	int e;

/* successfully read packet */
	skb->transport	= transport;
	skb->tstamp	= pgm_time_update_now();
	skb->data	= skb->head;
	skb->len	= len;
	skb->tail	= (guint8*)skb->data + len;

	if (!transport->udp_encap_port &&
	    AF_INET == pgm_sockaddr_family(&src))
	{
/* IPv4 PGM includes IP packet header which we can easily parse to grab destination multicast group
 */
		e = pgm_parse_raw (skb, (struct sockaddr*)&dst);
	}
	else
	{
/* UDP and IPv6 PGM requires use of IP control messages to get destination address
 */
		struct cmsghdr* cmsg;
		gboolean found_dstaddr = FALSE;

		for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg))
		{
			g_trace ("INFO", "cmsg: level=%d type=%d", cmsg->cmsg_level, cmsg->cmsg_type);
			if (IPPROTO_IP == cmsg->cmsg_level && 
			    IP_PKTINFO == cmsg->cmsg_type)
			{
				const struct in_pktinfo *in	= (struct in_pktinfo*) CMSG_DATA(cmsg);
				struct sockaddr_in* sin		= (struct sockaddr_in*)&dst;
				sin->sin_family			= AF_INET;
				sin->sin_addr.s_addr		= in->ipi_addr.s_addr;
				found_dstaddr			= TRUE;
				break;
			}

			if (IPPROTO_IPV6 == cmsg->cmsg_level && 
			    IPV6_PKTINFO == cmsg->cmsg_type)
			{
				const struct in6_pktinfo *in6	= (struct in6_pktinfo*) CMSG_DATA(cmsg);
				struct sockaddr_in6* sin6	= (struct sockaddr_in6*)&dst;
				sin6->sin6_family		= AF_INET6;
				sin6->sin6_addr			= in6->ipi6_addr;
				found_dstaddr			= TRUE;
				break;
			}
		}

/* set any empty address if no headers found */
		if (!found_dstaddr)
		{
			g_trace("INFO","no destination address found in header");
			struct sockaddr_in* sin		= (struct sockaddr_in*)&dst;
			sin->sin_family			= AF_INET;
			sin->sin_addr.s_addr		= INADDR_ANY;
		}

		e = pgm_parse_udp_encap (skb);
	}

	if (e < 0)
	{
/* TODO: difference between PGM_PC_SOURCE_CKSUM_ERRORS & PGM_PC_RECEIVER_CKSUM_ERRORS */
		if (e == -2)
			transport->cumulative_stats[PGM_PC_SOURCE_CKSUM_ERRORS]++;
		transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
		goto check_for_repeat;
	}

	pgm_peer_t* source = NULL;

	if (pgm_is_upstream (skb->pgm_header->pgm_type) || pgm_is_peer (skb->pgm_header->pgm_type))
	{

/* upstream = receiver to source, peer-to-peer = receive to receiver
 *
 * NB: SPMRs can be upstream or peer-to-peer, if the packet is multicast then its
 *     a peer-to-peer message, if its unicast its an upstream message.
 */

		if (skb->pgm_header->pgm_sport != transport->dport)
		{

/* its upstream/peer-to-peer for another session */

			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

		if ( pgm_is_peer (skb->pgm_header->pgm_type)
			&& pgm_sockaddr_is_addr_multicast ((struct sockaddr*)&dst) )
		{

/* its a multicast peer-to-peer message */

			if ( skb->pgm_header->pgm_dport == transport->tsi.sport )
			{

/* we are the source, propagate null as the source */

				source = NULL;

				if (!transport->can_send_data)
				{
					transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
					goto check_for_repeat;
				}
			}
			else
			{
/* we are not the source */

				if (!transport->can_recv)
				{
					transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
					goto check_for_repeat;
				}

/* check to see the source this peer-to-peer message is about is in our peer list */
				g_static_rw_lock_reader_lock (&transport->peers_lock);
				source = g_hash_table_lookup (transport->peers_hashtable, &skb->tsi);
				g_static_rw_lock_reader_unlock (&transport->peers_lock);
				if (source == NULL)
				{

/* this source is unknown, we don't care about messages about it */

					transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
					goto check_for_repeat;
				}
			}
		}
		else if ( pgm_is_upstream (skb->pgm_header->pgm_type)
			&& !pgm_sockaddr_is_addr_multicast ((struct sockaddr*)&dst)
			&& ( skb->pgm_header->pgm_dport == transport->tsi.sport )
			&& pgm_gsi_equal (&skb->tsi.gsi, &transport->tsi.gsi) )
		{

/* unicast upstream message, note that dport & sport are reversed */

			source = NULL;

			if (!transport->can_send_data)
			{
				transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
				goto check_for_repeat;
			}
		}
		else
		{

/* it is a mystery! */

			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

		skb->data       = (guint8*)skb->data + sizeof(struct pgm_header);
		skb->len       -= sizeof(struct pgm_header);

		switch (skb->pgm_header->pgm_type) {
		case PGM_NAK:
			if (source) {
				on_peer_nak (source, skb->pgm_header, skb->data, skb->len);
			} else if (!pgm_sockaddr_is_addr_multicast ((struct sockaddr*)&dst)) {
				_pgm_on_nak (transport, skb->pgm_header, skb->data, skb->len);
				goto check_for_repeat;
			} else {
/* ignore multicast NAKs as the source */
				transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
				goto check_for_repeat;
			}
			break;

		case PGM_NNAK:	_pgm_on_nnak (transport, skb->pgm_header, skb->data, skb->len); break;
		case PGM_SPMR:	_pgm_on_spmr (transport, source, skb->pgm_header, skb->data, skb->len); break;
		case PGM_POLR:
		default:
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}
	}
	else
	{

/* downstream = source to receivers */

		if (!pgm_is_downstream (skb->pgm_header->pgm_type))
		{
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

/* pgm packet DPORT contains our transport DPORT */
		if (skb->pgm_header->pgm_dport != transport->dport)
		{
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

		if (!transport->can_recv)
		{
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

/* search for TSI peer context or create a new one */
		g_static_rw_lock_reader_lock (&transport->peers_lock);
		source = g_hash_table_lookup (transport->peers_hashtable, &skb->tsi);
		g_static_rw_lock_reader_unlock (&transport->peers_lock);
		if (source == NULL)
		{
			source = new_peer (transport,
					   &skb->tsi,
					   (struct sockaddr*)&src, pgm_sockaddr_len(&src),
					   (struct sockaddr*)&dst, pgm_sockaddr_len(&dst));
		}

		source->cumulative_stats[PGM_PC_RECEIVER_BYTES_RECEIVED] += len;
		source->last_packet = skb->tstamp;

		skb->data       = (gpointer)( skb->pgm_header + 1 );
		skb->len       -= sizeof(struct pgm_header);

/* handle PGM packet type */
		switch (skb->pgm_header->pgm_type) {
		case PGM_ODATA:
		case PGM_RDATA:
			if (on_data (source, skb)) {
				skb = transport->rx_buffer = pgm_alloc_skb (transport->max_tpdu);
				iov.iov_base = skb->head;
			}
			break;

		case PGM_NCF:
			on_ncf (source, skb->pgm_header, skb->data, skb->len);
			break;

		case PGM_SPM:
			on_spm (source, skb->pgm_header, skb->data, skb->len);

/* update group NLA if appropriate */
			if (pgm_sockaddr_is_addr_multicast ((struct sockaddr*)&dst)) {
				memcpy (&source->group_nla, &dst, pgm_sockaddr_len(&dst));
			}
			break;

		default:
			transport->cumulative_stats[PGM_PC_SOURCE_PACKETS_DISCARDED]++;
			goto check_for_repeat;
		}

	} /* downstream message */

/* check whether source has waiting data */
	if (source &&
	    pgm_rxw_epoll ((pgm_rxw_t*)source->rxw) &&
	    !((pgm_rxw_t*)source->rxw)->waiting_link.data)
	{
		((pgm_rxw_t*)source->rxw)->waiting_link.data = source->rxw;
		((pgm_rxw_t*)source->rxw)->waiting_link.next = transport->peers_waiting;
		transport->peers_waiting = &((pgm_rxw_t*)source->rxw)->waiting_link;
		goto flush_waiting;	/* :D */
	}

flush_waiting:
/* flush any congtiguous packets generated by the receipt of this packet */
	while (transport->peers_waiting)
	{
		pgm_rxw_t* waiting_rxw = transport->peers_waiting->data;
		const gssize peer_bytes_read = pgm_rxw_readv (waiting_rxw, &pmsg, msg_end - pmsg);

		if (waiting_rxw->ack_cumulative_losses != waiting_rxw->cumulative_losses)
		{
			transport->has_lost_data = TRUE;
			waiting_rxw->pgm_sock_err.lost_count = waiting_rxw->cumulative_losses - waiting_rxw->ack_cumulative_losses;
			waiting_rxw->ack_cumulative_losses = waiting_rxw->cumulative_losses;
		}

		if (peer_bytes_read >= 0)
		{
			bytes_read += peer_bytes_read;
			data_read++;

			if (pgm_rxw_is_full (waiting_rxw)) 		/* window full */
			{
				goto out;
			}

			if (pmsg == msg_end) /* commit full */
			{
				goto out;
			}
		}
		if (transport->has_lost_data)
		{
			goto out;
		}
 
/* next */
		transport->peers_waiting->data = NULL;
		transport->peers_waiting->next = NULL;
		transport->peers_waiting = transport->peers_waiting->next;
	}

check_for_repeat:
/* repeat if non-blocking and not full */
	if (flags & MSG_DONTWAIT)
	{
		if (len > 0 && pmsg < msg_end &&
			( ( data_read == 0 && msg_len == 1 ) ||		/* leave early with one apdu */
			( msg_len > 1 ) )				/* or wait for vector to fill up */
		)
		{
			g_trace ("SPM","recv again on not-full");
			goto recv_again;		/* \:D/ */
		}
	}
	else
	{
/* repeat if blocking and empty, i.e. received non data packet.
 */
		if (0 == data_read)
		{
			int n_fds = 2;
#ifdef CONFIG_HAVE_POLL
			struct pollfd fds[ n_fds ];
			memset (fds, 0, sizeof(fds));
			if (-1 == pgm_transport_poll_info (transport, fds, &n_fds, POLLIN)) {
				g_trace ("SPM", "poll_info returned errno=%i",errno);
				return -1;
			}
#else
			fd_set readfds;
			FD_ZERO(&readfds);
			if (-1 == pgm_transport_select_info (transport, &readfds, NULL, &n_fds)) {
				g_trace ("SPM", "select_info returned errno=%i",errno);
				return -1;
			}
#endif /* CONFIG_HAVE_POLL */

/* flush any waiting notifications */
			if (transport->is_waiting_read) {
				pgm_notify_clear (&transport->waiting_notify);
				transport->is_waiting_read = FALSE;
			}

/* spin the locks to allow other thread to set waiting state,
 * first run should trigger waiting pipe event which will flush and loop.
 */
			g_static_mutex_unlock (&transport->waiting_mutex);

#ifdef CONFIG_HAVE_POLL
			int ready = poll (fds, n_fds, -1 /* timeout=∞ */);
#else
			int ready = select (n_fds, &readfds, NULL, NULL, NULL);
#endif

			if (-1 == ready) {
				g_trace ("SPM","block returned errno=%i",errno);
				return ready;
			}
			g_static_mutex_lock (&transport->waiting_mutex);

#ifdef CONFIG_HAVE_POLL
			if (fds[0].revents)
#else
			if (FD_ISSET(transport->recv_sock, &readfds))
#endif
			{
				g_trace ("SPM","recv again on empty");
				goto recv_again;
			} else {
				g_trace ("SPM","state generated event");
				goto flush_waiting;
			}
		}
	}

out:
	if (0 == data_read)
	{
		if (transport->is_waiting_read) {
			pgm_notify_clear (&transport->waiting_notify);
			transport->is_waiting_read = FALSE;
		}

		g_static_mutex_unlock (&transport->waiting_mutex);

		if (transport->has_lost_data) {
			pgm_rxw_t* lost_rxw = transport->peers_waiting->data;
			msg_start[0].msgv_len = sizeof(lost_rxw->pgm_sock_err);
			msg_start[0].msgv_skb[0] = (gpointer)&lost_rxw->pgm_sock_err;
			if (transport->will_close_on_failure) {
				transport->is_open = FALSE;
			} else {
				transport->has_lost_data = !transport->has_lost_data;
			}
			errno = ECONNRESET;
		} else {
			errno = EAGAIN;
		}

/* return reset on zero bytes instead of waiting for next call */
		return -1;
	}
	else if (transport->peers_waiting)
	{
		if (transport->is_waiting_read && transport->is_edge_triggered_recv)
		{
/* empty waiting-pipe */
			pgm_notify_clear (&transport->waiting_notify);
			transport->is_waiting_read = FALSE;
		}
		else if (!transport->is_waiting_read && !transport->is_edge_triggered_recv)
		{
/* fill waiting-pipe */
			if (!pgm_notify_send (&transport->waiting_notify)) {
				g_critical ("send to waiting notify channel failed :(");
			}
			transport->is_waiting_read = TRUE;
		}
	}

	g_static_mutex_unlock (&transport->waiting_mutex);
	return bytes_read;
}

/* read one contiguous apdu and return as a IO scatter/gather array.  msgv is owned by
 * the caller, tpdu contents are owned by the receive window.
 *
 * on success, returns the number of bytes read.  on error, -1 is returned, and
 * errno is set appropriately.
 */

gssize
pgm_transport_recvmsg (
	pgm_transport_t*	transport,
	pgm_msgv_t*		msgv,
	int			flags		/* MSG_DONTWAIT for non-blocking */
	)
{
	return pgm_transport_recvmsgv (transport, msgv, 1, flags);
}

/* vanilla read function.  copies from the receive window to the provided buffer
 * location.  the caller must provide an adequately sized buffer to store the largest
 * expected apdu or else it will be truncated.
 *
 * on success, returns the number of bytes read.  on error, -1 is returned, and
 * errno is set appropriately.
 */

gssize
pgm_transport_recvfrom (
	pgm_transport_t*	transport,
	gpointer		data,
	gsize			len,
	int			flags,		/* MSG_DONTWAIT for non-blocking */
	pgm_tsi_t*		from
	)
{
	pgm_msgv_t msgv;
	gssize bytes_read;

	bytes_read = pgm_transport_recvmsg (transport, &msgv, flags & ~MSG_FIN);

	if (bytes_read >= 0)
	{
		gssize bytes_copied = 0;
		struct pgm_sk_buff_t* skb = msgv.msgv_skb[0];

		if (from)
			memcpy (from, &skb->tsi, sizeof(pgm_tsi_t));

		while (bytes_copied < bytes_read)
		{
			gsize copy_len = skb->len;
			if (bytes_copied + copy_len > len) {
				g_error ("APDU truncated as provided buffer too small %" G_GSIZE_FORMAT " > %" G_GSIZE_FORMAT,
					bytes_read, len);
				copy_len = len - bytes_copied;
				bytes_read = len;
			}

			memcpy ((guint8*)data + bytes_copied, skb->data, copy_len);
			bytes_copied += copy_len;
			skb++;
		}
	}
	else if (errno == ECONNRESET)
	{
		memcpy (data, msgv.msgv_skb[0], msgv.msgv_len);
	}

	return bytes_read;
}

gssize
pgm_transport_recv (
	pgm_transport_t*	transport,
	gpointer		data,
	gsize			len,
	int			flags		/* MSG_DONTWAIT for non-blocking */
	)
{
	return pgm_transport_recvfrom (transport, data, len, flags, NULL);
}

/* SPM indicate start of a session, continued presence of a session, or flushing final packets
 * of a session.
 *
 * returns -EINVAL on invalid packet or duplicate SPM sequence number.
 */

static int
on_spm (
	pgm_peer_t*		sender,
	struct pgm_header*	header,
	gpointer		data,		/* data will be changed to host order on demand */
	gsize			len
	)
{
	int retval;

	if ((retval = pgm_verify_spm (header, data, len)) != 0)
	{
		sender->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
		sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	struct pgm_transport_t* transport = sender->transport;
	struct pgm_spm* spm = (struct pgm_spm*)data;
	struct pgm_spm6* spm6 = (struct pgm_spm6*)data;
	const pgm_time_t now = pgm_time_update_now ();

	spm->spm_sqn = g_ntohl (spm->spm_sqn);

/* check for advancing sequence number, or first SPM */
	g_static_mutex_lock (&transport->mutex);
	g_static_mutex_lock (&sender->mutex);
	if ( pgm_uint32_gte (spm->spm_sqn, sender->spm_sqn)
		|| ( ((struct sockaddr*)&sender->nla)->sa_family == 0 ) )
	{
/* copy NLA for replies */
		pgm_nla_to_sockaddr (&spm->spm_nla_afi, (struct sockaddr*)&sender->nla);

/* save sequence number */
		sender->spm_sqn = spm->spm_sqn;

/* update receive window */
		pgm_time_t nak_rb_expiry = now + nak_rb_ivl(transport);
		guint naks = pgm_rxw_update (sender->rxw,
					     g_ntohl (spm->spm_lead),
					     g_ntohl (spm->spm_trail),
					     nak_rb_expiry);
		if (naks && pgm_time_after(transport->next_poll, nak_rb_expiry))
		{
			transport->next_poll = nak_rb_expiry;
			g_trace ("INFO","on_spm: prod timer thread");
			if (!pgm_notify_send (&transport->timer_notify)) {
				g_critical ("send to timer notify channel failed :(");
				retval = -EINVAL;
			}
		}

/* mark receiver window for flushing on next recv() */
		pgm_rxw_t* sender_rxw = (pgm_rxw_t*)sender->rxw;
		if (sender_rxw->cumulative_losses != sender_rxw->ack_cumulative_losses &&
		    !sender_rxw->waiting_link.data)
		{
			transport->has_lost_data = TRUE;
			sender_rxw->pgm_sock_err.lost_count = sender_rxw->cumulative_losses - sender_rxw->ack_cumulative_losses;
			sender_rxw->ack_cumulative_losses = sender_rxw->cumulative_losses;

			sender_rxw->waiting_link.data = sender_rxw;
			sender_rxw->waiting_link.next = transport->peers_waiting;
			transport->peers_waiting = &sender_rxw->waiting_link;
		}
	}
	else
	{	/* does not advance SPM sequence number */
		sender->cumulative_stats[PGM_PC_RECEIVER_DUP_SPMS]++;
		sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		retval = -EINVAL;
	}

/* check whether peer can generate parity packets */
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		struct pgm_opt_length* opt_len = (spm->spm_nla_afi == AFI_IP6) ?
							(struct pgm_opt_length*)(spm6 + 1) :
							(struct pgm_opt_length*)(spm + 1);
		if (opt_len->opt_type != PGM_OPT_LENGTH)
		{
			retval = -EINVAL;
			sender->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
			sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out;
		}
		if (opt_len->opt_length != sizeof(struct pgm_opt_length))
		{
			retval = -EINVAL;
			sender->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
			sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out;
		}
/* TODO: check for > 16 options & past packet end */
		struct pgm_opt_header* opt_header = (struct pgm_opt_header*)opt_len;
		do {
			opt_header = (struct pgm_opt_header*)((char*)opt_header + opt_header->opt_length);

			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_PARITY_PRM)
			{
				struct pgm_opt_parity_prm* opt_parity_prm = (struct pgm_opt_parity_prm*)(opt_header + 1);

				if ((opt_parity_prm->opt_reserved & PGM_PARITY_PRM_MASK) == 0)
				{
					retval = -EINVAL;
					sender->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
					sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
					goto out;
				}

				guint32 parity_prm_tgs = g_ntohl (opt_parity_prm->parity_prm_tgs);
				if (parity_prm_tgs < 2 || parity_prm_tgs > 128)
				{
					retval = -EINVAL;
					sender->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
					sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
					goto out;
				}
			
				sender->use_proactive_parity = opt_parity_prm->opt_reserved & PGM_PARITY_PRM_PRO;
				sender->use_ondemand_parity = opt_parity_prm->opt_reserved & PGM_PARITY_PRM_OND;
				if (sender->rs_k != parity_prm_tgs)
				{
					sender->rs_n = PGM_RS_DEFAULT_N;
					sender->rs_k = parity_prm_tgs;
					sender->tg_sqn_shift = pgm_power2_log2 (sender->rs_k);
					if (sender->rs) {
						g_trace ("INFO", "Destroying existing Reed-Solomon state for peer.");
						_pgm_rs_destroy (sender->rs);
					}
					g_trace ("INFO", "Enabling Reed-Solomon forward error correction for peer, RS(%i,%i)",
						sender->rs_n, sender->rs_k);
					_pgm_rs_create (&sender->rs, sender->rs_n, sender->rs_k);
				}
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

/* either way bump expiration timer */
	sender->expiry = now + transport->peer_expiry;
	sender->spmr_expiry = 0;
	g_static_mutex_unlock (&sender->mutex);
	g_static_mutex_unlock (&transport->mutex);

out:
	return retval;
}

/* Multicast peer-to-peer NAK handling, pretty much the same as a NCF but different direction
 *
 * if NAK is valid, returns 0.  on error, -EINVAL is returned.
 */

static int
on_peer_nak (
	pgm_peer_t*		peer,
	struct pgm_header*	header,
	gpointer		data,
	gsize			len
	)
{
	g_trace ("INFO","on_peer_nak()");

	int retval;
	pgm_transport_t* transport = peer->transport;

	if ((retval = pgm_verify_nak (header, data, len)) != 0)
	{
		g_trace ("INFO", "Invalid NAK, ignoring.");
		peer->cumulative_stats[PGM_PC_RECEIVER_NAK_ERRORS]++;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	const struct pgm_nak* nak = (struct pgm_nak*)data;
	const struct pgm_nak6* nak6 = (struct pgm_nak6*)data;
		
/* NAK_SRC_NLA must not contain our transport unicast NLA */
	struct sockaddr_storage nak_src_nla;
	pgm_nla_to_sockaddr (&nak->nak_src_nla_afi, (struct sockaddr*)&nak_src_nla);

	if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_src_nla, (struct sockaddr*)&transport->send_addr) == 0) {
		retval = -EINVAL;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

/* NAK_GRP_NLA contains one of our transport receive multicast groups: the sources send multicast group */ 
	struct sockaddr_storage nak_grp_nla;
	pgm_nla_to_sockaddr ((nak->nak_src_nla_afi == AFI_IP6) ? &nak6->nak6_grp_nla_afi : &nak->nak_grp_nla_afi,
				(struct sockaddr*)&nak_grp_nla);

	gboolean found = FALSE;
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_grp_nla, (struct sockaddr*)&transport->recv_gsr[i].gsr_group) == 0)
		{
			found = TRUE;
		}
	}

	if (!found) {
		g_trace ("INFO", "NAK not destined for this multicast group.");
		retval = -EINVAL;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	g_static_mutex_lock (&transport->mutex);
	g_static_mutex_lock (&peer->mutex);

/* handle as NCF */
	pgm_time_update_now();
	pgm_rxw_confirm (peer->rxw,
			 g_ntohl (nak->nak_sqn),
			 pgm_time_now + transport->nak_rdata_ivl,
			 pgm_time_now + nak_rb_ivl(transport));

/* check NAK list */
	const guint32* nak_list = NULL;
	guint nak_list_len = 0;
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (nak->nak_src_nla_afi == AFI_IP6) ?
							(const struct pgm_opt_length*)(nak6 + 1) :
							(const struct pgm_opt_length*)(nak + 1);
		if (opt_len->opt_type != PGM_OPT_LENGTH)
		{
			g_trace ("INFO", "First PGM Option in NAK incorrect, ignoring.");
			retval = -EINVAL;
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out_unlock;
		}
		if (opt_len->opt_length != sizeof(struct pgm_opt_length))
		{
			g_trace ("INFO", "PGM Length Option has incorrect length, ignoring.");
			retval = -EINVAL;
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out_unlock;
		}
/* TODO: check for > 16 options & past packet end */
		const struct pgm_opt_header* opt_header = (const struct pgm_opt_header*)opt_len;
		do {
			opt_header = (const struct pgm_opt_header*)((const char*)opt_header + opt_header->opt_length);

			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST)
			{
				nak_list = ((const struct pgm_opt_nak_list*)(opt_header + 1))->opt_sqn;
				nak_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(guint8) ) / sizeof(guint32);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

	g_trace ("INFO", "NAK contains 1+%i sequence numbers.", nak_list_len);
	while (nak_list_len)
	{
		pgm_rxw_confirm (peer->rxw,
				 g_ntohl (*nak_list),
				 pgm_time_now + transport->nak_rdata_ivl,
				 pgm_time_now + nak_rb_ivl(transport));
		nak_list++;
		nak_list_len--;
	}

/* mark receiver window for flushing on next recv() */
	pgm_rxw_t* peer_rxw = (pgm_rxw_t*)peer->rxw;
	if (peer_rxw->cumulative_losses != peer_rxw->ack_cumulative_losses &&
	    !peer_rxw->waiting_link.data)
	{
		transport->has_lost_data = TRUE;
		peer_rxw->pgm_sock_err.lost_count = peer_rxw->cumulative_losses - peer_rxw->ack_cumulative_losses;
		peer_rxw->ack_cumulative_losses = peer_rxw->cumulative_losses;

		peer_rxw->waiting_link.data = peer_rxw;
		peer_rxw->waiting_link.next = transport->peers_waiting;
		transport->peers_waiting = &peer_rxw->waiting_link;
	}

out_unlock:
	g_static_mutex_unlock (&peer->mutex);
	g_static_mutex_unlock (&transport->mutex);

out:
	return retval;
}

/* NCF confirming receipt of a NAK from this transport or another on the LAN segment.
 *
 * Packet contents will match exactly the sent NAK, although not really that helpful.
 *
 * if NCF is valid, returns 0.  on error, -EINVAL is returned.
 */

static int
on_ncf (
	pgm_peer_t*		peer,
	struct pgm_header*	header,
	gpointer		data,
	gsize			len
	)
{
	g_trace ("INFO","on_ncf()");

	int retval;
	pgm_transport_t* transport = peer->transport;

	if ((retval = pgm_verify_ncf (header, data, len)) != 0)
	{
		g_trace ("INFO", "Invalid NCF, ignoring.");
		peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	const struct pgm_nak* ncf = (struct pgm_nak*)data;
	const struct pgm_nak6* ncf6 = (struct pgm_nak6*)data;
		
/* NCF_SRC_NLA may contain our transport unicast NLA, we don't really care */
	struct sockaddr_storage ncf_src_nla;
	pgm_nla_to_sockaddr (&ncf->nak_src_nla_afi, (struct sockaddr*)&ncf_src_nla);

#if 0
	if (pgm_sockaddr_cmp ((struct sockaddr*)&ncf_src_nla, (struct sockaddr*)&transport->send_addr) != 0) {
		retval = -EINVAL;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}
#endif

/* NCF_GRP_NLA contains our transport multicast group */ 
	struct sockaddr_storage ncf_grp_nla;
	pgm_nla_to_sockaddr ((ncf->nak_src_nla_afi == AFI_IP6) ? &ncf6->nak6_grp_nla_afi : &ncf->nak_grp_nla_afi,
				(struct sockaddr*)&ncf_grp_nla);

	if (pgm_sockaddr_cmp ((struct sockaddr*)&ncf_grp_nla, (struct sockaddr*)&transport->send_gsr.gsr_group) != 0) {
		g_trace ("INFO", "NCF not destined for this multicast group.");
		retval = -EINVAL;
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		goto out;
	}

	g_static_mutex_lock (&transport->mutex);
	g_static_mutex_lock (&peer->mutex);

	pgm_time_update_now();
	pgm_rxw_confirm (peer->rxw,
			 g_ntohl (ncf->nak_sqn),
			 pgm_time_now + transport->nak_rdata_ivl,
			 pgm_time_now + nak_rb_ivl(transport));

/* check NCF list */
	const guint32* ncf_list = NULL;
	guint ncf_list_len = 0;
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (ncf->nak_src_nla_afi == AFI_IP6) ?
							(const struct pgm_opt_length*)(ncf6 + 1) :
							(const struct pgm_opt_length*)(ncf + 1);
		if (opt_len->opt_type != PGM_OPT_LENGTH)
		{
			g_trace ("INFO", "First PGM Option in NCF incorrect, ignoring.");
			retval = -EINVAL;
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out_unlock;
		}
		if (opt_len->opt_length != sizeof(struct pgm_opt_length))
		{
			g_trace ("INFO", "PGM Length Option has incorrect length, ignoring.");
			retval = -EINVAL;
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
			goto out_unlock;
		}
/* TODO: check for > 16 options & past packet end */
		const struct pgm_opt_header* opt_header = (const struct pgm_opt_header*)opt_len;
		do {
			opt_header = (const struct pgm_opt_header*)((const char*)opt_header + opt_header->opt_length);

			if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_NAK_LIST)
			{
				ncf_list = ((const struct pgm_opt_nak_list*)(opt_header + 1))->opt_sqn;
				ncf_list_len = ( opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(guint8) ) / sizeof(guint32);
				break;
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

	g_trace ("INFO", "NCF contains 1+%i sequence numbers.", ncf_list_len);
	while (ncf_list_len)
	{
		pgm_rxw_confirm (peer->rxw,
				 g_ntohl (*ncf_list),
				 pgm_time_now + transport->nak_rdata_ivl,
				 pgm_time_now + nak_rb_ivl(transport));
		ncf_list++;
		ncf_list_len--;
	}

/* mark receiver window for flushing on next recv() */
	pgm_rxw_t* peer_rxw = (pgm_rxw_t*)peer->rxw;
	if (peer_rxw->cumulative_losses != peer_rxw->ack_cumulative_losses &&
	    !peer_rxw->waiting_link.data)
	{
		transport->has_lost_data = TRUE;
		peer_rxw->pgm_sock_err.lost_count = peer_rxw->cumulative_losses - peer_rxw->ack_cumulative_losses;
		peer_rxw->ack_cumulative_losses = peer_rxw->cumulative_losses;

		peer_rxw->waiting_link.data = peer_rxw;
		peer_rxw->waiting_link.next = transport->peers_waiting;
		transport->peers_waiting = &peer_rxw->waiting_link;
	}

out_unlock:
	g_static_mutex_unlock (&peer->mutex);
	g_static_mutex_unlock (&transport->mutex);

out:
	return retval;
}

/* send SPM-request to a new peer, this packet type has no contents
 *
 * on success, 0 is returned.  on error, -1 is returned, and errno set
 * appropriately.
 */

static int
send_spmr (
	pgm_peer_t*	peer
	)
{
	g_trace ("INFO","send_spmr");

	pgm_transport_t* transport = peer->transport;

/* cache peer information */
	const guint16	peer_sport = peer->tsi.sport;
	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->local_nla, sizeof(struct sockaddr_storage));

	const gsize tpdu_length = sizeof(struct pgm_header);
	guint8 buf[ tpdu_length ];
	struct pgm_header *header = (struct pgm_header*)buf;
	memcpy (header->pgm_gsi, &peer->tsi.gsi, sizeof(pgm_gsi_t));
/* dport & sport reversed communicating upstream */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= peer_sport;
	header->pgm_type	= PGM_SPMR;
	header->pgm_options	= 0;
	header->pgm_tsdu_length	= 0;
	header->pgm_checksum	= 0;
	header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

/* send multicast SPMR TTL 1 */
	g_trace ("INFO", "send multicast SPMR to %s", inet_ntoa( ((struct sockaddr_in*)&transport->send_gsr.gsr_group)->sin_addr ));
	pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_group), 1);
	gssize sent = _pgm_sendto (transport,
				FALSE,			/* not rate limited */
				FALSE,			/* regular socket */
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&transport->send_gsr.gsr_group,
				pgm_sockaddr_len(&transport->send_gsr.gsr_group));

/* send unicast SPMR with regular TTL */
	g_trace ("INFO", "send unicast SPMR to %s", inet_ntoa( ((struct sockaddr_in*)&peer->local_nla)->sin_addr ));
	pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_group), transport->hops);
	sent += _pgm_sendto (transport,
				FALSE,
				FALSE,
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&peer_nla,
				pgm_sockaddr_len(&peer_nla));

	peer->spmr_expiry = 0;

	if ( sent != (gssize)(tpdu_length * 2) ) 
	{
		return -1;
	}

	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += tpdu_length * 2;

	return 0;
}

/* send selective NAK for one sequence number.
 *
 * on success, 0 is returned.  on error, -1 is returned, and errno set
 * appropriately.
 */
static int
send_nak (
	pgm_peer_t*		peer,
	guint32			sequence_number
	)
{
#ifdef TRANSPORT_DEBUG
	char s[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop(&peer->nla, s, sizeof(s));
	g_trace ("INFO", "send_nak(%" G_GUINT32_FORMAT ") -> %s:%hu", sequence_number, s, g_ntohs(((struct sockaddr_in*)&peer->nla)->sin_port));
#endif

	pgm_transport_t* transport = peer->transport;

	const guint16	peer_sport = peer->tsi.sport;
	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->nla, sizeof(struct sockaddr_storage));

	gsize tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	if (pgm_sockaddr_family(&peer_nla) == AF_INET6) {
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	}
	guint8 buf[ tpdu_length ];

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
	struct pgm_nak6 *nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &peer->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= peer_sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= g_htonl (sequence_number);

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer_nla, (char*)&nak->nak_src_nla_afi);

/* group nla: we match the NAK NLA to the same as advertised by the source, we might
 * be listening to multiple multicast groups
 */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer->group_nla, (nak->nak_src_nla_afi == AFI_IP6) ?
								(char*)&nak6->nak6_grp_nla_afi :
								(char*)&nak->nak_grp_nla_afi );

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	gssize sent = _pgm_sendto (transport,
				FALSE,			/* not rate limited */
				TRUE,			/* with router alert */
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&peer_nla,
				pgm_sockaddr_len(&peer_nla));

	if ( sent != (gssize)tpdu_length )
	{
		return -1;
	}

	peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT]++;
	peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT]++;

	return 0;
}

/* Send a parity NAK requesting on-demand parity packet generation.
 *
 * on success, 0 is returned.  on error, -1 is returned, and errno set
 * appropriately.
 */
static int
send_parity_nak (
	pgm_peer_t*		peer,
	guint32			nak_tg_sqn,	/* transmission group (shifted) */
	guint32			nak_pkt_cnt	/* count of parity packets to request */
	)
{
	g_trace ("INFO", "send_parity_nak(%u, %u)", nak_tg_sqn, nak_pkt_cnt);

	pgm_transport_t* transport = peer->transport;

	guint16	peer_sport = peer->tsi.sport;
	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->nla, sizeof(struct sockaddr_storage));

	gsize tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	if (pgm_sockaddr_family(&peer_nla) == AF_INET6) {
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	}
	guint8 buf[ tpdu_length ];

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
	struct pgm_nak6 *nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &peer->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= peer_sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = PGM_OPT_PARITY;	/* this is a parity packet */
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= g_htonl (nak_tg_sqn | (nak_pkt_cnt - 1) );

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer_nla, (char*)&nak->nak_src_nla_afi);

/* group nla: we match the NAK NLA to the same as advertised by the source, we might
 * be listening to multiple multicast groups
 */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer->group_nla, (nak->nak_src_nla_afi == AFI_IP6) ?
									(char*)&nak6->nak6_grp_nla_afi :
									(char*)&nak->nak_grp_nla_afi );

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	gssize sent = _pgm_sendto (transport,
				FALSE,			/* not rate limited */
				TRUE,			/* with router alert */
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&peer_nla,
				pgm_sockaddr_len(&peer_nla));

	if ( sent != (gssize)tpdu_length )
	{
		return -1;
	}

	peer->cumulative_stats[PGM_PC_RECEIVER_PARITY_NAK_PACKETS_SENT]++;
	peer->cumulative_stats[PGM_PC_RECEIVER_PARITY_NAKS_SENT]++;

	return 0;
}

/* A NAK packet with a OPT_NAK_LIST option extension
 *
 * on success, 0 is returned.  on error, -1 is returned, and errno set
 * appropriately.
 */

#ifndef PGM_SINGLE_NAK
static int
send_nak_list (
	pgm_peer_t*	peer,
	pgm_sqn_list_t*	sqn_list
	)
{
	g_assert (sqn_list->len > 1);
	g_assert (sqn_list->len <= 63);

	pgm_transport_t* transport = peer->transport;

	guint16	peer_sport = peer->tsi.sport;
	struct sockaddr_storage peer_nla;
	memcpy (&peer_nla, &peer->nla, sizeof(struct sockaddr_storage));

	gsize tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak)
			+ sizeof(struct pgm_opt_length)		/* includes header */
			+ sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
			+ ( (sqn_list->len-1) * sizeof(guint32) );
	if (pgm_sockaddr_family(&peer_nla) == AFI_IP6) {
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	}
	guint8 buf[ tpdu_length ];
	memset (buf, 0, sizeof(buf));

	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
	struct pgm_nak6 *nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &peer->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= peer_sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = PGM_OPT_PRESENT | PGM_OPT_NETWORK;
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= g_htonl (sqn_list->sqn[0]);

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer_nla, (char*)&nak->nak_src_nla_afi);

/* group nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&peer->group_nla, (nak->nak_src_nla_afi == AFI_IP6) ? 
								(char*)&nak6->nak6_grp_nla_afi :
								(char*)&nak->nak_grp_nla_afi );

/* OPT_NAK_LIST */
	struct pgm_opt_length* opt_len = (nak->nak_src_nla_afi == AFI_IP6) ? 
						(struct pgm_opt_length*)(nak6 + 1) :
						(struct pgm_opt_length*)(nak + 1);
	opt_len->opt_type	= PGM_OPT_LENGTH;
	opt_len->opt_length	= sizeof(struct pgm_opt_length);
	opt_len->opt_total_length = g_htons (	sizeof(struct pgm_opt_length) +
						sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_nak_list) +
						( (sqn_list->len-1) * sizeof(guint32) ) );
	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
	opt_header->opt_type	= PGM_OPT_NAK_LIST | PGM_OPT_END;
	opt_header->opt_length	= sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list)
				+ ( (sqn_list->len-1) * sizeof(guint32) );
	struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
	opt_nak_list->opt_reserved = 0;

#ifdef TRANSPORT_DEBUG
	char s[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop(&peer->nla, s, sizeof(s));
	char nak1[1024];
	sprintf (nak1, "send_nak_list( %" G_GUINT32_FORMAT " + [", sqn_list->sqn[0]);
#endif
	for (unsigned i = 1; i < sqn_list->len; i++) {
		opt_nak_list->opt_sqn[i-1] = g_htonl (sqn_list->sqn[i]);

#ifdef TRANSPORT_DEBUG
		char nak2[1024];
		sprintf (nak2, "%" G_GUINT32_FORMAT " ", sqn_list->sqn[i]);
		strcat (nak1, nak2);
#endif
	}

#ifdef TRANSPORT_DEBUG
	g_trace ("INFO", "%s]%i ) -> %s:%hu", nak1, sqn_list->len, s, g_ntohs(((struct sockaddr_in*)&peer->nla)->sin_port));
#endif

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	gssize sent = _pgm_sendto (transport,
				FALSE,			/* not rate limited */
				FALSE,			/* regular socket */
				header,
				tpdu_length,
				MSG_CONFIRM,		/* not expecting a reply */
				(struct sockaddr*)&peer_nla,
				pgm_sockaddr_len(&peer_nla));

	if ( sent != (gssize)tpdu_length )
	{
		return -1;
	}

	peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT]++;
	peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT] += 1 + sqn_list->len;

	return 0;
}
#endif /* !PGM_SINGLE_NAK */

/* check all receiver windows for packets in BACK-OFF_STATE, on expiration send a NAK.
 * update transport::next_nak_rb_timestamp for next expiration time.
 *
 * peer object is locked before entry.
 */

static void
nak_rb_state (
	pgm_peer_t*		peer
	)
{
	pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;
	pgm_transport_t* transport = peer->transport;
	GList* list;
#ifndef PGM_SINGLE_NAK
	pgm_sqn_list_t nak_list = { .len = 0 };
#endif

	g_trace ("INFO", "nak_rb_state(len=%u)", g_list_length(rxw->backoff_queue.tail));

/* send all NAKs first, lack of data is blocking contiguous processing and its 
 * better to get the notification out a.s.a.p. even though it might be waiting
 * in a kernel queue.
 *
 * alternative: after each packet check for incoming data and return to the
 * event loop.  bias for shorter loops as retry count increases.
 */
	list = rxw->backoff_queue.tail;
	if (!list) {
		g_assert (rxw->backoff_queue.head == NULL);
		g_warning ("backoff queue is empty in nak_rb_state.");
		return;
	} else {
		g_assert (rxw->backoff_queue.head != NULL);
	}

	guint dropped_invalid = 0;

/* have not learned this peers NLA */
	const gboolean is_valid_nla = (((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr != INADDR_ANY);

/* TODO: process BOTH selective and parity NAKs? */

/* calculate current transmission group for parity enabled peers */
	if (peer->use_ondemand_parity)
	{
		const guint32 tg_sqn_mask = 0xffffffff << peer->tg_sqn_shift;

/* NAKs only generated previous to current transmission group */
		const guint32 current_tg_sqn = ((pgm_rxw_t*)peer->rxw)->lead & tg_sqn_mask;

		guint32 nak_tg_sqn = 0;
		guint32 nak_pkt_cnt = 0;

/* parity NAK generation */

		while (list)
		{
			GList* next_list_el = list->prev;
			struct pgm_sk_buff_t* skb	= (struct pgm_sk_buff_t*)list;
			pgm_rxw_state_t* state		= (pgm_rxw_state_t*)&skb->cb;

/* check this packet for state expiration */
			if (pgm_time_after_eq(pgm_time_now, state->nak_rb_expiry))
			{
				if (!is_valid_nla)
				{
					dropped_invalid++;
					g_trace ("INFO", "lost data #%u due to no peer NLA.", skb->sequence);
					pgm_rxw_lost (rxw, skb->sequence);

/* mark receiver window for flushing on next recv() */
					if (!rxw->waiting_link.data)
					{
						rxw->waiting_link.data = rxw;
						rxw->waiting_link.next = transport->peers_waiting;
						transport->peers_waiting = &rxw->waiting_link;
					}

					list = next_list_el;
					continue;
				}

/* TODO: parity nak lists */
				const guint32 tg_sqn = skb->sequence & tg_sqn_mask;
				if (	( nak_pkt_cnt && tg_sqn == nak_tg_sqn ) ||
					( !nak_pkt_cnt && tg_sqn != current_tg_sqn )	)
				{
					pgm_rxw_state (rxw, skb, PGM_PKT_WAIT_NCF_STATE);

					if (!nak_pkt_cnt++)
						nak_tg_sqn = tg_sqn;
					state->nak_transmit_count++;

#ifdef PGM_ABSOLUTE_EXPIRY
					state->nak_rpt_expiry = state->nak_rb_expiry + transport->nak_rpt_ivl;
					while (pgm_time_after_eq(pgm_time_now, state->nak_rpt_expiry){
						state->nak_rpt_expiry += transport->nak_rpt_ivl;
						state->ncf_retry_count++;
					}
#else
					state->nak_rpt_expiry = pgm_time_now + transport->nak_rpt_ivl;
#endif
				}
				else
				{	/* different transmission group */
					break;
				}
			}
			else
			{	/* packet expires some time later */
				break;
			}

			list = next_list_el;
		}

		if (nak_pkt_cnt)
		{
			send_parity_nak (peer, nak_tg_sqn, nak_pkt_cnt);
		}
	}
	else
	{

/* select NAK generation */

		while (list)
		{
			GList* next_list_el = list->prev;
			struct pgm_sk_buff_t* skb	= (struct pgm_sk_buff_t*)list;
			pgm_rxw_state_t* state		= (pgm_rxw_state_t*)&skb->cb;

/* check this packet for state expiration */
			if (pgm_time_after_eq(pgm_time_now, state->nak_rb_expiry))
			{
				if (!is_valid_nla) {
					dropped_invalid++;
					g_trace ("INFO", "lost data #%u due to no peer NLA.", skb->sequence);
					pgm_rxw_lost (rxw, skb->sequence);

/* mark receiver window for flushing on next recv() */
					if (!rxw->waiting_link.data)
					{
						rxw->waiting_link.data = rxw;
						rxw->waiting_link.next = transport->peers_waiting;
						transport->peers_waiting = &rxw->waiting_link;
					}

					list = next_list_el;
					continue;
				}

				pgm_rxw_state (rxw, skb, PGM_PKT_WAIT_NCF_STATE);
#if PGM_SINGLE_NAK
				if (transport->can_send_nak)
					send_nak (transport, peer, skb->sequence);
				pgm_time_update_now();
#else
				nak_list.sqn[nak_list.len++] = skb->sequence;
#endif
				state->nak_transmit_count++;

/* we have two options here, calculate the expiry time in the new state relative to the current
 * state execution time, skipping missed expirations due to delay in state processing, or base
 * from the actual current time.
 */
#ifdef PGM_ABSOLUTE_EXPIRY
				state->nak_rpt_expiry = state->nak_rb_expiry + transport->nak_rpt_ivl;
				while (pgm_time_after_eq(pgm_time_now, state->nak_rpt_expiry){
					state->nak_rpt_expiry += transport->nak_rpt_ivl;
					state->ncf_retry_count++;
				}
#else
				state->nak_rpt_expiry = pgm_time_now + transport->nak_rpt_ivl;
g_trace("INFO", "rp->nak_rpt_expiry in %f seconds.",
		pgm_to_secsf( state->nak_rpt_expiry - pgm_time_now ) );
#endif

#ifndef PGM_SINGLE_NAK
				if (nak_list.len == G_N_ELEMENTS(nak_list.sqn)) {
					if (transport->can_send_nak)
						send_nak_list (peer, &nak_list);
					pgm_time_update_now();
					nak_list.len = 0;
				}
#endif
			}
			else
			{	/* packet expires some time later */
				break;
			}

			list = next_list_el;
		}

#ifndef PGM_SINGLE_NAK
		if (transport->can_send_nak && nak_list.len)
		{
			if (nak_list.len > 1) {
				send_nak_list (peer, &nak_list);
			} else {
				g_assert (nak_list.len == 1);
				send_nak (peer, nak_list.sqn[0]);
			}
		}
#endif

	}

	if (dropped_invalid)
	{
		g_warning ("dropped %u messages due to invalid NLA.", dropped_invalid);

/* mark receiver window for flushing on next recv() */
		if (rxw->cumulative_losses != rxw->ack_cumulative_losses &&
		    !rxw->waiting_link.data)
		{
			transport->has_lost_data = TRUE;
			rxw->pgm_sock_err.lost_count = rxw->cumulative_losses - rxw->ack_cumulative_losses;
			rxw->ack_cumulative_losses = rxw->cumulative_losses;

			rxw->waiting_link.data = rxw;
			rxw->waiting_link.next = transport->peers_waiting;
			transport->peers_waiting = &rxw->waiting_link;
		}
	}

	if (rxw->backoff_queue.length == 0)
	{
		g_assert ((struct rxw_packet*)rxw->backoff_queue.head == NULL);
		g_assert ((struct rxw_packet*)rxw->backoff_queue.tail == NULL);
	}
	else
	{
		g_assert ((struct rxw_packet*)rxw->backoff_queue.head != NULL);
		g_assert ((struct rxw_packet*)rxw->backoff_queue.tail != NULL);
	}

	if (rxw->backoff_queue.tail)
	{
		g_trace ("INFO", "next expiry set in %f seconds.",
			pgm_to_secsf(next_nak_rb_expiry(rxw) - pgm_time_now));
	}
	else
	{
		g_trace ("INFO", "backoff queue empty.");
	}
}

/* check this peer for NAK state timers, uses the tail of each queue for the nearest
 * timer execution.
 */

void
_pgm_check_peer_nak_state (
	pgm_transport_t*	transport
	)
{
	if (!transport->peers_list) {
		return;
	}

	GList* list = transport->peers_list;
	do {
		GList* next = list->next;
		pgm_peer_t* peer = list->data;
		pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;

		g_static_mutex_lock (&peer->mutex);

		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (pgm_time_now, peer->spmr_expiry))
			{
				if (transport->can_send_nak)
					send_spmr (peer);
				else
					peer->spmr_expiry = 0;
			}
		}

		if (rxw->backoff_queue.tail)
		{
			if (pgm_time_after_eq (pgm_time_now, next_nak_rb_expiry(rxw)))
			{
				nak_rb_state (peer);
			}
		}
		
		if (rxw->wait_ncf_queue.tail)
		{
			if (pgm_time_after_eq (pgm_time_now, next_nak_rpt_expiry(rxw)))
			{
				nak_rpt_state (peer);
			}
		}

		if (rxw->wait_data_queue.tail)
		{
			if (pgm_time_after_eq (pgm_time_now, next_nak_rdata_expiry(rxw)))
			{
				nak_rdata_state (peer);
			}
		}

/* expired, remove from hash table and linked list */
		if (pgm_time_after_eq (pgm_time_now, peer->expiry))
		{
			if (((pgm_rxw_t*)peer->rxw)->committed_count)
			{
				g_trace ("INFO", "peer expiration postponed due to committed data, tsi %s", pgm_print_tsi (&peer->tsi));
				peer->expiry += transport->peer_expiry;
				g_static_mutex_unlock (&peer->mutex);
			}
			else
			{
				g_warning ("peer expired, tsi %s", pgm_print_tsi (&peer->tsi));
				g_hash_table_remove (transport->peers_hashtable, &peer->tsi);
				transport->peers_list = g_list_remove_link (transport->peers_list, &peer->link_);
				g_static_mutex_unlock (&peer->mutex);
				_pgm_peer_unref (peer);
			}
		}
		else
		{
			g_static_mutex_unlock (&peer->mutex);
		}

		list = next;
	} while (list);

/* check for waiting contiguous packets */
	if (transport->peers_waiting && !transport->is_waiting_read)
	{
		g_trace ("INFO","prod rx thread");
		if (!pgm_notify_send (&transport->waiting_notify)) {
			g_critical ("send to waiting notify channel failed :(");
		}
		transport->is_waiting_read = TRUE;
	}
}

/* find the next state expiration time among the transports peers.
 *
 * on success, returns the earliest of the expiration parameter or next
 * peer expiration time.
 */

pgm_time_t
_pgm_min_nak_expiry (
	pgm_time_t		expiration,
	pgm_transport_t*	transport
	)
{
	if (!transport->peers_list) {
		goto out;
	}

	GList* list = transport->peers_list;
	do {
		GList* next = list->next;
		pgm_peer_t* peer = (pgm_peer_t*)list->data;
		pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;
	
		g_static_mutex_lock (&peer->mutex);

		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (expiration, peer->spmr_expiry))
			{
				expiration = peer->spmr_expiry;
			}
		}

		if (rxw->backoff_queue.tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rb_expiry(rxw)))
			{
				expiration = next_nak_rb_expiry(rxw);
			}
		}

		if (rxw->wait_ncf_queue.tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rpt_expiry(rxw)))
			{
				expiration = next_nak_rpt_expiry(rxw);
			}
		}

		if (rxw->wait_data_queue.tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rdata_expiry(rxw)))
			{
				expiration = next_nak_rdata_expiry(rxw);
			}
		}
	
		g_static_mutex_unlock (&peer->mutex);

		list = next;
	} while (list);

out:
	return expiration;
}

/* check WAIT_NCF_STATE, on expiration move back to BACK-OFF_STATE, on exceeding NAK_NCF_RETRIES
 * cancel the sequence number.
 */
static void
nak_rpt_state (
	pgm_peer_t*		peer
	)
{
	pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;
	pgm_transport_t* transport = peer->transport;
	GList* list = rxw->wait_ncf_queue.tail;

	g_trace ("INFO", "nak_rpt_state(len=%u)", g_list_length(rxw->wait_ncf_queue.tail));

	guint dropped_invalid = 0;
	guint dropped = 0;

/* have not learned this peers NLA */
	const gboolean is_valid_nla = (((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr != INADDR_ANY);

	while (list)
	{
		GList* next_list_el = list->prev;
		struct pgm_sk_buff_t* skb	= (struct pgm_sk_buff_t*)list;
		pgm_rxw_state_t* state		= (pgm_rxw_state_t*)&skb->cb;

/* check this packet for state expiration */
		if (pgm_time_after_eq(pgm_time_now, state->nak_rpt_expiry))
		{
			if (!is_valid_nla) {
				dropped_invalid++;
				g_trace ("INFO", "lost data #%u due to no peer NLA.", skb->sequence);
				pgm_rxw_lost (rxw, skb->sequence);

/* mark receiver window for flushing on next recv() */
				if (!rxw->waiting_link.data)
				{
					rxw->waiting_link.data = rxw;
					rxw->waiting_link.next = transport->peers_waiting;
					transport->peers_waiting = &rxw->waiting_link;
				}

				list = next_list_el;
				continue;
			}

			if (++state->ncf_retry_count >= transport->nak_ncf_retries)
			{
/* cancellation */
				dropped++;
				g_trace ("INFO", "lost data #%u due to cancellation.", skb->sequence);

				const guint32 fail_time = pgm_time_now - skb->tstamp;
				if (!peer->max_fail_time) {
					peer->max_fail_time = peer->min_fail_time = fail_time;
				}
				else
				{
					if (fail_time > peer->max_fail_time)
						peer->max_fail_time = fail_time;
					else if (fail_time < peer->min_fail_time)
						peer->min_fail_time = fail_time;
				}

				pgm_rxw_lost (rxw, skb->sequence);

/* mark receiver window for flushing on next recv() */
				if (!rxw->waiting_link.data)
				{
					rxw->waiting_link.data = rxw;
					rxw->waiting_link.next = transport->peers_waiting;
					transport->peers_waiting = &rxw->waiting_link;
				}

				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_NCF_RETRIES_EXCEEDED]++;
			}
			else
			{
/* retry */
//				state->nak_rb_expiry = pkt->nak_rpt_expiry + nak_rb_ivl(transport);
				state->nak_rb_expiry = pgm_time_now + nak_rb_ivl(transport);

				pgm_rxw_state (rxw, skb, PGM_PKT_BACK_OFF_STATE);

				g_trace("INFO", "retry #%u attempt %u/%u.", skb->sequence, state->ncf_retry_count, transport->nak_ncf_retries);
			}
		}
		else
		{
/* packet expires some time later */
			g_trace("INFO", "#%u retry is delayed %f seconds.",
				skb->sequence, pgm_to_secsf(state->nak_rpt_expiry - pgm_time_now));
			break;
		}
		

		list = next_list_el;
	}

	if (rxw->wait_ncf_queue.length == 0)
	{
		g_assert ((pgm_rxw_state_t*)rxw->wait_ncf_queue.head == NULL);
		g_assert ((pgm_rxw_state_t*)rxw->wait_ncf_queue.tail == NULL);
	}
	else
	{
		g_assert ((pgm_rxw_state_t*)rxw->wait_ncf_queue.head != NULL);
		g_assert ((pgm_rxw_state_t*)rxw->wait_ncf_queue.tail != NULL);
	}

	if (dropped_invalid) {
		g_warning ("dropped %u messages due to invalid NLA.", dropped_invalid);
	}

	if (dropped) {
		g_trace ("INFO", "dropped %u messages due to ncf cancellation, "
				"rxw_sqns %" G_GUINT32_FORMAT
				" bo %" G_GUINT32_FORMAT
				" ncf %" G_GUINT32_FORMAT
				" wd %" G_GUINT32_FORMAT
				" lost %" G_GUINT32_FORMAT
				" frag %" G_GUINT32_FORMAT,
				dropped,
				pgm_rxw_length(rxw),
				rxw->backoff_queue.length,
				rxw->wait_ncf_queue.length,
				rxw->wait_data_queue.length,
				rxw->lost_count,
				rxw->fragment_count);
	}

/* mark receiver window for flushing on next recv() */
	if (rxw->cumulative_losses != rxw->ack_cumulative_losses &&
	    !rxw->waiting_link.data)
	{
		transport->has_lost_data = TRUE;
		rxw->pgm_sock_err.lost_count = rxw->cumulative_losses - rxw->ack_cumulative_losses;
		rxw->ack_cumulative_losses = rxw->cumulative_losses;

		rxw->waiting_link.data = rxw;
		rxw->waiting_link.next = transport->peers_waiting;
		transport->peers_waiting = &rxw->waiting_link;
	}

	if (rxw->wait_ncf_queue.tail)
	{
		if (next_nak_rpt_expiry(rxw) > pgm_time_now)
		{
			g_trace ("INFO", "next expiry set in %f seconds.", pgm_to_secsf(next_nak_rpt_expiry(rxw) - pgm_time_now));
		} else {
			g_trace ("INFO", "next expiry set in -%f seconds.", pgm_to_secsf(pgm_time_now - next_nak_rpt_expiry(rxw)));
		}
	}
	else
	{
		g_trace ("INFO", "wait ncf queue empty.");
	}
}

/* check WAIT_DATA_STATE, on expiration move back to BACK-OFF_STATE, on exceeding NAK_DATA_RETRIES
 * canel the sequence number.
 */
static void
nak_rdata_state (
	pgm_peer_t*		peer
	)
{
	pgm_rxw_t* rxw = (pgm_rxw_t*)peer->rxw;
	pgm_transport_t* transport = peer->transport;
	GList* list = rxw->wait_data_queue.tail;

	g_trace ("INFO", "nak_rdata_state(len=%u)", g_list_length(rxw->wait_data_queue.tail));

	guint dropped_invalid = 0;
	guint dropped = 0;

/* have not learned this peers NLA */
	const gboolean is_valid_nla = (((struct sockaddr_in*)&peer->nla)->sin_addr.s_addr != INADDR_ANY);

	while (list)
	{
		GList* next_list_el = list->prev;
		struct pgm_sk_buff_t* rdata_skb	= (struct pgm_sk_buff_t*)list;
		g_assert (rdata_skb);
		pgm_rxw_state_t* rdata_state	= (pgm_rxw_state_t*)&rdata_skb->cb;

/* check this packet for state expiration */
		if (pgm_time_after_eq(pgm_time_now, rdata_state->nak_rdata_expiry))
		{
			if (!is_valid_nla) {
				dropped_invalid++;
				g_trace ("INFO", "lost data #%u due to no peer NLA.", rdata_skb->sequence);
				pgm_rxw_lost (rxw, rdata_skb->sequence);

/* mark receiver window for flushing on next recv() */
				if (!rxw->waiting_link.data)
				{
					rxw->waiting_link.data = rxw;
					rxw->waiting_link.next = transport->peers_waiting;
					transport->peers_waiting = &rxw->waiting_link;
				}

				list = next_list_el;
				continue;
			}

			if (++rdata_state->data_retry_count >= transport->nak_data_retries)
			{
/* cancellation */
				dropped++;
				g_trace ("INFO", "lost data #%u due to cancellation.", rdata_skb->sequence);

				const guint32 fail_time = pgm_time_now - rdata_skb->tstamp;
				if (fail_time > peer->max_fail_time)		peer->max_fail_time = fail_time;
				else if (fail_time < peer->min_fail_time)	peer->min_fail_time = fail_time;

				pgm_rxw_lost (rxw, rdata_skb->sequence);

/* mark receiver window for flushing on next recv() */
				if (!rxw->waiting_link.data)
				{
					rxw->waiting_link.data = rxw;
					rxw->waiting_link.next = transport->peers_waiting;
					transport->peers_waiting = &rxw->waiting_link;
				}

				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_DATA_RETRIES_EXCEEDED]++;

				list = next_list_el;
				continue;
			}

//			rdata_state->nak_rb_expiry = rdata_pkt->nak_rdata_expiry + nak_rb_ivl(transport);
			rdata_state->nak_rb_expiry = pgm_time_now + nak_rb_ivl(transport);

			pgm_rxw_state (rxw, rdata_skb, PGM_PKT_BACK_OFF_STATE);

/* retry back to back-off state */
			g_trace("INFO", "retry #%u attempt %u/%u.", rdata_skb->sequence, rdata_state->data_retry_count, transport->nak_data_retries);
		}
		else
		{	/* packet expires some time later */
			break;
		}
		

		list = next_list_el;
	}

	if (rxw->wait_data_queue.length == 0)
	{
		g_assert ((pgm_rxw_state_t*)rxw->wait_data_queue.head == NULL);
		g_assert ((pgm_rxw_state_t*)rxw->wait_data_queue.tail == NULL);
	}
	else
	{
		g_assert ((pgm_rxw_state_t*)rxw->wait_data_queue.head);
		g_assert ((pgm_rxw_state_t*)rxw->wait_data_queue.tail);
	}

	if (dropped_invalid) {
		g_warning ("dropped %u messages due to invalid NLA.", dropped_invalid);
	}

	if (dropped) {
		g_trace ("INFO", "dropped %u messages due to data cancellation.", dropped);
	}

/* mark receiver window for flushing on next recv() */
	if (rxw->cumulative_losses != rxw->ack_cumulative_losses &&
	    !rxw->waiting_link.data)
	{
		transport->has_lost_data = TRUE;
		rxw->pgm_sock_err.lost_count = rxw->cumulative_losses - rxw->ack_cumulative_losses;
		rxw->ack_cumulative_losses = rxw->cumulative_losses;

		rxw->waiting_link.data = rxw;
		rxw->waiting_link.next = transport->peers_waiting;
		transport->peers_waiting = &rxw->waiting_link;
	}

	if (rxw->wait_data_queue.tail)
		g_trace ("INFO", "next expiry set in %f seconds.", pgm_to_secsf(next_nak_rdata_expiry(rxw) - pgm_time_now));
	else
		g_trace ("INFO", "wait data queue empty.");
}

/* ODATA or RDATA packet with any of the following options:
 *
 * OPT_FRAGMENT - this TPDU part of a larger APDU.
 *
 * Ownership of skb is taken and must be passed to the receive window or destroyed.
 *
 * returns TRUE is skb has been replaced, FALSE is remains unchanged and can be recycled.
 */

static gboolean
on_data (
	pgm_peer_t*		sender,
	struct pgm_sk_buff_t*	skb
	)
{
	g_assert (sender);
	g_assert (skb);
	g_trace ("INFO","on_data");

	int retval = 0;
	guint msg_count = 0;
	const pgm_time_t nak_rb_expiry = skb->tstamp + nak_rb_ivl(sender->transport);
	const guint16 tsdu_length = g_ntohs (skb->pgm_header->pgm_tsdu_length);

	skb->pgm_data = skb->data;

	const guint16 opt_total_length = (skb->pgm_header->pgm_options & PGM_OPT_PRESENT) ? g_ntohs(*(guint16*)( (char*)( skb->pgm_data + 1 ) + sizeof(guint16))) : 0;

/* advance data pointer to payload */
	pgm_skb_pull (skb, sizeof(struct pgm_data) + opt_total_length);

	g_static_mutex_lock (&sender->mutex);
	if (opt_total_length > 0)
		 _pgm_get_opt_fragment ((gpointer)(skb->pgm_data + 1), &skb->pgm_opt_fragment);

	retval = pgm_rxw_add (sender->rxw, skb, nak_rb_expiry);

/* reference is now invalid */
	skb = NULL;

	g_static_mutex_unlock (&sender->mutex);

	gboolean flush_naks = FALSE;

	switch (retval) {
	case PGM_RXW_MISSING:
		flush_naks = TRUE;
/* fall through */
	case PGM_RXW_INSERTED:
	case PGM_RXW_APPENDED:
		msg_count++;
		break;

	case PGM_RXW_DUPLICATE:
		sender->cumulative_stats[PGM_PC_RECEIVER_DUP_DATAS]++;
		goto discarded;

	case PGM_RXW_MALFORMED:
		sender->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_ODATA]++;
/* fall through */
	case PGM_RXW_BOUNDS:
discarded:
		sender->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		return FALSE;

	default: g_assert_not_reached(); break;
	}

/* valid data */
	sender->cumulative_stats[PGM_PC_RECEIVER_DATA_BYTES_RECEIVED] += tsdu_length;
	sender->cumulative_stats[PGM_PC_RECEIVER_DATA_MSGS_RECEIVED]  += msg_count;

	if (flush_naks)
	{
/* flush out 1st time nak packets */
		g_static_mutex_lock (&sender->transport->mutex);

		if (pgm_time_after (sender->transport->next_poll, nak_rb_expiry))
		{
			sender->transport->next_poll = nak_rb_expiry;
			g_trace ("INFO","on_odata: prod timer thread");
			if (!pgm_notify_send (&sender->transport->timer_notify)) {
				g_critical ("send to timer notify channel failed :(");
				retval = -EINVAL;
			}
		}

		g_static_mutex_unlock (&sender->transport->mutex);
	}

	return TRUE;
}

/* eof */
