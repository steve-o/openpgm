/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM receiver transport.
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

#include <pgm/receiver.h>
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
	transport->is_open = TRUE;
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
	pgm_notify_init (&transport->waiting_notify);
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
	pgmhdr->pgm_checksum	= pgm_csum_fold (pgm_csum_partial (pgmhdr, sizeof(struct pgm_header) + sizeof(struct pgm_data) + source_len, 0));

/* and IP checksum */
	iphdr->ip_sum		= pgm_inet_checksum (skb->head, skb->len, 0);

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
pgm_peer_t*
generate_peer (void)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_peer_t* peer = g_malloc0 (sizeof(pgm_peer_t));
	peer->rxw = pgm_rxw_create (&tsi, PGM_MAX_TPDU, 0, 60, 10*1000);
	g_atomic_int_inc (&peer->ref_count);
	return peer;
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
gboolean
mock__pgm_on_nak (
	pgm_transport_t* const		transport,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock__pgm_on_nnak (
	pgm_transport_t* const		transport,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock__pgm_on_spmr (
	pgm_transport_t* const		transport,
	pgm_peer_t* const		peer,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

/** net module */
static
gssize
mock__pgm_sendto (
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
#define _pgm_on_nak		mock__pgm_on_nak
#define _pgm_on_nnak		mock__pgm_on_nnak
#define _pgm_on_spmr		mock__pgm_on_spmr
#define _pgm_sendto		mock__pgm_sendto
#define pgm_time_now		mock_pgm_time_now
#define pgm_time_update_now	mock_pgm_time_update_now
#define recvmsg			mock_recvmsg

#define RECEIVER_DEBUG
#include "receiver.c"
#undef g_trace

#include "rxwi.c"


/* target:
 *	gssize
 *	pgm_transport_recv (
 *		pgm_transport_t*	transport,
 *		gpointer		data,
 *		gsize			len,
 *		int			flags
 *		)
 */

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

/* target:
 *	void
 *	_pgm_peer_unref (
 *		pgm_peer_t*		peer
 *		)
 */

/* last ref */
START_TEST (test_peer_unref_pass_001)
{
	pgm_peer_t* peer = generate_peer();
	_pgm_peer_unref (peer);
}
END_TEST

/* non-last ref */
START_TEST (test_peer_unref_pass_002)
{
	pgm_peer_t* peer = pgm_peer_ref (generate_peer());
	_pgm_peer_unref (peer);
}
END_TEST


START_TEST (test_peer_unref_fail_001)
{
	_pgm_peer_unref (NULL);
	fail ();
}
END_TEST

/* target:
 *	void
 *	_pgm_check_peer_nak_state (
 *		pgm_transport_t*	transport
 *		)
 */

START_TEST (test_check_peer_nak_state_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	_pgm_check_peer_nak_state (transport);
}
END_TEST

START_TEST (test_check_peer_nak_state_fail_001)
{
	_pgm_check_peer_nak_state (NULL);
	fail ();
}
END_TEST

/* target:
 *	pgm_time_t
 *	_pgm_min_nak_expiry (
 *		pgm_time_t		expiration,
 *		pgm_transport_t*	transport
 *		)
 */

START_TEST (test_min_nak_expiry_pass_001)
{
	pgm_transport_t* transport = generate_transport();
	const pgm_time_t expiration = pgm_secs(1);
	pgm_time_t next_expiration = _pgm_min_nak_expiry (expiration, transport);
}
END_TEST

START_TEST (test_min_nak_expiry_fail_001)
{
	const pgm_time_t expiration = pgm_secs(1);
	_pgm_min_nak_expiry (expiration, NULL);
	fail ();
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_rxw_sqns (
 *		pgm_transport_t*	transport,
 *		const guint		sqns
 *	)
 */

START_TEST (test_set_rxw_sqns_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	fail_unless (TRUE == pgm_transport_set_rxw_sqns (transport, 100));
}
END_TEST

START_TEST (test_set_rxw_sqns_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_rxw_sqns (NULL, 100));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_rxw_secs (
 *		pgm_transport_t*	transport,
 *		const guint		secs
 *	)
 */

START_TEST (test_set_rxw_secs_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	fail_unless (TRUE == pgm_transport_set_rxw_secs (transport, 10));
}
END_TEST

START_TEST (test_set_rxw_secs_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_rxw_secs (NULL, 10));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_rxw_max_rte (
 *		pgm_transport_t*	transport,
 *		const guint		rate
 *	)
 */

START_TEST (test_set_rxw_max_rte_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	fail_unless (TRUE == pgm_transport_set_rxw_max_rte (transport, 100*1000));
}
END_TEST

START_TEST (test_set_rxw_max_rte_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_rxw_max_rte (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_peer_expiry (
 *		pgm_transport_t*	transport,
 *		const guint		expiration_time
 *	)
 */

START_TEST (test_set_peer_expiry_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	transport->spm_ambient_interval = pgm_secs(30);
	fail_unless (TRUE == pgm_transport_set_peer_expiry (transport, pgm_secs(100)));
}
END_TEST

START_TEST (test_set_peer_expiry_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_peer_expiry (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_spmr_expiry (
 *		pgm_transport_t*	transport,
 *		const guint		expiration_time
 *	)
 */

START_TEST (test_set_spmr_expiry_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	transport->spm_ambient_interval = pgm_secs(30);
	fail_unless (TRUE == pgm_transport_set_spmr_expiry (transport, pgm_secs(10)));
}
END_TEST

START_TEST (test_set_spmr_expiry_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_spmr_expiry (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_bo_ivl (
 *		pgm_transport_t*	transport,
 *		const guint		interval
 *	)
 */

START_TEST (test_set_nak_bo_ivl_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	fail_unless (TRUE == pgm_transport_set_nak_bo_ivl (transport, 100*1000));
}
END_TEST

START_TEST (test_set_nak_bo_ivl_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_bo_ivl (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_rpt_ivl (
 *		pgm_transport_t*	transport,
 *		const guint		interval
 *	)
 */

START_TEST (test_set_nak_rpt_ivl_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	fail_unless (TRUE == pgm_transport_set_nak_rpt_ivl (transport, 100*1000));
}
END_TEST

START_TEST (test_set_nak_rpt_ivl_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_rpt_ivl (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_rdata_ivl (
 *		pgm_transport_t*	transport,
 *		const guint		interval
 *	)
 */

START_TEST (test_set_nak_rdata_ivl_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	fail_unless (TRUE == pgm_transport_set_nak_rdata_ivl (transport, 100*1000));
}
END_TEST

START_TEST (test_set_nak_rdata_ivl_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_rdata_ivl (NULL, 100*1000));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_data_retries (
 *		pgm_transport_t*	transport,
 *		const guint		cnt
 *	)
 */

START_TEST (test_set_nak_data_retries_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	fail_unless (TRUE == pgm_transport_set_nak_data_retries (transport, 100));
}
END_TEST

START_TEST (test_set_nak_data_retries_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_data_retries (NULL, 100));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_nak_ncf_retries (
 *		pgm_transport_t*	transport,
 *		const guint		cnt
 *	)
 */

START_TEST (test_set_nak_ncf_retries_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	transport->is_bound = FALSE;
	fail_unless (TRUE == pgm_transport_set_nak_ncf_retries (transport, 100));
}
END_TEST

START_TEST (test_set_nak_ncf_retries_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_nak_ncf_retries (NULL, 100));
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_recv = tcase_create ("recv");
	suite_add_tcase (s, tc_recv);
	tcase_add_checked_fixture (tc_recv, mock_setup, NULL);
	tcase_add_test (tc_recv, test_recv_pass_001);
	tcase_add_test (tc_recv, test_recv_pass_002);
	tcase_add_test (tc_recv, test_recv_fail_001);

	TCase* tc_recvfrom = tcase_create ("recvfrom");
	suite_add_tcase (s, tc_recvfrom);
	tcase_add_checked_fixture (tc_recvfrom, mock_setup, NULL);
	tcase_add_test (tc_recvfrom, test_recvfrom_pass_001);
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

	TCase* tc_peer_unref = tcase_create ("peer_unref");
	suite_add_tcase (s, tc_peer_unref);
	tcase_add_checked_fixture (tc_peer_unref, mock_setup, NULL);
	tcase_add_test (tc_peer_unref, test_peer_unref_pass_001);
	tcase_add_test_raise_signal (tc_peer_unref, test_peer_unref_fail_001, SIGABRT);

	TCase* tc_check_peer_nak_state = tcase_create ("check-peer-nak-state");
	suite_add_tcase (s, tc_check_peer_nak_state);
	tcase_add_checked_fixture (tc_check_peer_nak_state, mock_setup, NULL);
	tcase_add_test (tc_check_peer_nak_state, test_check_peer_nak_state_pass_001);
	tcase_add_test_raise_signal (tc_check_peer_nak_state, test_check_peer_nak_state_fail_001, SIGABRT);

	TCase* tc_min_nak_expiry = tcase_create ("min-nak-expiry");
	suite_add_tcase (s, tc_min_nak_expiry);
	tcase_add_checked_fixture (tc_min_nak_expiry, mock_setup, NULL);
	tcase_add_test (tc_min_nak_expiry, test_min_nak_expiry_pass_001);
	tcase_add_test_raise_signal (tc_min_nak_expiry, test_min_nak_expiry_fail_001, SIGABRT);

	TCase* tc_set_rxw_sqns = tcase_create ("set-rxw_sqns");
	suite_add_tcase (s, tc_set_rxw_sqns);
	tcase_add_checked_fixture (tc_set_rxw_sqns, mock_setup, NULL);
	tcase_add_test (tc_set_rxw_sqns, test_set_rxw_sqns_pass_001);
	tcase_add_test (tc_set_rxw_sqns, test_set_rxw_sqns_fail_001);

	TCase* tc_set_rxw_secs = tcase_create ("set-rxw-secs");
	suite_add_tcase (s, tc_set_rxw_secs);
	tcase_add_checked_fixture (tc_set_rxw_secs, mock_setup, NULL);
	tcase_add_test (tc_set_rxw_secs, test_set_rxw_secs_pass_001);
	tcase_add_test (tc_set_rxw_secs, test_set_rxw_secs_fail_001);

	TCase* tc_set_rxw_max_rte = tcase_create ("set-rxw-max-rte");
	suite_add_tcase (s, tc_set_rxw_max_rte);
	tcase_add_checked_fixture (tc_set_rxw_max_rte, mock_setup, NULL);
	tcase_add_test (tc_set_rxw_max_rte, test_set_rxw_max_rte_pass_001);
	tcase_add_test (tc_set_rxw_max_rte, test_set_rxw_max_rte_fail_001);

	TCase* tc_set_peer_expiry = tcase_create ("set-peer-expiry");
	suite_add_tcase (s, tc_set_peer_expiry);
	tcase_add_checked_fixture (tc_set_peer_expiry, mock_setup, NULL);
	tcase_add_test (tc_set_peer_expiry, test_set_peer_expiry_pass_001);
	tcase_add_test (tc_set_peer_expiry, test_set_peer_expiry_fail_001);

	TCase* tc_set_spmr_expiry = tcase_create ("set-spmr-expiry");
	suite_add_tcase (s, tc_set_spmr_expiry);
	tcase_add_checked_fixture (tc_set_spmr_expiry, mock_setup, NULL);
	tcase_add_test (tc_set_spmr_expiry, test_set_spmr_expiry_pass_001);
	tcase_add_test (tc_set_spmr_expiry, test_set_spmr_expiry_fail_001);

	TCase* tc_set_nak_bo_ivl = tcase_create ("set-nak-bo-ivl");
	suite_add_tcase (s, tc_set_nak_bo_ivl);
	tcase_add_checked_fixture (tc_set_nak_bo_ivl, mock_setup, NULL);
	tcase_add_test (tc_set_nak_bo_ivl, test_set_nak_bo_ivl_pass_001);
	tcase_add_test (tc_set_nak_bo_ivl, test_set_nak_bo_ivl_fail_001);

	TCase* tc_set_nak_rpt_ivl = tcase_create ("set-nak-rpt-ivl");
	suite_add_tcase (s, tc_set_nak_rpt_ivl);
	tcase_add_checked_fixture (tc_set_nak_rpt_ivl, mock_setup, NULL);
	tcase_add_test (tc_set_nak_rpt_ivl, test_set_nak_rpt_ivl_pass_001);
	tcase_add_test (tc_set_nak_rpt_ivl, test_set_nak_rpt_ivl_fail_001);

	TCase* tc_set_nak_rdata_ivl = tcase_create ("set-nak-rdata-ivl");
	suite_add_tcase (s, tc_set_nak_rdata_ivl);
	tcase_add_checked_fixture (tc_set_nak_rdata_ivl, mock_setup, NULL);
	tcase_add_test (tc_set_nak_rdata_ivl, test_set_nak_rdata_ivl_pass_001);
	tcase_add_test (tc_set_nak_rdata_ivl, test_set_nak_rdata_ivl_fail_001);

	TCase* tc_set_nak_data_retries = tcase_create ("set-nak-data-retries");
	suite_add_tcase (s, tc_set_nak_data_retries);
	tcase_add_checked_fixture (tc_set_nak_data_retries, mock_setup, NULL);
	tcase_add_test (tc_set_nak_data_retries, test_set_nak_data_retries_pass_001);
	tcase_add_test (tc_set_nak_data_retries, test_set_nak_data_retries_fail_001);

	TCase* tc_set_nak_ncf_retries = tcase_create ("set-nak-ncf-retries");
	suite_add_tcase (s, tc_set_nak_ncf_retries);
	tcase_add_checked_fixture (tc_set_nak_ncf_retries, mock_setup, NULL);
	tcase_add_test (tc_set_nak_ncf_retries, test_set_nak_ncf_retries_pass_001);
	tcase_add_test (tc_set_nak_ncf_retries, test_set_nak_ncf_retries_fail_001);
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
