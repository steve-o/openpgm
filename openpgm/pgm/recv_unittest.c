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


#include <signal.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>

#include <pgm/recv.h>
#include <pgm/rxwi.h>
#include <pgm/ip.h>
#include <pgm/skbuff.h>
#include <pgm/reed_solomon.h>
#include <pgm/checksum.h>


/* mock state */

#define PGM_NETWORK		""
#define PGM_PORT		7500
#define PGM_MAX_TPDU		1500
#define PGM_TXW_SQNS		32
#define PGM_RXW_SQNS		32
#define PGM_HOPS		16
#define PGM_SPM_AMBIENT		( pgm_secs(30) )
#define PGM_SPM_HEARTBEAT_INIT	{ pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(100), pgm_msecs(1300), pgm_secs(7), pgm_secs(16), pgm_secs(25), pgm_secs(30) }
#define PGM_PEER_EXPIRY		( pgm_secs(300) )
#define PGM_SPMR_EXPIRY		( pgm_msecs(250) )
#define PGM_NAK_BO_IVL		( pgm_msecs(50) )
#define PGM_NAK_RPT_IVL		( pgm_secs(2) )
#define PGM_NAK_RDATA_IVL	( pgm_secs(2) )
#define PGM_NAK_DATA_RETRIES	5
#define PGM_NAK_NCF_RETRIES	2

struct mock_recvmsg_t {
	struct msghdr*		mr_msg;
	ssize_t			mr_retval;
	int			mr_errno;
};

GList* mock_recvmsg_list = NULL;
static int mock_pgm_type = -1;

static pgm_rxw_t* mock_pgm_rxw_create (const pgm_tsi_t*, const guint16, const guint32, const guint, const guint);
static pgm_time_t mock_pgm_time_update_now (void);


static
void
mock_setup (void)
{
	if (!g_thread_supported ()) g_thread_init (NULL);
}

static
struct pgm_transport_t*
generate_transport (void)
{
	struct pgm_transport_t* transport = g_malloc0 (sizeof(struct pgm_transport_t));
	transport->is_bound = TRUE;
	transport->rx_buffer = pgm_alloc_skb (PGM_MAX_TPDU);
	transport->max_tpdu = PGM_MAX_TPDU;
	transport->rxw_sqns = PGM_RXW_SQNS;
	transport->dport = g_htons(PGM_PORT);
	transport->can_send_data = TRUE;
	transport->can_send_nak = TRUE;
	transport->can_recv_data = TRUE;
	transport->peers_hashtable = g_hash_table_new (pgm_tsi_hash, pgm_tsi_equal);
	transport->rand_ = g_rand_new();
	transport->nak_bo_ivl = 100*1000;
	pgm_notify_init (&transport->pending_notify);
	return transport;
}

static
void
generate_apdu (
	const char*		source,
	const guint		source_len,
	const guint32		sequence,
	gpointer*		packet,
	gsize*			len
	)
{
	struct pgm_sk_buff_t* skb;
	GError* err = NULL;

	skb = pgm_alloc_skb (PGM_MAX_TPDU);
	skb->data		= skb->head;
	skb->len		= sizeof(struct pgm_ip) + sizeof(struct pgm_header) + sizeof(struct pgm_data) + source_len;
	skb->tail		= (guint8*)skb->data + skb->len;

/* add IP header */
	struct pgm_ip* iphdr = skb->data;
	iphdr->ip_hl		= sizeof(struct pgm_ip) / 4;
	iphdr->ip_v		= 4;
	iphdr->ip_tos		= 0;
	iphdr->ip_len		= g_htons (skb->len);
	iphdr->ip_id		= 0;
	iphdr->ip_off		= 0;
	iphdr->ip_ttl		= 16;
	iphdr->ip_p		= IPPROTO_PGM;
	iphdr->ip_sum		= 0;
	iphdr->ip_src.s_addr	= inet_addr ("127.0.0.1");
	iphdr->ip_dst.s_addr	= inet_addr ("127.0.0.2");

/* add PGM header */
	struct pgm_header* pgmhdr = (gpointer)(iphdr + 1);
	pgmhdr->pgm_sport	= g_htons ((guint16)1000);
	pgmhdr->pgm_dport	= g_htons ((guint16)PGM_PORT);
	pgmhdr->pgm_type	= PGM_ODATA;
	pgmhdr->pgm_options	= 0;
	pgmhdr->pgm_gsi[0]	= 1;
	pgmhdr->pgm_gsi[1]	= 2;
	pgmhdr->pgm_gsi[2]	= 3;
	pgmhdr->pgm_gsi[3]	= 4;
	pgmhdr->pgm_gsi[4]	= 5;
	pgmhdr->pgm_gsi[5]	= 6;
	pgmhdr->pgm_tsdu_length = g_htons (source_len);

/* add ODATA header */
	struct pgm_data* datahdr = (gpointer)(pgmhdr + 1);
	datahdr->data_sqn	= g_htonl (sequence);
	datahdr->data_trail	= g_htonl ((guint32)-1);

/* add payload */
	gpointer data = (gpointer)(datahdr + 1);
	memcpy (data, source, source_len);

/* finally PGM checksum */
	pgmhdr->pgm_checksum 	= 0;

/* and IP checksum */
	iphdr->ip_sum		= 0;

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
	struct sockaddr_in addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr("127.0.0.3")
	};
	struct iovec iov = {
		.iov_base		= packet,
		.iov_len		= packet_len
	};
	struct cmsghdr* packet_cmsg = g_malloc0 (sizeof(struct cmsghdr) + sizeof(struct in_pktinfo));
	packet_cmsg->cmsg_len   = sizeof(struct cmsghdr) + sizeof(struct in_pktinfo);
	packet_cmsg->cmsg_level = IPPROTO_IP;
	packet_cmsg->cmsg_type  = IP_PKTINFO;
	struct in_pktinfo packet_info = {
		.ipi_ifindex		= 2,
		.ipi_spec_dst		= inet_addr("127.0.0.2"),
		.ipi_addr		= inet_addr("127.0.0.1")
	};
	memcpy ((char*)(packet_cmsg + 1), &packet_info, sizeof(struct in_pktinfo));
	struct msghdr packet_msg = {
		.msg_name		= g_memdup (&addr, sizeof(addr)),
		.msg_namelen		= sizeof(addr),
		.msg_iov		= g_memdup (&iov, sizeof(iov)),
		.msg_iovlen		= 1,
		.msg_control		= &packet_cmsg,
		.msg_controllen		= sizeof(struct cmsghdr) + sizeof(struct in_pktinfo),
		.msg_flags		= 0
	};

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
	mr->mr_errno	= EAGAIN;
	mr->mr_retval	= -1;
	mock_recvmsg_list = g_list_append (mock_recvmsg_list, mr);
}

/** packet module */
static
gboolean
mock_pgm_parse_raw (
	struct pgm_sk_buff_t* const	skb,
	struct sockaddr* const		dst,
	GError**			error
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

static
gboolean
mock_pgm_parse_udp_encap (
	struct pgm_sk_buff_t* const	skb,
	GError**			error
	)
{
	skb->pgm_header = skb->data;
	memcpy (&skb->tsi.gsi, skb->pgm_header->pgm_gsi, sizeof(pgm_gsi_t));
	skb->tsi.sport = skb->pgm_header->pgm_sport;
	return TRUE;
}

static
gboolean
mock_pgm_verify_spm (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_nak (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_verify_ncf (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

/** transport module */
static
int
mock_pgm_transport_poll_info (
	pgm_transport_t* const	transport,
	struct pollfd*		fds,
	int*			n_fds,
	int			events
	)
{
}

static
pgm_peer_t*
mock__pgm_peer_ref (
	pgm_peer_t*	peer
	)
{
	g_atomic_int_inc (&peer->ref_count);
	return peer;
}

static
pgm_peer_t*
mock_pgm_new_peer (
	pgm_transport_t* const		transport,
	const pgm_tsi_t* const		tsi,
	const struct sockaddr* const	src_addr,
	const gsize			src_addr_len,
	const struct sockaddr* const	dst_addr,
	const gsize			dst_addr_len
	)
{
	pgm_peer_t* peer = g_malloc0 (sizeof(pgm_peer_t));
	peer->last_packet = mock_pgm_time_update_now();
	peer->expiry = peer->last_packet + transport->peer_expiry;
	peer->transport = transport;
	memcpy (&peer->tsi, tsi, sizeof(pgm_tsi_t));
	memcpy (&peer->group_nla, dst_addr, dst_addr_len);
	memcpy (&peer->local_nla, src_addr, src_addr_len);
	((struct sockaddr_in*)&peer->local_nla)->sin_port = g_htons (transport->udp_encap_ucast_port);
	((struct sockaddr_in*)&peer->nla)->sin_port = g_htons (transport->udp_encap_ucast_port);
	peer->window = mock_pgm_rxw_create (&peer->tsi,
					    transport->max_tpdu,
					    transport->rxw_sqns,
					    transport->rxw_secs,
					    transport->rxw_max_rte);
	memcpy (&((pgm_rxw_t*)peer->window)->pgm_sock_err.tsi, &peer->tsi, sizeof(pgm_tsi_t));
	peer->spmr_expiry = peer->last_packet + transport->spmr_expiry;
	gpointer entry = mock__pgm_peer_ref(peer);
	g_hash_table_insert (transport->peers_hashtable, &peer->tsi, entry);
	peer->peers_link.next = transport->peers_list;
	peer->peers_link.data = peer;
	if (transport->peers_list)
		transport->peers_list->prev = &peer->peers_link;
	transport->peers_list = &peer->peers_link;
	return peer;
}

static
void
mock_pgm_set_reset_error (
	pgm_transport_t* const          transport,
	pgm_peer_t* const               source,
	pgm_msgv_t* const               msgv
	)
{
}

static
int
mock_pgm_flush_peers_pending (
	pgm_transport_t* const          transport,
	pgm_msgv_t**                    pmsg,
	const pgm_msgv_t* const         msg_end,
	gsize* const                    bytes_read,
	guint* const                    data_read
	)
{
	return 0;
}

static
gboolean
mock_pgm_peer_has_pending (
	pgm_peer_t* const               peer
	)
{
	return FALSE;
}

static
void
mock_pgm_peer_set_pending (
	pgm_transport_t* const          transport,
	pgm_peer_t* const               peer
	)
{
}

static
gboolean
mock_pgm_on_data (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		sender,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_data (transport:%p sender:%p skb:%p)",
		(gpointer)transport, (gpointer)sender, (gpointer)skb);
	mock_pgm_type = PGM_ODATA;
	((pgm_rxw_t*)sender->window)->has_event = 1;
	return TRUE;
}

static
gboolean
mock_pgm_on_nak (
	pgm_transport_t* const		transport,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_nak (transport:%p skb:%p)",
		(gpointer)transport, (gpointer)skb);
	mock_pgm_type = PGM_NAK;
	return TRUE;
}

static
gboolean
mock_pgm_on_peer_nak (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		sender,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_peer_nak (transport:%p sender:%p skb:%p)",
		(gpointer)transport, (gpointer)sender, (gpointer)skb);
	mock_pgm_type = PGM_NAK;
	return TRUE;
}

static
gboolean
mock_pgm_on_ncf (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		sender,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_ncf (transport:%p sender:%p skb:%p)",
		(gpointer)transport, (gpointer)sender, (gpointer)skb);
	mock_pgm_type = PGM_NCF;
	return TRUE;
}

static
gboolean
mock_pgm_on_nnak (
	pgm_transport_t* const		transport,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_nnak (transport:%p skb:%p)",
		(gpointer)transport, (gpointer)skb);
	mock_pgm_type = PGM_NNAK;
	return TRUE;
}

static
gboolean
mock_pgm_on_spm (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		sender,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_spm (transport:%p sender:%p skb:%p)",
		(gpointer)transport, (gpointer)sender, (gpointer)skb);
	mock_pgm_type = PGM_SPM;
	return TRUE;
}

static
gboolean
mock_pgm_on_spmr (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		peer,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_on_spmr (transport:%p peer:%p skb:%p)",
		(gpointer)transport, (gpointer)peer, (gpointer)skb);
	mock_pgm_type = PGM_SPMR;
	return TRUE;
}

/** receive window */
static
pgm_rxw_t*
mock_pgm_rxw_create (
	const pgm_tsi_t*	tsi,
	const guint16		tpdu_size,
	const guint32		sqns,
	const guint		secs,
	const guint		max_rte
	)
{
	return g_malloc0 (sizeof(pgm_rxw_t));
}

static
gssize
mock_pgm_rxw_readv (
	pgm_rxw_t* const	window,
	pgm_msgv_t**		pmsg,
	const guint		pmsglen
	)
{
	return -1;
}

/** net module */
static
gssize
mock_pgm_sendto (
	pgm_transport_t*		transport,
	gboolean			use_rate_limit,
	gboolean			use_router_alert,
	const void*			buf,
	gsize				len,
	int				flags,
	const struct sockaddr*		to,
	gsize				tolen
	)
{
	return len;
}

/** time module */
static pgm_time_t mock_pgm_time_now = 0x1;

static
pgm_time_t
mock_pgm_time_update_now (void)
{
	return mock_pgm_time_now;
}

/** libc */
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
		msg->msg_flags = mock_msg->msg_flags;
	}
	errno = mock_errno;
	return mock_retval;
}


/* mock functions for external references */

#define pgm_parse_raw		mock_pgm_parse_raw
#define pgm_parse_udp_encap	mock_pgm_parse_udp_encap
#define pgm_verify_spm		mock_pgm_verify_spm
#define pgm_verify_nak		mock_pgm_verify_nak
#define pgm_verify_ncf		mock_pgm_verify_ncf
#define pgm_transport_poll_info	mock_pgm_transport_poll_info
#define pgm_set_reset_error	mock_pgm_set_reset_error
#define pgm_flush_peers_pending	mock_pgm_flush_peers_pending
#define pgm_peer_has_pending	mock_pgm_peer_has_pending
#define pgm_peer_set_pending	mock_pgm_peer_set_pending
#define pgm_rxw_create		mock_pgm_rxw_create
#define pgm_rxw_readv		mock_pgm_rxw_readv
#define pgm_new_peer		mock_pgm_new_peer
#define pgm_on_data		mock_pgm_on_data
#define pgm_on_spm		mock_pgm_on_spm
#define pgm_on_nak		mock_pgm_on_nak
#define pgm_on_peer_nak		mock_pgm_on_peer_nak
#define pgm_on_nnak		mock_pgm_on_nnak
#define pgm_on_ncf		mock_pgm_on_ncf
#define pgm_on_spmr		mock_pgm_on_spmr
#define pgm_sendto		mock_pgm_sendto
#define pgm_time_now		mock_pgm_time_now
#define pgm_time_update_now	mock_pgm_time_update_now
#define recvmsg			mock_recvmsg

#define RECV_DEBUG
#include "recv.c"
#undef g_trace


/* target:
 *	GIOStatus
 *	pgm_transport_recv (
 *		pgm_transport_t*	transport,
 *		gpointer		data,
 *		gsize			len,
 *		int			flags,
 *		gsize*			bytes_read,
 *		GError**		error
 *		)
 */

/* recv -> on_data */
START_TEST (test_recv_data_pass_001)
{
	const char source[] = "i am not a string";
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_apdu (source, sizeof(source), 0, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (PGM_ODATA == mock_pgm_type);
}
END_TEST

/* recv -> on_spm */
/* recv -> on_nak */
/* recv -> on_peer_nak */
/* recv -> on_nnak */
/* recv -> on_ncf */
/* recv -> on_spmr */

/* recv -> invalid packet */

/* recv -> lost data */
/* recv -> lost data and close transport */
/* recv -> data & loss */
/* recv -> data & loss & close transport */

/* new waiting peer -> return data */
/* waiting from data -> return data */
/* zero length data waiting */
/* more than vector length data waiting */

/* edge triggered waiting */

#if 0
START_TEST (test_recv_pass_001)
{
	const char* source[] = {
		"i am not a string",
		"i am not an iguana",
		"i am not a peach"
	};
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_apdu (source[0], 1+strlen(source[0]), 0, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	generate_apdu (source[2], 1+strlen(source[2]), 2, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	generate_apdu (source[1], 1+strlen(source[1]), 1, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	fail_unless ((gssize)(1+strlen(source[0])) == pgm_transport_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT));
	g_message ("buffer:\"%s\"", buffer);
	fail_unless ((gssize)(1+strlen(source[1])) == pgm_transport_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT));
	g_message ("buffer:\"%s\"", buffer);
	fail_unless ((gssize)(1+strlen(source[2])) == pgm_transport_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT));
	g_message ("buffer:\"%s\"", buffer);
}
END_TEST

/* force external cycle per packet */
START_TEST (test_recv_pass_002)
{
	const char* source[] = {
		"i am not a string",
		"i am not an iguana",
		"i am not a peach"
	};
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_apdu (source[0], 1+strlen(source[0]), 0, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	fail_unless ((gssize)(1+strlen(source[0])) == pgm_transport_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT));
	g_message ("buffer:\"%s\"", buffer);
	generate_apdu (source[2], 1+strlen(source[2]), 2, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
/* block */
	struct mock_recvmsg_t* mr = g_malloc (sizeof(struct mock_recvmsg_t));
	mr->mr_msg	= NULL;
	mr->mr_errno	= EAGAIN;
	mr->mr_retval	= -1;
	mock_recvmsg_list = g_list_append (mock_recvmsg_list, mr);
	fail_unless ((gssize)-1 == pgm_transport_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT));
	fail_unless (EAGAIN == errno);
	generate_apdu (source[1], 1+strlen(source[1]), 1, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	fail_unless ((gssize)(1+strlen(source[1])) == pgm_transport_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT));
	g_message ("buffer:\"%s\"", buffer);
	fail_unless ((gssize)(1+strlen(source[2])) == pgm_transport_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT));
	g_message ("buffer:\"%s\"", buffer);
}
END_TEST

START_TEST (test_recv_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	fail_unless (-1 == pgm_transport_recv (NULL, buffer, sizeof(buffer), 0));
}
END_TEST

/* target:
 *	gssize
 *	pgm_transport_recvfrom (
 *		pgm_transport_t*	transport,
 *		gpointer		data,
 *		gsize			len,
 *		int			flags,
 *		pgm_tsi_t*		from
 *		)
 */

START_TEST (test_recvfrom_pass_001)
{
	const char source[] = "i am not a string";
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	const pgm_tsi_t ref = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_tsi_t from;
	gpointer packet; gsize packet_len;
	generate_apdu (source, sizeof(source), 0, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	fail_unless ((gssize)sizeof(source) == pgm_transport_recvfrom (transport, buffer, sizeof(buffer), 0, &from));
	fail_unless (TRUE == pgm_tsi_equal (&from, &ref));
	g_message ("buffer:\"%s\" from:%s", buffer, pgm_tsi_print(&from));
}
END_TEST

START_TEST (test_recvfrom_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	pgm_tsi_t tsi;
	fail_unless (-1 == pgm_transport_recvfrom (NULL, buffer, sizeof(buffer), 0, &tsi));
}
END_TEST

/* target:
 *	gssize
 *	pgm_transport_recvmsg (
 *		pgm_transport_t*	transport,
 *		pgm_msgv_t*		msgv,
 *		int			flags
 *		)
 */

START_TEST (test_recvmsg_pass_001)
{
	const char source[] = "i am not a string";
	pgm_transport_t* transport = generate_transport();
	pgm_msgv_t msgv;
	const pgm_tsi_t ref = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	gpointer packet; gsize packet_len;
	generate_apdu (source, sizeof(source), 0, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	fail_unless ((gssize)sizeof(source) == pgm_transport_recvmsg (transport, &msgv, 0));
	fail_unless (1 == msgv.msgv_len);
	fail_unless (sizeof(source) == msgv.msgv_skb[0]->len);
}
END_TEST

START_TEST (test_recvmsg_fail_001)
{
	pgm_msgv_t msgv;
	fail_unless (-1 == pgm_transport_recvmsg (NULL, &msgv, 0));
}
END_TEST

/* target:
 *	gssize
 *	pgm_transport_recvmsgv (
 *		pgm_transport_t*	transport,
 *		pgm_msgv_t*		msgv,
 *		guint			msgv_length,
 *		int			flags
 *		)
 */

START_TEST (test_recvmsgv_pass_001)
{
	const char source[] = "i am not a string";
	pgm_transport_t* transport = generate_transport();
	pgm_msgv_t msgv[1];
	gpointer packet; gsize packet_len;
	generate_apdu (source, sizeof(source), 0, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	fail_unless ((gssize)sizeof(source) == pgm_transport_recvmsgv (transport, msgv, G_N_ELEMENTS(msgv), 0));
	fail_unless (1 == msgv[0].msgv_len);
	fail_unless (sizeof(source) == msgv[0].msgv_skb[0]->len);
}
END_TEST

START_TEST (test_recvmsgv_fail_001)
{
	pgm_msgv_t msgv[1];
	fail_unless (-1 == pgm_transport_recvmsgv (NULL, msgv, G_N_ELEMENTS(msgv), 0));
}
END_TEST
#endif


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_recv_data = tcase_create ("recv-data");
	suite_add_tcase (s, tc_recv_data);
	tcase_add_checked_fixture (tc_recv_data, mock_setup, NULL);
	tcase_add_test (tc_recv_data, test_recv_data_pass_001);

#if 0
	TCase* tc_recv = tcase_create ("recv");
	suite_add_tcase (s, tc_recv);
	tcase_add_checked_fixture (tc_recv, mock_setup, NULL);
	tcase_add_test (tc_recv, test_recv_pass_001);
	tcase_add_test (tc_recv, test_recv_pass_002);
	tcase_add_test (tc_recv, test_recv_fail_001);

	TCase* tc_recvfrom = tcase_create ("recvfrom");
	suite_add_tcase (s, tc_recvfrom);
	tcase_add_checked_fixture (tc_recvfrom, mock_setup, NULL);
	tcase_add_test (tc_recvfrom, test_recvfrom_fail_001);

	TCase* tc_recvmsg = tcase_create ("recvmsg");
	suite_add_tcase (s, tc_recvmsg);
	tcase_add_checked_fixture (tc_recvmsg, mock_setup, NULL);
	tcase_add_test (tc_recvmsg, test_recvmsg_pass_001);
	tcase_add_test (tc_recvmsg, test_recvmsg_fail_001);

	TCase* tc_recvmsgv = tcase_create ("recvmsgv");
	suite_add_tcase (s, tc_recvmsgv);
	tcase_add_checked_fixture (tc_recvmsgv, mock_setup, NULL);
	tcase_add_test (tc_recvmsgv, test_recvmsgv_pass_001);
	tcase_add_test (tc_recvmsgv, test_recvmsgv_fail_001);
#endif
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
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
