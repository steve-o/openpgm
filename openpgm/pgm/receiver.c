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
#include "pgm/ip.h"
#include "pgm/packet.h"
#include "pgm/math.h"
#include "pgm/net.h"
#include "pgm/txwi.h"
#include "pgm/rxwi.h"
#include "pgm/rate_control.h"
#include "pgm/sn.h"
#include "pgm/time.h"
#include "pgm/timer.h"
#include "pgm/checksum.h"
#include "pgm/reed_solomon.h"
#include "pgm/histogram.h"


//#define RECEIVER_DEBUG
//#define SPM_DEBUG

#ifndef RECEIVER_DEBUG
#	define G_DISABLE_ASSERT
#	define g_trace(m,...)		while (0)
#else
#include <ctype.h>
#	ifdef SPM_DEBUG
#		define g_trace(m,...)		g_debug(__VA_ARGS__)
#	else
#		define g_trace(m,...)		do { if (strcmp((m),"SPM")) { g_debug(__VA_ARGS__); } } while (0)
#	endif
#endif
#ifndef g_assert_cmpuint
#	define g_assert_cmpuint(n1, cmp, n2)	do { (void) 0; } while (0)
#endif

#ifndef ENOBUFS
#	define ENOBUFS		WSAENOBUFS
#endif
#ifndef ECONNRESET
#	define ECONNRESET	WSAECONNRESET
#endif
#ifndef ENOBUFS
#	define ENOBUFS		WSAENOBUFS
#endif


static gboolean send_spmr (pgm_transport_t* const, pgm_peer_t* const);
static gboolean send_nak (pgm_transport_t* const, pgm_peer_t* const, const guint32);
static gboolean send_parity_nak (pgm_transport_t* const, pgm_peer_t* const, const guint, const guint);
static gboolean send_nak_list (pgm_transport_t* const, pgm_peer_t* const, const pgm_sqn_list_t* const);
static gboolean nak_rb_state (pgm_peer_t*, const pgm_time_t);
static void nak_rpt_state (pgm_peer_t*, const pgm_time_t);
static void nak_rdata_state (pgm_peer_t*, const pgm_time_t);
static inline pgm_peer_t* _pgm_peer_ref (pgm_peer_t*);


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
	pgm_transport_t* 	transport
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != transport->rand_);
	g_assert_cmpuint (transport->nak_bo_ivl, >, 1);

	return g_rand_int_range (transport->rand_, 1 /* us */, transport->nak_bo_ivl);
}

/* mark sequence as recovery failed.
 */

static
void
cancel_skb (
	pgm_transport_t*	transport,
	pgm_peer_t*		peer,
	struct pgm_sk_buff_t*	skb,
	pgm_time_t		now
	)
{
	g_trace ("INFO", "lost data #%u due to cancellation.", skb->sequence);

	const guint32 fail_time = now - skb->tstamp;
	if (!peer->max_fail_time)
		peer->max_fail_time = peer->min_fail_time = fail_time;
	else if (fail_time > peer->max_fail_time)
		peer->max_fail_time = fail_time;
	else if (fail_time < peer->min_fail_time)
		peer->min_fail_time = fail_time;

	pgm_rxw_lost (peer->window, skb->sequence);
	PGM_HISTOGRAM_TIMES("Rx.FailTime", fail_time);

/* mark receiver window for flushing on next recv() */
	pgm_peer_set_pending (transport, peer);
}

/* increase reference count for peer object
 *
 * on success, returns peer object.
 */

static inline
pgm_peer_t*
_pgm_peer_ref (
	pgm_peer_t*	peer
	)
{
/* pre-conditions */
	g_assert (NULL != peer);

	g_atomic_int_inc (&peer->ref_count);
	return peer;
}

/* decrease reference count of peer object, destroying on last reference.
 */

void
pgm_peer_unref (
	pgm_peer_t*		peer
	)
{
/* pre-conditions */
	g_assert (NULL != peer);

	const gboolean is_zero = g_atomic_int_dec_and_test (&peer->ref_count);
	if (G_UNLIKELY (is_zero))
	{
/* receive window */
		pgm_rxw_destroy (peer->window);
		peer->window = NULL;

/* object */
		g_free (peer);
	}
}

/* TODO: this should be in on_io_data to be more streamlined, or a generic options parser.
 *
 * returns TRUE if opt_fragment is found, otherwise FALSE is returned.
 */

static
gboolean
get_opt_fragment (
	struct pgm_opt_header*		opt_header,
	struct pgm_opt_fragment**	opt_fragment
	)
{
/* pre-conditions */
	g_assert (NULL != opt_header);
	g_assert (NULL != opt_fragment);
	g_assert (opt_header->opt_type   == PGM_OPT_LENGTH);
	g_assert (opt_header->opt_length == sizeof(struct pgm_opt_length));

	g_trace ("INFO","get_opt_fragment (opt-header:%p opt-fragment:%p)",
		(gpointer)opt_header, (gpointer)opt_fragment);

/* always at least two options, first is always opt_length */
	do {
		opt_header = (struct pgm_opt_header*)((char*)opt_header + opt_header->opt_length);

		if ((opt_header->opt_type & PGM_OPT_MASK) == PGM_OPT_FRAGMENT)
		{
			*opt_fragment = (struct pgm_opt_fragment*)(opt_header + 1);
			return TRUE;
		}

	} while (!(opt_header->opt_type & PGM_OPT_END));

	*opt_fragment = NULL;
	return FALSE;
}

/* set interval timer & expiration timeout for peer expiration, very lax checking.
 * the ambient SPM interval MUST be set before calling this function.
 *
 * 0 < 2 * spm_ambient_interval <= peer_expiry
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_peer_expiry (
	pgm_transport_t* const	transport,
	const guint		peer_expiry	/* in microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (peer_expiry > 0, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed ||
	    (peer_expiry < (2 * transport->spm_ambient_interval)))
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->peer_expiry = peer_expiry;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* set maximum back off range for listening for multicast SPMR
 * the ambient SPM interval MUST be set before calling this function.
 *
 * 0 < spmr_expiry < spm_ambient_interval
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_spmr_expiry (
	pgm_transport_t* const	transport,
	const guint		spmr_expiry	/* in microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (spmr_expiry > 0, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed ||
	    ( transport->can_send_data &&
	      spmr_expiry >= transport->spm_ambient_interval ))
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->spmr_expiry = spmr_expiry;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < rxw_sqns < one less than half sequence space
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_rxw_sqns (
	pgm_transport_t* const	transport,
	const guint		sqns
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (sqns < ((UINT32_MAX/2)-1), FALSE);
	g_return_val_if_fail (sqns > 0, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->rxw_sqns = sqns;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < secs < ( rxw_sqns / rxw_max_rte )
 *
 * can only be enforced upon bind.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_rxw_secs (
	pgm_transport_t* const	transport,
	const guint		secs
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (secs > 0, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->rxw_secs = secs;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* 0 < rxw_max_rte < interface capacity
 *
 *  10mb :   1250000
 * 100mb :  12500000
 *   1gb : 125000000
 *
 * no practical way to determine upper limit and enforce.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_rxw_max_rte (
	pgm_transport_t* const	transport,
	const guint		max_rte
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (max_rte > 0, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->rxw_max_rte = max_rte;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* Actual NAK back-off, NAK_RB_IVL, is random time interval 1 < NAK_BO_IVL,
 * randomized to reduce storms.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_nak_bo_ivl (
	pgm_transport_t* const	transport,
	const guint		usec		/* microseconds */
	)
{
	g_return_val_if_fail (transport != NULL, FALSE);
	g_return_val_if_fail (usec > 1, FALSE);
	if (!g_static_rw_lock_reader_trylock (&transport->lock))
		g_return_val_if_reached (FALSE);
	if (transport->is_bound ||
	    transport->is_destroyed)
	{
		g_static_rw_lock_reader_unlock (&transport->lock);
		g_return_val_if_reached (FALSE);
	}
	transport->nak_bo_ivl = usec;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* Set NAK_RPT_IVL, the repeat interval before re-sending a NAK.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_nak_rpt_ivl (
	pgm_transport_t* const	transport,
	const guint		usec		/* microseconds */
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
	transport->nak_rpt_ivl = usec;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* Set NAK_RDATA_IVL, the interval waiting for data.
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_nak_rdata_ivl (
	pgm_transport_t* const	transport,
	const guint		usec		/* microseconds */
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
	transport->nak_rdata_ivl = usec;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* statistics are limited to guint8, i.e. 255 retries
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_nak_data_retries (
	pgm_transport_t* const	transport,
	const guint		cnt
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
	transport->nak_data_retries = cnt;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* statistics are limited to guint8, i.e. 255 retries
 *
 * on success, returns TRUE.  on invalid setting, returns FALSE.
 */

gboolean
pgm_transport_set_nak_ncf_retries (
	pgm_transport_t* const	transport,
	const guint		cnt
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
	transport->nak_ncf_retries = cnt;
	g_static_rw_lock_reader_unlock (&transport->lock);
	return TRUE;
}

/* a peer in the context of the transport is another party on the network sending PGM
 * packets.  for each peer we need a receive window and network layer address (nla) to
 * which nak requests can be forwarded to.
 *
 * on success, returns new peer object.
 */

pgm_peer_t*
pgm_new_peer (
	pgm_transport_t* const		transport,
	const pgm_tsi_t* const		tsi,
	const struct sockaddr* const	src_addr,
	const gsize			src_addrlen,
	const struct sockaddr* const	dst_addr,
	const gsize			dst_addrlen
	)
{
	pgm_peer_t* peer;

/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != src_addr);
	g_assert (src_addrlen > 0);
	g_assert (NULL != dst_addr);
	g_assert (dst_addrlen > 0);

#ifdef RECEIVER_DEBUG
	char saddr[INET6_ADDRSTRLEN], daddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (src_addr, saddr, sizeof(saddr));
	pgm_sockaddr_ntop (dst_addr, daddr, sizeof(daddr));
	g_trace ("INFO","pgm_new_peer (transport:%p tsi:%s src-addr:%s src-addrlen:%" G_GSIZE_FORMAT " dst-addr:%s dst-addrlen:%" G_GSIZE_FORMAT ")",
		(gpointer)transport, pgm_tsi_print (tsi), saddr, src_addrlen, daddr, dst_addrlen);
#endif

	peer = g_malloc0 (sizeof(pgm_peer_t));
	peer->expiry = peer->last_packet + transport->peer_expiry;
	peer->transport = transport;
	memcpy (&peer->tsi, tsi, sizeof(pgm_tsi_t));
	memcpy (&peer->group_nla, dst_addr, dst_addrlen);
	memcpy (&peer->local_nla, src_addr, src_addrlen);
/* port at same location for sin/sin6 */
	((struct sockaddr_in*)&peer->local_nla)->sin_port = g_htons (transport->udp_encap_ucast_port);
	((struct sockaddr_in*)&peer->nla)->sin_port = g_htons (transport->udp_encap_ucast_port);

/* lock on rx window */
	peer->window = pgm_rxw_create (&peer->tsi,
					transport->max_tpdu,
					transport->rxw_sqns,
					transport->rxw_secs,
					transport->rxw_max_rte);
	peer->spmr_expiry = peer->last_packet + transport->spmr_expiry;

/* add peer to hash table and linked list */
	g_static_rw_lock_writer_lock (&transport->peers_lock);
	gpointer entry = _pgm_peer_ref(peer);
	g_hash_table_insert (transport->peers_hashtable, &peer->tsi, entry);
/* there is no g_list_prepend_link(): */
	peer->peers_link.next = transport->peers_list;
	peer->peers_link.data = peer;
/* update next entries previous link */
	if (transport->peers_list)
		transport->peers_list->prev = &peer->peers_link;
/* update head */
	transport->peers_list = &peer->peers_link;
	g_static_rw_lock_writer_unlock (&transport->peers_lock);

	pgm_timer_lock (transport);
	if (pgm_time_after( transport->next_poll, peer->spmr_expiry ))
		transport->next_poll = peer->spmr_expiry;
	pgm_timer_unlock (transport);
	return peer;
}

/* copy any contiguous buffers in the peer list to the provided 
 * message vector.
 * returns -ENOBUFS if the vector is full, returns -ECONNRESET if
 * data loss is detected, returns 0 when all peers flushed.
 */

int
pgm_flush_peers_pending (
	pgm_transport_t* const		transport,
	pgm_msgv_t**			pmsg,
	const pgm_msgv_t* const		msg_end,
	gsize* const			bytes_read,	/* added to, not set */
	guint* const			data_read
	)
{
	int retval = 0;

/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != pmsg);
	g_assert (NULL != *pmsg);
	g_assert (NULL != msg_end);
	g_assert (NULL != bytes_read);
	g_assert (NULL != data_read);

	g_trace ("INFO","pgm_flush_peers_pending (transport:%p pmsg:%p msg-end:%p bytes-read:%p data-read:%p)",
		(gpointer)transport, (gpointer)pmsg, (gconstpointer)msg_end, (gpointer)bytes_read, (gpointer)data_read);

	while (transport->peers_pending)
	{
		pgm_peer_t* peer = transport->peers_pending->data;
		const gssize peer_bytes = pgm_rxw_readv (peer->window, pmsg, msg_end - *pmsg);

		if (peer->last_cumulative_losses != ((pgm_rxw_t*)peer->window)->cumulative_losses)
		{
			transport->is_reset = TRUE;
			peer->lost_count = ((pgm_rxw_t*)peer->window)->cumulative_losses - peer->last_cumulative_losses;
			peer->last_cumulative_losses = ((pgm_rxw_t*)peer->window)->cumulative_losses;
		}
	
		if (peer_bytes >= 0)
		{
			(*bytes_read) += peer_bytes;
			(*data_read)  ++;
			if (*pmsg == msg_end) {			/* commit full */
				retval = -ENOBUFS;
				break;
			}
		}
		if (transport->is_reset) {
			retval = -ECONNRESET;
			break;
		}
/* clear this reference and move to next */
		transport->peers_pending->data = NULL;
		GSList* next_peer = transport->peers_pending->next;
		transport->peers_pending->next = NULL;
		transport->peers_pending = next_peer;
	}

	return retval;
}

/* edge trigerred has receiver pending events
 */

gboolean
pgm_peer_has_pending (
	pgm_peer_t* const		peer
	)
{
/* pre-conditions */
	g_assert (NULL != peer);

	if (NULL == peer->pending_link.data && ((pgm_rxw_t*)peer->window)->has_event) {
		((pgm_rxw_t*)peer->window)->has_event = 0;
		return TRUE;
	}
	return FALSE;
}

/* set receiver in pending event queue
 */

void
pgm_peer_set_pending (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		peer
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != peer);

	if (peer->pending_link.data) return;
	peer->pending_link.data = peer;
	peer->pending_link.next = transport->peers_pending;
	transport->peers_pending = &peer->pending_link;
}

/* Create a new error SKB detailing data loss.
 */

void
pgm_set_reset_error (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	pgm_msgv_t* const		msgv
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != source);
	g_assert (NULL != msgv);

	struct pgm_sk_buff_t* error_skb = pgm_alloc_skb (0);
	error_skb->transport	= transport;
	error_skb->tstamp	= pgm_time_update_now ();
	memcpy (&error_skb->tsi, &source->tsi, sizeof(pgm_tsi_t));
	error_skb->sequence	= source->lost_count;
	msgv->msgv_skb[0]	= error_skb;
	msgv->msgv_len		= 1;
}

/* SPM indicate start of a session, continued presence of a session, or flushing final packets
 * of a session.
 *
 * returns TRUE on valid packet, FALSE on invalid packet or duplicate SPM sequence number.
 */

gboolean
pgm_on_spm (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != source);
	g_assert (NULL != skb);

	g_trace("INFO","pgm_on_spm (transport:%p source:%p skb:%p)",
		(gpointer)transport, (gpointer)source, (gpointer)skb);

	if (!pgm_verify_spm (skb)) {
		g_trace("INFO","Discarded invalid SPM.");
		source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
		return FALSE;
	}

	struct pgm_spm*  spm  = (struct pgm_spm*) skb->data;
	struct pgm_spm6* spm6 = (struct pgm_spm6*)skb->data;
	const guint32 spm_sqn = g_ntohl (spm->spm_sqn);

/* check for advancing sequence number, or first SPM */
	if ( pgm_uint32_gte (spm->spm_sqn, source->spm_sqn) ||
	     ((struct sockaddr*)&source->nla)->sa_family == 0 )
	{
/* copy NLA for replies */
		pgm_nla_to_sockaddr (&spm->spm_nla_afi, (struct sockaddr*)&source->nla);

/* save sequence number */
		source->spm_sqn = spm_sqn;

/* update receive window */
		const pgm_time_t nak_rb_expiry = skb->tstamp + nak_rb_ivl (transport);
		const guint naks = pgm_rxw_update (source->window,
						   g_ntohl (spm->spm_lead),
						   g_ntohl (spm->spm_trail),
						   skb->tstamp,
						   nak_rb_expiry);
		if (naks) {
			pgm_timer_lock (transport);
			if (pgm_time_after (transport->next_poll, nak_rb_expiry))
				transport->next_poll = nak_rb_expiry;
			pgm_timer_unlock (transport);
		}

/* mark receiver window for flushing on next recv() */
		pgm_rxw_t* window = (pgm_rxw_t*)source->window;
		if (window->cumulative_losses != source->last_cumulative_losses &&
		    !source->pending_link.data)
		{
			transport->is_reset = TRUE;
			source->lost_count = window->cumulative_losses - source->last_cumulative_losses;
			source->last_cumulative_losses = window->cumulative_losses;
			pgm_peer_set_pending (transport, source);
		}
	}
	else
	{	/* does not advance SPM sequence number */
		g_trace ("INFO","Discarded duplicate SPM.");
		source->cumulative_stats[PGM_PC_RECEIVER_DUP_SPMS]++;
		return FALSE;
	}

/* check whether peer can generate parity packets */
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		struct pgm_opt_length* opt_len = (spm->spm_nla_afi == AFI_IP6) ?
							(struct pgm_opt_length*)(spm6 + 1) :
							(struct pgm_opt_length*)(spm  + 1);
		if (opt_len->opt_type != PGM_OPT_LENGTH)
		{
			g_trace ("INFO","Discarded malformed SPM.");
			source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
			return FALSE;
		}
		if (opt_len->opt_length != sizeof(struct pgm_opt_length))
		{
			g_trace ("INFO","Discarded malformed SPM.");
			source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
			return FALSE;
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
					g_trace ("INFO","Discarded malformed SPM.");
					source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
					return FALSE;
				}

				const guint32 parity_prm_tgs = g_ntohl (opt_parity_prm->parity_prm_tgs);
				if (parity_prm_tgs < 2 || parity_prm_tgs > 128)
				{
					g_trace ("INFO","Discarded malformed SPM.");
					source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_SPMS]++;
					return FALSE;
				}
			
				source->has_proactive_parity = opt_parity_prm->opt_reserved & PGM_PARITY_PRM_PRO;
				source->has_ondemand_parity  = opt_parity_prm->opt_reserved & PGM_PARITY_PRM_OND;
				if (source->has_proactive_parity || source->has_ondemand_parity) {
					source->is_fec_enabled = 1;
					pgm_rxw_update_fec (source->window, parity_prm_tgs);
				}
			}
		} while (!(opt_header->opt_type & PGM_OPT_END));
	}

/* either way bump expiration timer */
	source->expiry = skb->tstamp + transport->peer_expiry;
	source->spmr_expiry = 0;
	if (source->spmr_tstamp > 0) {
		PGM_HISTOGRAM_TIMES("Rx.SpmRequestResponseTime", skb->tstamp - source->spmr_tstamp);
		source->spmr_tstamp = 0;
	}
	return TRUE;
}

/* Multicast peer-to-peer NAK handling, pretty much the same as a NCF but different direction
 *
 * if NAK is valid, returns TRUE.  on error, FALSE is returned.
 */

gboolean
pgm_on_peer_nak (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		peer,
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != peer);
	g_assert (NULL != skb);

	g_trace ("INFO","pgm_on_peer_nak (transport:%p peer:%p skb:%p)",
		(gpointer)transport, (gpointer)peer, (gpointer)skb);

	if (!pgm_verify_nak (skb))
	{
		g_trace ("INFO","Discarded invalid multicast NAK.");
		peer->cumulative_stats[PGM_PC_RECEIVER_NAK_ERRORS]++;
		return FALSE;
	}

	const struct pgm_nak*  nak  = (struct pgm_nak*) skb->data;
	const struct pgm_nak6* nak6 = (struct pgm_nak6*)skb->data;
		
/* NAK_SRC_NLA must not contain our transport unicast NLA */
	struct sockaddr_storage nak_src_nla;
	pgm_nla_to_sockaddr (&nak->nak_src_nla_afi, (struct sockaddr*)&nak_src_nla);
	if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_src_nla, (struct sockaddr*)&transport->send_addr) == 0)
	{
		g_trace ("INFO","Discarded multicast NAK on NLA mismatch.");
		return FALSE;
	}

/* NAK_GRP_NLA contains one of our transport receive multicast groups: the sources send multicast group */ 
	struct sockaddr_storage nak_grp_nla;
	pgm_nla_to_sockaddr ((nak->nak_src_nla_afi == AFI_IP6) ? &nak6->nak6_grp_nla_afi : &nak->nak_grp_nla_afi, (struct sockaddr*)&nak_grp_nla);
	gboolean found = FALSE;
	for (unsigned i = 0; i < transport->recv_gsr_len; i++)
	{
		if (pgm_sockaddr_cmp ((struct sockaddr*)&nak_grp_nla, (struct sockaddr*)&transport->recv_gsr[i].gsr_group) == 0)
		{
			found = TRUE;
		}
	}

	if (!found) {
		g_trace ("INFO","Discarded multicast NAK on multicast group mismatch.");
		return FALSE;
	}

/* handle as NCF */
	int status = pgm_rxw_confirm (peer->window,
				      g_ntohl (nak->nak_sqn),
				      skb->tstamp,
				      skb->tstamp + transport->nak_rdata_ivl,
				      skb->tstamp + nak_rb_ivl(transport));
	if (PGM_RXW_UPDATED == status || PGM_RXW_APPENDED == status)
		peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED]++;

/* check NAK list */
	const guint32* nak_list = NULL;
	guint nak_list_len = 0;
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (nak->nak_src_nla_afi == AFI_IP6) ?
							(const struct pgm_opt_length*)(nak6 + 1) :
							(const struct pgm_opt_length*)(nak + 1);
		if (opt_len->opt_type != PGM_OPT_LENGTH)
		{
			g_trace ("INFO","Discarded malformed multicast NAK.");
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			return FALSE;
		}
		if (opt_len->opt_length != sizeof(struct pgm_opt_length))
		{
			g_trace ("INFO","Discarded malformed multicast NAK.");
			peer->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			return FALSE;
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

	while (nak_list_len) {
		status = pgm_rxw_confirm (peer->window,
					  g_ntohl (*nak_list),
					  skb->tstamp,
					  skb->tstamp + transport->nak_rdata_ivl,
					  skb->tstamp + nak_rb_ivl(transport));
		if (PGM_RXW_UPDATED == status || PGM_RXW_APPENDED == status)
			peer->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED]++;
		nak_list++;
		nak_list_len--;
	}

/* mark receiver window for flushing on next recv() */
	pgm_rxw_t* window = (pgm_rxw_t*)peer->window;
	if (window->cumulative_losses != peer->last_cumulative_losses &&
	    !peer->pending_link.data)
	{
		transport->is_reset = TRUE;
		peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
		peer->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (transport, peer);
	}
	return TRUE;
}

/* NCF confirming receipt of a NAK from this transport or another on the LAN segment.
 *
 * Packet contents will match exactly the sent NAK, although not really that helpful.
 *
 * if NCF is valid, returns TRUE.  on error, FALSE is returned.
 */

gboolean
pgm_on_ncf (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != source);
	g_assert (NULL != skb);

	g_trace ("INFO","pgm_on_ncf (transport:%p source:%p skb:%p)",
		(gpointer)transport, (gpointer)source, (gpointer)skb);

	if (!pgm_verify_ncf (skb))
	{
		g_trace ("INFO", "Discarded invalid NCF.");
		source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
		return FALSE;
	}

	const struct pgm_nak*  ncf  = (struct pgm_nak*) skb->data;
	const struct pgm_nak6* ncf6 = (struct pgm_nak6*)skb->data;
		
/* NCF_SRC_NLA may contain our transport unicast NLA, we don't really care */
	struct sockaddr_storage ncf_src_nla;
	pgm_nla_to_sockaddr (&ncf->nak_src_nla_afi, (struct sockaddr*)&ncf_src_nla);

#if 0
	if (pgm_sockaddr_cmp ((struct sockaddr*)&ncf_src_nla, (struct sockaddr*)&transport->send_addr) != 0) {
		g_trace ("INFO", "Discarded NCF on NLA mismatch.");
		peer->cumulative_stats[PGM_PC_RECEIVER_PACKETS_DISCARDED]++;
		return FALSE;
	}
#endif

/* NCF_GRP_NLA contains our transport multicast group */ 
	struct sockaddr_storage ncf_grp_nla;
	pgm_nla_to_sockaddr ((ncf->nak_src_nla_afi == AFI_IP6) ? &ncf6->nak6_grp_nla_afi : &ncf->nak_grp_nla_afi, (struct sockaddr*)&ncf_grp_nla);
	if (pgm_sockaddr_cmp ((struct sockaddr*)&ncf_grp_nla, (struct sockaddr*)&transport->send_gsr.gsr_group) != 0)
	{
		g_trace ("INFO", "Discarded NCF on multicast group mismatch.");
		return FALSE;
	}

	const pgm_time_t ncf_rdata_ivl = skb->tstamp + transport->nak_rdata_ivl;
	const pgm_time_t ncf_rb_ivl    = skb->tstamp + nak_rb_ivl(transport);
	int status = pgm_rxw_confirm (source->window,
				      g_ntohl (ncf->nak_sqn),
				      skb->tstamp,
				      ncf_rdata_ivl,
				      ncf_rb_ivl);
	if (PGM_RXW_UPDATED == status || PGM_RXW_APPENDED == status)
	{
		pgm_time_t ncf_ivl = (PGM_RXW_APPENDED == status) ? ncf_rb_ivl : ncf_rdata_ivl;
		pgm_timer_lock (transport);
		if (pgm_time_after (transport->next_poll, ncf_ivl)) {
			transport->next_poll = ncf_ivl;
		}
		pgm_timer_unlock (transport);
		source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED]++;
	}

/* check NCF list */
	const guint32* ncf_list = NULL;
	guint ncf_list_len = 0;
	if (skb->pgm_header->pgm_options & PGM_OPT_PRESENT)
	{
		const struct pgm_opt_length* opt_len = (ncf->nak_src_nla_afi == AFI_IP6) ?
							(const struct pgm_opt_length*)(ncf6 + 1) :
							(const struct pgm_opt_length*)(ncf  + 1);
		if (opt_len->opt_type != PGM_OPT_LENGTH)
		{
			g_trace ("INFO","Discarded malformed NCF.");
			source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			return FALSE;
		}
		if (opt_len->opt_length != sizeof(struct pgm_opt_length))
		{
			g_trace ("INFO","Discarded malformed NCF.");
			source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_NCFS]++;
			return FALSE;
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
		status = pgm_rxw_confirm (source->window,
					  g_ntohl (*ncf_list),
					  skb->tstamp,
					  ncf_rdata_ivl,
					  ncf_rb_ivl);
		if (PGM_RXW_UPDATED == status || PGM_RXW_APPENDED == status)
			source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SUPPRESSED]++;
		ncf_list++;
		ncf_list_len--;
	}

/* mark receiver window for flushing on next recv() */
	pgm_rxw_t* window = (pgm_rxw_t*)source->window;
	if (window->cumulative_losses != source->last_cumulative_losses &&
	    !source->pending_link.data)
	{
		transport->is_reset = TRUE;
		source->lost_count = window->cumulative_losses - source->last_cumulative_losses;
		source->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (transport, source);
	}
	return TRUE;
}

/* send SPM-request to a new peer, this packet type has no contents
 *
 * on success, TRUE is returned, if operation would block FALSE is
 * returned.
 */

static
gboolean
send_spmr (
	pgm_transport_t* const	transport,
	pgm_peer_t* const	source
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != source);

	g_trace ("INFO","send_spmr (transport:%p source:%p)",
		(gpointer)transport, (gpointer)source);

	const gsize tpdu_length = sizeof(struct pgm_header);
	guint8 buf[ tpdu_length ];
	struct pgm_header *header = (struct pgm_header*)buf;
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));
/* dport & sport reversed communicating upstream */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type	= PGM_SPMR;
	header->pgm_options	= 0;
	header->pgm_tsdu_length	= 0;
	header->pgm_checksum	= 0;
	header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

/* send multicast SPMR TTL 1 */
	pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_group), 1);
	gssize sent = pgm_sendto (transport,
				  FALSE,			/* not rate limited */
				  FALSE,			/* regular socket */
				  header,
				  tpdu_length,
				  (struct sockaddr*)&transport->send_gsr.gsr_group,
				  pgm_sockaddr_len(&transport->send_gsr.gsr_group));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno))
		return FALSE;

/* send unicast SPMR with regular TTL */
	pgm_sockaddr_multicast_hops (transport->send_sock, pgm_sockaddr_family(&transport->send_gsr.gsr_group), transport->hops);
	sent = pgm_sendto (transport,
			   FALSE,
			   FALSE,
			   header,
			   tpdu_length,
			   (struct sockaddr*)&source->local_nla,
			   pgm_sockaddr_len(&source->local_nla));
	if (sent < 0 && EAGAIN == errno)
		return FALSE;

	source->spmr_expiry = 0;
	transport->cumulative_stats[PGM_PC_SOURCE_BYTES_SENT] += tpdu_length * 2;
	return TRUE;
}

/* send selective NAK for one sequence number.
 *
 * on success, TRUE is returned, returns FALSE if would block on operation.
 */
static
gboolean
send_nak (
	pgm_transport_t* const	transport,
	pgm_peer_t* const	source,
	const guint32		sequence
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != source);

	g_trace ("INFO", "send_nak (transport:%p peer:%p sequence:%" G_GUINT32_FORMAT ")",
		(gpointer)transport, (gpointer)source, sequence);

	gsize tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	if (AF_INET6 == pgm_sockaddr_family(&source->nla))
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	guint8 buf[ tpdu_length ];
	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
	struct pgm_nak6 *nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = 0;
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= g_htonl (sequence);

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->nla, (char*)&nak->nak_src_nla_afi);

/* group nla: we match the NAK NLA to the same as advertised by the source, we might
 * be listening to multiple multicast groups
 */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->group_nla, (nak->nak_src_nla_afi == AFI_IP6) ?
								(char*)&nak6->nak6_grp_nla_afi :
								(char*)&nak->nak_grp_nla_afi );
        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	const gssize sent = pgm_sendto (transport,
					FALSE,		/* not rate limited */
					TRUE,			/* with router alert */
					header,
					tpdu_length,
					(struct sockaddr*)&source->nla,
					pgm_sockaddr_len(&source->nla));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno))
		return FALSE;

	source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT]++;
	source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT]++;
	return TRUE;
}

/* Send a parity NAK requesting on-demand parity packet generation.
 *
 * on success, TRUE is returned, returns FALSE if operation would block.
 */
static
gboolean
send_parity_nak (
	pgm_transport_t* const	transport,
	pgm_peer_t* const	source,
	const guint32		nak_tg_sqn,	/* transmission group (shifted) */
	const guint32		nak_pkt_cnt	/* count of parity packets to request */
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != source);
	g_assert (nak_pkt_cnt > 0);

	g_trace ("INFO", "send_parity_nak (transport:%p source:%p nak-tg-sqn:%" G_GUINT32_FORMAT " nak-pkt-cnt:%" G_GUINT32_FORMAT ")",
		(gpointer)transport, (gpointer)source, nak_tg_sqn, nak_pkt_cnt);

	gsize tpdu_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	if (AF_INET6 == pgm_sockaddr_family(&source->nla))
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	guint8 buf[ tpdu_length ];
	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
	struct pgm_nak6 *nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = PGM_OPT_PARITY;	/* this is a parity packet */
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= g_htonl (nak_tg_sqn | (nak_pkt_cnt - 1) );

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->nla, (char*)&nak->nak_src_nla_afi);

/* group nla: we match the NAK NLA to the same as advertised by the source, we might
 * be listening to multiple multicast groups
 */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->group_nla, (nak->nak_src_nla_afi == AFI_IP6) ?
									(char*)&nak6->nak6_grp_nla_afi :
									(char*)&nak->nak_grp_nla_afi );
        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	const gssize sent = pgm_sendto (transport,
					FALSE,		/* not rate limited */
					TRUE,		/* with router alert */
					header,
					tpdu_length,
					(struct sockaddr*)&source->nla,
					pgm_sockaddr_len(&source->nla));
	if (sent < 0 && (EAGAIN == errno || ENOBUFS == errno))
		return FALSE;

	source->cumulative_stats[PGM_PC_RECEIVER_PARITY_NAK_PACKETS_SENT]++;
	source->cumulative_stats[PGM_PC_RECEIVER_PARITY_NAKS_SENT]++;
	return TRUE;
}

/* A NAK packet with a OPT_NAK_LIST option extension
 *
 * on success, 0 is returned.  on error, -1 is returned, and errno set
 * appropriately.
 */

static
gboolean
send_nak_list (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	const pgm_sqn_list_t* const	sqn_list
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != source);
	g_assert (NULL != sqn_list);
	g_assert_cmpuint (sqn_list->len, >, 1);
	g_assert_cmpuint (sqn_list->len, <=, 63);

#ifdef RECEIVER_DEBUG
	char list[1024];
	sprintf (list, "%" G_GUINT32_FORMAT, sqn_list->sqn[0]);
	for (unsigned i = 1; i < sqn_list->len; i++) {
		char sequence[2 + strlen("4294967295")];
		sprintf (sequence, " %" G_GUINT32_FORMAT, sqn_list->sqn[i]);
		strcat (list, sequence);
	}
	g_trace("INFO","send_nak_list (transport:%p source:%p sqn-list:[%s])",
		(gpointer)transport, (gpointer)source, list);
#endif

	gsize tpdu_length = sizeof(struct pgm_header) +
			    sizeof(struct pgm_nak) +
			    sizeof(struct pgm_opt_length) +		/* includes header */
			    sizeof(struct pgm_opt_header) +
			    sizeof(struct pgm_opt_nak_list) +
			    ( (sqn_list->len-1) * sizeof(guint32) );
	if (AFI_IP6 == pgm_sockaddr_family(&source->nla))
		tpdu_length += sizeof(struct pgm_nak6) - sizeof(struct pgm_nak);
	guint8 buf[ tpdu_length ];
	if (G_UNLIKELY(g_mem_gc_friendly))
		memset (buf, 0, tpdu_length);
	struct pgm_header *header = (struct pgm_header*)buf;
	struct pgm_nak *nak = (struct pgm_nak*)(header + 1);
	struct pgm_nak6 *nak6 = (struct pgm_nak6*)(header + 1);
	memcpy (header->pgm_gsi, &source->tsi.gsi, sizeof(pgm_gsi_t));

/* dport & sport swap over for a nak */
	header->pgm_sport	= transport->dport;
	header->pgm_dport	= source->tsi.sport;
	header->pgm_type        = PGM_NAK;
        header->pgm_options     = PGM_OPT_PRESENT | PGM_OPT_NETWORK;
        header->pgm_tsdu_length = 0;

/* NAK */
	nak->nak_sqn		= g_htonl (sqn_list->sqn[0]);

/* source nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->nla, (char*)&nak->nak_src_nla_afi);

/* group nla */
	pgm_sockaddr_to_nla ((struct sockaddr*)&source->group_nla, (nak->nak_src_nla_afi == AFI_IP6) ? 
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

	for (unsigned i = 1; i < sqn_list->len; i++)
		opt_nak_list->opt_sqn[i-1] = g_htonl (sqn_list->sqn[i]);

        header->pgm_checksum    = 0;
        header->pgm_checksum	= pgm_csum_fold (pgm_csum_partial ((char*)header, tpdu_length, 0));

	const gssize sent = pgm_sendto (transport,
					FALSE,			/* not rate limited */
					FALSE,			/* regular socket */
					header,
					tpdu_length,
					(struct sockaddr*)&source->nla,
					pgm_sockaddr_len(&source->nla));
	if ( sent != (gssize)tpdu_length )
		return FALSE;

	source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAK_PACKETS_SENT]++;
	source->cumulative_stats[PGM_PC_RECEIVER_SELECTIVE_NAKS_SENT] += 1 + sqn_list->len;
	return TRUE;
}

/* check all receiver windows for packets in BACK-OFF_STATE, on expiration send a NAK.
 * update transport::next_nak_rb_timestamp for next expiration time.
 *
 * peer object is locked before entry.
 *
 * returns TRUE on success, returns FALSE if operation would block.
 */

static
gboolean
nak_rb_state (
	pgm_peer_t*		peer,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	g_assert (NULL != peer);

	g_trace ("INFO", "nak_rb_state (peer:%p now:%" PGM_TIME_FORMAT ")",
		(gpointer)peer, now);

	pgm_rxw_t* window = (pgm_rxw_t*)peer->window;
	pgm_transport_t* transport = peer->transport;
	GList* list;
	pgm_sqn_list_t nak_list = { .len = 0 };

/* send all NAKs first, lack of data is blocking contiguous processing and its 
 * better to get the notification out a.s.a.p. even though it might be waiting
 * in a kernel queue.
 *
 * alternative: after each packet check for incoming data and return to the
 * event loop.  bias for shorter loops as retry count increases.
 */
	list = window->backoff_queue.tail;
	if (!list) {
		g_assert (window->backoff_queue.head == NULL);
		g_warning ("backoff queue is empty in nak_rb_state.");
		return TRUE;
	} else {
		g_assert (window->backoff_queue.head != NULL);
	}

	guint dropped_invalid = 0;

/* have not learned this peers NLA */
	const gboolean is_valid_nla = 0 != pgm_sockaddr_family(&peer->nla);

/* TODO: process BOTH selective and parity NAKs? */

/* calculate current transmission group for parity enabled peers */
	if (peer->has_ondemand_parity)
	{
		const guint32 tg_sqn_mask = 0xffffffff << window->tg_sqn_shift;

/* NAKs only generated previous to current transmission group */
		const guint32 current_tg_sqn = window->lead & tg_sqn_mask;

		guint32 nak_tg_sqn = 0;
		guint32 nak_pkt_cnt = 0;

/* parity NAK generation */

		while (list)
		{
			GList* next_list_el = list->prev;
			struct pgm_sk_buff_t* skb	= (struct pgm_sk_buff_t*)list;
			pgm_rxw_state_t* state		= (pgm_rxw_state_t*)&skb->cb;

/* check this packet for state expiration */
			if (pgm_time_after_eq(now, state->nak_rb_expiry))
			{
				if (!is_valid_nla) {
					dropped_invalid++;
					g_trace ("INFO", "lost data #%u due to no peer NLA.", skb->sequence);
					pgm_rxw_lost (window, skb->sequence);
/* mark receiver window for flushing on next recv() */
					pgm_peer_set_pending (transport, peer);
					list = next_list_el;
					continue;
				}

/* TODO: parity nak lists */
				const guint32 tg_sqn = skb->sequence & tg_sqn_mask;
				if (	( nak_pkt_cnt && tg_sqn == nak_tg_sqn ) ||
					( !nak_pkt_cnt && tg_sqn != current_tg_sqn )	)
				{
					pgm_rxw_state (window, skb, PGM_PKT_WAIT_NCF_STATE);

					if (!nak_pkt_cnt++)
						nak_tg_sqn = tg_sqn;
					state->nak_transmit_count++;

#ifdef PGM_ABSOLUTE_EXPIRY
					state->nak_rpt_expiry = state->nak_rb_expiry + transport->nak_rpt_ivl;
					while (pgm_time_after_eq(now, state->nak_rpt_expiry)) {
						state->nak_rpt_expiry += transport->nak_rpt_ivl;
						state->ncf_retry_count++;
					}
#else
					state->nak_rpt_expiry = now + transport->nak_rpt_ivl;
#endif
					pgm_timer_lock (transport);
					if (pgm_time_after (transport->next_poll, state->nak_rpt_expiry))
						transport->next_poll = state->nak_rpt_expiry;
					pgm_timer_unlock (transport);
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

		if (nak_pkt_cnt && !send_parity_nak (transport, peer, nak_tg_sqn, nak_pkt_cnt))
			return FALSE;
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
			if (pgm_time_after_eq(now, state->nak_rb_expiry))
			{
				if (!is_valid_nla) {
					dropped_invalid++;
					g_trace ("INFO", "lost data #%u due to no peer NLA.", skb->sequence);
					pgm_rxw_lost (window, skb->sequence);
/* mark receiver window for flushing on next recv() */
					pgm_peer_set_pending (transport, peer);
					list = next_list_el;
					continue;
				}

				pgm_rxw_state (window, skb, PGM_PKT_WAIT_NCF_STATE);
				nak_list.sqn[nak_list.len++] = skb->sequence;
				state->nak_transmit_count++;

/* we have two options here, calculate the expiry time in the new state relative to the current
 * state execution time, skipping missed expirations due to delay in state processing, or base
 * from the actual current time.
 */
#ifdef PGM_ABSOLUTE_EXPIRY
				state->nak_rpt_expiry = state->nak_rb_expiry + transport->nak_rpt_ivl;
				while (pgm_time_after_eq(now, state->nak_rpt_expiry)){
					state->nak_rpt_expiry += transport->nak_rpt_ivl;
					state->ncf_retry_count++;
				}
#else
				state->nak_rpt_expiry = now + transport->nak_rpt_ivl;
g_trace("INFO", "rp->nak_rpt_expiry in %f seconds.",
		pgm_to_secsf( state->nak_rpt_expiry - now ) );
#endif
				pgm_timer_lock (transport);
				if (pgm_time_after (transport->next_poll, state->nak_rpt_expiry))
					transport->next_poll = state->nak_rpt_expiry;
				pgm_timer_unlock (transport);

				if (nak_list.len == G_N_ELEMENTS(nak_list.sqn)) {
					if (transport->can_send_nak && !send_nak_list (transport, peer, &nak_list))
						return FALSE;
					nak_list.len = 0;
				}
			}
			else
			{	/* packet expires some time later */
				break;
			}

			list = next_list_el;
		}

		if (transport->can_send_nak && nak_list.len)
		{
			if (nak_list.len > 1 && !send_nak_list (transport, peer, &nak_list))
				return FALSE;
			else if (!send_nak (transport, peer, nak_list.sqn[0]))
				return FALSE;
		}

	}

	if (dropped_invalid)
	{
		g_warning ("dropped %u messages due to invalid NLA.", dropped_invalid);

/* mark receiver window for flushing on next recv() */
		if (window->cumulative_losses != peer->last_cumulative_losses &&
		    !peer->pending_link.data)
		{
			transport->is_reset = TRUE;
			peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
			peer->last_cumulative_losses = window->cumulative_losses;
			pgm_peer_set_pending (transport, peer);
		}
	}

	if (window->backoff_queue.length == 0)
	{
		g_assert ((struct rxw_packet*)window->backoff_queue.head == NULL);
		g_assert ((struct rxw_packet*)window->backoff_queue.tail == NULL);
	}
	else
	{
		g_assert ((struct rxw_packet*)window->backoff_queue.head != NULL);
		g_assert ((struct rxw_packet*)window->backoff_queue.tail != NULL);
	}

	if (window->backoff_queue.tail)
	{
		g_trace ("INFO", "next expiry set in %f seconds.",
			pgm_to_secsf(next_nak_rb_expiry(window) - now));
	}
	else
	{
		g_trace ("INFO", "backoff queue empty.");
	}
	return TRUE;
}

/* check this peer for NAK state timers, uses the tail of each queue for the nearest
 * timer execution.
 *
 * returns TRUE on complete sweep, returns FALSE if operation would block.
 */

gboolean
pgm_check_peer_nak_state (
	pgm_transport_t*	transport,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	g_assert (NULL != transport);

	g_trace ("INFO","pgm_check_peer_nak_state (transport:%p now:%" PGM_TIME_FORMAT ")",
		(gpointer)transport, now);

	if (!transport->peers_list)
		return TRUE;

	GList* list = transport->peers_list;
	do {
		GList* next = list->next;
		pgm_peer_t* peer = list->data;
		pgm_rxw_t* window = (pgm_rxw_t*)peer->window;

		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (now, peer->spmr_expiry))
			{
				if (transport->can_send_nak) {
					if (!send_spmr (transport, peer)) {
						return FALSE;
					}
					peer->spmr_tstamp = now;
				} else
					peer->spmr_expiry = 0;
			}
		}

		if (window->backoff_queue.tail)
		{
			if (pgm_time_after_eq (now, next_nak_rb_expiry(window)))
				if (!nak_rb_state (peer, now)) {
					return FALSE;
				}
		}
		
		if (window->wait_ncf_queue.tail)
		{
			if (pgm_time_after_eq (now, next_nak_rpt_expiry(window)))
				nak_rpt_state (peer, now);
		}

		if (window->wait_data_queue.tail)
		{
			if (pgm_time_after_eq (now, next_nak_rdata_expiry(window)))
				nak_rdata_state (peer, now);
		}

/* expired, remove from hash table and linked list */
		if (pgm_time_after_eq (now, peer->expiry))
		{
			if (window->committed_count)
			{
				g_trace ("INFO", "peer expiration postponed due to committed data, tsi %s", pgm_tsi_print (&peer->tsi));
				peer->expiry += transport->peer_expiry;
			}
			else
			{
				g_warning ("peer expired, tsi %s", pgm_tsi_print (&peer->tsi));
				g_hash_table_remove (transport->peers_hashtable, &peer->tsi);
				transport->peers_list = g_list_remove_link (transport->peers_list, &peer->peers_link);
				pgm_peer_unref (peer);
			}
		}

		list = next;
	} while (list);

/* check for waiting contiguous packets */
	if (transport->peers_pending && !transport->is_pending_read)
	{
		g_trace ("INFO","prod rx thread");
		pgm_notify_send (&transport->pending_notify);
		transport->is_pending_read = TRUE;
	}
	return TRUE;
}

/* find the next state expiration time among the transports peers.
 *
 * on success, returns the earliest of the expiration parameter or next
 * peer expiration time.
 */

pgm_time_t
pgm_min_nak_expiry (
	pgm_time_t		expiration,		/* absolute time */
	pgm_transport_t*	transport
	)
{
/* pre-conditions */
	g_assert (NULL != transport);

	g_trace ("INFO","pgm_min_nak_expiry (expiration:%" PGM_TIME_FORMAT " transport:%p)",
		expiration, (gpointer)transport);

	if (!transport->peers_list)
		return expiration;

	GList* list = transport->peers_list;
	do {
		GList* next = list->next;
		pgm_peer_t* peer = (pgm_peer_t*)list->data;
		pgm_rxw_t* window = (pgm_rxw_t*)peer->window;
	
		if (peer->spmr_expiry)
		{
			if (pgm_time_after_eq (expiration, peer->spmr_expiry))
				expiration = peer->spmr_expiry;
		}

		if (window->backoff_queue.tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rb_expiry(window)))
				expiration = next_nak_rb_expiry(window);
		}

		if (window->wait_ncf_queue.tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rpt_expiry(window)))
				expiration = next_nak_rpt_expiry(window);
		}

		if (window->wait_data_queue.tail)
		{
			if (pgm_time_after_eq (expiration, next_nak_rdata_expiry(window)))
				expiration = next_nak_rdata_expiry(window);
		}
	
		list = next;
	} while (list);

	return expiration;
}

/* check WAIT_NCF_STATE, on expiration move back to BACK-OFF_STATE, on exceeding NAK_NCF_RETRIES
 * cancel the sequence number.
 */
static
void
nak_rpt_state (
	pgm_peer_t*		peer,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	g_assert (NULL != peer);

	g_trace ("INFO","nak_rpt_state (peer:%p now:%" PGM_TIME_FORMAT ")",
		(gpointer)peer, now);

	pgm_rxw_t* window = (pgm_rxw_t*)peer->window;
	pgm_transport_t* transport = peer->transport;
	GList* list = window->wait_ncf_queue.tail;

	guint dropped_invalid = 0;
	guint dropped = 0;

/* have not learned this peers NLA */
	const gboolean is_valid_nla = 0 != pgm_sockaddr_family(&peer->nla);

	while (list)
	{
		GList* next_list_el = list->prev;
		struct pgm_sk_buff_t* skb	= (struct pgm_sk_buff_t*)list;
		pgm_rxw_state_t* state		= (pgm_rxw_state_t*)&skb->cb;

/* check this packet for state expiration */
		if (pgm_time_after_eq(now, state->nak_rpt_expiry))
		{
			if (!is_valid_nla) {
				dropped_invalid++;
				g_trace ("INFO", "lost data #%u due to no peer NLA.", skb->sequence);
				pgm_rxw_lost (window, skb->sequence);
/* mark receiver window for flushing on next recv() */
				pgm_peer_set_pending (transport, peer);
				list = next_list_el;
				continue;
			}

			if (++state->ncf_retry_count >= transport->nak_ncf_retries)
			{
				dropped++;
				cancel_skb (transport, peer, skb, now);
				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_NCF_RETRIES_EXCEEDED]++;
			}
			else
			{
/* retry */
//				state->nak_rb_expiry = pkt->nak_rpt_expiry + nak_rb_ivl(transport);
				state->nak_rb_expiry = now + nak_rb_ivl(transport);
				pgm_rxw_state (window, skb, PGM_PKT_BACK_OFF_STATE);
				g_trace("INFO", "retry #%u attempt %u/%u.", skb->sequence, state->ncf_retry_count, transport->nak_ncf_retries);
			}
		}
		else
		{
/* packet expires some time later */
			g_trace("INFO", "#%u retry is delayed %f seconds.",
				skb->sequence, pgm_to_secsf(state->nak_rpt_expiry - now));
			break;
		}
		
		list = next_list_el;
	}

	if (window->wait_ncf_queue.length == 0)
	{
		g_assert ((pgm_rxw_state_t*)window->wait_ncf_queue.head == NULL);
		g_assert ((pgm_rxw_state_t*)window->wait_ncf_queue.tail == NULL);
	}
	else
	{
		g_assert ((pgm_rxw_state_t*)window->wait_ncf_queue.head != NULL);
		g_assert ((pgm_rxw_state_t*)window->wait_ncf_queue.tail != NULL);
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
				pgm_rxw_length(window),
				window->backoff_queue.length,
				window->wait_ncf_queue.length,
				window->wait_data_queue.length,
				window->lost_count,
				window->fragment_count);
	}

/* mark receiver window for flushing on next recv() */
	if (window->cumulative_losses != peer->last_cumulative_losses &&
	    !peer->pending_link.data)
	{
		transport->is_reset = TRUE;
		peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
		peer->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (transport, peer);
	}

	if (window->wait_ncf_queue.tail)
	{
		if (next_nak_rpt_expiry(window) > now)
		{
			g_trace ("INFO", "next expiry set in %f seconds.", pgm_to_secsf(next_nak_rpt_expiry(window) - now));
		} else {
			g_trace ("INFO", "next expiry set in -%f seconds.", pgm_to_secsf(now - next_nak_rpt_expiry(window)));
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
static
void
nak_rdata_state (
	pgm_peer_t*		peer,
	const pgm_time_t	now
	)
{
/* pre-conditions */
	g_assert (NULL != peer);

	g_trace ("INFO","nak_rdata_state (peer:%p now:%" PGM_TIME_FORMAT ")",
		(gpointer)peer, now);

	pgm_rxw_t* window = (pgm_rxw_t*)peer->window;
	pgm_transport_t* transport = peer->transport;
	GList* list = window->wait_data_queue.tail;

	guint dropped_invalid = 0;
	guint dropped = 0;

/* have not learned this peers NLA */
	const gboolean is_valid_nla = 0 != pgm_sockaddr_family(&peer->nla);

	while (list)
	{
		GList* next_list_el = list->prev;
		struct pgm_sk_buff_t* rdata_skb	= (struct pgm_sk_buff_t*)list;
		g_assert (rdata_skb);
		pgm_rxw_state_t* rdata_state	= (pgm_rxw_state_t*)&rdata_skb->cb;

/* check this packet for state expiration */
		if (pgm_time_after_eq(now, rdata_state->nak_rdata_expiry))
		{
			if (!is_valid_nla) {
				dropped_invalid++;
				g_trace ("INFO", "lost data #%u due to no peer NLA.", rdata_skb->sequence);
				pgm_rxw_lost (window, rdata_skb->sequence);
/* mark receiver window for flushing on next recv() */
				pgm_peer_set_pending (transport, peer);
				list = next_list_el;
				continue;
			}

			if (++rdata_state->data_retry_count >= transport->nak_data_retries)
			{
				dropped++;
				cancel_skb (transport, peer, rdata_skb, now);
				peer->cumulative_stats[PGM_PC_RECEIVER_NAKS_FAILED_DATA_RETRIES_EXCEEDED]++;
				list = next_list_el;
				continue;
			}

//			rdata_state->nak_rb_expiry = rdata_pkt->nak_rdata_expiry + nak_rb_ivl(transport);
			rdata_state->nak_rb_expiry = now + nak_rb_ivl(transport);
			pgm_rxw_state (window, rdata_skb, PGM_PKT_BACK_OFF_STATE);

/* retry back to back-off state */
			g_trace("INFO", "retry #%u attempt %u/%u.", rdata_skb->sequence, rdata_state->data_retry_count, transport->nak_data_retries);
		}
		else
		{	/* packet expires some time later */
			break;
		}
		

		list = next_list_el;
	}

	if (window->wait_data_queue.length == 0)
	{
		g_assert ((pgm_rxw_state_t*)window->wait_data_queue.head == NULL);
		g_assert ((pgm_rxw_state_t*)window->wait_data_queue.tail == NULL);
	}
	else
	{
		g_assert ((pgm_rxw_state_t*)window->wait_data_queue.head);
		g_assert ((pgm_rxw_state_t*)window->wait_data_queue.tail);
	}

	if (dropped_invalid) {
		g_warning ("dropped %u messages due to invalid NLA.", dropped_invalid);
	}

	if (dropped) {
		g_trace ("INFO", "dropped %u messages due to data cancellation.", dropped);
	}

/* mark receiver window for flushing on next recv() */
	if (window->cumulative_losses != peer->last_cumulative_losses &&
	    !peer->pending_link.data)
	{
		transport->is_reset = TRUE;
		peer->lost_count = window->cumulative_losses - peer->last_cumulative_losses;
		peer->last_cumulative_losses = window->cumulative_losses;
		pgm_peer_set_pending (transport, peer);
	}

	if (window->wait_data_queue.tail)
		g_trace ("INFO", "next expiry set in %f seconds.", pgm_to_secsf(next_nak_rdata_expiry(window) - now));
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

gboolean
pgm_on_data (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		source,
	struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (NULL != source);
	g_assert (NULL != skb);

	g_trace ("INFO","pgm_on_data (transport:%p source:%p skb:%p)",
		(gpointer)transport, (gpointer)source, (gpointer)skb);

	guint msg_count = 0;
	const pgm_time_t nak_rb_expiry = skb->tstamp + nak_rb_ivl (transport);
	const guint16 tsdu_length = g_ntohs (skb->pgm_header->pgm_tsdu_length);

	skb->pgm_data = skb->data;

	const guint16 opt_total_length = (skb->pgm_header->pgm_options & PGM_OPT_PRESENT) ? g_ntohs(*(guint16*)( (char*)( skb->pgm_data + 1 ) + sizeof(guint16))) : 0;

/* advance data pointer to payload */
	pgm_skb_pull (skb, sizeof(struct pgm_data) + opt_total_length);

	if (opt_total_length > 0)
		 get_opt_fragment ((gpointer)(skb->pgm_data + 1), &skb->pgm_opt_fragment);

	const int add_status = pgm_rxw_add (source->window, skb, skb->tstamp, nak_rb_expiry);

/* skb reference is now invalid */
	gboolean flush_naks = FALSE;

	switch (add_status) {
	case PGM_RXW_MISSING:
		flush_naks = TRUE;
/* fall through */
	case PGM_RXW_INSERTED:
	case PGM_RXW_APPENDED:
		msg_count++;
		break;

	case PGM_RXW_DUPLICATE:
		source->cumulative_stats[PGM_PC_RECEIVER_DUP_DATAS]++;
		goto discarded;

	case PGM_RXW_MALFORMED:
		source->cumulative_stats[PGM_PC_RECEIVER_MALFORMED_ODATA]++;
/* fall through */
	case PGM_RXW_BOUNDS:
discarded:
		return FALSE;

	default: g_assert_not_reached(); break;
	}

/* valid data */
	PGM_HISTOGRAM_COUNTS("Rx.DataBytesReceived", tsdu_length);
	source->cumulative_stats[PGM_PC_RECEIVER_DATA_BYTES_RECEIVED] += tsdu_length;
	source->cumulative_stats[PGM_PC_RECEIVER_DATA_MSGS_RECEIVED]  += msg_count;

	if (flush_naks) {
/* flush out 1st time nak packets */
		pgm_timer_lock (transport);
		if (pgm_time_after (transport->next_poll, nak_rb_expiry))
			transport->next_poll = nak_rb_expiry;
		pgm_timer_unlock (transport);
	}
	return TRUE;
}

/* eof */
