/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM source transport.
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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <glib.h>
#include <check.h>


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

static gboolean mock_is_valid_spmr = TRUE;
static gboolean mock_is_valid_nak = TRUE;
static gboolean mock_is_valid_nnak = TRUE;

static size_t mock_pgm_transport_pkt_offset (const bool can_fragment);


#define pgm_txw_get_unfolded_checksum	mock_pgm_txw_get_unfolded_checksum
#define pgm_txw_set_unfolded_checksum	mock_pgm_txw_set_unfolded_checksum
#define pgm_txw_inc_retransmit_count	mock_pgm_txw_inc_retransmit_count
#define pgm_txw_add			mock_pgm_txw_add
#define pgm_txw_peek			mock_pgm_txw_peek
#define pgm_txw_retransmit_push		mock_pgm_txw_retransmit_push
#define pgm_txw_retransmit_try_peek	mock_pgm_txw_retransmit_try_peek
#define pgm_txw_retransmit_remove_head	mock_pgm_txw_retransmit_remove_head
#define pgm_rs_encode			mock_pgm_rs_encode
#define pgm_rate_check			mock_pgm_rate_check
#define pgm_verify_spmr			mock_pgm_verify_spmr
#define pgm_verify_nak			mock_pgm_verify_nak
#define pgm_verify_nnak			mock_pgm_verify_nnak
#define pgm_compat_csum_partial		mock_pgm_compat_csum_partial
#define pgm_compat_csum_partial_copy	mock_pgm_compat_csum_partial_copy
#define pgm_csum_block_add		mock_pgm_csum_block_add
#define pgm_csum_fold			mock_pgm_csum_fold
#define pgm_sendto			mock_pgm_sendto
#define pgm_time_update_now		mock_pgm_time_update_now
#define pgm_transport_pkt_offset	mock_pgm_transport_pkt_offset


#define SOURCE_DEBUG
#include "source.c"


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
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, g_htons(1000) };
	struct pgm_transport_t* transport = g_new0 (struct pgm_transport_t, 1);
	memcpy (&transport->tsi, &tsi, sizeof(pgm_tsi_t));
	((struct sockaddr*)&transport->send_addr)->sa_family = AF_INET;
	((struct sockaddr_in*)&transport->send_addr)->sin_addr.s_addr = inet_addr ("127.0.0.2");
	((struct sockaddr*)&transport->send_gsr.gsr_group)->sa_family = AF_INET;
	((struct sockaddr_in*)&transport->send_gsr.gsr_group)->sin_addr.s_addr = inet_addr ("239.192.0.1");
	transport->dport = g_htons(PGM_PORT);
	transport->window = g_malloc0 (sizeof(pgm_txw_t));
	transport->txw_sqns = PGM_TXW_SQNS;
	transport->max_tpdu = PGM_MAX_TPDU;
	transport->max_tsdu = PGM_MAX_TPDU - sizeof(struct pgm_ip) - mock_pgm_transport_pkt_offset (FALSE);
	transport->max_tsdu_fragment = PGM_MAX_TPDU - sizeof(struct pgm_ip) - mock_pgm_transport_pkt_offset (TRUE);
	transport->max_apdu = MIN(PGM_TXW_SQNS, PGM_MAX_FRAGMENTS) * transport->max_tsdu_fragment;
	transport->iphdr_len = sizeof(struct pgm_ip);
	transport->spm_heartbeat_interval = g_malloc0 (sizeof(guint) * (2+2));
	transport->spm_heartbeat_interval[0] = pgm_secs(1);
	pgm_spinlock_init (&transport->txw_spinlock);
	transport->is_bound = FALSE;
	transport->is_destroyed = FALSE;
	return transport;
}

static
struct pgm_sk_buff_t*
generate_skb (void)
{
	const char source[] = "i am not a string";
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (PGM_MAX_TPDU);
	pgm_skb_reserve (skb, mock_pgm_transport_pkt_offset (FALSE));
	pgm_skb_put (skb, sizeof(source));
	memcpy (skb->data, source, sizeof(source));
	return skb;
}

static
struct pgm_sk_buff_t*
generate_fragment_skb (void)
{
	const char source[] = "i am not a string";
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (PGM_MAX_TPDU);
	pgm_skb_reserve (skb, mock_pgm_transport_pkt_offset (TRUE));
	pgm_skb_put (skb, sizeof(source));
	memcpy (skb->data, source, sizeof(source));
	return skb;
}

static
struct pgm_sk_buff_t*
generate_odata (void)
{
	const char source[] = "i am not a string";
	const guint16 header_length = sizeof(struct pgm_header) + sizeof(struct pgm_data);
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (PGM_MAX_TPDU);
	pgm_skb_reserve (skb, header_length);
	memset (skb->head, 0, header_length);
	skb->pgm_header = (struct pgm_header*)skb->head;
	skb->pgm_data   = (struct pgm_data*)(skb->pgm_header + 1);
	skb->pgm_header->pgm_type = PGM_ODATA;
	skb->pgm_header->pgm_tsdu_length = g_htons (sizeof(source));
	memcpy (skb->data, source, sizeof(source));
	pgm_skb_put (skb, sizeof(source));
/* reverse pull */
	skb->len += (guint8*)skb->data - (guint8*)skb->head;
	skb->data = skb->head;
	return skb;
}

static
pgm_peer_t*
generate_peer (void)
{
	pgm_peer_t* peer = g_malloc0 (sizeof(pgm_peer_t));
	return peer;
}

static
struct pgm_sk_buff_t*
generate_spmr (void)
{
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (PGM_MAX_TPDU);
	const guint16 header_length = sizeof(struct pgm_header);
	pgm_skb_reserve (skb, header_length);
	memset (skb->head, 0, header_length);
	skb->pgm_header = (struct pgm_header*)skb->head;
	skb->pgm_header->pgm_type = PGM_SPMR;
	pgm_skb_put (skb, header_length);
	return skb;
}

static
struct pgm_sk_buff_t*
generate_single_nak (void)
{
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (PGM_MAX_TPDU);
	const guint16 header_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak);
	pgm_skb_reserve (skb, sizeof(struct pgm_header));
	memset (skb->head, 0, header_length);
	skb->pgm_header = (struct pgm_header*)skb->head;
	skb->pgm_header->pgm_type = PGM_NAK;
	struct pgm_nak* nak = (struct pgm_nak*)(skb->pgm_header + 1);
	struct sockaddr_in nla = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr("127.0.0.2")
	};
	pgm_sockaddr_to_nla ((struct sockaddr*)&nla, (char*)&nak->nak_src_nla_afi);
	struct sockaddr_in group = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr("239.192.0.1")
	};
	pgm_sockaddr_to_nla ((struct sockaddr*)&group, (char*)&nak->nak_grp_nla_afi);
	pgm_skb_put (skb, header_length);
	return skb;
}

static
struct pgm_sk_buff_t*
generate_single_nnak (void)
{
	struct pgm_sk_buff_t* skb = generate_single_nak ();
	skb->pgm_header->pgm_type = PGM_NNAK;
	return skb;
}

static
struct pgm_sk_buff_t*
generate_parity_nak (void)
{
	struct pgm_sk_buff_t* skb = generate_single_nak ();
	skb->pgm_header->pgm_options = PGM_OPT_PARITY;
	return skb;
}

static
struct pgm_sk_buff_t*
generate_nak_list (void)
{
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (PGM_MAX_TPDU);
	const guint16 header_length = sizeof(struct pgm_header) + sizeof(struct pgm_nak) +
				      sizeof(struct pgm_opt_length) +
				      sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list) +
				      ( 62 * sizeof(guint32) );
	pgm_skb_reserve (skb, sizeof(struct pgm_header));
	memset (skb->head, 0, header_length);
	skb->pgm_header = (struct pgm_header*)skb->head;
	skb->pgm_header->pgm_type = PGM_NAK;
	skb->pgm_header->pgm_options = PGM_OPT_PRESENT | PGM_OPT_NETWORK;
	struct pgm_nak *nak = (struct pgm_nak*)(skb->pgm_header + 1);
	struct sockaddr_in nla = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr("127.0.0.2")
	};
	pgm_sockaddr_to_nla ((struct sockaddr*)&nla, (char*)&nak->nak_src_nla_afi);
	struct sockaddr_in group = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= inet_addr("239.192.0.1")
	};
	pgm_sockaddr_to_nla ((struct sockaddr*)&group, (char*)&nak->nak_grp_nla_afi);
	struct pgm_opt_length* opt_len = (struct pgm_opt_length*)(nak + 1);
	opt_len->opt_type = PGM_OPT_LENGTH;
	opt_len->opt_length = sizeof(struct pgm_opt_length);
	opt_len->opt_total_length = g_htons (   sizeof(struct pgm_opt_length) +
						sizeof(struct pgm_opt_header) +
						sizeof(struct pgm_opt_nak_list) +
						( 62 * sizeof(guint32) ) );
	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)(opt_len + 1);
	opt_header->opt_type = PGM_OPT_NAK_LIST | PGM_OPT_END;
	opt_header->opt_length = sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_nak_list) +
				 ( 62 * sizeof(guint32) );
	struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
	for (unsigned i = 1; i < 63; i++) {
		opt_nak_list->opt_sqn[i-1] = g_htonl (i);
	}
	pgm_skb_put (skb, header_length);
	return skb;
}

static
struct pgm_sk_buff_t*
generate_parity_nak_list (void)
{
	struct pgm_sk_buff_t* skb = generate_nak_list ();
	skb->pgm_header->pgm_options = PGM_OPT_PARITY | PGM_OPT_PRESENT | PGM_OPT_NETWORK;
	return skb;
}

void
mock_pgm_txw_add (
	pgm_txw_t* const		window,
	struct pgm_sk_buff_t* const	skb
	)
{
	g_debug ("mock_pgm_txw_add (window:%p skb:%p)",
		(gpointer)window, (gpointer)skb);
}

struct pgm_sk_buff_t*
mock_pgm_txw_peek (
	const pgm_txw_t* const		window,
	const uint32_t			sequence
	)
{
	g_debug ("mock_pgm_txw_peek (window:%p sequence:%" G_GUINT32_FORMAT ")",
		(gpointer)window, sequence);
	return NULL;
}

bool
mock_pgm_txw_retransmit_push (
	pgm_txw_t* const		window,
	const uint32_t			sequence,
	const bool			is_parity,
	const uint8_t			tg_sqn_shift
	)
{
	g_debug ("mock_pgm_txw_retransmit_push (window:%p sequence:%" G_GUINT32_FORMAT " is-parity:%s tg-sqn-shift:%d)",
		(gpointer)window,
		sequence,
		is_parity ? "YES" : "NO",
		tg_sqn_shift);
	return TRUE;
}

void
mock_pgm_txw_set_unfolded_checksum (
	struct pgm_sk_buff_t*const skb,
	const uint32_t		csum
	)
{
}

uint32_t
pgm_txw_get_unfolded_checksum (
	struct pgm_sk_buff_t*const skb
	)
{
	return 0;
}

void
mock_pgm_txw_inc_retransmit_count (
	struct pgm_sk_buff_t*const skb
	)
{
}

struct pgm_sk_buff_t*
mock_pgm_txw_retransmit_try_peek (
	pgm_txw_t* const		window
	)
{
	g_debug ("mock_pgm_txw_retransmit_try_peek (window:%p)",
		(gpointer)window);
	return generate_odata (); 
}

void
mock_pgm_txw_retransmit_remove_head (
	pgm_txw_t* const		window
	)
{
	g_debug ("mock_pgm_txw_retransmit_remove_head (window:%p)",
		(gpointer)window);
}

void
mock_pgm_rs_encode (
	pgm_rs_t*			rs,
	const pgm_gf8_t**		src,
	uint8_t				offset,
	pgm_gf8_t*			dst,
	uint16_t			len
	)
{
	g_debug ("mock_pgm_rs_encode (rs:%p src:%p offset:%u dst:%p len:%" G_GSIZE_FORMAT ")",
		rs, src, offset, dst, len);
}

PGM_GNUC_INTERNAL
bool
mock_pgm_rate_check (
	pgm_rate_t*			bucket,
	const size_t			data_size,
	const bool			is_nonblocking
	)
{
	g_debug ("mock_pgm_rate_check (bucket:%p data-size:%u is-nonblocking:%s)",
		bucket, data_size, is_nonblocking ? "TRUE" : "FALSE");
	return TRUE;
}

bool
mock_pgm_verify_spmr (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return mock_is_valid_spmr;
}

bool
mock_pgm_verify_nak (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return mock_is_valid_nak;
}

bool
mock_pgm_verify_nnak (
	const struct pgm_sk_buff_t* const	skb
	)
{
	return mock_is_valid_nnak;
}

uint32_t
mock_pgm_compat_csum_partial (
	const void*			addr,
	uint16_t			len,
	uint32_t			csum
	)
{
	return 0x0;
}

uint32_t
mock_pgm_compat_csum_partial_copy (
	const void*			src,
	void*				dst,
	uint16_t			len,
	uint32_t			csum
	)
{
	return 0x0;
}

uint32_t
mock_pgm_csum_block_add (
	uint32_t			csum,
	uint32_t			csum2,
	uint16_t			offset
	)
{
	return 0x0;
}

uint16_t
mock_pgm_csum_fold (
	uint32_t			csum
	)
{
	return 0x0;
}

PGM_GNUC_INTERNAL
ssize_t
mock_pgm_sendto (
	pgm_transport_t*		transport,
	bool				use_rate_limit,
	bool				use_router_alert,
	const void*			buf,
	size_t				len,
	const struct sockaddr*		to,
	socklen_t			tolen
	)
{
	char saddr[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop (to, saddr, sizeof(saddr));
	g_debug ("mock_pgm_sendto (transport:%p use-rate-limit:%s use-router-alert:%s buf:%p len:%d to:%s tolen:%d)",
		(gpointer)transport,
		use_rate_limit ? "YES" : "NO",
		use_router_alert ? "YES" : "NO",
		buf,
		len,
		saddr,
		tolen);
	return len;
}

/** time module */
static pgm_time_t _mock_pgm_time_update_now (void);
pgm_time_update_func mock_pgm_time_update_now = _mock_pgm_time_update_now;

static
pgm_time_t
_mock_pgm_time_update_now (void)
{
	return 0x1;
}

/** transport module */
static
size_t
mock_pgm_transport_pkt_offset (
	const bool			can_fragment
	)
{
	return can_fragment ? ( sizeof(struct pgm_header)
			      + sizeof(struct pgm_data)
			      + sizeof(struct pgm_opt_length)
	                      + sizeof(struct pgm_opt_header)
			      + sizeof(struct pgm_opt_fragment) )
			    : ( sizeof(struct pgm_header) + sizeof(struct pgm_data) );
}


/* mock functions for external references */


/* target:
 *	PGMIOStatus
 *	pgm_send (
 *		pgm_transport_t*	transport,
 *		gconstpointer		apdu,
 *		gsize			apdu_length,
 *		gsize*			bytes_written
 *		)
 */

START_TEST (test_send_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->is_bound = TRUE;
	const gsize apdu_length = 100;
	guint8 buffer[ apdu_length ];
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_send (transport, buffer, apdu_length, &bytes_written), "send not normal");
	fail_unless ((gssize)apdu_length == bytes_written, "send underrun");
}
END_TEST

/* large apdu */
START_TEST (test_send_pass_002)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->is_bound = TRUE;
	const gsize apdu_length = 16000;
	guint8 buffer[ apdu_length ];
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_send (transport, buffer, apdu_length, &bytes_written), "send not normal");
	fail_unless ((gssize)apdu_length == bytes_written, "send underrun");
}
END_TEST

START_TEST (test_send_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	const gsize apdu_length = 100;
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_ERROR == pgm_send (NULL, buffer, apdu_length, &bytes_written), "send not error");
}
END_TEST

/* target:
 *	PGMIOStatus
 *	pgm_sendv (
 *		pgm_transport_t*	transport,
 *		const struct pgmiovec*	vector,
 *		guint			count,
 *		gboolean		is_one_apdu,
 *		gsize*			bytes_written
 *		)
 */

START_TEST (test_sendv_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->is_bound = TRUE;
	const gsize apdu_length = 100;
	guint8 buffer[ apdu_length ];
	struct pgm_iovec vector[] = { { .iov_base = buffer, .iov_len = apdu_length } };
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_sendv (transport, vector, 1, TRUE, &bytes_written), "send not normal");
	fail_unless ((gssize)apdu_length == bytes_written, "send underrun");
}
END_TEST

/* large apdu */
START_TEST (test_sendv_pass_002)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->is_bound = TRUE;
	const gsize apdu_length = 16000;
	guint8 buffer[ apdu_length ];
	struct pgm_iovec vector[] = { { .iov_base = buffer, .iov_len = apdu_length } };
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_sendv (transport, vector, 1, TRUE, &bytes_written), "send not normal");
	fail_unless ((gssize)apdu_length == bytes_written, "send underrun");
}
END_TEST

/* multipart apdu */
START_TEST (test_sendv_pass_003)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->is_bound = TRUE;
	const gsize apdu_length = 16000;
	guint8 buffer[ apdu_length ];
	struct pgm_iovec vector[ 16 ];
	for (unsigned i = 0; i < G_N_ELEMENTS(vector); i++) {
		vector[i].iov_base = &buffer[ (i * apdu_length) / G_N_ELEMENTS(vector) ];
		vector[i].iov_len  = apdu_length / G_N_ELEMENTS(vector);
	}
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_sendv (transport, vector, G_N_ELEMENTS(vector), TRUE, &bytes_written), "send not normal");
	fail_unless ((gssize)apdu_length == bytes_written, "send underrun");
}
END_TEST

/* multiple apdus */
START_TEST (test_sendv_pass_004)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->is_bound = TRUE;
	const gsize apdu_length = 16000;
	struct pgm_iovec vector[ 16 ];
	for (unsigned i = 0; i < G_N_ELEMENTS(vector); i++) {
		vector[i].iov_base = g_malloc0 (apdu_length);
		vector[i].iov_len  = apdu_length;
	}
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_sendv (transport, vector, G_N_ELEMENTS(vector), FALSE, &bytes_written), "send not normal");
	fail_unless ((gssize)(apdu_length * G_N_ELEMENTS(vector)) == bytes_written, "send underrun");
}
END_TEST

START_TEST (test_sendv_fail_001)
{
	guint8 buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	const gsize tsdu_length = 100;
	struct pgm_iovec vector[] = { { .iov_base = buffer, .iov_len = tsdu_length } };
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_ERROR == pgm_sendv (NULL, vector, 1, TRUE, &bytes_written), "send not error");
}
END_TEST

/* target:
 *	PGMIOStatus
 *	pgm_send_skbv (
 *		pgm_transport_t*	transport,
 *		struct pgm_sk_buff_t*	vector[],
 *		guint			count,
 *		gboolean		is_one_apdu,
 *		gsize*			bytes_written
 *		)
 */

START_TEST (test_send_skbv_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->is_bound = TRUE;
	struct pgm_sk_buff_t* skb = NULL;
	skb = generate_skb ();
	fail_if (NULL == skb, "generate_skb failed");
	gsize apdu_length = (gsize)skb->len;
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_send_skbv (transport, &skb, 1, TRUE, &bytes_written), "send not normal");
	fail_unless (apdu_length == bytes_written, "send underrun");
}
END_TEST

/* multipart apdu */
START_TEST (test_send_skbv_pass_002)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->is_bound = TRUE;
	struct pgm_sk_buff_t* skb[16];
	for (unsigned i = 0; i < G_N_ELEMENTS(skb); i++) {
		skb[i] = generate_fragment_skb ();
		fail_if (NULL == skb[i], "generate_fragment_skb failed");
	}
	gsize apdu_length = (gsize)skb[0]->len * G_N_ELEMENTS(skb);
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_send_skbv (transport, skb, G_N_ELEMENTS(skb), TRUE, &bytes_written), "send not normal");
	fail_unless (apdu_length == bytes_written, "send underrun");
}
END_TEST

/* multiple apdus */
START_TEST (test_send_skbv_pass_003)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->is_bound = TRUE;
	struct pgm_sk_buff_t* skb[16];
	for (unsigned i = 0; i < G_N_ELEMENTS(skb); i++) {
		skb[i] = generate_skb ();
		fail_if (NULL == skb[i], "generate_skb failed");
	}
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_NORMAL == pgm_send_skbv (transport, skb, G_N_ELEMENTS(skb), FALSE, &bytes_written), "send not normal");
	fail_unless ((gssize)(skb[0]->len * G_N_ELEMENTS(skb)) == bytes_written, "send underrun");
}
END_TEST

START_TEST (test_send_skbv_fail_001)
{
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (PGM_MAX_TPDU);
	fail_if (NULL == skb, "alloc_skb failed");
/* reserve PGM header */
	pgm_skb_put (skb, mock_pgm_transport_pkt_offset (TRUE));
	const gsize tsdu_length = 100;
	gsize bytes_written;
	fail_unless (PGM_IO_STATUS_ERROR == pgm_send_skbv (NULL, skb, 1, TRUE, &bytes_written), "send not error");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_send_spm (
 *		pgm_transport_t*	transport,
 *		int			flags
 *		)
 */

START_TEST (test_send_spm_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	fail_unless (TRUE == pgm_send_spm (transport, 0), "send_spm failed");
}
END_TEST

START_TEST (test_send_spm_fail_001)
{
	pgm_send_spm (NULL, 0);
	fail ("reached");
}
END_TEST

/* target:
 *	void
 *	pgm_on_deferred_nak (
 *		pgm_transport_t*	transport
 *		)
 */

START_TEST (test_on_deferred_nak_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	pgm_on_deferred_nak (transport);
}
END_TEST
	
START_TEST (test_on_deferred_nak_fail_001)
{
	pgm_on_deferred_nak (NULL);
	fail ("reached");
}
END_TEST
	
/* target:
 *	gboolean
 *	pgm_on_spmr (
 *		pgm_transport_t*	transport,
 *		pgm_peer_t*		peer,
 *		struct pgm_sk_buff_t*	skb
 *	)
 */

/* peer spmr */
START_TEST (test_on_spmr_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	pgm_peer_t* peer = generate_peer ();
	fail_if (NULL == peer, "generate_peer failed");
	struct pgm_sk_buff_t* skb = generate_spmr ();
	fail_if (NULL == skb, "generate_spmr failed");
	skb->transport = transport;
	fail_unless (TRUE == pgm_on_spmr (transport, peer, skb), "on_spmr failed");
}
END_TEST

/* source spmr */
START_TEST (test_on_spmr_pass_002)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	struct pgm_sk_buff_t* skb = generate_spmr ();
	fail_if (NULL == skb, "generate_spmr failed");
	skb->transport = transport;
	fail_unless (TRUE == pgm_on_spmr (transport, NULL, skb), "on_spmr failed");
}
END_TEST

/* invalid spmr */
START_TEST (test_on_spmr_fail_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	pgm_peer_t* peer = generate_peer ();
	fail_if (NULL == peer, "generate_peer failed");
	struct pgm_sk_buff_t* skb = generate_spmr ();
	fail_if (NULL == skb, "generate_spmr failed");
	skb->transport = transport;
	mock_is_valid_spmr = FALSE;
	fail_unless (FALSE == pgm_on_spmr (transport, peer, skb), "on_spmr failed");
}
END_TEST

START_TEST (test_on_spmr_fail_002)
{
	pgm_on_spmr (NULL, NULL, NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_on_nak (
 *		pgm_transport_t*	transport,
 *		struct pgm_sk_buff_t*	skb
 *	)
 */

/* single nak */
START_TEST (test_on_nak_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	struct pgm_sk_buff_t* skb = generate_single_nak ();
	fail_if (NULL == skb, "generate_single_nak failed");
	skb->transport = transport;
	fail_unless (TRUE == pgm_on_nak (transport, skb), "on_nak failed");
}
END_TEST

/* nak list */
START_TEST (test_on_nak_pass_002)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	struct pgm_sk_buff_t* skb = generate_nak_list ();
	fail_if (NULL == skb, "generate_nak_list failed");
	skb->transport = transport;
	fail_unless (TRUE == pgm_on_nak (transport, skb), "on_nak failed");
}
END_TEST

/* single parity nak */
START_TEST (test_on_nak_pass_003)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->use_ondemand_parity = TRUE;
	struct pgm_sk_buff_t* skb = generate_parity_nak ();
	fail_if (NULL == skb, "generate_parity_nak failed");
	skb->transport = transport;
	fail_unless (TRUE == pgm_on_nak (transport, skb), "on_nak failed");
}
END_TEST

/* parity nak list */
START_TEST (test_on_nak_pass_004)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	transport->use_ondemand_parity = TRUE;
	struct pgm_sk_buff_t* skb = generate_parity_nak_list ();
	fail_if (NULL == skb, "generate_parity_nak_list failed");
	skb->transport = transport;
	fail_unless (TRUE == pgm_on_nak (transport, skb), "on_nak failed");
}
END_TEST

START_TEST (test_on_nak_fail_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	struct pgm_sk_buff_t* skb = generate_single_nak ();
	fail_if (NULL == skb, "generate_single_nak failed");
	skb->transport = transport;
	mock_is_valid_nak = FALSE;
	fail_unless (FALSE == pgm_on_nak (transport, skb), "on_nak failed");
}
END_TEST

START_TEST (test_on_nak_fail_002)
{
	pgm_on_nak (NULL, NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_on_nnak (
 *		pgm_transport_t*	transport,
 *		struct pgm_sk_buff_t*	skb
 *	)
 */

START_TEST (test_on_nnak_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	struct pgm_sk_buff_t* skb = generate_single_nnak ();
	fail_if (NULL == skb, "generate_single_nnak failed");
	skb->transport = transport;
	fail_unless (TRUE == pgm_on_nnak (transport, skb), "on_nnak failed");
}
END_TEST

START_TEST (test_on_nnak_fail_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	struct pgm_sk_buff_t* skb = generate_single_nnak ();
	fail_if (NULL == skb, "generate_single_nnak failed");
	skb->transport = transport;
	mock_is_valid_nnak = FALSE;
	fail_unless (FALSE == pgm_on_nnak (transport, skb), "on_nnak failed");
}
END_TEST

START_TEST (test_on_nnak_fail_002)
{
	pgm_on_nnak (NULL, NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_ambient_spm (
 *		pgm_transport_t*	transport,
 *		guint			interval
 *		)
 */

START_TEST (test_set_ambient_spm_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	fail_unless (TRUE == pgm_transport_set_ambient_spm (transport, 1000), "set_ambient_spm failed");
}
END_TEST

START_TEST (test_set_ambient_spm_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_ambient_spm (NULL, 1000), "set_ambient_spm failed");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_heartbeat_spm (
 *		pgm_transport_t*	transport,
 *		const guint*		intervals,
		const int		count
 *		)
 */

START_TEST (test_set_heartbeat_spm_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	const guint intervals[] = { 1, 2, 3, 4, 5 };
	fail_unless (TRUE == pgm_transport_set_heartbeat_spm (transport, intervals, G_N_ELEMENTS(intervals)), "set_heartbeat_spm failed");
}
END_TEST

START_TEST (test_set_heartbeat_spm_fail_001)
{
	const guint intervals[] = { 1, 2, 3, 4, 5 };
	fail_unless (FALSE == pgm_transport_set_heartbeat_spm (NULL, intervals, G_N_ELEMENTS(intervals)), "set_heartbeat_spm failed");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_txw_sqns (
 *		pgm_transport_t*	transport,
 *		const guint		sqns
 *	)
 */

START_TEST (test_set_txw_sqns_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	fail_unless (TRUE == pgm_transport_set_txw_sqns (transport, 100), "set_txw_sqns failed");
}
END_TEST

START_TEST (test_set_txw_sqns_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_txw_sqns (NULL, 100), "set_txw_sqns failed");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_txw_secs (
 *		pgm_transport_t*	transport,
 *		const guint		secs
 *	)
 */

START_TEST (test_set_txw_secs_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_if (NULL == transport, "generate_transport failed");
	fail_unless (TRUE == pgm_transport_set_txw_secs (transport, 10), "set_txw_secs failed");
}
END_TEST

START_TEST (test_set_txw_secs_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_txw_secs (NULL, 10), "set_txw_secs failed");
}
END_TEST

/* target:
 *	gboolean
 *	pgm_transport_set_txw_max_rte (
 *		pgm_transport_t*	transport,
 *		const guint		rate
 *	)
 */

START_TEST (test_set_txw_max_rte_pass_001)
{
	pgm_transport_t* transport = generate_transport ();
	fail_unless (TRUE == pgm_transport_set_txw_max_rte (transport, 100*1000), "set_txw_max_rte failed");
}
END_TEST

START_TEST (test_set_txw_max_rte_fail_001)
{
	fail_unless (FALSE == pgm_transport_set_txw_max_rte (NULL, 100*1000), "set_txw_max_rte failed");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_send = tcase_create ("send");
	suite_add_tcase (s, tc_send);
	tcase_add_checked_fixture (tc_send, mock_setup, NULL);
	tcase_add_test (tc_send, test_send_pass_001);
	tcase_add_test (tc_send, test_send_pass_002);
	tcase_add_test (tc_send, test_send_fail_001);

	TCase* tc_sendv = tcase_create ("sendv");
	suite_add_tcase (s, tc_sendv);
	tcase_add_checked_fixture (tc_sendv, mock_setup, NULL);
	tcase_add_test (tc_sendv, test_sendv_pass_001);
	tcase_add_test (tc_sendv, test_sendv_pass_002);
	tcase_add_test (tc_sendv, test_sendv_pass_003);
	tcase_add_test (tc_sendv, test_sendv_pass_004);
	tcase_add_test (tc_sendv, test_sendv_fail_001);

	TCase* tc_send_skbv = tcase_create ("send-skbv");
	suite_add_tcase (s, tc_send_skbv);
	tcase_add_checked_fixture (tc_send_skbv, mock_setup, NULL);
	tcase_add_test (tc_send_skbv, test_send_skbv_pass_001);
	tcase_add_test (tc_send_skbv, test_send_skbv_pass_002);
	tcase_add_test (tc_send_skbv, test_send_skbv_fail_001);

	TCase* tc_send_spm = tcase_create ("send-spm");
	suite_add_tcase (s, tc_send_spm);
	tcase_add_checked_fixture (tc_send_spm, mock_setup, NULL);
	tcase_add_test (tc_send_spm, test_send_spm_pass_001);
	tcase_add_test_raise_signal (tc_send_spm, test_send_spm_fail_001, SIGABRT);

	TCase* tc_on_deferred_nak = tcase_create ("on-deferred-nak");
	suite_add_tcase (s, tc_on_deferred_nak);
	tcase_add_checked_fixture (tc_on_deferred_nak, mock_setup, NULL);
	tcase_add_test (tc_on_deferred_nak, test_on_deferred_nak_pass_001);
	tcase_add_test_raise_signal (tc_on_deferred_nak, test_on_deferred_nak_fail_001, SIGABRT);

	TCase* tc_on_spmr = tcase_create ("on-spmr");
	suite_add_tcase (s, tc_on_spmr);
	tcase_add_checked_fixture (tc_on_spmr, mock_setup, NULL);
	tcase_add_test (tc_on_spmr, test_on_spmr_pass_001);
	tcase_add_test (tc_on_spmr, test_on_spmr_pass_002);
	tcase_add_test (tc_on_spmr, test_on_spmr_fail_001);
	tcase_add_test_raise_signal (tc_on_spmr, test_on_spmr_fail_002, SIGABRT);

	TCase* tc_on_nak = tcase_create ("on-nak");
	suite_add_tcase (s, tc_on_nak);
	tcase_add_checked_fixture (tc_on_nak, mock_setup, NULL);
	tcase_add_test (tc_on_nak, test_on_nak_pass_001);
	tcase_add_test (tc_on_nak, test_on_nak_pass_002);
	tcase_add_test (tc_on_nak, test_on_nak_pass_003);
	tcase_add_test (tc_on_nak, test_on_nak_pass_004);
	tcase_add_test (tc_on_nak, test_on_nak_fail_001);
	tcase_add_test_raise_signal (tc_on_nak, test_on_nak_fail_002, SIGABRT);

	TCase* tc_on_nnak = tcase_create ("on-nnak");
	suite_add_tcase (s, tc_on_nnak);
	tcase_add_checked_fixture (tc_on_nnak, mock_setup, NULL);
	tcase_add_test (tc_on_nnak, test_on_nnak_pass_001);
	tcase_add_test (tc_on_nnak, test_on_nnak_fail_001);
	tcase_add_test_raise_signal (tc_on_nnak, test_on_nnak_fail_002, SIGABRT);

	TCase* tc_set_ambient_spm = tcase_create ("set-ambient-spm");
	suite_add_tcase (s, tc_set_ambient_spm);
	tcase_add_checked_fixture (tc_set_ambient_spm, mock_setup, NULL);
	tcase_add_test (tc_set_ambient_spm, test_set_ambient_spm_pass_001);
	tcase_add_test (tc_set_ambient_spm, test_set_ambient_spm_fail_001);

	TCase* tc_set_heartbeat_spm = tcase_create ("set-heartbeat-spm");
	suite_add_tcase (s, tc_set_heartbeat_spm);
	tcase_add_checked_fixture (tc_set_heartbeat_spm, mock_setup, NULL);
	tcase_add_test (tc_set_heartbeat_spm, test_set_heartbeat_spm_pass_001);
	tcase_add_test (tc_set_heartbeat_spm, test_set_heartbeat_spm_fail_001);

	TCase* tc_set_txw_sqns = tcase_create ("set-txw-sqns");
	suite_add_tcase (s, tc_set_txw_sqns);
	tcase_add_checked_fixture (tc_set_txw_sqns, mock_setup, NULL);
	tcase_add_test (tc_set_txw_sqns, test_set_txw_sqns_pass_001);
	tcase_add_test (tc_set_txw_sqns, test_set_txw_sqns_fail_001);

	TCase* tc_set_txw_secs = tcase_create ("set-txw-secs");
	suite_add_tcase (s, tc_set_txw_secs);
	tcase_add_checked_fixture (tc_set_txw_secs, mock_setup, NULL);
	tcase_add_test (tc_set_txw_secs, test_set_txw_secs_pass_001);
	tcase_add_test (tc_set_txw_secs, test_set_txw_secs_fail_001);

	TCase* tc_set_txw_max_rte = tcase_create ("set-txw-max-rte");
	suite_add_tcase (s, tc_set_txw_max_rte);
	tcase_add_checked_fixture (tc_set_txw_max_rte, mock_setup, NULL);
	tcase_add_test (tc_set_txw_max_rte, test_set_txw_max_rte_pass_001);
	tcase_add_test (tc_set_txw_max_rte, test_set_txw_max_rte_fail_001);

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
