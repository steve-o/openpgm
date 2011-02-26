/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM socket.
 *
 * Copyright (c) 2009-2010 Miru Limited.
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
#include <stdlib.h>
#ifndef _WIN32
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
#include <glib.h>
#include <check.h>


/* mock state */

#define TEST_NETWORK		""
#define TEST_PORT		7500
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

#define pgm_ipproto_pgm		mock_pgm_ipproto_pgm
#define pgm_peer_unref		mock_pgm_peer_unref
#define pgm_on_nak_notify	mock_pgm_on_nak_notify
#define pgm_send_spm		mock_pgm_send_spm
#define pgm_timer_prepare	mock_pgm_timer_prepare
#define pgm_timer_check		mock_pgm_timer_check
#define pgm_timer_expiration	mock_pgm_timer_expiration
#define pgm_timer_dispatch	mock_pgm_timer_dispatch
#define pgm_txw_create		mock_pgm_txw_create
#define pgm_txw_shutdown	mock_pgm_txw_shutdown
#define pgm_rate_create		mock_pgm_rate_create
#define pgm_rate_destroy	mock_pgm_rate_destroy
#define pgm_rate_remaining	mock_pgm_rate_remaining
#define pgm_rs_create		mock_pgm_rs_create
#define pgm_rs_destroy		mock_pgm_rs_destroy
#define pgm_time_update_now	mock_pgm_time_update_now

#define SOCK_DEBUG
#include "socket.c"

int mock_pgm_ipproto_pgm = IPPROTO_PGM;


static
void
mock_setup (void)
{
	if (!g_thread_supported ()) g_thread_init (NULL);
}

static
void
mock_teardown (void)
{
}

/* stock create pgm sockaddr structure for calls to pgm_bind()
 */

static
struct pgm_sockaddr_t*
generate_asm_sockaddr (void)
{
	const pgm_gsi_t gsi = { 200, 202, 203, 204, 205, 206 };
	struct pgm_sockaddr_t* pgmsa = g_new0 (struct pgm_sockaddr_t, 1);
	pgmsa->sa_port = TEST_PORT;
	memcpy (&pgmsa->sa_addr.gsi, &gsi, sizeof(gsi));
	return pgmsa;
}

/* apply minimum socket options to new socket to pass bind()
 */

static
void
prebind_socket (
	struct pgm_sock_t*	sock
	)
{
	sock->max_tpdu = TEST_MAX_TPDU;
	sock->max_tsdu = TEST_MAX_TPDU - sizeof(struct pgm_ip) - pgm_pkt_offset (FALSE, FALSE);
	sock->max_tsdu_fragment = TEST_MAX_TPDU - sizeof(struct pgm_ip) - pgm_pkt_offset (TRUE, FALSE);
	sock->max_apdu = MIN(TEST_TXW_SQNS, PGM_MAX_FRAGMENTS) * sock->max_tsdu_fragment;

/* tx */
	sock->can_send_data = TRUE;
	sock->spm_ambient_interval = TEST_SPM_AMBIENT;
	const guint interval_init[] = TEST_SPM_HEARTBEAT_INIT;
	sock->spm_heartbeat_len = sizeof(interval_init) / sizeof(interval_init[0]);
	sock->spm_heartbeat_interval = g_new0 (guint, sock->spm_heartbeat_len + 1);
	for (guint i = 1; i < sock->spm_heartbeat_len; i++)
		sock->spm_heartbeat_interval[i] = interval_init[i];
	sock->txw_sqns = TEST_TXW_SQNS;

/* rx */
	sock->can_recv_data = TRUE;
	sock->rxw_sqns = TEST_RXW_SQNS;
	sock->peer_expiry = TEST_PEER_EXPIRY;
	sock->spmr_expiry = TEST_SPMR_EXPIRY;
	sock->nak_bo_ivl = TEST_NAK_BO_IVL;
	sock->nak_rpt_ivl = TEST_NAK_RPT_IVL;
	sock->nak_rdata_ivl = TEST_NAK_RDATA_IVL;
	sock->nak_data_retries = TEST_NAK_DATA_RETRIES;
	sock->nak_ncf_retries = TEST_NAK_NCF_RETRIES;
}

/* apply minimum sockets options to pass connect()
 */

static
void
preconnect_socket (
	struct pgm_sock_t*	sock
	)
{
/* tx */
	((struct sockaddr*)&sock->send_gsr.gsr_group)->sa_family = AF_INET;
	((struct sockaddr_in*)&sock->send_gsr.gsr_group)->sin_addr.s_addr = inet_addr ("239.192.0.1");

/* rx */
	sock->recv_gsr_len = 1;
	((struct sockaddr*)&sock->recv_gsr[0].gsr_group)->sa_family = ((struct sockaddr*)&sock->send_gsr.gsr_group)->sa_family;
	((struct sockaddr*)&sock->recv_gsr[0].gsr_source)->sa_family = ((struct sockaddr*)&sock->send_gsr.gsr_group)->sa_family;
	((struct sockaddr_in*)&sock->recv_gsr[0].gsr_group)->sin_addr.s_addr = ((struct sockaddr_in*)&sock->send_gsr.gsr_group)->sin_addr.s_addr;
	((struct sockaddr_in*)&sock->recv_gsr[0].gsr_source)->sin_addr.s_addr = ((struct sockaddr_in*)&sock->send_gsr.gsr_group)->sin_addr.s_addr;
}

/* stock create unconnected socket for pgm_setsockopt(), etc.
 */

static
struct pgm_sock_t*
generate_sock (void)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, g_htons(TEST_PORT) };
	struct pgm_sock_t* sock = g_new0 (struct pgm_sock_t, 1);
	memcpy (&sock->tsi, &tsi, sizeof(pgm_tsi_t));
	sock->is_bound = FALSE;
	sock->is_connected = FALSE;
	sock->is_destroyed = FALSE;
	sock->is_reset = FALSE;
	sock->family = AF_INET;
	sock->protocol = IPPROTO_IP;
	sock->recv_sock = socket (AF_INET, SOCK_RAW, 113);
	sock->send_sock = socket (AF_INET, SOCK_RAW, 113);
	sock->send_with_router_alert_sock = socket (AF_INET, SOCK_RAW, 113);
	((struct sockaddr*)&sock->send_addr)->sa_family = AF_INET;
	((struct sockaddr_in*)&sock->send_addr)->sin_addr.s_addr = inet_addr ("127.0.0.2");
	sock->dport = g_htons(TEST_PORT);
	sock->window = g_new0 (pgm_txw_t, 1);
	sock->iphdr_len = sizeof(struct pgm_ip);
	pgm_spinlock_init (&sock->txw_spinlock);
	pgm_rwlock_init (&sock->lock);
	return sock;
}

/** receiver module */
PGM_GNUC_INTERNAL
void
mock_pgm_peer_unref (
	pgm_peer_t*		peer
	)
{
}

/** source module */
static
bool
mock_pgm_on_nak_notify (
	GIOChannel*		source,
	GIOCondition		condition,
	gpointer		data
	)
{
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_send_spm (
	pgm_sock_t*		sock,
	int			flags
	)
{
	return TRUE;
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

/** transmit window module */
pgm_txw_t*
mock_pgm_txw_create (
	const pgm_tsi_t* const	tsi,
	const uint16_t		tpdu_size,
	const uint32_t		sqns,
	const unsigned		secs,
	const ssize_t		max_rte,
	const bool		use_fec,
	const uint8_t		rs_n,
	const uint8_t		rs_k
	)
{
	pgm_txw_t* window = g_new0 (pgm_txw_t, 1);
	return window;
}

void
mock_pgm_txw_shutdown (
	pgm_txw_t* const	window
	)
{
	g_free (window);
}

/** rate control module */
PGM_GNUC_INTERNAL
void
mock_pgm_rate_create (
	pgm_rate_t*		bucket,
	ssize_t			rate_per_sec,
	size_t			iphdr_len,
	uint16_t		max_tpdu
	)
{
}

PGM_GNUC_INTERNAL
void
mock_pgm_rate_destroy (
	pgm_rate_t*		bucket
	)
{
}

PGM_GNUC_INTERNAL
pgm_time_t
mock_pgm_rate_remaining (
	pgm_rate_t*		bucket,
	gsize			packetlen
	)
{
	return 0;
}

/** reed solomon module */
void
mock_pgm_rs_create (
	pgm_rs_t*		rs,
	const uint8_t		n,
	const uint8_t		k
	)
{
}

void
mock_pgm_rs_destroy (
	pgm_rs_t*		rs
	)
{
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

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

/* mock functions for external references */


/* target:
 *	bool
 *	pgm_socket (
 *		pgm_sock_t**		sock,
 *		const sa_family_t	family,
 *		const int		pgm_sock_type,
 *		const int		protocol,
 *		pgm_error_t**		error
 *	)
 */

START_TEST (test_create_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock;
/* only one type currently implemented */
	const int pgm_sock_type = SOCK_SEQPACKET;
/* PGM/IPv4 */
	sock = NULL;
	fail_unless (TRUE == pgm_socket (&sock, AF_INET, pgm_sock_type, IPPROTO_PGM, &err), "create/1 failed");
/* PGM/UDP over IPv4 */
	sock = NULL;
	fail_unless (TRUE == pgm_socket (&sock, AF_INET, pgm_sock_type, IPPROTO_UDP, &err), "create/2 failed");
/* PGM/IPv6 */
	sock = NULL;
	fail_unless (TRUE == pgm_socket (&sock, AF_INET6, pgm_sock_type, IPPROTO_PGM, &err), "create6/1 failed");
/* PGM/UDP over IPv6 */
	sock = NULL;
	fail_unless (TRUE == pgm_socket (&sock, AF_INET6, pgm_sock_type, IPPROTO_UDP, &err), "create6/2 failed");
	fail_unless (NULL == err, "error raised");
}
END_TEST

/* NULL socket */
START_TEST (test_create_fail_002)
{
	pgm_error_t* err = NULL;
	fail_unless (FALSE == pgm_socket (NULL, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &err), "create failed");
}
END_TEST

/* invalid protocol family */
START_TEST (test_create_fail_003)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	fail_unless (FALSE == pgm_socket (&sock, AF_UNSPEC, SOCK_SEQPACKET, IPPROTO_PGM, &err), "create failed");
}
END_TEST

/* invalid socket type */
START_TEST (test_create_fail_004)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	fail_unless (FALSE == pgm_socket (&sock, AF_INET, SOCK_STREAM, IPPROTO_PGM, &err), "create failed");
	fail_unless (FALSE == pgm_socket (&sock, AF_INET, SOCK_DGRAM, IPPROTO_PGM, &err), "create failed");
}
END_TEST

/* invalid protocol */
START_TEST (test_create_fail_005)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	fail_unless (FALSE == pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_TCP, &err), "create failed");
}
END_TEST


/* target:
 *	bool
 *	pgm_bind (
 *		pgm_sock_t*			sock,
 *		const struct pgm_sockaddr_t*	sockaddr,
 *		const socklen_t			sockaddrlen,
 *		pgm_error_t**			error
 *		)
 */

START_TEST (test_bind_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	struct pgm_sockaddr_t* pgmsa = generate_asm_sockaddr ();
	fail_if (NULL == pgmsa, "generate_asm_sockaddr failed");
	fail_unless (TRUE == pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_bind (sock, pgmsa, sizeof(*pgmsa), &err), "bind failed");
}
END_TEST

/* fail on unset options */
START_TEST (test_bind_fail_001)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	struct pgm_sockaddr_t* pgmsa = generate_asm_sockaddr ();
	fail_if (NULL == pgmsa, "generate_asm_sockaddr failed");
	fail_unless (TRUE == pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (FALSE == pgm_bind (sock, pgmsa, sizeof(*pgmsa), &err), "bind failed");
}
END_TEST

/* invalid parameters */
START_TEST (test_bind_fail_002)
{
	pgm_error_t* err = NULL;
	fail_unless (FALSE == pgm_bind (NULL, NULL, 0, &err), "bind failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_bind3 (
 *		pgm_sock_t*				sock,
 *		const struct pgm_sockaddr_t*		sockaddr,
 *		const socklen_t				sockaddrlen,
 *		const struct pgm_interface_req_t*	send_req,
 *		const socklen_t				send_req_len,
 *		const struct pgm_interface_req_t*	recv_req,
 *		const socklen_t				recv_req_len,
 *		pgm_error_t**				error
 *		)
 */

/* fail on unset options */
START_TEST (test_bind3_fail_001)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	struct pgm_sockaddr_t* pgmsa = generate_asm_sockaddr ();
	fail_if (NULL == pgmsa, "generate_asm_sockaddr failed");
	fail_unless (TRUE == pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	struct pgm_interface_req_t send_req = { .ir_interface = 0, .ir_scope_id = 0 },
				   recv_req = { .ir_interface = 0, .ir_scope_id = 0 };
	fail_unless (FALSE == pgm_bind3 (sock,
					 pgmsa, sizeof(*pgmsa),
					 &send_req, sizeof(send_req),
					 &recv_req, sizeof(recv_req),
					 &err), "bind failed");
}
END_TEST

/* invalid parameters */
START_TEST (test_bind3_fail_002)
{
	pgm_error_t* err = NULL;
	fail_unless (FALSE == pgm_bind3 (NULL, NULL, 0, NULL, 0, NULL, 0, &err), "bind failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_connect (
 *		pgm_sock_t*		sock,
 *		pgm_error_t**		error
 *		)
 */

START_TEST (test_connect_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	struct pgm_sockaddr_t* pgmsa = generate_asm_sockaddr ();
	fail_if (NULL == pgmsa, "generate_asm_sockaddr failed");
	fail_unless (TRUE == pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	prebind_socket (sock);
	fail_unless (TRUE == pgm_bind (sock, pgmsa, sizeof(*pgmsa), &err), "bind failed");
	preconnect_socket (sock);
	fail_unless (TRUE == pgm_connect (sock, &err), "connect failed");
}
END_TEST

/* invalid parameters */
START_TEST (test_connect_fail_001)
{
	pgm_error_t* err = NULL;
	fail_unless (FALSE == pgm_connect (NULL, &err), "connect failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_close (
 *		pgm_sock_t*		sock,
 *		bool			flush
 *		)
 */

/* socket > close */
START_TEST (test_destroy_pass_001)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	fail_unless (TRUE == pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &err), "create failed");
	fail_unless (TRUE == pgm_close (sock, FALSE), "destroy failed");
}
END_TEST

/* socket > bind > close */
START_TEST (test_destroy_pass_002)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	struct pgm_sockaddr_t* pgmsa = generate_asm_sockaddr ();
	fail_if (NULL == pgmsa, "generate_asm_sockaddr failed");
	fail_unless (TRUE == pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_bind (sock, pgmsa, sizeof(*pgmsa), &err), "bind failed");
	fail_unless (TRUE == pgm_close (sock, FALSE), "destroy failed");
}
END_TEST

/* socket > bind > connect > close */
START_TEST (test_destroy_pass_003)
{
	pgm_error_t* err = NULL;
	pgm_sock_t* sock = NULL;
	struct pgm_sockaddr_t* pgmsa = generate_asm_sockaddr ();
	fail_if (NULL == pgmsa, "generate_asm_sockaddr failed");
	fail_unless (TRUE == pgm_socket (&sock, AF_INET, SOCK_SEQPACKET, IPPROTO_PGM, &err), "create failed");
	fail_unless (NULL == err, "error raised");
	fail_unless (TRUE == pgm_bind (sock, pgmsa, sizeof(*pgmsa), &err), "bind failed");
	fail_unless (TRUE == pgm_connect (sock, &err), "connect failed");
	fail_unless (TRUE == pgm_close (sock, FALSE), "destroy failed");
}
END_TEST

/* invalid parameters */
START_TEST (test_destroy_fail_001)
{
	fail_unless (FALSE == pgm_close (NULL, FALSE), "destroy failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_MAX_TPDU,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *		)
 */

START_TEST (test_set_max_tpdu_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_MTU;
	const int max_tpdu	= 1500;
	const void* optval	= &max_tpdu;
	const socklen_t optlen	= sizeof(max_tpdu);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_max_tpdu failed");
}
END_TEST

/* invalid parameters */
START_TEST (test_set_max_tpdu_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_MTU;
	const int max_tpdu	= 1500;
	const void* optval	= &max_tpdu;
	const socklen_t optlen	= sizeof(max_tpdu);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_max_tpdu failed");
}
END_TEST

/* too small */
START_TEST (test_set_max_tpdu_fail_002)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_MTU;
	const int max_tpdu	= 1;
	const void* optval	= &max_tpdu;
	const socklen_t optlen	= sizeof(max_tpdu);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_max_tpdu failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_MULTICAST_LOOP,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *		)
 */

START_TEST (test_set_multicast_loop_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_MULTICAST_LOOP;
	const int loop_enabled	= 1;
	const void* optval	= &loop_enabled;
	const socklen_t optlen	= sizeof(loop_enabled);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_multicast_loop failed");
}
END_TEST

START_TEST (test_set_multicast_loop_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_MULTICAST_LOOP;
	const int loop_enabled	= 1;
	const void* optval	= &loop_enabled;
	const socklen_t optlen	= sizeof(loop_enabled);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_multicast_loop failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_MULTICAST_HOPS,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_hops_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_MULTICAST_HOPS;
	const int hops		= 16;
	const void* optval	= &hops;
	const socklen_t optlen	= sizeof(hops);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_hops failed");
}
END_TEST

START_TEST (test_set_hops_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_MULTICAST_HOPS;
	const int hops		= 16;
	const void* optval	= &hops;
	const socklen_t optlen	= sizeof(hops);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_hops failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = SOL_SOCKET,
 *		const int		optname = SO_SNDBUF,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_sndbuf_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= SOL_SOCKET;
	const int optname	= SO_SNDBUF;
	const int bufsize	= 131071;	/* 128kB */
	const void* optval	= &bufsize;
	const socklen_t optlen	= sizeof(bufsize);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_sndbuf failed");
}
END_TEST

START_TEST (test_set_sndbuf_fail_001)
{
	const int level		= SOL_SOCKET;
	const int optname	= SO_SNDBUF;
	const int bufsize	= 131071;	/* 128kB */
	const void* optval	= &bufsize;
	const socklen_t optlen	= sizeof(bufsize);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_sndbuf failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = SOL_SOCKET,
 *		const int		optname = PGM_RCVBUF,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_rcvbuf_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= SOL_SOCKET;
	const int optname	= SO_RCVBUF;
	const int bufsize	= 131071;	/* 128kB */
	const void* optval	= &bufsize;
	const socklen_t optlen	= sizeof(bufsize);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_rcvbuf failed");
}
END_TEST

START_TEST (test_set_rcvbuf_fail_001)
{
	const int level		= SOL_SOCKET;
	const int optname	= SO_RCVBUF;
	const int bufsize	= 131071;	/* 128kB */
	const void* optval	= &bufsize;
	const socklen_t optlen	= sizeof(bufsize);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_rcvbuf failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_USE_FEC,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(struct pgm_fecinfo_t)
 *	)
 */

START_TEST (test_set_fec_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_USE_FEC;
	const struct pgm_fecinfo_t fecinfo = {
		.ondemand_parity_enabled	= TRUE,
		.proactive_packets		= 16,
		.var_pktlen_enabled		= TRUE,
		.block_size			= 255,
		.group_size			= 64
	};
	const void* optval	= &fecinfo;
	const socklen_t optlen	= sizeof(fecinfo);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_fec failed");
}
END_TEST

START_TEST (test_set_fec_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_USE_FEC;
	const struct pgm_fecinfo_t fecinfo = {
		.ondemand_parity_enabled	= TRUE,
		.proactive_packets		= 16,
		.var_pktlen_enabled		= TRUE,
		.block_size			= 255,
		.group_size			= 64
	};
	const void* optval	= &fecinfo;
	const socklen_t optlen	= sizeof(fecinfo);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_fec failed");
}
END_TEST

/* TODO: invalid Reed-Solomon parameters
 */

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_USE_PGMCC,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(struct pgm_pgmccinfo_t)
 *	)
 */

START_TEST (test_set_pgmcc_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_USE_PGMCC;
	const struct pgm_pgmccinfo_t pgmccinfo = {
		.ack_bo_ivl	= pgm_msecs(100),
		.ack_c		= 123,
		.ack_c_p	= 456
	};
	const void* optval	= &pgmccinfo;
	const socklen_t optlen	= sizeof(pgmccinfo);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_pgmcc failed");
}
END_TEST

START_TEST (test_set_pgmcc_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_USE_PGMCC;
	const struct pgm_pgmccinfo_t pgmccinfo = {
		.ack_bo_ivl	= pgm_msecs(100),
		.ack_c		= 123,
		.ack_c_p	= 456
	};
	const void* optval	= &pgmccinfo;
	const socklen_t optlen	= sizeof(pgmccinfo);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_pgmcc failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_USE_CR,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_cr_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_USE_CR;
	const int magic_bunny	= 1;
	const void* optval	= &magic_bunny;
	const socklen_t optlen	= sizeof(magic_bunny);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_cr failed");
}
END_TEST

START_TEST (test_set_cr_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_USE_CR;
	const int magic_bunny	= 1;
	const void* optval	= &magic_bunny;
	const socklen_t optlen	= sizeof(magic_bunny);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_cr failed");
}
END_TEST


/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_SEND_ONLY,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_send_only_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_SEND_ONLY;
	const int send_only	= 1;
	const void* optval	= &send_only;
	const socklen_t optlen	= sizeof(send_only);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_send_only failed");
}
END_TEST

START_TEST (test_set_send_only_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_SEND_ONLY;
	const int send_only	= 1;
	const void* optval	= &send_only;
	const socklen_t optlen	= sizeof(send_only);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_send_only failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_RECV_ONLY,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_recv_only_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_RECV_ONLY;
	const int recv_only	= 1;
	const void* optval	= &recv_only;
	const socklen_t optlen	= sizeof(recv_only);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_recv_only failed");
}
END_TEST

START_TEST (test_set_recv_only_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_RECV_ONLY;
	const int recv_only	= 1;
	const void* optval	= &recv_only;
	const socklen_t optlen	= sizeof(recv_only);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_recv_only failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_PASSIVE,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_recv_only_pass_002)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_PASSIVE;
	const int passive	= 1;
	const void* optval	= &passive;
	const socklen_t optlen	= sizeof(passive);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_passive failed");
}
END_TEST

START_TEST (test_set_recv_only_fail_002)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_PASSIVE;
	const int passive	= 1;
	const void* optval	= &passive;
	const socklen_t optlen	= sizeof(passive);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_passive failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_ABORT_ON_RESET,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_abort_on_reset_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_ABORT_ON_RESET;
	const int abort_on_reset= 1;
	const void* optval	= &abort_on_reset;
	const socklen_t optlen	= sizeof(abort_on_reset);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_abort_on_reset failed");
}
END_TEST

START_TEST (test_set_abort_on_reset_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_ABORT_ON_RESET;
	const int abort_on_reset= 1;
	const void* optval	= &abort_on_reset;
	const socklen_t optlen	= sizeof(abort_on_reset);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_abort_on_reset failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_NOBLOCK,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_noblock_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NOBLOCK;
	const int noblock       = 1;
	const void* optval	= &noblock;
	const socklen_t optlen	= sizeof(noblock);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_noblock failed");
}
END_TEST

START_TEST (test_set_noblock_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NOBLOCK;
	const int noblock       = 1;
	const void* optval	= &noblock;
	const socklen_t optlen	= sizeof(noblock);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_noblock failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_UDP_ENCAP_UCAST_PORT,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_udp_unicast_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NOBLOCK;
	const int unicast_port  = 10001;
	const void* optval	= &unicast_port;
	const socklen_t optlen	= sizeof(unicast_port);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_udp_unicast failed");
}
END_TEST

START_TEST (test_set_udp_unicast_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NOBLOCK;
	const int unicast_port  = 10001;
	const void* optval	= &unicast_port;
	const socklen_t optlen	= sizeof(unicast_port);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_udp_unicast failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_UDP_ENCAP_MCAST_PORT,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_udp_multicast_pass_001)
{
	pgm_sock_t* sock = generate_sock ();
	fail_if (NULL == sock, "generate_sock failed");
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NOBLOCK;
	const int multicast_port= 10001;
	const void* optval	= &multicast_port;
	const socklen_t optlen	= sizeof(multicast_port);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_udp_multicast failed");
}
END_TEST

START_TEST (test_set_udp_multicast_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NOBLOCK;
	const int multicast_port= 10002;
	const void* optval	= &multicast_port;
	const socklen_t optlen	= sizeof(multicast_port);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_udp_multicast failed");
}
END_TEST

static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_create = tcase_create ("create");
	suite_add_tcase (s, tc_create);
	tcase_add_checked_fixture (tc_create, mock_setup, mock_teardown);
	tcase_add_test (tc_create, test_create_pass_001);
	tcase_add_test (tc_create, test_create_fail_002);
	tcase_add_test (tc_create, test_create_fail_003);
	tcase_add_test (tc_create, test_create_fail_004);
	tcase_add_test (tc_create, test_create_fail_005);

	TCase* tc_bind = tcase_create ("bind");
	suite_add_tcase (s, tc_bind);
	tcase_add_checked_fixture (tc_bind, mock_setup, mock_teardown);
	tcase_add_test (tc_bind, test_bind_fail_001);
	tcase_add_test (tc_bind, test_bind_fail_002);

	TCase* tc_connect = tcase_create ("connect");
	suite_add_tcase (s, tc_connect);
	tcase_add_checked_fixture (tc_connect, mock_setup, mock_teardown);
	tcase_add_test (tc_connect, test_connect_pass_001);
	tcase_add_test (tc_connect, test_connect_fail_001);

	TCase* tc_destroy = tcase_create ("destroy");
	suite_add_tcase (s, tc_destroy);
	tcase_add_checked_fixture (tc_destroy, mock_setup, mock_teardown);
	tcase_add_test (tc_destroy, test_destroy_pass_001);
	tcase_add_test (tc_destroy, test_destroy_fail_001);

	TCase* tc_set_max_tpdu = tcase_create ("set-max-tpdu");
	suite_add_tcase (s, tc_set_max_tpdu);
	tcase_add_checked_fixture (tc_set_max_tpdu, mock_setup, mock_teardown);
	tcase_add_test (tc_set_max_tpdu, test_set_max_tpdu_pass_001);
	tcase_add_test (tc_set_max_tpdu, test_set_max_tpdu_fail_001);

	TCase* tc_set_multicast_loop = tcase_create ("set-multicast-loop");
	suite_add_tcase (s, tc_set_multicast_loop);
	tcase_add_checked_fixture (tc_set_multicast_loop, mock_setup, mock_teardown);
	tcase_add_test (tc_set_multicast_loop, test_set_multicast_loop_pass_001);
	tcase_add_test (tc_set_multicast_loop, test_set_multicast_loop_fail_001);

	TCase* tc_set_hops = tcase_create ("set-hops");
	suite_add_tcase (s, tc_set_hops);
	tcase_add_checked_fixture (tc_set_hops, mock_setup, mock_teardown);
	tcase_add_test (tc_set_hops, test_set_hops_pass_001);
	tcase_add_test (tc_set_hops, test_set_hops_fail_001);

	TCase* tc_set_sndbuf = tcase_create ("set-sndbuf");
	suite_add_tcase (s, tc_set_sndbuf);
	tcase_add_checked_fixture (tc_set_sndbuf, mock_setup, mock_teardown);
	tcase_add_test (tc_set_sndbuf, test_set_sndbuf_pass_001);
	tcase_add_test (tc_set_sndbuf, test_set_sndbuf_fail_001);

	TCase* tc_set_rcvbuf = tcase_create ("set-rcvbuf");
	suite_add_tcase (s, tc_set_rcvbuf);
	tcase_add_checked_fixture (tc_set_rcvbuf, mock_setup, mock_teardown);
	tcase_add_test (tc_set_rcvbuf, test_set_rcvbuf_pass_001);
	tcase_add_test (tc_set_rcvbuf, test_set_rcvbuf_fail_001);

	TCase* tc_set_fec = tcase_create ("set-fec");
	suite_add_tcase (s, tc_set_fec);
	tcase_add_checked_fixture (tc_set_fec, mock_setup, mock_teardown);
	tcase_add_test (tc_set_fec, test_set_fec_pass_001);
	tcase_add_test (tc_set_fec, test_set_fec_fail_001);

	TCase* tc_set_pgmcc = tcase_create ("set-pgmcc");
	suite_add_tcase (s, tc_set_pgmcc);
	tcase_add_checked_fixture (tc_set_pgmcc, mock_setup, mock_teardown);
	tcase_add_test (tc_set_pgmcc, test_set_pgmcc_pass_001);
	tcase_add_test (tc_set_pgmcc, test_set_pgmcc_fail_001);

	TCase* tc_set_cr = tcase_create ("set-cr");
	suite_add_tcase (s, tc_set_cr);
	tcase_add_checked_fixture (tc_set_cr, mock_setup, mock_teardown);
	tcase_add_test (tc_set_cr, test_set_cr_pass_001);
	tcase_add_test (tc_set_cr, test_set_cr_fail_001);

	TCase* tc_set_send_only = tcase_create ("set-send-only");
	suite_add_tcase (s, tc_set_send_only);
	tcase_add_checked_fixture (tc_set_send_only, mock_setup, mock_teardown);
	tcase_add_test (tc_set_send_only, test_set_send_only_pass_001);
	tcase_add_test (tc_set_send_only, test_set_send_only_fail_001);

	TCase* tc_set_recv_only = tcase_create ("set-recv-only");
	suite_add_tcase (s, tc_set_recv_only);
	tcase_add_checked_fixture (tc_set_recv_only, mock_setup, mock_teardown);
	tcase_add_test (tc_set_recv_only, test_set_recv_only_pass_001);
	tcase_add_test (tc_set_recv_only, test_set_recv_only_pass_002);
	tcase_add_test (tc_set_recv_only, test_set_recv_only_fail_001);
	tcase_add_test (tc_set_recv_only, test_set_recv_only_fail_002);

	TCase* tc_set_abort_on_reset = tcase_create ("set-abort-on-reset");
	suite_add_tcase (s, tc_set_abort_on_reset);
	tcase_add_checked_fixture (tc_set_abort_on_reset, mock_setup, mock_teardown);
	tcase_add_test (tc_set_abort_on_reset, test_set_abort_on_reset_pass_001);
	tcase_add_test (tc_set_abort_on_reset, test_set_abort_on_reset_fail_001);

	TCase* tc_set_noblock = tcase_create ("set-non-blocking");
	suite_add_tcase (s, tc_set_noblock);
	tcase_add_checked_fixture (tc_set_noblock, mock_setup, mock_teardown);
	tcase_add_test (tc_set_noblock, test_set_noblock_pass_001);
	tcase_add_test (tc_set_noblock, test_set_noblock_fail_001);

	TCase* tc_set_udp_unicast = tcase_create ("set-udp-encap-ucast-port");
	suite_add_tcase (s, tc_set_udp_unicast);
	tcase_add_checked_fixture (tc_set_udp_unicast, mock_setup, mock_teardown);
	tcase_add_test (tc_set_udp_unicast, test_set_udp_unicast_pass_001);
	tcase_add_test (tc_set_udp_unicast, test_set_udp_unicast_fail_001);

	TCase* tc_set_udp_multicast = tcase_create ("set-udp-encap-mcast-port");
	suite_add_tcase (s, tc_set_udp_multicast);
	tcase_add_checked_fixture (tc_set_udp_multicast, mock_setup, mock_teardown);
	tcase_add_test (tc_set_udp_multicast, test_set_udp_multicast_pass_001);
	tcase_add_test (tc_set_udp_multicast, test_set_udp_multicast_fail_001);

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
#ifndef _WIN32
	if (0 != getuid()) {
		fprintf (stderr, "This test requires super-user privileges to run.\n");
		return EXIT_FAILURE;
	}
#else
	WORD wVersionRequested = MAKEWORD (2, 2);
	WSADATA wsaData;
	g_assert (0 == WSAStartup (wVersionRequested, &wsaData));
	g_assert (LOBYTE (wsaData.wVersion) == 2 && HIBYTE (wsaData.wVersion) == 2);
#endif
	g_assert (pgm_time_init (NULL));
	pgm_messages_init();
	pgm_rand_init();
	pgm_thread_init();
	pgm_rwlock_init (&pgm_sock_list_lock);
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	pgm_rwlock_free (&pgm_sock_list_lock);
	pgm_thread_shutdown();
	pgm_rand_shutdown();
	pgm_messages_shutdown();
	g_assert (pgm_time_shutdown());
#ifdef _WIN32
	WSACleanup();
#endif
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
