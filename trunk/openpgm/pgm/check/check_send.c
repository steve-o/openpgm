
#include <stdlib.h>
#include <check.h>

#include "check_pgm.h"

#include "pgm/if.h"
#include <../transport.c>

static pgm_transport_t* g_transport = NULL;
static gsize max_tsdu = 0;


static void
setup (void)
{
	g_assert( NULL == g_transport );

	pgm_gsi_t	gsi;
	const char*	network = "";
	guint		port = 7500;
	struct pgm_sock_mreq recv_smr, send_smr;
	int		smr_len = 1;
	guint		max_tpdu = 1500;
	guint		txw_sqns = 10;
	guint		rxw_sqns = 10;
	guint		hops = 16;
	guint		spm_ambient = 8192*1000;
        guint		spm_heartbeat[] = { 1*1000, 1*1000, 2*1000, 4*1000, 8*1000, 16*1000, 32*1000, 64*1000, 128*1000,
 					  256*1000, 512*1000, 1024*1000, 2048*1000, 4096*1000, 8192*1000 };
	guint		peer_expiry = 5*8192*1000;
	guint		spmr_expiry = 250*1000;
	guint		nak_bo_ivl = 50*1000;
	guint		nak_rpt_ivl = 200*1000;
	guint		nak_rdata_ivl = 200*1000;
	guint		nak_data_retries = 5;
	guint		nak_ncf_retries = 2;

	pgm_init();

	g_assert( 0 == pgm_create_md5_gsi (&gsi) );
	g_assert( 0 == pgm_if_parse_transport (network, AF_INET, &recv_smr, &send_smr, &smr_len) );
	g_assert( 1 == smr_len );
	g_assert( 0 == pgm_transport_create (&g_transport, &gsi, port, &recv_smr, 1, &send_smr) );
	g_assert( 0 == pgm_transport_set_max_tpdu (g_transport, max_tpdu) );
	g_assert( 0 == pgm_transport_set_txw_sqns (g_transport, txw_sqns) );
	g_assert( 0 == pgm_transport_set_rxw_sqns (g_transport, rxw_sqns) );
	g_assert( 0 == pgm_transport_set_hops (g_transport, hops) );
	g_assert( 0 == pgm_transport_set_ambient_spm (g_transport, spm_ambient) );
	g_assert( 0 == pgm_transport_set_heartbeat_spm (g_transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat)) );
	g_assert( 0 == pgm_transport_set_peer_expiry (g_transport, peer_expiry) );
	g_assert( 0 == pgm_transport_set_spmr_expiry (g_transport, spmr_expiry) );
	g_assert( 0 == pgm_transport_set_nak_bo_ivl (g_transport, nak_bo_ivl) );
	g_assert( 0 == pgm_transport_set_nak_rpt_ivl (g_transport, nak_rpt_ivl) );
	g_assert( 0 == pgm_transport_set_nak_rdata_ivl (g_transport, nak_rdata_ivl) );
	g_assert( 0 == pgm_transport_set_nak_data_retries (g_transport, nak_data_retries) );
	g_assert( 0 == pgm_transport_set_nak_ncf_retries (g_transport, nak_ncf_retries) );
	g_assert( 0 == pgm_transport_set_send_only (g_transport) );
	g_assert( 0 == pgm_transport_bind (g_transport) );
}

static void
teardown (void)
{
	g_assert( g_transport );
	pgm_transport_destroy (g_transport, TRUE);
	g_transport = NULL;
}

/* target: pgm_transport_send_one (
 * 		   pgm_transport_t*        transport,
 * 		   gconstpointer           tsdu,
 * 		   gsize                   tsdu_length,
 * 		   G_GNUC_UNUSED int       flags
 * 		   )
 *
 *  pre-condition: transport is valid,
 *		   buf is offset to payload from txw allocated packet.
 * post-condition: return value equals count.
 */
START_TEST (test_send_one)
{
	fail_unless (g_transport);

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	gpointer	pkt		= (guint8*)pgm_txw_alloc (g_transport->txw) + pgm_transport_pkt_offset (FALSE);
	gsize		tsdu_length	= _i;
	int		flags		= 0;
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);

	fail_unless ((gssize)tsdu_length == pgm_transport_send_one (g_transport, pkt, tsdu_length, flags));
}
END_TEST

START_TEST (test_send_one_fail)
{
	fail_unless (g_transport);

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	gpointer	pkt		= (guint8*)pgm_txw_alloc (g_transport->txw) + pgm_transport_pkt_offset (FALSE);
	gsize		tsdu_length	= max_tsdu + 1;
	int		flags		= 0;
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);

	fail_unless (-EINVAL == pgm_transport_send_one (g_transport, pkt, tsdu_length, flags));
}
END_TEST

/* target: pgm_transport_send_one_copy (
 * 		   pgm_transport_t*        transport,
 * 		   gconstpointer           tsdu,
 * 		   gsize                   tsdu_length,
 * 		   G_GNUC_UNUSED int       flags
 * 		   )
 *
 *  pre-condition: transport is valid
 *                 txw_lock
 * post-condition: txw_lock unlocked
 */
START_TEST (test_send_one_copy_000)
{
	fail_unless (g_transport);

	guint8		buffer[ 1500 ];
	gsize		tsdu_length	= _i;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_one_copy (g_transport, buffer, tsdu_length, flags));
}
END_TEST

/* null pointer
 */
START_TEST (test_send_one_copy_001)
{
	fail_unless (g_transport);

	gpointer	ptr		= NULL;
	gsize		tsdu_length	= 0;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_one_copy (g_transport, ptr, tsdu_length, flags));
}
END_TEST

START_TEST (test_send_one_copy_fail)
{
	fail_unless (g_transport);

	guint8		buffer[ 1500 ];
	gsize		tsdu_length	= max_tsdu + 1;
	int		flags		= 0;

	fail_unless (-EINVAL == pgm_transport_send_one (g_transport, buffer, tsdu_length, flags));
}
END_TEST

/* target: pgm_transport_send_onev (
 * 		   pgm_transport_t*        transport,
 * 		   const struct iovec*     vector,
 * 		   guint                   count,
 * 		   G_GNUC_UNUSED int       flags
 * 		   )
 *
 *  pre-condition: transport is valid,
 *		   buf is offset to payload from txw allocated packet.
 * post-condition: return value equals count.
 */
START_TEST (test_send_onev_000)
{
	fail_unless (g_transport);

	guint8		buffer[ 1500 ];
	struct iovec	vector[ 1 ]	= { { .iov_base = buffer, .iov_len = _i } };
	guint		count		= 1;
	gsize		tsdu_length	= _i;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_onev (g_transport, vector, count, flags));
}
END_TEST

/* zero length TSDU in non-zero vector
 */
START_TEST (test_send_onev_001)
{
	fail_unless (g_transport);

	guint8		buffer[ 1500 ];
	struct iovec	vector[ 1 ]	= { { .iov_base = buffer, .iov_len = 0 } };
	guint		count		= 1;
	gsize		tsdu_length	= 0;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_onev (g_transport, vector, count, flags));
}
END_TEST

/* zero length vector
 */
START_TEST (test_send_onev_002)
{
	fail_unless (g_transport);

	guint8		buffer[ 1500 ];
	struct iovec	vector[ 1 ]	= { { .iov_base = buffer, .iov_len = 0 } };
	guint		count		= 0;
	gsize		tsdu_length	= 0;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_onev (g_transport, vector, count, flags));
}
END_TEST

/* null vector
 */
START_TEST (test_send_onev_003)
{
	fail_unless (g_transport);

	struct iovec*	vector		= NULL;
	guint		count		= 0;
	gsize		tsdu_length	= 0;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_onev (g_transport, vector, count, flags));
}
END_TEST

/* zero lead vector
 */
START_TEST (test_send_onev_004)
{
	fail_unless (g_transport);

	guint8		buffer[ 1500 ];
	struct iovec	vector[ 2 ]	= { { .iov_base = NULL,   .iov_len =  0 },
					    { .iov_base = buffer, .iov_len = _i } };
	guint		count		= 2;
	gsize		tsdu_length	= _i;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_onev (g_transport, vector, count, flags));
}
END_TEST

/* zero trail vector
 */
START_TEST (test_send_onev_005)
{
	fail_unless (g_transport);

	guint8		buffer[ 1500 ];
	struct iovec	vector[ 2 ]	= { { .iov_base = buffer, .iov_len = _i },
					    { .iov_base = NULL,   .iov_len =  0 } };
	guint		count		= 2;
	gsize		tsdu_length	= _i;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_onev (g_transport, vector, count, flags));
}
END_TEST

/* flattened vector
 */
START_TEST (test_send_onev_006)
{
	fail_unless (g_transport);

	guint8		buffer[ 1500 ];
	struct iovec	vector[ 1500 ];
	for (int _j = 0; _j < _i; _j++)
	{
		vector[_j].iov_base = &buffer[ _j ];
		vector[_j].iov_len  = 1;
	}
	guint		count		= _i;
	gsize		tsdu_length	= _i;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_onev (g_transport, vector, count, flags));
}
END_TEST

/* too long
 */
START_TEST (test_send_onev_fail)
{
	fail_unless (g_transport);

	guint8		buffer[ 1500 ];
	struct iovec	vector[ 1 ]	= { { .iov_base = buffer, .iov_len = max_tsdu + 1 } };
	guint		count		= 1;
	int		flags		= 0;

	fail_unless (-EINVAL == pgm_transport_send_onev (g_transport, vector, count, flags));
}
END_TEST


Suite*
make_send_suite (void)
{
	Suite* s = suite_create ("Send");

	TCase* tc_send_one = tcase_create ("send_one");
	TCase* tc_send_one_copy = tcase_create ("send_one_copy");
	TCase* tc_send_onev = tcase_create ("send_onev");

	tcase_add_checked_fixture (tc_send_one, setup, teardown);
	tcase_add_checked_fixture (tc_send_one_copy, setup, teardown);
	tcase_add_checked_fixture (tc_send_onev, setup, teardown);

	max_tsdu = 1500 - 20 - pgm_transport_pkt_offset (FALSE);

	suite_add_tcase (s, tc_send_one);
	tcase_add_loop_test (tc_send_one, test_send_one, 0, 4);
	tcase_add_loop_test (tc_send_one, test_send_one, max_tsdu - 4, max_tsdu);
	tcase_add_test (tc_send_one, test_send_one_fail);

	suite_add_tcase (s, tc_send_one_copy);
	tcase_add_loop_test (tc_send_one_copy, test_send_one_copy_000, 0, 4);
	tcase_add_loop_test (tc_send_one_copy, test_send_one_copy_000, max_tsdu - 4, max_tsdu);
	tcase_add_test (tc_send_one_copy, test_send_one_copy_001);
	tcase_add_test (tc_send_one_copy, test_send_one_copy_fail);

	suite_add_tcase (s, tc_send_onev);
	tcase_add_loop_test (tc_send_onev, test_send_onev_000, 0, 4);
	tcase_add_loop_test (tc_send_onev, test_send_onev_000, max_tsdu - 4, max_tsdu);
	tcase_add_test (tc_send_onev, test_send_onev_001);
	tcase_add_test (tc_send_onev, test_send_onev_002);
	tcase_add_test (tc_send_onev, test_send_onev_003);
	tcase_add_loop_test (tc_send_onev, test_send_onev_004, 0, 4);
	tcase_add_loop_test (tc_send_onev, test_send_onev_004, max_tsdu - 4, max_tsdu);
	tcase_add_loop_test (tc_send_onev, test_send_onev_005, 0, 4);
	tcase_add_loop_test (tc_send_onev, test_send_onev_005, max_tsdu - 4, max_tsdu);
	tcase_add_loop_test (tc_send_onev, test_send_onev_006, 0, 4);
	tcase_add_loop_test (tc_send_onev, test_send_onev_006, max_tsdu - 4, max_tsdu);
	tcase_add_test (tc_send_onev, test_send_onev_fail);

	return s;
}

/* eof */
