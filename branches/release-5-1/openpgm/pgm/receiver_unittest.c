/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM receiver transport.
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
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


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


#define pgm_histogram_add	mock_pgm_histogram_add
#define pgm_verify_spm		mock_pgm_verify_spm
#define pgm_verify_nak		mock_pgm_verify_nak
#define pgm_verify_ncf		mock_pgm_verify_ncf
#define pgm_verify_poll		mock_pgm_verify_poll
#define pgm_sendto_hops		mock_pgm_sendto_hops
#define pgm_time_now		mock_pgm_time_now
#define pgm_time_update_now	mock_pgm_time_update_now
#define pgm_rxw_destroy		mock_pgm_rxw_destroy
#define pgm_rxw_create		mock_pgm_rxw_create
#define pgm_rxw_update		mock_pgm_rxw_update
#define pgm_rxw_update_fec	mock_pgm_rxw_update_fec
#define pgm_rxw_confirm		mock_pgm_rxw_confirm
#define pgm_rxw_lost		mock_pgm_rxw_lost
#define pgm_rxw_state		mock_pgm_rxw_state
#define pgm_rxw_add		mock_pgm_rxw_add
#define pgm_rxw_remove_commit	mock_pgm_rxw_remove_commit
#define pgm_rxw_readv		mock_pgm_rxw_readv
#define pgm_csum_fold		mock_pgm_csum_fold
#define pgm_compat_csum_partial	mock_pgm_compat_csum_partial
#define pgm_histogram_init	mock_pgm_histogram_init
#define pgm_setsockopt		mock_pgm_setsockopt


#define RECEIVER_DEBUG
#include "receiver.c"


static
void
mock_setup (void)
{
	if (!g_thread_supported ()) g_thread_init (NULL);
}

static
struct pgm_sock_t*
generate_sock (void)
{
	struct pgm_sock_t* sock = g_malloc0 (sizeof(struct pgm_sock_t));
	return sock;
}

static
pgm_peer_t*
generate_peer (void)
{
	const pgm_tsi_t tsi = { { 1, 2, 3, 4, 5, 6 }, 1000 };
	pgm_peer_t* peer = g_malloc0 (sizeof(pgm_peer_t));
	peer->window = g_malloc0 (sizeof(pgm_rxw_t));
	pgm_atomic_inc32 (&peer->ref_count);
	return peer;
}

/** socket module */
static
int
mock_pgm_poll_info (
	pgm_sock_t* const	sock,
	struct pollfd*		fds,
	int*			n_fds,
	int			events
	)
{
}

static
gboolean
mock_pgm_on_nak (
	pgm_sock_t* const		sock,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_on_nnak (
	pgm_sock_t* const		sock,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

static
gboolean
mock_pgm_on_spmr (
	pgm_sock_t* const		sock,
	pgm_peer_t* const		peer,
	struct pgm_sk_buff_t* const	skb
	)
{
	return TRUE;
}

/** net module */
PGM_GNUC_INTERNAL
ssize_t
mock_pgm_sendto_hops (
	pgm_sock_t*			sock,
	bool				use_rate_limit,
	pgm_rate_t*			minor_rate_control,
	bool				use_router_alert,
	int				hops,
	const void*			buf,
	size_t				len,
	const struct sockaddr*		to,
	socklen_t			tolen
	)
{
	return len;
}

/** time module */
static pgm_time_t mock_pgm_time_now = 0x1;
static pgm_time_t _mock_pgm_time_update_now (void);
pgm_time_update_func mock_pgm_time_update_now = _mock_pgm_time_update_now;

pgm_time_t
_mock_pgm_time_update_now (void)
{
	return mock_pgm_time_now;
}

/* packet module */
bool
mock_pgm_verify_spm (
	const struct pgm_sk_buff_t* const       skb
	)
{
	return TRUE;
}

bool
mock_pgm_verify_nak (
	const struct pgm_sk_buff_t* const       skb
	)
{
	return TRUE;
}

bool
mock_pgm_verify_ncf (
	const struct pgm_sk_buff_t* const       skb
	)
{
	return TRUE;
}

bool
mock_pgm_verify_poll (
	const struct pgm_sk_buff_t* const       skb
	)
{
	return TRUE;
}

/* receive window module */
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
	return g_malloc0 (sizeof(pgm_rxw_t));
}

void
mock_pgm_rxw_destroy (
	pgm_rxw_t* const	window
	)
{
	g_assert (NULL != window);
	g_free (window);
}

int
mock_pgm_rxw_confirm (
	pgm_rxw_t* const	window,
	const uint32_t		sequence,
	const pgm_time_t	now,
	const pgm_time_t	nak_rdata_expiry,
	const pgm_time_t	nak_rb_expiry
	)
{
	return PGM_RXW_DUPLICATE;
}

void
mock_pgm_rxw_lost (
	pgm_rxw_t* const	window,
	const uint32_t		sequence
	)
{
}

void
mock_pgm_rxw_state (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb,
	const int			new_state
	)
{
}

unsigned
mock_pgm_rxw_update (
	pgm_rxw_t* const		window,
	const uint32_t			txw_lead,
	const uint32_t			txw_trail,
	const pgm_time_t		now,
	const pgm_time_t		nak_rb_expiry
	)
{
	return 0;
}

void
mock_pgm_rxw_update_fec (
	pgm_rxw_t* const		window,
	const uint8_t			rs_k
	)
{
}

int
mock_pgm_rxw_add (
	pgm_rxw_t* const		window,
	struct pgm_sk_buff_t* const	skb,
	const pgm_time_t		now,
	const pgm_time_t		nak_rb_expiry
	)
{
	return PGM_RXW_APPENDED;
}

void
mock_pgm_rxw_remove_commit (
	pgm_rxw_t* const		window
	)
{
}

ssize_t
mock_pgm_rxw_readv (
	pgm_rxw_t* const		window,
	struct pgm_msgv_t**		pmsg,
	const unsigned			pmsglen
	)
{
	return 0;
}

/* checksum module */
uint16_t
mock_pgm_csum_fold (
	uint32_t			csum
	)
{
	return 0x0;
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

void
mock_pgm_histogram_init (
	pgm_histogram_t*	histogram
	)
{
}

void
mock_pgm_histogram_add (
	pgm_histogram_t*	histogram,
	int			value
	)
{
}

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

bool
mock_pgm_setsockopt (
        pgm_sock_t* const       sock,
	const int		level,
        const int               optname,
        const void*             optval,
        const socklen_t         optlen
        )
{
        if (NULL == sock)
                return FALSE;
        return TRUE;
}

/* target:
 *	void
 *	pgm_peer_unref (
 *		pgm_peer_t*		peer
 *		)
 */

/* last ref */
START_TEST (test_peer_unref_pass_001)
{
	pgm_peer_t* peer = generate_peer();
	pgm_peer_unref (peer);
}
END_TEST

/* non-last ref */
START_TEST (test_peer_unref_pass_002)
{
	pgm_peer_t* peer = _pgm_peer_ref (generate_peer());
	pgm_peer_unref (peer);
}
END_TEST


START_TEST (test_peer_unref_fail_001)
{
	pgm_peer_unref (NULL);
	fail ("reached");
}
END_TEST

/* target:
 *	bool
 *	pgm_check_peer_state (
 *		pgm_sock_t*		sock,
 *		const pgm_time_t	now
 *		)
 */

START_TEST (test_check_peer_state_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	sock->is_bound = TRUE;
	pgm_check_peer_state (sock, mock_pgm_time_now);
}
END_TEST

START_TEST (test_check_peer_state_fail_001)
{
	pgm_check_peer_state (NULL, mock_pgm_time_now);
	fail ("reached");
}
END_TEST

/* target:
 *	pgm_time_t
 *	pgm_min_receiver_expiry (
 *		pgm_sock_t*		sock,
 *		pgm_time_t		expiration
 *		)
 */

START_TEST (test_min_receiver_expiry_pass_001)
{
	pgm_sock_t* sock = generate_sock();
	sock->is_bound = TRUE;
	const pgm_time_t expiration = pgm_secs(1);
	pgm_time_t next_expiration = pgm_min_receiver_expiry (sock, expiration);
}
END_TEST

START_TEST (test_min_receiver_expiry_fail_001)
{
	const pgm_time_t expiration = pgm_secs(1);
	pgm_min_receiver_expiry (NULL, expiration);
	fail ("reached");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_RXW_SQNS,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_rxw_sqns_pass_001)
{
	pgm_sock_t* sock	= generate_sock ();
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_RXW_SQNS;
	const int rxw_sqns	= 100;
	const void* optval	= &rxw_sqns;
	const socklen_t optlen	= sizeof(int);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_rxw_sqns failed");
}
END_TEST

START_TEST (test_set_rxw_sqns_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_RXW_SQNS;
	const int rxw_sqns	= 100;
	const void* optval	= &rxw_sqns;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_rxw_sqns failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_RXW_SECS,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_rxw_secs_pass_001)
{
	pgm_sock_t* sock	= generate_sock ();
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_RXW_SECS;
	const int rxw_secs	= 10;
	const void* optval	= &rxw_secs;
	const socklen_t optlen	= sizeof(int);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_rxw_secs failed");
}
END_TEST

START_TEST (test_set_rxw_secs_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_RXW_SECS;
	const int rxw_secs	= 10;
	const void* optval	= &rxw_secs;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_rxw_secs failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_RXW_MAX_RTE,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_rxw_max_rte_pass_001)
{
	pgm_sock_t* sock	= generate_sock ();
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_RXW_MAX_RTE;
	const int rxw_max_rte	= 100*1000;
	const void* optval	= &rxw_max_rte;
	const socklen_t optlen	= sizeof(int);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_rxw_max_rte failed");
}
END_TEST

START_TEST (test_set_rxw_max_rte_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_RXW_MAX_RTE;
	const int rxw_max_rte	= 100*1000;
	const void* optval	= &rxw_max_rte;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_rxw_max_rte failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_PEER_EXPIRY,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_peer_expiry_pass_001)
{
	pgm_sock_t* sock	= generate_sock ();
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_PEER_EXPIRY;
	const int peer_expiry	= pgm_secs(100);
	const void* optval	= &peer_expiry;
	const socklen_t optlen	= sizeof(int);
/* pre-checking should verify value to spm ambient interval
	sock->spm_ambient_interval = pgm_secs(30);
 */
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_peer_expiry failed");
}
END_TEST

START_TEST (test_set_peer_expiry_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_PEER_EXPIRY;
	const int peer_expiry	= pgm_secs(100);
	const void* optval	= &peer_expiry;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_peer_expiry failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_SPMR_EXPIRY,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_spmr_expiry_pass_001)
{
	pgm_sock_t* sock	= generate_sock ();
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_SPMR_EXPIRY;
	const int spmr_expiry	= pgm_secs(10);
	const void* optval	= &spmr_expiry;
	const socklen_t optlen	= sizeof(int);
/* pre-checking should verify value to spm ambient interval
	sock->spm_ambient_interval = pgm_secs(30);
 */
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_spmr_expiry failed");
}
END_TEST

START_TEST (test_set_spmr_expiry_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_SPMR_EXPIRY;
	const int spmr_expiry	= pgm_secs(10);
	const void* optval	= &spmr_expiry;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_spmr_expiry failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_NAK_BO_IVL,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_nak_bo_ivl_pass_001)
{
	const int level		= IPPROTO_PGM;
	pgm_sock_t* sock	= generate_sock ();
	const int optname	= PGM_NAK_BO_IVL;
	const int nak_bo_ivl	= pgm_msecs(1000);
	const void* optval	= &nak_bo_ivl;
	const socklen_t optlen	= sizeof(int);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_nak_bo_ivl failed");
}
END_TEST

START_TEST (test_set_nak_bo_ivl_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NAK_BO_IVL;
	const int nak_bo_ivl	= pgm_msecs(1000);
	const void* optval	= &nak_bo_ivl;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_nak_bo_ivl failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_NAK_RPT_IVL,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_nak_rpt_ivl_pass_001)
{
	pgm_sock_t* sock	= generate_sock ();
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NAK_RPT_IVL;
	const int nak_rpt_ivl	= pgm_msecs(1000);
	const void* optval	= &nak_rpt_ivl;
	const socklen_t optlen	= sizeof(int);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_nak_rpt_ivl failed");
}
END_TEST

START_TEST (test_set_nak_rpt_ivl_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NAK_RPT_IVL;
	const int nak_rpt_ivl	= pgm_msecs(1000);
	const void* optval	= &nak_rpt_ivl;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_nak_rpt_ivl failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_NAK_RDATA_IVL,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_nak_rdata_ivl_pass_001)
{
	pgm_sock_t* sock	= generate_sock ();
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NAK_RDATA_IVL;
	const int nak_rdata_ivl	= pgm_msecs(1000);
	const void* optval	= &nak_rdata_ivl;
	const socklen_t optlen	= sizeof(int);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_nak_rdata_ivl failed");
}
END_TEST

START_TEST (test_set_nak_rdata_ivl_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NAK_RDATA_IVL;
	const int nak_rdata_ivl	= pgm_msecs(1000);
	const void* optval	= &nak_rdata_ivl;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_nak_rdata_ivl failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_NAK_DATA_RETRIES,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_nak_data_retries_pass_001)
{
	pgm_sock_t* sock	= generate_sock ();
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NAK_DATA_RETRIES;
	const int retries	= 1000;
	const void* optval	= &retries;
	const socklen_t optlen	= sizeof(int);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_nak_data_retries failed");
}
END_TEST

START_TEST (test_set_nak_data_retries_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NAK_DATA_RETRIES;
	const int retries	= 1000;
	const void* optval	= &retries;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_nak_data_retries failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_setsockopt (
 *		pgm_sock_t* const	sock,
 *		const int		level = IPPROTO_PGM,
 *		const int		optname = PGM_NAK_NCF_RETRIES,
 *		const void*		optval,
 *		const socklen_t		optlen = sizeof(int)
 *	)
 */

START_TEST (test_set_nak_ncf_retries_pass_001)
{
	pgm_sock_t* sock	= generate_sock ();
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NAK_NCF_RETRIES;
	const int retries	= 1000;
	const void* optval	= &retries;
	const socklen_t optlen	= sizeof(int);
	fail_unless (TRUE == pgm_setsockopt (sock, level, optname, optval, optlen), "set_ncf_data_retries failed");
}
END_TEST

START_TEST (test_set_nak_ncf_retries_fail_001)
{
	const int level		= IPPROTO_PGM;
	const int optname	= PGM_NAK_NCF_RETRIES;
	const int retries	= 1000;
	const void* optval	= &retries;
	const socklen_t optlen	= sizeof(int);
	fail_unless (FALSE == pgm_setsockopt (NULL, level, optname, optval, optlen), "set_ncf_data_retries failed");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_peer_unref = tcase_create ("peer_unref");
	suite_add_tcase (s, tc_peer_unref);
	tcase_add_checked_fixture (tc_peer_unref, mock_setup, NULL);
	tcase_add_test (tc_peer_unref, test_peer_unref_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_peer_unref, test_peer_unref_fail_001, SIGABRT);
#endif

/* formally check-peer-nak-state */
	TCase* tc_check_peer_state = tcase_create ("check-peer-state");
	suite_add_tcase (s, tc_check_peer_state);
	tcase_add_checked_fixture (tc_check_peer_state, mock_setup, NULL);
	tcase_add_test (tc_check_peer_state, test_check_peer_state_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_check_peer_state, test_check_peer_state_fail_001, SIGABRT);
#endif

/* formally min-nak-expiry */
	TCase* tc_min_receiver_expiry = tcase_create ("min-receiver-expiry");
	suite_add_tcase (s, tc_min_receiver_expiry);
	tcase_add_checked_fixture (tc_min_receiver_expiry, mock_setup, NULL);
	tcase_add_test (tc_min_receiver_expiry, test_min_receiver_expiry_pass_001);
#ifndef PGM_CHECK_NOFORK
	tcase_add_test_raise_signal (tc_min_receiver_expiry, test_min_receiver_expiry_fail_001, SIGABRT);
#endif

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
