/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for transport recv api
 *
 * Copyright (c) 2009 Miru Limited.
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

#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#ifndef _WIN32
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>		/* _GNU_SOURCE for in6_pktinfo */
#	include <arpa/inet.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
#include <glib.h>
#include <check.h>


/* mock state */

#define TEST_NETWORK		""
#define TEST_DPORT		7500
#define TEST_SPORT		1000
#define TEST_XPORT		1001
#define TEST_SRC_ADDR		"127.0.0.1"
#define TEST_END_ADDR		"127.0.0.2"
#define TEST_GROUP_ADDR		"239.192.0.1"
#define TEST_PEER_ADDR		"127.0.0.6"
#define TEST_DLR_ADDR		"127.0.0.9"
#define TEST_MAX_TPDU		1500
#define TEST_TXW_SQNS		32
#define TEST_RXW_SQNS		32
#define TEST_HOPS		16
#define TEST_SPM_AMBIENT	( pgm_secs(30) )
#define TEST_SPM_HEARTBEAT_INIT	{ pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(1300), pgm_secs(7), pgm_secs(16), pgm_secs(25), pgm_secs(30) }
#define TEST_PEER_EXPIRY	( pgm_secs(300) )
#define TEST_SPMR_EXPIRY	( pgm_msecs(250) )
#define TEST_NAK_BO_IVL		( pgm_msecs(50) )
#define TEST_NAK_RPT_IVL	( pgm_secs(2) )
#define TEST_NAK_RDATA_IVL	( pgm_secs(2) )
#define TEST_NAK_DATA_RETRIES	5
#define TEST_NAK_NCF_RETRIES	2

struct mock_recvmsg_t {
#ifndef _WIN32
	struct msghdr*		mr_msg;
#else
	WSAMSG*			mr_msg;
#endif
	ssize_t			mr_retval;
	int			mr_errno;
};

struct pgm_peer_t;

GList* mock_recvmsg_list = NULL;
static int mock_pgm_type = -1;
static gboolean mock_reset_on_spmr = FALSE;
static gboolean mock_data_on_spmr = FALSE;
static struct pgm_peer_t* mock_peer = NULL;
GList* mock_data_list = NULL;
unsigned mock_pgm_loss_rate = 0;


#ifndef _WIN32
static ssize_t mock_recvmsg (int, struct msghdr*, int);
#else
static int mock_recvfrom (SOCKET, char*, int, int, struct sockaddr*, int*);
#endif

#define pgm_parse_raw			mock_pgm_parse_raw
#define pgm_parse_udp_encap		mock_pgm_parse_udp_encap
#define pgm_verify_spm			mock_pgm_verify_spm
#define pgm_verify_nak			mock_pgm_verify_nak
#define pgm_verify_ncf			mock_pgm_verify_ncf
#define pgm_select_info			mock_pgm_select_info
#define pgm_poll_info			mock_pgm_poll_info
#define pgm_set_reset_error		mock_pgm_set_reset_error
#define pgm_flush_peers_pending		mock_pgm_flush_peers_pending
#define pgm_peer_has_pending		mock_pgm_peer_has_pending
#define pgm_peer_set_pending		mock_pgm_peer_set_pending
#define pgm_txw_retransmit_is_empty	mock_pgm_txw_retransmit_is_empty
#define pgm_rxw_create			mock_pgm_rxw_create
#define pgm_rxw_readv			mock_pgm_rxw_readv
#define pgm_new_peer			mock_pgm_new_peer
#define pgm_on_data			mock_pgm_on_data
#define pgm_on_spm			mock_pgm_on_spm
#define pgm_on_ack			mock_pgm_on_ack
#define pgm_on_nak			mock_pgm_on_nak
#define pgm_on_deferred_nak		mock_pgm_on_deferred_nak
#define pgm_on_peer_nak			mock_pgm_on_peer_nak
#define pgm_on_nnak			mock_pgm_on_nnak
#define pgm_on_ncf			mock_pgm_on_ncf
#define pgm_on_spmr			mock_pgm_on_spmr
#define pgm_sendto			mock_pgm_sendto
#define pgm_timer_prepare		mock_pgm_timer_prepare
#define pgm_timer_check			mock_pgm_timer_check
#define pgm_timer_expiration		mock_pgm_timer_expiration
#define pgm_timer_dispatch		mock_pgm_timer_dispatch
#define pgm_time_now			mock_pgm_time_now
#define pgm_time_update_now		mock_pgm_time_update_now
#define recvmsg				mock_recvmsg
#define recvfrom			mock_recvfrom
#define pgm_WSARecvMsg			mock_pgm_WSARecvMsg
#define pgm_loss_rate			mock_pgm_loss_rate

#define RECV_DEBUG
#include "recv.c"


pgm_rxw_t* mock_pgm_rxw_create (const pgm_tsi_t*, const uint16_t, const unsigned, const unsigned, const ssize_t, const uint32_t);
static pgm_time_t _mock_pgm_time_update_now (void);
pgm_time_update_func mock_pgm_time_update_now = _mock_pgm_time_update_now;


static
void
mock_setup (void)
{
	if (!g_thread_supported ()) g_thread_init (NULL);
}

/* cleanup test state */
static
void
mock_teardown (void)
{
	mock_recvmsg_list = NULL;
	mock_pgm_type = -1;
	mock_reset_on_spmr = FALSE;
	mock_data_on_spmr = FALSE;
	mock_peer = NULL;
	mock_data_list = NULL;
	mock_pgm_loss_rate = 0;
}

static
struct pgm_sock_t*
generate_sock (void)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, g_htons((guint16)TEST_SPORT) };
	struct pgm_sock_t* sock = g_new0 (struct pgm_sock_t, 1);
	sock->window = g_new0 (pgm_txw_t, 1);
	memcpy (&sock->tsi, &tsi, sizeof(pgm_tsi_t));
	sock->is_nonblocking = TRUE;
	sock->is_bound = TRUE;
	sock->is_destroyed = FALSE;
	sock->is_reset = FALSE;
	sock->rx_buffer = pgm_alloc_skb (TEST_MAX_TPDU);
	sock->max_tpdu = TEST_MAX_TPDU;
	sock->rxw_sqns = TEST_RXW_SQNS;
	sock->dport = g_htons((guint16)TEST_DPORT);
	sock->can_send_data = TRUE;
	sock->can_send_nak = TRUE;
	sock->can_recv_data = TRUE;
	sock->peers_hashtable = pgm_hashtable_new (pgm_tsi_hash, pgm_tsi_equal);
	pgm_rand_create (&sock->rand_);
	sock->nak_bo_ivl = 100*1000;
	pgm_notify_init (&sock->pending_notify);
	pgm_notify_init (&sock->rdata_notify);
	pgm_mutex_init (&sock->receiver_mutex);
	pgm_rwlock_init (&sock->lock);
	pgm_rwlock_init (&sock->peers_lock);
	return sock;
}

static
struct pgm_sk_buff_t*
generate_packet (void)
{
	struct pgm_sk_buff_t* skb;

	skb = pgm_alloc_skb (TEST_MAX_TPDU);
	skb->data		= skb->head;
	skb->len		= sizeof(struct pgm_ip) + sizeof(struct pgm_header);
	skb->tail		= (guint8*)skb->data + skb->len;

/* add IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_hl		= sizeof(struct pgm_ip) / 4;
	iphdr->ip_v		= 4;
	iphdr->ip_tos		= 0;
	iphdr->ip_id		= 0;
	iphdr->ip_off		= 0;
	iphdr->ip_ttl		= 16;
	iphdr->ip_p		= IPPROTO_PGM;
	iphdr->ip_sum		= 0;
	iphdr->ip_src.s_addr	= inet_addr (TEST_SRC_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (TEST_GROUP_ADDR);

/* add PGM header */
	struct pgm_header* pgmhdr = (gpointer)(iphdr + 1);
	pgmhdr->pgm_sport	= g_htons ((guint16)TEST_XPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)TEST_DPORT);
	pgmhdr->pgm_options	= 0;
	pgmhdr->pgm_gsi[0]	= 1;
	pgmhdr->pgm_gsi[1]	= 2;
	pgmhdr->pgm_gsi[2]	= 3;
	pgmhdr->pgm_gsi[3]	= 4;
	pgmhdr->pgm_gsi[4]	= 5;
	pgmhdr->pgm_gsi[5]	= 6;
	pgmhdr->pgm_tsdu_length = 0;
	pgmhdr->pgm_checksum 	= 0;

	skb->pgm_header = pgmhdr;
	return skb;
}

static
void
generate_odata (
	const char*		source,
	const guint		source_len,
	const guint32		data_sqn,
	const guint32		data_trail,
	gpointer*		packet,
	gsize*			len
	)
{
	struct pgm_sk_buff_t* skb = generate_packet ();
	pgm_skb_put (skb, sizeof(struct pgm_data) + source_len);

/* add ODATA header */
	struct pgm_data* datahdr = (gpointer)(skb->pgm_header + 1);
	datahdr->data_sqn	= g_htonl (data_sqn);
	datahdr->data_trail	= g_htonl (data_trail);

/* add payload */
	gpointer data = (gpointer)(datahdr + 1);
	memcpy (data, source, source_len);

/* finalize PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_type	= PGM_ODATA;
	pgmhdr->pgm_tsdu_length = g_htons (source_len);

/* finalize IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_len		= g_htons (skb->len);

	*packet = skb->head;
	*len    = skb->len;
}

static
void
generate_spm (
	const guint32		spm_sqn,
	const guint32		spm_trail,
	const guint32		spm_lead,
	gpointer*		packet,
	gsize*			len
	)
{
	struct pgm_sk_buff_t* skb = generate_packet ();
	pgm_skb_put (skb, sizeof(struct pgm_spm));

/* add SPM header */
	struct pgm_spm* spm = (gpointer)(skb->pgm_header + 1);
	spm->spm_sqn	= g_htonl (spm_sqn);
	spm->spm_trail	= g_htonl (spm_trail);
	spm->spm_lead	= g_htonl (spm_lead);

/* finalize PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_type	= PGM_SPM;

/* finalize IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_len		= g_htons (skb->len);

	*packet = skb->head;
	*len    = skb->len;
}

static
void
generate_nak (
	const guint32		nak_sqn,
	gpointer*		packet,
	gsize*			len
	)
{
	struct pgm_sk_buff_t* skb = generate_packet ();
	pgm_skb_put (skb, sizeof(struct pgm_spm));

/* update IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_src.s_addr	= inet_addr (TEST_END_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (TEST_SRC_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)TEST_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)TEST_SPORT);

/* add NAK header */
	struct pgm_nak* nak = (gpointer)(skb->pgm_header + 1);
	nak->nak_sqn	= g_htonl (nak_sqn);

/* finalize PGM header */
	pgmhdr->pgm_type	= PGM_NAK;

/* finalize IP header */
	iphdr->ip_len		= g_htons (skb->len);

	*packet = skb->head;
	*len    = skb->len;
}

static
void
generate_peer_nak (
	const guint32		nak_sqn,
	gpointer*		packet,
	gsize*			len
	)
{
	struct pgm_sk_buff_t* skb = generate_packet ();
	pgm_skb_put (skb, sizeof(struct pgm_spm));

/* update IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_src.s_addr	= inet_addr (TEST_PEER_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (TEST_GROUP_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)TEST_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)TEST_SPORT);

/* add NAK header */
	struct pgm_nak* nak = (gpointer)(skb->pgm_header + 1);
	nak->nak_sqn	= g_htonl (nak_sqn);

/* finalize PGM header */
	pgmhdr->pgm_type	= PGM_NAK;

/* finalize IP header */
	iphdr->ip_len		= g_htons (skb->len);

	*packet = skb->head;
	*len    = skb->len;
}

static
void
generate_nnak (
	const guint32		nak_sqn,
	gpointer*		packet,
	gsize*			len
	)
{
	struct pgm_sk_buff_t* skb = generate_packet ();
	pgm_skb_put (skb, sizeof(struct pgm_nak));

/* update IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_src.s_addr	= inet_addr (TEST_DLR_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (TEST_SRC_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)TEST_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)TEST_SPORT);

/* add NNAK header */
	struct pgm_nak* nak = (gpointer)(skb->pgm_header + 1);
	nak->nak_sqn	= g_htonl (nak_sqn);

/* finalize PGM header */
	pgmhdr->pgm_type	= PGM_NNAK;

/* finalize IP header */
	iphdr->ip_len		= g_htons (skb->len);

	*packet = skb->head;
	*len    = skb->len;
}

static
void
generate_ncf (
	const guint32		nak_sqn,
	gpointer*		packet,
	gsize*			len
	)
{
	struct pgm_sk_buff_t* skb = generate_packet ();
	pgm_skb_put (skb, sizeof(struct pgm_nak));

/* add NAK header */
	struct pgm_nak* nak = (gpointer)(skb->pgm_header + 1);
	nak->nak_sqn	= g_htonl (nak_sqn);

/* finalize PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_type	= PGM_NCF;

/* finalize IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_len		= g_htons (skb->len);

	*packet = skb->head;
	*len    = skb->len;
}

static
void
generate_spmr (
	gpointer*		packet,
	gsize*			len
	)
{
	struct pgm_sk_buff_t* skb = generate_packet ();

/* update IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_src.s_addr	= inet_addr (TEST_END_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (TEST_SRC_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)TEST_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)TEST_SPORT);

/* finalize PGM header */
	pgmhdr->pgm_type	= PGM_SPMR;

/* finalize IP header */
	iphdr->ip_len		= g_htons (skb->len);

	*packet = skb->head;
	*len    = skb->len;
}

static
void
generate_peer_spmr (
	gpointer*		packet,
	gsize*			len
	)
{
	struct pgm_sk_buff_t* skb = generate_packet ();

/* update IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_src.s_addr	= inet_addr (TEST_PEER_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (TEST_GROUP_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)TEST_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)TEST_SPORT);

/* finalize PGM header */
	pgmhdr->pgm_type	= PGM_SPMR;

/* finalize IP header */
	iphdr->ip_len		= g_htons (skb->len);

	*packet = skb->head;
	*len    = skb->len;
}

static
void
generate_msghdr (
	const gpointer		packet,
	const gsize		packet_len
	)
{
	struct pgm_ip* iphdr = packet;
	struct sockaddr_in addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= iphdr->ip_src.s_addr
	};
	struct pgm_iovec iov = {
		.iov_base		= packet,
		.iov_len		= packet_len
	};
	struct pgm_cmsghdr* packet_cmsg = g_malloc0 (sizeof(struct pgm_cmsghdr) + sizeof(struct in_pktinfo));
	packet_cmsg->cmsg_len   = sizeof(*packet_cmsg) + sizeof(struct in_pktinfo);
	packet_cmsg->cmsg_level = IPPROTO_IP;
	packet_cmsg->cmsg_type  = IP_PKTINFO;
	struct in_pktinfo packet_info = {
		.ipi_ifindex		= 2,
#if !defined(_WIN32) && !defined(__CYGWIN__)
		.ipi_spec_dst		= iphdr->ip_src.s_addr,		/* local address */
#endif
		.ipi_addr		= iphdr->ip_dst.s_addr		/* destination address */
	};
	memcpy ((char*)(packet_cmsg + 1), &packet_info, sizeof(struct in_pktinfo));
#ifndef _WIN32
	struct msghdr packet_msg = {
		.msg_name		= g_memdup (&addr, sizeof(addr)),	/* source address */
		.msg_namelen		= sizeof(addr),
		.msg_iov		= g_memdup (&iov, sizeof(iov)),
		.msg_iovlen		= 1,
		.msg_control		= &packet_cmsg,
		.msg_controllen		= sizeof(struct cmsghdr) + sizeof(struct in_pktinfo),
		.msg_flags		= 0
	};
#else
	WSAMSG packet_msg = {
		.name			= (LPSOCKADDR)g_memdup (&addr, sizeof(addr)),
		.namelen		= sizeof(addr),
		.lpBuffers		= (LPWSABUF)g_memdup (&iov, sizeof(iov)),
		.dwBufferCount		= 1,
		.dwFlags		= 0
	};
	packet_msg.Control.buf = (char*)&packet_cmsg;
	packet_msg.Control.len = sizeof(struct pgm_cmsghdr) + sizeof(struct in_pktinfo);
#endif

	struct mock_recvmsg_t* mr = g_malloc (sizeof(struct mock_recvmsg_t));
	mr->mr_msg	= g_memdup (&packet_msg, sizeof(packet_msg));
	mr->mr_errno	= 0;
	mr->mr_retval	= packet_len;
	mock_recvmsg_list = g_list_append (mock_recvmsg_list, mr);
}

static
void
push_block_event (void)
{
/* block */
	struct mock_recvmsg_t* mr = g_malloc (sizeof(struct mock_recvmsg_t));
	mr->mr_msg	= NULL;
	mr->mr_errno	= PGM_SOCK_EAGAIN;
	mr->mr_retval	= SOCKET_ERROR;
	mock_recvmsg_list = g_list_append (mock_recvmsg_list, mr);
}

/** packet module */
bool
mock_pgm_parse_raw (
	struct pgm_sk_buff_t* const	skb,
	struct sockaddr* const		dst,
	pgm_error_t**			error
	)
{
	const struct pgm_ip* ip = (struct pgm_ip*)skb->data;
	struct sockaddr_in* sin = (struct sockaddr_in*)dst;
	sin->sin_family         = AF_INET;
	sin->sin_addr.s_addr    = ip->ip_dst.s_addr;
	const gsize ip_header_length = ip->ip_hl * 4;
	skb->pgm_header = (gpointer)( (guint8*)skb->data + ip_header_length );
	skb->data       = skb->pgm_header;
	skb->len       -= ip_header_length;
	memcpy (&skb->tsi.gsi, skb->pgm_header->pgm_gsi, sizeof(pgm_gsi_t));
	skb->tsi.sport = skb->pgm_header->pgm_sport;
	return TRUE;
}

bool
mock_pgm_parse_udp_encap (
	struct pgm_sk_buff_t* const	skb,
	pgm_error_t**			error
	)
{
	skb->pgm_header = skb->data;
	memcpy (&skb->tsi.gsi, skb->pgm_header->pgm_gsi, sizeof(pgm_gsi_t));
	skb->tsi.sport = skb->pgm_header->pgm_sport;
	return TRUE;
}

bool
mock_pgm_verify_spm (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

bool
mock_pgm_verify_nak (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

bool
mock_pgm_verify_ncf (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

/** socket module */
#ifdef HAVE_POLL
int
mock_pgm_poll_info (
	pgm_sock_t* const	sock,
	struct pollfd*		fds,
	int*			n_fds,
	short			events
	)
{
}
#else
int
mock_pgm_select_info (
	pgm_sock_t* const	sock,
	fd_set*const		readfds,
	fd_set*const		writefds,
	int*const		n_fds
	)
{
}
#endif

pgm_peer_t*
mock__pgm_peer_ref (
	pgm_peer_t*	peer
	)
{
	pgm_atomic_inc32 (&peer->ref_count);
	return peer;
}

PGM_GNUC_INTERNAL
pgm_peer_t*
mock_pgm_new_peer (
	pgm_sock_t* const		sock,
	const pgm_tsi_t* const		tsi,
	const struct sockaddr* const	src_addr,
	const socklen_t			src_addr_len,
	const struct sockaddr* const	dst_addr,
	const socklen_t			dst_addr_len,
	const pgm_time_t		now
	)
{
	pgm_peer_t* peer = g_malloc0 (sizeof(pgm_peer_t));
	peer->expiry = now + sock->peer_expiry;
	memcpy (&peer->tsi, tsi, sizeof(pgm_tsi_t));
	memcpy (&peer->group_nla, dst_addr, dst_addr_len);
	memcpy (&peer->local_nla, src_addr, src_addr_len);
	((struct sockaddr_in*)&peer->local_nla)->sin_port = g_htons (sock->udp_encap_ucast_port);
	((struct sockaddr_in*)&peer->nla)->sin_port = g_htons (sock->udp_encap_ucast_port);
	peer->window = mock_pgm_rxw_create (&peer->tsi,
					    sock->max_tpdu,
					    sock->rxw_sqns,
					    sock->rxw_secs,
					    sock->rxw_max_rte,
					    sock->ack_c_p);
	peer->spmr_expiry = now + sock->spmr_expiry;
	gpointer entry = mock__pgm_peer_ref(peer);
	pgm_hashtable_insert (sock->peers_hashtable, &peer->tsi, entry);
	peer->peers_link.next = sock->peers_list;
	peer->peers_link.data = peer;
	if (sock->peers_list)
		sock->peers_list->prev = &peer->peers_link;
	sock->peers_list = &peer->peers_link;
	return peer;
}

PGM_GNUC_INTERNAL
void
mock_pgm_set_reset_error (
	pgm_sock_t* const          sock,
	pgm_peer_t* const               source,
	struct pgm_msgv_t* const	msgv
	)
{
}

PGM_GNUC_INTERNAL
int
mock_pgm_flush_peers_pending (
	pgm_sock_t* const          sock,
	struct pgm_msgv_t**		pmsg,
	const struct pgm_msgv_t* const	msg_end,
	size_t* const			bytes_read,
	unsigned* const			data_read
	)
{
	if (mock_data_list) {
		size_t len = 0;
		unsigned count = 0;
		while (mock_data_list && *pmsg <= msg_end) {
			 struct pgm_msgv_t* mock_msgv = mock_data_list->data;
			(*pmsg)->msgv_len = mock_msgv->msgv_len;
			for (unsigned i = 0; i < mock_msgv->msgv_len; i++) {
				(*pmsg)->msgv_skb[i] = mock_msgv->msgv_skb[i];
				len += mock_msgv->msgv_skb[i]->len;
			}
			count++;
			(*pmsg)++;
			mock_data_list = g_list_delete_link (mock_data_list, mock_data_list);
		}
		*bytes_read = len;
		*data_read = count;
		if (*pmsg > msg_end)
			return -PGM_SOCK_ENOBUFS;
	}
	return 0;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_peer_has_pending (
	pgm_peer_t* const               peer
	)
{
	return FALSE;
}

PGM_GNUC_INTERNAL
void
mock_pgm_peer_set_pending (
	pgm_sock_t* const          sock,
	pgm_peer_t* const               peer
	)
{
	g_assert (NULL != sock);
	g_assert (NULL != peer);
	if (peer->pending_link.data) return;
	peer->pending_link.data = peer;
	peer->pending_link.next = sock->peers_pending;
	sock->peers_pending = &peer->pending_link;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_on_data (
	pgm_sock_t* const		sock,
	pgm_peer_t* const		sender,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_data (sock:%p sender:%p skb:%p)",
		(gpointer)sock, (gpointer)sender, (gpointer)skb);
	mock_pgm_type = PGM_ODATA;
	((pgm_rxw_t*)sender->window)->has_event = 1;
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_on_ack (
	pgm_sock_t* const		sock,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_ack (sock:%p skb:%p)",
		(gpointer)sock, (gpointer)skb);
	mock_pgm_type = PGM_ACK;
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_on_deferred_nak (
	pgm_sock_t* const		sock
	)
{
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_on_nak (
	pgm_sock_t* const		sock,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_nak (sock:%p skb:%p)",
		(gpointer)sock, (gpointer)skb);
	mock_pgm_type = PGM_NAK;
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_on_peer_nak (
	pgm_sock_t* const		sock,
	pgm_peer_t* const		sender,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_peer_nak (sock:%p sender:%p skb:%p)",
		(gpointer)sock, (gpointer)sender, (gpointer)skb);
	mock_pgm_type = PGM_NAK;
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_on_ncf (
	pgm_sock_t* const		sock,
	pgm_peer_t* const		sender,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_ncf (sock:%p sender:%p skb:%p)",
		(gpointer)sock, (gpointer)sender, (gpointer)skb);
	mock_pgm_type = PGM_NCF;
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_on_nnak (
	pgm_sock_t* const		sock,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_nnak (sock:%p skb:%p)",
		(gpointer)sock, (gpointer)skb);
	mock_pgm_type = PGM_NNAK;
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_on_spm (
	pgm_sock_t* const		sock,
	pgm_peer_t* const		sender,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_spm (sock:%p sender:%p skb:%p)",
		(gpointer)sock, (gpointer)sender, (gpointer)skb);
	mock_pgm_type = PGM_SPM;
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_on_spmr (
	pgm_sock_t* const		sock,
	pgm_peer_t* const		peer,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_spmr (sock:%p peer:%p skb:%p)",
		(gpointer)sock, (gpointer)peer, (gpointer)skb);
	mock_pgm_type = PGM_SPMR;
	if (mock_reset_on_spmr) {
		sock->is_reset = TRUE;
		mock_pgm_peer_set_pending (sock, mock_peer);
	}
	if (mock_data_on_spmr) {
		mock_pgm_peer_set_pending (sock, mock_peer);
	}
	return TRUE;
}

/** transmit window */
PGM_GNUC_INTERNAL
bool
mock_pgm_txw_retransmit_is_empty (
	const pgm_txw_t*const	window
	)
{
	return TRUE;
}

/** receive window */
pgm_rxw_t*
mock_pgm_rxw_create (
	const pgm_tsi_t*	tsi,
	const uint16_t		tpdu_size,
	const unsigned		sqns,
	const unsigned		secs,
	const ssize_t		max_rte,
	const uint32_t		ack_c_p
	)
{
	return g_new0 (pgm_rxw_t, 1);
}

ssize_t
mock_pgm_rxw_readv (
	pgm_rxw_t* const	window,
	struct pgm_msgv_t**	pmsg,
	const unsigned		pmsglen
	)
{
	return -1;
}

/** net module */
PGM_GNUC_INTERNAL
ssize_t
mock_pgm_sendto (
	pgm_sock_t*		sock,
	bool				use_rate_limit,
	bool				use_router_alert,
	const void*			buf,
	size_t				len,
	const struct sockaddr*		to,
	socklen_t			tolen
	)
{
	return len;
}

/** timer module */
PGM_GNUC_INTERNAL
bool
mock_pgm_timer_prepare (
	pgm_sock_t* const		sock
	)
{
	return FALSE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_timer_check (
	pgm_sock_t* const		sock
	)
{
	return FALSE;
}

PGM_GNUC_INTERNAL
pgm_time_t
mock_pgm_timer_expiration (
	pgm_sock_t* const		sock
	)
{
	return 100L;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_timer_dispatch (
	pgm_sock_t* const		sock
	)
{
	return TRUE;
}

/** time module */
static pgm_time_t mock_pgm_time_now = 0x1;

static
pgm_time_t
_mock_pgm_time_update_now (void)
{
	return mock_pgm_time_now;
}

/** libc */
#ifndef _WIN32
static
ssize_t
mock_recvmsg (
	int			s,
	struct msghdr*		msg,
	int			flags
	)
{
	g_assert (NULL != msg);
	g_assert (NULL != mock_recvmsg_list);

	g_debug ("mock_recvmsg (s:%d msg:%p flags:%d)",
		s, (gpointer)msg, flags);

	struct mock_recvmsg_t* mr = mock_recvmsg_list->data;
	struct msghdr* mock_msg	= mr->mr_msg;
	ssize_t mock_retval	= mr->mr_retval;
	int mock_errno		= mr->mr_errno;
	mock_recvmsg_list = g_list_delete_link (mock_recvmsg_list, mock_recvmsg_list);
	if (mock_msg) {
		g_assert_cmpuint (mock_msg->msg_namelen, <=, msg->msg_namelen);
		g_assert_cmpuint (mock_msg->msg_iovlen, <=, msg->msg_iovlen);
		g_assert_cmpuint (mock_msg->msg_controllen, <=, msg->msg_controllen);
		if (mock_msg->msg_namelen)
			memcpy (msg->msg_name, mock_msg->msg_name, mock_msg->msg_namelen);
		if (mock_msg->msg_iovlen) {
			for (unsigned i = 0; i < mock_msg->msg_iovlen; i++) {
				g_assert (mock_msg->msg_iov[i].iov_len <= msg->msg_iov[i].iov_len);
				memcpy (msg->msg_iov[i].iov_base, mock_msg->msg_iov[i].iov_base, mock_msg->msg_iov[i].iov_len);
			}
		}
		if (mock_msg->msg_controllen)
			memcpy (msg->msg_control, mock_msg->msg_control, mock_msg->msg_controllen);
		msg->msg_controllen = mock_msg->msg_controllen;
		msg->msg_flags = mock_msg->msg_flags;
	}
	errno = mock_errno;
	return mock_retval;
}
#else
static
int
mock_WSARecvMsg (
	SOCKET			s,
	LPWSAMSG		lpMsg,
	LPDWORD			lpdwNumberOfBytesRecvd,
	LPWSAOVERLAPPED		lpOverlapped,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
	)
{
	g_assert (NULL != lpMsg);
	g_assert (NULL != mock_recvmsg_list);

	g_debug ("mock_WSARecvMsg (s:%d msg:%p recvd:%d ol:%p cr:%p)",
		s, (gpointer)lpMsg, lpdwNumberOfBytesRecvd, (gpointer)lpOverlapped, (gpointer)lpCompletionRoutine);

	struct mock_recvmsg_t* mr = mock_recvmsg_list->data;
	WSAMSG* mock_msg	= mr->mr_msg;
/* only return 0 on success, not bytes received */
	ssize_t mock_retval	= mr->mr_retval < 0 ? mr->mr_retval : 0;
	int mock_errno		= mr->mr_errno;
	mock_recvmsg_list = g_list_delete_link (mock_recvmsg_list, mock_recvmsg_list);
	if (mock_msg) {
		g_assert_cmpuint (mock_msg->namelen, <=, lpMsg->namelen);
		g_assert_cmpuint (mock_msg->dwBufferCount, <=, lpMsg->dwBufferCount);
		if (mock_msg->namelen)
			memcpy (lpMsg->name, mock_msg->name, mock_msg->namelen);
		if (mock_msg->dwBufferCount) {
			for (unsigned i = 0; i < mock_msg->dwBufferCount; i++) {
				g_assert (mock_msg->lpBuffers[i].len <= lpMsg->lpBuffers[i].len);
				memcpy (lpMsg->lpBuffers[i].buf, mock_msg->lpBuffers[i].buf, mock_msg->lpBuffers[i].len);
			}
		}
		if (mr->mr_retval >= 0)
			*lpdwNumberOfBytesRecvd = mr->mr_retval;
		if (mock_msg->Control.len)
			memcpy (lpMsg->Control.buf, mock_msg->Control.buf, mock_msg->Control.len);
		lpMsg->Control.len = mock_msg->Control.len;
		lpMsg->dwFlags = mock_msg->dwFlags;
		g_debug ("namelen %d buffers %d", mock_msg->namelen, mock_msg->dwBufferCount);
	}
	g_debug ("returning %d (errno:%d)", mock_retval, mock_errno);
	WSASetLastError (mock_errno);
	return mock_retval;
}

LPFN_WSARECVMSG mock_pgm_WSARecvMsg = mock_WSARecvMsg;

static
int
mock_recvfrom (
	SOCKET			s,
	char*			buf,
	int			len,
	int			flags,
	struct sockaddr*	from,
	int*			fromlen
	)
{
	g_assert (NULL != buf);
	g_assert (NULL != mock_recvmsg_list);

	g_debug ("mock_recvfrom (s:%d buf:%p len:%d flags:%d from:%p fromlen:%p)",
		s, (gpointer)buf, len, flags, (gpointer)from, (gpointer)fromlen);

	struct mock_recvmsg_t* mr = mock_recvmsg_list->data;
	WSAMSG* mock_msg	= mr->mr_msg;
/* only return 0 on success, not bytes received */
	int mock_retval		= mr->mr_retval < 0 ? mr->mr_retval : 0;
	int mock_errno		= mr->mr_errno;
	mock_recvmsg_list = g_list_delete_link (mock_recvmsg_list, mock_recvmsg_list);
	if (mock_msg) {
		if (NULL != fromlen) {
			g_assert_cmpuint (mock_msg->namelen, <=, *fromlen);
			*fromlen = mock_msg->namelen;
		}
		if (NULL != from && mock_msg->namelen) {
			memcpy (from, mock_msg->name, mock_msg->namelen);
		}
		if (mock_msg->dwBufferCount) {
			for (unsigned i = 0; i < mock_msg->dwBufferCount; i++) {
				const size_t count = MIN(len, mock_msg->lpBuffers[i].len);
				memcpy (buf, mock_msg->lpBuffers[i].buf, count);
				buf += count;
				len -= count;
				mock_retval += count;
			}
		}
	}
	g_debug ("returning %d (errno:%d)", mock_retval, mock_errno);
	WSASetLastError (mock_errno);
	return mock_retval;
}
#endif /* _WIN32 */


/* mock functions for external references */

size_t
pgm_pkt_offset (
        const bool                      can_fragment,
        const sa_family_t		pgmcc_family	/* 0 = disable */
        )
{
        return 0;
}

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}


/* target:
 *	int
 *	pgm_recv (
 *		pgm_sock_t*		sock,
 *		void*			data,
 *		size_t			len,
 *		int			flags,
 *		size_t*			bytes_read,
 *		pgm_error_t**		error
 *		)
 *
 * Most tests default to PGM_IO_STATUS_TIMER_PENDING, PGM_IO_STATUS_WOULD_BLOCK is not expected due
 * to peer state engine and SPM broadcasts.
 */

START_TEST (test_block_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	push_block_event ();
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
}
END_TEST

/* recv -> on_data */
START_TEST (test_data_pass_001)
{
	const char source[] = "i am not a string";
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_odata (source, sizeof(source), 0 /* sqn */, -1 /* trail */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (PGM_ODATA == mock_pgm_type, "unexpected PGM packet");
}
END_TEST

/* recv -> on_spm */
START_TEST (test_spm_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_spm (200 /* spm-sqn */, -1 /* trail */, 0 /* lead */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (PGM_SPM == mock_pgm_type, "unexpected PGM packet");
}
END_TEST

/* recv -> on_nak */
START_TEST (test_nak_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_nak (0 /* sqn */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (PGM_NAK == mock_pgm_type, "unexpected PGM packet");
}
END_TEST

/* recv -> on_peer_nak */
START_TEST (test_peer_nak_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_peer_nak (0 /* sqn */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (PGM_NAK == mock_pgm_type, "unexpected PGM packet");
}
END_TEST

/* recv -> on_nnak */
START_TEST (test_nnak_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_nnak (0 /* sqn */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (PGM_NNAK == mock_pgm_type, "unexpected PGM packet");
}
END_TEST

/* recv -> on_ncf */
START_TEST (test_ncf_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_ncf (0 /* sqn */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (PGM_NCF == mock_pgm_type, "unexpected PGM packet");
}
END_TEST

/* recv -> on_spmr */
START_TEST (test_spmr_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (PGM_SPMR == mock_pgm_type, "unexpected PGM packet");
}
END_TEST

/* recv -> on (peer) spmr */
START_TEST (test_peer_spmr_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_peer_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (PGM_SPMR == mock_pgm_type, "unexpected PGM packet");
}
END_TEST

/* recv -> lost data */
START_TEST (test_lost_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	sock->is_reset = TRUE;
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_END_ADDR)
	};
	pgm_peer_t* peer = mock_pgm_new_peer (sock, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr), mock_pgm_time_now);
	fail_if (NULL == peer, "new_peer failed");
	mock_pgm_peer_set_pending (sock, peer);
	push_block_event ();
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_RESET == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	if (err) {
		g_message ("%s", err->message);
		pgm_error_free (err);
		err = NULL;
	}
	push_block_event ();
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
}
END_TEST

/* recv -> lost data and abort transport */
START_TEST (test_abort_on_lost_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	sock->is_reset = TRUE;
	sock->is_abort_on_reset = TRUE;
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_END_ADDR)
	};
	pgm_peer_t* peer = mock_pgm_new_peer (sock, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr), mock_pgm_time_now);
	fail_if (NULL == peer, "new_peer failed");
	mock_pgm_peer_set_pending (sock, peer);
	push_block_event ();
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_RESET == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	if (err) {
		g_message ("%s", err->message);
		pgm_error_free (err);
		err = NULL;
	}
	push_block_event ();
	fail_unless (PGM_IO_STATUS_RESET == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
}
END_TEST

/* recv -> (spmr) & loss */
START_TEST (test_then_lost_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	mock_reset_on_spmr = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (sock, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr), mock_pgm_time_now);
	fail_if (NULL == mock_peer, "new_peer failed");
	push_block_event ();
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_RESET == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	if (err) {
		g_message ("%s", err->message);
		pgm_error_free (err);
		err = NULL;
	}
	push_block_event ();
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
}
END_TEST

/* recv -> data & loss & abort transport */
START_TEST (test_then_abort_on_lost_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	mock_reset_on_spmr = TRUE;
	sock->is_abort_on_reset = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (sock, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr), mock_pgm_time_now);
	fail_if (NULL == mock_peer, "new_peer failed");
	push_block_event ();
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_RESET == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	if (err) {
		g_message ("%s", err->message);
		pgm_error_free (err);
		err = NULL;
	}
	push_block_event ();
	fail_unless (PGM_IO_STATUS_RESET == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
}
END_TEST

/* new waiting peer -> return data */
START_TEST (test_on_data_pass_001)
{
	const char source[] = "i am not a string";
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	mock_data_on_spmr = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (sock, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr), mock_pgm_time_now);
	fail_if (NULL == mock_peer, "new_peer failed");
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (TEST_MAX_TPDU);
	pgm_skb_put (skb, sizeof(source));
	memcpy (skb->data, source, sizeof(source));
	struct pgm_msgv_t* msgv = g_new0 (struct pgm_msgv_t, 1);
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
	push_block_event ();
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (NULL == err, "error raised");
	fail_unless ((gsize)sizeof(source) == bytes_read, "unexpected data length");
	push_block_event ();
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
}
END_TEST

/* zero length data waiting */
START_TEST (test_on_zero_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	mock_data_on_spmr = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (sock, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr), mock_pgm_time_now);
	fail_if (NULL == mock_peer, "new_peer failed");
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (TEST_MAX_TPDU);
	struct pgm_msgv_t* msgv = g_new0 (struct pgm_msgv_t, 1);
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
	push_block_event ();
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (NULL == err, "error raised");
	fail_unless ((gsize)0 == bytes_read, "unexpected data length");
	push_block_event ();
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
}
END_TEST

/* more than vector length data waiting */
START_TEST (test_on_many_data_pass_001)
{
	const char* source[] = {
		"i am not a string",
		"i am not an iguana",
		"i am not a peach"
	};
	pgm_sock_t* sock = generate_sock();
	fail_if (NULL == sock, "generate_sock failed");
	mock_data_on_spmr = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(TEST_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (sock, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr), mock_pgm_time_now);
	fail_if (NULL == mock_peer, "new_peer failed");
	struct pgm_sk_buff_t* skb;
	struct pgm_msgv_t* msgv;
/* #1 */
	msgv = g_new0 (struct pgm_msgv_t, 1);
	skb = pgm_alloc_skb (TEST_MAX_TPDU);
	pgm_skb_put (skb, strlen(source[0]) + 1);
	memcpy (skb->data, source[0], strlen(source[0]));
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
/* #2 */
	msgv = g_new0 (struct pgm_msgv_t, 1);
	skb = pgm_alloc_skb (TEST_MAX_TPDU);
	pgm_skb_put (skb, strlen(source[1]) + 1);
	memcpy (skb->data, source[1], strlen(source[1]));
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
/* #3 */
	msgv = g_new0 (struct pgm_msgv_t, 1);
	skb = pgm_alloc_skb (TEST_MAX_TPDU);
	pgm_skb_put (skb, strlen(source[2]) + 1);
	memcpy (skb->data, source[2], strlen(source[2]));
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	gsize bytes_read;
	pgm_error_t* err = NULL;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (NULL == err, "error raised");
	fail_unless ((gsize)(strlen(source[0]) + 1) == bytes_read, "unexpected data length");
	g_message ("#1 = \"%s\"", buffer);
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (NULL == err, "error raised");
	fail_unless ((gsize)(strlen(source[1]) + 1) == bytes_read, "unexpected data length");
	g_message ("#2 = \"%s\"", buffer);
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
	fail_unless (NULL == err, "error raised");
	fail_unless ((gsize)(strlen(source[2]) + 1) == bytes_read, "unexpected data length");
	g_message ("#3 = \"%s\"", buffer);
	push_block_event ();
	fail_unless (PGM_IO_STATUS_TIMER_PENDING == pgm_recv (sock, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err), "recv failed");
}
END_TEST

START_TEST (test_recv_fail_001)
{
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	fail_unless (PGM_IO_STATUS_ERROR == pgm_recv (NULL, buffer, sizeof(buffer), 0, NULL, NULL), "recv failed");
}
END_TEST

/* target:
 *	int
 *	pgm_recvfrom (
 *		pgm_sock_t*		sock,
 *		void*			data,
 *		size_t			len,
 *		int			flags,
 *		size_t*			bytes_read,
 *		struct pgm_sockaddr_t*	from,
 *		socklen_t*		fromlen,
 *		pgm_error_t**		error
 *		)
 */

START_TEST (test_recvfrom_fail_001)
{
	guint8 buffer[ TEST_TXW_SQNS * TEST_MAX_TPDU ];
	struct pgm_sockaddr_t from;
	socklen_t fromlen = sizeof(from);
	fail_unless (PGM_IO_STATUS_ERROR == pgm_recvfrom (NULL, buffer, sizeof(buffer), 0, NULL, &from, &fromlen, NULL), "recvfrom failed");
}
END_TEST

/* target:
 *	int
 *	pgm_recvmsg (
 *		pgm_sock_t*	sock,
 *		pgm_msgv_t*		msgv,
 *		int			flags,
 *		size_t*			bytes_read,
 *		pgm_error_t**		error
 *		)
 */

START_TEST (test_recvmsg_fail_001)
{
	struct pgm_msgv_t msgv;
	fail_unless (PGM_IO_STATUS_ERROR == pgm_recvmsg (NULL, &msgv, 0, NULL, NULL), "recvmsg failed");
}
END_TEST

/* target:
 *	int
 *	pgm_recvmsgv (
 *		pgm_sock_t*	sock,
 *		pgm_msgv_t*		msgv,
 *		unsigned		msgv_length,
 *		int			flags,
 *		size_t*			bytes_read,
 *		pgm_error_t**		error
 *		)
 */

START_TEST (test_recvmsgv_fail_001)
{
	struct pgm_msgv_t msgv[1];
	fail_unless (PGM_IO_STATUS_ERROR == pgm_recvmsgv (NULL, msgv, G_N_ELEMENTS(msgv), 0, NULL, NULL), "recvmsgv failed");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_block = tcase_create ("block");
	suite_add_tcase (s, tc_block);
	tcase_add_checked_fixture (tc_block, mock_setup, mock_teardown);
	tcase_add_test (tc_block, test_block_pass_001);

	TCase* tc_data = tcase_create ("data");
	suite_add_tcase (s, tc_data);
	tcase_add_checked_fixture (tc_data, mock_setup, mock_teardown);
	tcase_add_test (tc_data, test_data_pass_001);

	TCase* tc_spm = tcase_create ("spm");
	suite_add_tcase (s, tc_spm);
	tcase_add_checked_fixture (tc_spm, mock_setup, mock_teardown);
	tcase_add_test (tc_spm, test_spm_pass_001);

	TCase* tc_nak = tcase_create ("nak");
	suite_add_tcase (s, tc_nak);
	tcase_add_checked_fixture (tc_nak, mock_setup, mock_teardown);
	tcase_add_test (tc_nak, test_nak_pass_001);

	TCase* tc_peer_nak = tcase_create ("peer-nak");
	suite_add_tcase (s, tc_peer_nak);
	tcase_add_checked_fixture (tc_peer_nak, mock_setup, mock_teardown);
	tcase_add_test (tc_peer_nak, test_peer_nak_pass_001);

	TCase* tc_nnak = tcase_create ("nnak");
	suite_add_tcase (s, tc_nnak);
	tcase_add_checked_fixture (tc_nnak, mock_setup, mock_teardown);
	tcase_add_test (tc_nnak, test_nnak_pass_001);

	TCase* tc_ncf = tcase_create ("ncf");
	suite_add_tcase (s, tc_ncf);
	tcase_add_checked_fixture (tc_ncf, mock_setup, mock_teardown);
	tcase_add_test (tc_ncf, test_ncf_pass_001);

	TCase* tc_spmr = tcase_create ("spmr");
	suite_add_tcase (s, tc_spmr);
	tcase_add_checked_fixture (tc_spmr, mock_setup, mock_teardown);
	tcase_add_test (tc_spmr, test_spmr_pass_001);

	TCase* tc_peer_spmr = tcase_create ("peer-spmr");
	suite_add_tcase (s, tc_peer_spmr);
	tcase_add_checked_fixture (tc_peer_spmr, mock_setup, mock_teardown);
	tcase_add_test (tc_peer_spmr, test_peer_spmr_pass_001);

	TCase* tc_lost = tcase_create ("lost");
	suite_add_tcase (s, tc_lost);
	tcase_add_checked_fixture (tc_lost, mock_setup, mock_teardown);
	tcase_add_test (tc_lost, test_lost_pass_001);

	TCase* tc_abort_on_lost = tcase_create ("abort-on-lost");
	suite_add_tcase (s, tc_abort_on_lost);
	tcase_add_checked_fixture (tc_abort_on_lost, mock_setup, mock_teardown);
	tcase_add_test (tc_abort_on_lost, test_abort_on_lost_pass_001);

	TCase* tc_then_lost = tcase_create ("then-lost");
	suite_add_tcase (s, tc_then_lost);
	tcase_add_checked_fixture (tc_then_lost, mock_setup, mock_teardown);
	tcase_add_test (tc_then_lost, test_then_lost_pass_001);

	TCase* tc_then_abort_on_lost = tcase_create ("then-abort-on-lost");
	suite_add_tcase (s, tc_then_abort_on_lost);
	tcase_add_checked_fixture (tc_then_abort_on_lost, mock_setup, mock_teardown);
	tcase_add_test (tc_then_abort_on_lost, test_then_abort_on_lost_pass_001);

	TCase* tc_on_data = tcase_create ("on-data");
	suite_add_tcase (s, tc_on_data);
	tcase_add_checked_fixture (tc_on_data, mock_setup, mock_teardown);
	tcase_add_test (tc_on_data, test_on_data_pass_001);

	TCase* tc_on_zero = tcase_create ("on-zero");
	suite_add_tcase (s, tc_on_zero);
	tcase_add_checked_fixture (tc_on_zero, mock_setup, mock_teardown);
	tcase_add_test (tc_on_zero, test_on_zero_pass_001);

	TCase* tc_on_many_data = tcase_create ("on-many-data");
	suite_add_tcase (s, tc_on_many_data);
	tcase_add_checked_fixture (tc_on_many_data, mock_setup, mock_teardown);
	tcase_add_test (tc_on_many_data, test_on_many_data_pass_001);

	TCase* tc_recv = tcase_create ("recv");
	suite_add_tcase (s, tc_recv);
	tcase_add_checked_fixture (tc_recv, mock_setup, mock_teardown);
	tcase_add_test (tc_recv, test_recv_fail_001);

	TCase* tc_recvfrom = tcase_create ("recvfrom");
	suite_add_tcase (s, tc_recvfrom);
	tcase_add_checked_fixture (tc_recvfrom, mock_setup, mock_teardown);
	tcase_add_test (tc_recvfrom, test_recvfrom_fail_001);

	TCase* tc_recvmsg = tcase_create ("recvmsg");
	suite_add_tcase (s, tc_recvmsg);
	tcase_add_checked_fixture (tc_recvmsg, mock_setup, mock_teardown);
	tcase_add_test (tc_recvmsg, test_recvmsg_fail_001);

	TCase* tc_recvmsgv = tcase_create ("recvmsgv");
	suite_add_tcase (s, tc_recvmsgv);
	tcase_add_checked_fixture (tc_recvmsgv, mock_setup, mock_teardown);
	tcase_add_test (tc_recvmsgv, test_recvmsgv_fail_001);

	return s;
}

static
Suite*
make_master_suite (void)
{
	Suite* s = suite_create ("Master");
	return s;
}

int
main (void)
{
#ifdef _WIN32
	WORD wVersionRequested = MAKEWORD (2, 2);
	WSADATA wsaData;
	g_assert (0 == WSAStartup (wVersionRequested, &wsaData));
	g_assert (LOBYTE (wsaData.wVersion) == 2 && HIBYTE (wsaData.wVersion) == 2);
#endif
	g_assert (pgm_time_init (NULL));
	pgm_rand_init();
	pgm_messages_init();
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	pgm_messages_shutdown();
	pgm_rand_shutdown();
	g_assert (pgm_time_shutdown());
#ifdef _WIN32
	WSACleanup();
#endif
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
