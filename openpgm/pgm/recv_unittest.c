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
#define PGM_DPORT		7500
#define PGM_SPORT		1000
#define PGM_SRC_ADDR		"127.0.0.1"
#define PGM_END_ADDR		"127.0.0.2"
#define PGM_GROUP_ADDR		"239.192.0.1"
#define PGM_PEER_ADDR		"127.0.0.6"
#define PGM_DLR_ADDR		"127.0.0.9"
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
static gboolean mock_reset_on_spmr = FALSE;
static gboolean mock_data_on_spmr = FALSE;
static pgm_peer_t* mock_peer = NULL;
GList* mock_data_list = NULL;

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
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, g_htons(PGM_SPORT) };
	struct pgm_transport_t* transport = g_malloc0 (sizeof(struct pgm_transport_t));
	memcpy (&transport->tsi, &tsi, sizeof(pgm_tsi_t));
	transport->is_bound = TRUE;
	transport->rx_buffer = pgm_alloc_skb (PGM_MAX_TPDU);
	transport->max_tpdu = PGM_MAX_TPDU;
	transport->rxw_sqns = PGM_RXW_SQNS;
	transport->dport = g_htons(PGM_DPORT);
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
struct pgm_sk_buff_t*
generate_packet (void)
{
	struct pgm_sk_buff_t* skb;

	skb = pgm_alloc_skb (PGM_MAX_TPDU);
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
	iphdr->ip_src.s_addr	= inet_addr (PGM_SRC_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (PGM_GROUP_ADDR);

/* add PGM header */
	struct pgm_header* pgmhdr = (gpointer)(iphdr + 1);
	pgmhdr->pgm_sport	= g_htons ((guint16)PGM_SPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)PGM_DPORT);
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
	iphdr->ip_src.s_addr	= inet_addr (PGM_END_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (PGM_SRC_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)PGM_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)PGM_SPORT);

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
	iphdr->ip_src.s_addr	= inet_addr (PGM_PEER_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (PGM_GROUP_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)PGM_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)PGM_SPORT);

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
	iphdr->ip_src.s_addr	= inet_addr (PGM_DLR_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (PGM_SRC_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)PGM_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)PGM_SPORT);

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
	iphdr->ip_src.s_addr	= inet_addr (PGM_END_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (PGM_SRC_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)PGM_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)PGM_SPORT);

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
	iphdr->ip_src.s_addr	= inet_addr (PGM_PEER_ADDR);
	iphdr->ip_dst.s_addr	= inet_addr (PGM_GROUP_ADDR);

/* update PGM header */
	struct pgm_header* pgmhdr = skb->pgm_header;
	pgmhdr->pgm_sport	= g_htons ((guint16)PGM_DPORT);
	pgmhdr->pgm_dport	= g_htons ((guint16)PGM_SPORT);

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
		.ipi_spec_dst		= iphdr->ip_src.s_addr,		/* local address */
		.ipi_addr		= iphdr->ip_dst.s_addr		/* destination address */
	};
	memcpy ((char*)(packet_cmsg + 1), &packet_info, sizeof(struct in_pktinfo));
	struct msghdr packet_msg = {
		.msg_name		= g_memdup (&addr, sizeof(addr)),	/* source address */
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
	if (mock_data_list) {
		gsize len = 0;
		guint count = 0;
		while (mock_data_list && *pmsg != msg_end) {
			pgm_msgv_t* mock_msgv = mock_data_list->data;
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
		if (*pmsg == msg_end)
			return -ENOBUFS;
	}
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
	g_assert (NULL != transport);
	g_assert (NULL != peer);
	if (peer->pending_link.data) return;
	peer->pending_link.data = peer;
	peer->pending_link.next = transport->peers_pending;
	transport->peers_pending = &peer->pending_link;
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
mock_pgm_on_deferred_nak (
	pgm_transport_t* const		transport
	)
{
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
	if (mock_reset_on_spmr) {
		transport->is_reset = TRUE;
		mock_pgm_peer_set_pending (transport, mock_peer);
	}
	if (mock_data_on_spmr) {
		mock_pgm_peer_set_pending (transport, mock_peer);
	}
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
	const struct sockaddr*		to,
	gsize				tolen
	)
{
	return len;
}

/** timer module */
static
gboolean
mock_pgm_timer_prepare (
	pgm_transport_t* const		transport
	)
{
	return FALSE;
}

static
gboolean
mock_pgm_timer_check (
	pgm_transport_t* const		transport
	)
{
	return FALSE;
}

static
long
mock_pgm_timer_expiration (
	pgm_transport_t* const		transport
	)
{
	return 100L;
}

static
gboolean
mock_pgm_timer_dispatch (
	pgm_transport_t* const		transport
	)
{
	return TRUE;
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
#define pgm_on_deferred_nak	mock_pgm_on_deferred_nak
#define pgm_on_peer_nak		mock_pgm_on_peer_nak
#define pgm_on_nnak		mock_pgm_on_nnak
#define pgm_on_ncf		mock_pgm_on_ncf
#define pgm_on_spmr		mock_pgm_on_spmr
#define pgm_sendto		mock_pgm_sendto
#define pgm_timer_prepare	mock_pgm_timer_prepare
#define pgm_timer_check		mock_pgm_timer_check
#define pgm_timer_expiration	mock_pgm_timer_expiration
#define pgm_timer_dispatch	mock_pgm_timer_dispatch
#define pgm_time_now		mock_pgm_time_now
#define pgm_time_update_now	mock_pgm_time_update_now
#define recvmsg			mock_recvmsg

#define RECV_DEBUG
#include "recv.c"
#undef g_trace


/* target:
 *	GIOStatus
 *	pgm_recv (
 *		pgm_transport_t*	transport,
 *		gpointer		data,
 *		gsize			len,
 *		int			flags,
 *		gsize*			bytes_read,
 *		GError**		error
 *		)
 */

/* recv -> on_data */
START_TEST (test_data_pass_001)
{
	const char source[] = "i am not a string";
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_odata (source, sizeof(source), 0 /* sqn */, -1 /* trail */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (PGM_ODATA == mock_pgm_type);
}
END_TEST

/* recv -> on_spm */
START_TEST (test_spm_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_spm (200 /* spm-sqn */, -1 /* trail */, 0 /* lead */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (PGM_SPM == mock_pgm_type);
}
END_TEST

/* recv -> on_nak */
START_TEST (test_nak_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_nak (0 /* sqn */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (PGM_NAK == mock_pgm_type);
}
END_TEST

/* recv -> on_peer_nak */
START_TEST (test_peer_nak_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_peer_nak (0 /* sqn */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (PGM_NAK == mock_pgm_type);
}
END_TEST

/* recv -> on_nnak */
START_TEST (test_nnak_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_nnak (0 /* sqn */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (PGM_NNAK == mock_pgm_type);
}
END_TEST

/* recv -> on_ncf */
START_TEST (test_ncf_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_ncf (0 /* sqn */, &packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (PGM_NCF == mock_pgm_type);
}
END_TEST

/* recv -> on_spmr */
START_TEST (test_spmr_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (PGM_SPMR == mock_pgm_type);
}
END_TEST

/* recv -> on (peer) spmr */
START_TEST (test_peer_spmr_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gpointer packet; gsize packet_len;
	generate_peer_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	push_block_event ();
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (PGM_SPMR == mock_pgm_type);
}
END_TEST

/* recv -> lost data */
START_TEST (test_lost_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	transport->is_reset = TRUE;
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_END_ADDR)
	};
	pgm_peer_t* peer = mock_pgm_new_peer (transport, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr));
	mock_pgm_peer_set_pending (transport, peer);
	push_block_event ();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_EOF == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	if (err) {
		g_message (err->message);
		g_error_free (err);
		err = NULL;
	}
	push_block_event ();
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
}
END_TEST

/* recv -> lost data and abort transport */
START_TEST (test_abort_on_lost_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	transport->is_reset = TRUE;
	transport->is_abort_on_reset = TRUE;
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_END_ADDR)
	};
	pgm_peer_t* peer = mock_pgm_new_peer (transport, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr));
	mock_pgm_peer_set_pending (transport, peer);
	push_block_event ();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_EOF == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	if (err) {
		g_message (err->message);
		g_error_free (err);
		err = NULL;
	}
	push_block_event ();
	fail_unless (G_IO_STATUS_EOF == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
}
END_TEST

/* recv -> (spmr) & loss */
START_TEST (test_then_lost_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	mock_reset_on_spmr = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (transport, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr));
	push_block_event ();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_EOF == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	if (err) {
		g_message (err->message);
		g_error_free (err);
		err = NULL;
	}
	push_block_event ();
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
}
END_TEST

/* recv -> data & loss & abort transport */
START_TEST (test_then_abort_on_lost_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	mock_reset_on_spmr = TRUE;
	transport->is_abort_on_reset = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (transport, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr));
	push_block_event ();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_EOF == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	if (err) {
		g_message (err->message);
		g_error_free (err);
		err = NULL;
	}
	push_block_event ();
	fail_unless (G_IO_STATUS_EOF == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
}
END_TEST

/* new waiting peer -> return data */
START_TEST (test_on_data_pass_001)
{
	const char source[] = "i am not a string";
	pgm_transport_t* transport = generate_transport();
	mock_data_on_spmr = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (transport, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr));
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (PGM_MAX_TPDU);
	pgm_skb_put (skb, sizeof(source));
	memcpy (skb->data, source, sizeof(source));
	pgm_msgv_t* msgv = g_malloc0 (sizeof(pgm_msgv_t));
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
	push_block_event ();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_NORMAL == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (NULL == err);
	fail_unless ((gsize)sizeof(source) == bytes_read);
	push_block_event ();
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
}
END_TEST

/* zero length data waiting */
START_TEST (test_on_zero_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	mock_data_on_spmr = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (transport, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr));
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (PGM_MAX_TPDU);
	pgm_msgv_t* msgv = g_malloc0 (sizeof(pgm_msgv_t));
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
	push_block_event ();
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_NORMAL == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (NULL == err);
	fail_unless ((gsize)0 == bytes_read);
	push_block_event ();
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
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
	pgm_transport_t* transport = generate_transport();
	mock_data_on_spmr = TRUE;
	gpointer packet; gsize packet_len;
	generate_spmr (&packet, &packet_len);
	generate_msghdr (packet, packet_len);
	const pgm_tsi_t peer_tsi = { { 9, 8, 7, 6, 5, 4 }, g_htons(9000) };
	struct sockaddr_in grp_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_GROUP_ADDR)
	}, peer_addr = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr(PGM_END_ADDR)
	};
	mock_peer = mock_pgm_new_peer (transport, &peer_tsi, (struct sockaddr*)&grp_addr, sizeof(grp_addr), (struct sockaddr*)&peer_addr, sizeof(peer_addr));
	struct pgm_sk_buff_t* skb;
	pgm_msgv_t* msgv;
/* #1 */
	msgv = g_malloc0 (sizeof(pgm_msgv_t));
	skb = pgm_alloc_skb (PGM_MAX_TPDU);
	pgm_skb_put (skb, strlen(source[0]) + 1);
	memcpy (skb->data, source[0], strlen(source[0]));
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
/* #2 */
	msgv = g_malloc0 (sizeof(pgm_msgv_t));
	skb = pgm_alloc_skb (PGM_MAX_TPDU);
	pgm_skb_put (skb, strlen(source[1]) + 1);
	memcpy (skb->data, source[1], strlen(source[1]));
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
/* #3 */
	msgv = g_malloc0 (sizeof(pgm_msgv_t));
	skb = pgm_alloc_skb (PGM_MAX_TPDU);
	pgm_skb_put (skb, strlen(source[2]) + 1);
	memcpy (skb->data, source[2], strlen(source[2]));
	msgv->msgv_len = 1;
	msgv->msgv_skb[0] = skb;
	mock_data_list = g_list_append (mock_data_list, msgv);
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gsize bytes_read;
	GError* err = NULL;
	fail_unless (G_IO_STATUS_NORMAL == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (NULL == err);
	fail_unless ((gsize)(strlen(source[0]) + 1) == bytes_read);
	g_message ("#1 = \"%s\"", buffer);
	fail_unless (G_IO_STATUS_NORMAL == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (NULL == err);
	fail_unless ((gsize)(strlen(source[1]) + 1) == bytes_read);
	g_message ("#2 = \"%s\"", buffer);
	fail_unless (G_IO_STATUS_NORMAL == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
	fail_unless (NULL == err);
	fail_unless ((gsize)(strlen(source[2]) + 1) == bytes_read);
	g_message ("#3 = \"%s\"", buffer);
	push_block_event ();
	fail_unless (G_IO_STATUS_AGAIN == pgm_recv (transport, buffer, sizeof(buffer), MSG_DONTWAIT, &bytes_read, &err));
}
END_TEST

START_TEST (test_recv_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	fail_unless (G_IO_STATUS_ERROR == pgm_recv (NULL, buffer, sizeof(buffer), 0, NULL, NULL));
}
END_TEST

/* target:
 *	GIOStatus
 *	pgm_recvfrom (
 *		pgm_transport_t*	transport,
 *		gpointer		data,
 *		gsize			len,
 *		int			flags,
 *		gsize*			bytes_read,
 *		pgm_tsi_t*		from,
 *		GError**		error
 *		)
 */

START_TEST (test_recvfrom_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	pgm_tsi_t tsi;
	fail_unless (G_IO_STATUS_ERROR == pgm_recvfrom (NULL, buffer, sizeof(buffer), 0, NULL, &tsi, NULL));
}
END_TEST

/* target:
 *	GIOStatus
 *	pgm_recvmsg (
 *		pgm_transport_t*	transport,
 *		pgm_msgv_t*		msgv,
 *		int			flags,
 *		gsize*			bytes_read,
 *		GError**		error
 *		)
 */

START_TEST (test_recvmsg_fail_001)
{
	pgm_msgv_t msgv;
	fail_unless (G_IO_STATUS_ERROR == pgm_recvmsg (NULL, &msgv, 0, NULL, NULL));
}
END_TEST

/* target:
 *	GIOStatus
 *	pgm_recvmsgv (
 *		pgm_transport_t*	transport,
 *		pgm_msgv_t*		msgv,
 *		guint			msgv_length,
 *		int			flags,
 *		gsize*			bytes_read,
 *		GError**		error
 *		)
 */

START_TEST (test_recvmsgv_fail_001)
{
	pgm_msgv_t msgv[1];
	fail_unless (G_IO_STATUS_ERROR == pgm_recvmsgv (NULL, msgv, G_N_ELEMENTS(msgv), 0, NULL, NULL));
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_data = tcase_create ("data");
	suite_add_tcase (s, tc_data);
	tcase_add_checked_fixture (tc_data, mock_setup, NULL);
	tcase_add_test (tc_data, test_data_pass_001);

	TCase* tc_spm = tcase_create ("spm");
	suite_add_tcase (s, tc_spm);
	tcase_add_checked_fixture (tc_spm, mock_setup, NULL);
	tcase_add_test (tc_spm, test_spm_pass_001);

	TCase* tc_nak = tcase_create ("nak");
	suite_add_tcase (s, tc_nak);
	tcase_add_checked_fixture (tc_nak, mock_setup, NULL);
	tcase_add_test (tc_nak, test_nak_pass_001);

	TCase* tc_peer_nak = tcase_create ("peer-nak");
	suite_add_tcase (s, tc_peer_nak);
	tcase_add_checked_fixture (tc_peer_nak, mock_setup, NULL);
	tcase_add_test (tc_peer_nak, test_peer_nak_pass_001);

	TCase* tc_nnak = tcase_create ("nnak");
	suite_add_tcase (s, tc_nnak);
	tcase_add_checked_fixture (tc_nnak, mock_setup, NULL);
	tcase_add_test (tc_nnak, test_nnak_pass_001);

	TCase* tc_ncf = tcase_create ("ncf");
	suite_add_tcase (s, tc_ncf);
	tcase_add_checked_fixture (tc_ncf, mock_setup, NULL);
	tcase_add_test (tc_ncf, test_ncf_pass_001);

	TCase* tc_spmr = tcase_create ("spmr");
	suite_add_tcase (s, tc_spmr);
	tcase_add_checked_fixture (tc_spmr, mock_setup, NULL);
	tcase_add_test (tc_spmr, test_spmr_pass_001);

	TCase* tc_peer_spmr = tcase_create ("peer-spmr");
	suite_add_tcase (s, tc_peer_spmr);
	tcase_add_checked_fixture (tc_peer_spmr, mock_setup, NULL);
	tcase_add_test (tc_peer_spmr, test_peer_spmr_pass_001);

	TCase* tc_lost = tcase_create ("lost");
	suite_add_tcase (s, tc_lost);
	tcase_add_checked_fixture (tc_lost, mock_setup, NULL);
	tcase_add_test (tc_lost, test_lost_pass_001);

	TCase* tc_abort_on_lost = tcase_create ("abort-on-lost");
	suite_add_tcase (s, tc_abort_on_lost);
	tcase_add_checked_fixture (tc_abort_on_lost, mock_setup, NULL);
	tcase_add_test (tc_abort_on_lost, test_abort_on_lost_pass_001);

	TCase* tc_then_lost = tcase_create ("then-lost");
	suite_add_tcase (s, tc_then_lost);
	tcase_add_checked_fixture (tc_then_lost, mock_setup, NULL);
	tcase_add_test (tc_then_lost, test_then_lost_pass_001);

	TCase* tc_then_abort_on_lost = tcase_create ("then-abort-on-lost");
	suite_add_tcase (s, tc_then_abort_on_lost);
	tcase_add_checked_fixture (tc_then_abort_on_lost, mock_setup, NULL);
	tcase_add_test (tc_then_abort_on_lost, test_then_abort_on_lost_pass_001);

	TCase* tc_on_data = tcase_create ("on-data");
	suite_add_tcase (s, tc_on_data);
	tcase_add_checked_fixture (tc_on_data, mock_setup, NULL);
	tcase_add_test (tc_on_data, test_on_data_pass_001);

	TCase* tc_on_zero = tcase_create ("on-zero");
	suite_add_tcase (s, tc_on_zero);
	tcase_add_checked_fixture (tc_on_zero, mock_setup, NULL);
	tcase_add_test (tc_on_zero, test_on_zero_pass_001);

	TCase* tc_on_many_data = tcase_create ("on-many-data");
	suite_add_tcase (s, tc_on_many_data);
	tcase_add_checked_fixture (tc_on_many_data, mock_setup, NULL);
	tcase_add_test (tc_on_many_data, test_on_many_data_pass_001);

	TCase* tc_recv = tcase_create ("recv");
	suite_add_tcase (s, tc_recv);
	tcase_add_checked_fixture (tc_recv, mock_setup, NULL);
	tcase_add_test (tc_recv, test_recv_fail_001);

	TCase* tc_recvfrom = tcase_create ("recvfrom");
	suite_add_tcase (s, tc_recvfrom);
	tcase_add_checked_fixture (tc_recvfrom, mock_setup, NULL);
	tcase_add_test (tc_recvfrom, test_recvfrom_fail_001);

	TCase* tc_recvmsg = tcase_create ("recvmsg");
	suite_add_tcase (s, tc_recvmsg);
	tcase_add_checked_fixture (tc_recvmsg, mock_setup, NULL);
	tcase_add_test (tc_recvmsg, test_recvmsg_fail_001);

	TCase* tc_recvmsgv = tcase_create ("recvmsgv");
	suite_add_tcase (s, tc_recvmsgv);
	tcase_add_checked_fixture (tc_recvmsgv, mock_setup, NULL);
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
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
