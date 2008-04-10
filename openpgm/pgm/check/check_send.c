
#include <stdlib.h>
#include <check.h>

#include "check_pgm.h"

#include "pgm/if.h"
#include <../transport.c>

static pgm_transport_t* g_transport = NULL;


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

/* target: pgm_transport_send_one_unlocked (
 * 		   pgm_transport_t*        transport,
 * 		   gconstpointer           buf,
 * 		   gsize                   count,
 * 		   G_GNUC_UNUSED int       flags
 * 		   )
 *
 *  pre-condition: transport is valid
 *                 txw_lock
 * post-condition: txw_lock unlocked
 */

START_TEST (test_pgm_transport_send_one_unlocked)
{
	fail_unless (g_transport,
			"Invalid transport.");

	gconstpointer	pkt		= (guint8*)pgm_txw_alloc (g_transport->txw) + pgm_transport_pkt_offset (FALSE);
	gsize		tsdu_length	= 2;
	int		flags		= 0;

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	fail_unless ((gssize)tsdu_length == pgm_transport_send_one_unlocked (g_transport, pkt, tsdu_length, flags));
	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);
}
END_TEST

Suite*
make_send_suite (void)
{
	Suite* s = suite_create ("Send");

	TCase* tc_send = tcase_create ("Basic");

	tcase_add_checked_fixture (tc_send, setup, teardown);

	suite_add_tcase (s, tc_send);
	tcase_add_test (tc_send, test_pgm_transport_send_one_unlocked);

	return s;
}

/* eof */
