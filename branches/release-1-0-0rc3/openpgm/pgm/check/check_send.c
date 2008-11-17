
#include <stdlib.h>
#include <check.h>

#include "check_pgm.h"

#include "pgm/if.h"
#include <../transport.c>

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

static pgm_transport_t* g_transport = NULL;
static gsize		g_max_tsdu = 0;
static gsize		g_max_apdu = 0;


static void
setup (void)
{
	g_assert( NULL == g_transport );

	pgm_gsi_t	gsi;
	struct group_source_req recv_gsr, send_gsr;
	int		gsr_len = 1;
	const guint	spm_heartbeat[] = PGM_SPM_HEARTBEAT_INIT;

	pgm_init();

	g_assert( 0 == pgm_create_md5_gsi (&gsi) );
	g_assert( 0 == pgm_if_parse_transport (PGM_NETWORK, AF_INET, &recv_gsr, &send_gsr, &gsr_len) );
	g_assert( 1 == gsr_len );
	g_assert( 0 == pgm_transport_create (&g_transport, &gsi, PGM_PORT, &recv_gsr, 1, &send_gsr) );
	g_assert( 0 == pgm_transport_set_max_tpdu (g_transport, PGM_MAX_TPDU) );
	g_assert( 0 == pgm_transport_set_txw_sqns (g_transport, PGM_TXW_SQNS) );
	g_assert( 0 == pgm_transport_set_rxw_sqns (g_transport, PGM_RXW_SQNS) );
	g_assert( 0 == pgm_transport_set_hops (g_transport, PGM_HOPS) );
	g_assert( 0 == pgm_transport_set_ambient_spm (g_transport, PGM_SPM_AMBIENT) );
	g_assert( 0 == pgm_transport_set_heartbeat_spm (g_transport, spm_heartbeat, G_N_ELEMENTS(spm_heartbeat)) );
	g_assert( 0 == pgm_transport_set_peer_expiry (g_transport, PGM_PEER_EXPIRY) );
	g_assert( 0 == pgm_transport_set_spmr_expiry (g_transport, PGM_SPMR_EXPIRY) );
	g_assert( 0 == pgm_transport_set_nak_bo_ivl (g_transport, PGM_NAK_BO_IVL) );
	g_assert( 0 == pgm_transport_set_nak_rpt_ivl (g_transport, PGM_NAK_RPT_IVL) );
	g_assert( 0 == pgm_transport_set_nak_rdata_ivl (g_transport, PGM_NAK_RDATA_IVL) );
	g_assert( 0 == pgm_transport_set_nak_data_retries (g_transport, PGM_NAK_DATA_RETRIES) );
	g_assert( 0 == pgm_transport_set_nak_ncf_retries (g_transport, PGM_NAK_NCF_RETRIES) );
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
 * 		   int 			   flags
 * 		   )
 *
 *  pre-condition: transport is valid,
 *		   tsdu is offset to payload from txw allocated packet.
 *		   tsdu_length is less than max_tsdu.
 * post-condition: return value equals tsdu_length.
 */
START_TEST (test_send_one)
{
	fail_unless (g_transport);

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	gpointer	pkt		= pgm_packetv_alloc (g_transport, FALSE);
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
	gpointer	pkt		= pgm_packetv_alloc (g_transport, FALSE);
	gsize		tsdu_length	= g_max_tsdu + 1;
	int		flags		= 0;
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);

	fail_unless (-EINVAL == pgm_transport_send_one (g_transport, pkt, tsdu_length, flags));
}
END_TEST

/* target: pgm_transport_send_one_copy (
 * 		   pgm_transport_t*        transport,
 * 		   gconstpointer           tsdu,
 * 		   gsize                   tsdu_length,
 * 		   int 			   flags
 * 		   )
 *
 *  pre-condition: transport is valid,
 *  		   tsdu points to a payload buffer less than max_tsdu in length.
 * post-condition: return value equals tsdu_length.
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

	guint8		buffer[ PGM_MAX_TPDU ];
	gsize		tsdu_length	= g_max_tsdu + 1;
	int		flags		= 0;

	fail_unless (-EINVAL == pgm_transport_send_one (g_transport, buffer, tsdu_length, flags));
}
END_TEST

/* target: pgm_transport_send_onev (
 * 		   pgm_transport_t*        transport,
 * 		   const struct iovec*     vector,
 * 		   guint                   count,
 * 		   int		           flags
 * 		   )
 *
 *  pre-condition: transport is valid,
 *  		   vector contains count elements.
 * post-condition: return value equals ∑ iovec::iov_len.
 */
START_TEST (test_send_onev_000)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_MAX_TPDU ];
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

	guint8		buffer[ PGM_MAX_TPDU ];
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

	guint8		buffer[ PGM_MAX_TPDU ];
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

	guint8		buffer[ PGM_MAX_TPDU ];
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

	guint8		buffer[ PGM_MAX_TPDU ];
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

	guint8		buffer[ PGM_MAX_TPDU ];
	struct iovec	vector[ PGM_MAX_TPDU ];
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

	guint8		buffer[ PGM_MAX_TPDU ];
	struct iovec	vector[ 1 ]	= { { .iov_base = buffer, .iov_len = g_max_tsdu + 1 } };
	guint		count		= 1;
	int		flags		= 0;

	fail_unless (-EINVAL == pgm_transport_send_onev (g_transport, vector, count, flags));
}
END_TEST

/* target: pgm_transport_send (
 * 		   pgm_transport_t*        transport,
 * 		   gconstpointer           apdu,
 * 		   gsize                   apdu_length,
 * 		   int			   flags
 * 		   )
 *
 *  pre-condition: transport is valid,
 *                 apdu must point to something valid if apdu_length > 0.
 * post-condition: return value equals apdu_length.
 */
START_TEST (test_send_000)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gsize		tsdu_length	= _i;
	int		flags		= 0;

	fail_unless ((gssize)tsdu_length == pgm_transport_send (g_transport, buffer, tsdu_length, flags));
}
END_TEST

/* too long
 */
START_TEST (test_send_fail)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	gsize		tsdu_length	= g_max_apdu + 1;
	int		flags		= 0;

	fail_unless (-EINVAL == pgm_transport_send (g_transport, buffer, tsdu_length, flags));
}
END_TEST

/* target: pgm_transport_sendv (
 * 		   pgm_transport_t*        transport,
 * 		   const struct iovec*     vector,
 * 		   guint                   count,
 * 		   int			   flags,
 * 		   gboolean		   is_one_apdu
 * 		   )
 *
 *  pre-condition: transport is valid,
 *                 vector must point to something valid if count > 0.
 * post-condition: return value equals apdu_length.
 */
START_TEST (test_sendv_000)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	struct iovec	vector[ 1 ]	= { { .iov_base = buffer, .iov_len = _i } };
	guint		count		= 1;
	gsize		tsdu_length	= _i;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_sendv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* zero length TSDU in non-zero vector
 */
START_TEST (test_sendv_001)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	struct iovec	vector[ 1 ]	= { { .iov_base = buffer, .iov_len = 0 } };
	guint		count		= 1;
	gsize		tsdu_length	= 0;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_sendv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* zero length vector
 */
START_TEST (test_sendv_002)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	struct iovec	vector[ 1 ]	= { { .iov_base = buffer, .iov_len = 0 } };
	guint		count		= 0;
	gsize		tsdu_length	= 0;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_sendv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* null vector
 */
START_TEST (test_sendv_003)
{
	fail_unless (g_transport);

	struct iovec*	vector		= NULL;
	guint		count		= 0;
	gsize		tsdu_length	= 0;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_sendv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* zero lead vector
 */
START_TEST (test_sendv_004)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	struct iovec	vector[ 2 ]	= { { .iov_base = NULL,   .iov_len =  0 },
					    { .iov_base = buffer, .iov_len = _i } };
	guint		count		= 2;
	gsize		tsdu_length	= _i;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_sendv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* zero trail vector
 */
START_TEST (test_sendv_005)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	struct iovec	vector[ 2 ]	= { { .iov_base = buffer, .iov_len = _i },
					    { .iov_base = NULL,  .iov_len =  0 } };
	guint		count		= 2;
	gsize		tsdu_length	= _i;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_sendv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* flattened vector
 */
START_TEST (test_sendv_006)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	struct iovec	vector[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	for (int _j = 0; _j < _i; _j++)
	{
		vector[_j].iov_base = &buffer[ _j ];
		vector[_j].iov_len  = 1;
	}
	guint		count		= _i;
	gsize		tsdu_length	= _i;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_sendv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* packet length multiples
 */
START_TEST (test_sendv_007)
{
	fail_unless (g_transport);
	fail_unless ( (PGM_TXW_SQNS % (2 << _i)) == 0 );

	guint8		buffer[ PGM_TXW_SQNS * PGM_MAX_TPDU ];
	struct iovec	vector[ PGM_TXW_SQNS ];
	guint		factor = 2 << _i;
	int		count = PGM_TXW_SQNS / factor;
	gsize		tsdu_length	= 0;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;
	for (int _j = 0; _j < count; _j++)
	{
		vector[_j].iov_base = &buffer[ tsdu_length ];
		vector[_j].iov_len  = factor * pgm_transport_max_tsdu (g_transport, TRUE);
		tsdu_length	   += factor * pgm_transport_max_tsdu (g_transport, TRUE);
	}

	fail_unless ((gssize)tsdu_length == pgm_transport_sendv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* too long
 */
START_TEST (test_sendv_fail)
{
	fail_unless (g_transport);

	guint8		buffer[ PGM_MAX_TPDU ];
	struct iovec	vector[ 1 ]	= { { .iov_base = buffer, .iov_len = g_max_apdu + 1 } };
	guint		count		= 1;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless (-EINVAL == pgm_transport_sendv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* target: pgm_transport_send_packetv (
 * 		   pgm_transport_t*        transport,
 * 		   const struct iovec*     vector,
 * 		   guint                   count,
 * 		   int			   flags,
 * 		   gboolean		   is_one_apdu
 * 		   )
 *
 *  pre-condition: transport is valid,
 *                 vector must point to something valid if count > 0.
 *                 vector::iov_base must be offset to payload in packets.
 * post-condition: return value equals apdu_length.
 */
START_TEST (test_send_packetv_000)
{
	fail_unless (g_transport);

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	gpointer	pkt		= pgm_packetv_alloc (g_transport, FALSE);
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);

	gsize		tsdu_length	= _i;
	int		flags		= 0;
	struct iovec	vector[ 1 ]	= { { .iov_base = pkt, .iov_len = _i } };
	guint		count		= 1;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_packetv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* zero length TSDU in non-zero vector
 */
START_TEST (test_send_packetv_001)
{
	fail_unless (g_transport);

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	gpointer	pkt		= pgm_packetv_alloc (g_transport, FALSE);
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);

	gsize		tsdu_length	= 0;
	int		flags		= 0;
	struct iovec	vector[ 1 ]	= { { .iov_base = pkt, .iov_len = 0 } };
	guint		count		= 1;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_packetv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* zero length vector
 */
START_TEST (test_send_packetv_002)
{
	fail_unless (g_transport);

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	gpointer	pkt		= pgm_packetv_alloc (g_transport, FALSE);
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);

	gsize		tsdu_length	= 0;
	int		flags		= 0;
	struct iovec	vector[ 1 ]	= { { .iov_base = pkt, .iov_len = 0 } };
	guint		count		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless ((gssize)tsdu_length == pgm_transport_send_packetv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* multiple packet apdus
 */
START_TEST (test_send_packetv_003)
{
	fail_unless (g_transport);

	struct iovec	vector[ PGM_TXW_SQNS ];
	int		count		= _i;
	gsize		tsdu_length	= 0;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	for (int _j = 0; _j < count; _j++)
	{
		vector[_j].iov_base = pgm_packetv_alloc (g_transport, count > 1);
		vector[_j].iov_len  = pgm_transport_max_tsdu (g_transport, count > 1);
		tsdu_length	   += pgm_transport_max_tsdu (g_transport, count > 1);
	}
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);

	fail_unless ((gssize)tsdu_length == pgm_transport_send_packetv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* too long on single packet APDU
 */
START_TEST (test_send_packetv_fail_000)
{
	fail_unless (g_transport);

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	gpointer	pkt		= pgm_packetv_alloc (g_transport, FALSE);
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);

	int		flags		= 0;
	struct iovec	vector[ 1 ]	= { { .iov_base = pkt, .iov_len = pgm_transport_max_tsdu (g_transport, FALSE) + 1 } };
	guint		count		= 1;
	gboolean	is_one_apdu	= TRUE;

	fail_unless (-EINVAL == pgm_transport_send_packetv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST

/* too long on multiple packet APDU
 */
START_TEST (test_send_packetv_fail_001)
{
	fail_unless (g_transport);

	struct iovec 	vector[ 2 ];
	int		count		= 2;
	int		flags		= 0;
	gboolean	is_one_apdu	= TRUE;

	fail_unless (g_static_rw_lock_writer_trylock (&g_transport->txw_lock));
	for (int _j = 0; _j < count; _j++)
	{
		vector[_j].iov_base = pgm_packetv_alloc (g_transport, count > 1);
		vector[_j].iov_len  = pgm_transport_max_tsdu (g_transport, count > 1) + 1;
	}
	g_static_rw_lock_writer_unlock (&g_transport->txw_lock);

	fail_unless (-EINVAL == pgm_transport_send_packetv (g_transport, vector, count, flags, is_one_apdu));
}
END_TEST


Suite*
make_send_suite (void)
{
	Suite* s = suite_create ("pgm_transport_send*()");

	TCase* tc_send_one = tcase_create ("send_one");
	TCase* tc_send_one_copy = tcase_create ("send_one_copy");
	TCase* tc_send_onev = tcase_create ("send_onev");
	TCase* tc_send = tcase_create ("send");
	TCase* tc_sendv = tcase_create ("sendv");
	TCase* tc_send_packetv = tcase_create ("send_packetv");

	tcase_add_checked_fixture (tc_send_one, setup, teardown);
	tcase_add_checked_fixture (tc_send_one_copy, setup, teardown);
	tcase_add_checked_fixture (tc_send_onev, setup, teardown);
	tcase_add_checked_fixture (tc_send, setup, teardown);
	tcase_add_checked_fixture (tc_sendv, setup, teardown);
	tcase_add_checked_fixture (tc_send_packetv, setup, teardown);

	g_max_tsdu = PGM_MAX_TPDU - 20 - pgm_transport_pkt_offset (FALSE);
	g_max_apdu = ( PGM_MAX_TPDU - 20 - pgm_transport_pkt_offset (TRUE) ) * PGM_TXW_SQNS;

/* one packet api */
	suite_add_tcase (s, tc_send_one);
	tcase_add_loop_test (tc_send_one, test_send_one, 0, 4);
	tcase_add_loop_test (tc_send_one, test_send_one, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_test (tc_send_one, test_send_one_fail);

	suite_add_tcase (s, tc_send_one_copy);
	tcase_add_loop_test (tc_send_one_copy, test_send_one_copy_000, 0, 4);
	tcase_add_loop_test (tc_send_one_copy, test_send_one_copy_000, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_test (tc_send_one_copy, test_send_one_copy_001);
	tcase_add_test (tc_send_one_copy, test_send_one_copy_fail);

	suite_add_tcase (s, tc_send_onev);
	tcase_add_loop_test (tc_send_onev, test_send_onev_000, 0, 4);
	tcase_add_loop_test (tc_send_onev, test_send_onev_000, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_test (tc_send_onev, test_send_onev_001);
	tcase_add_test (tc_send_onev, test_send_onev_002);
	tcase_add_test (tc_send_onev, test_send_onev_003);
	tcase_add_loop_test (tc_send_onev, test_send_onev_004, 0, 4);
	tcase_add_loop_test (tc_send_onev, test_send_onev_004, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_loop_test (tc_send_onev, test_send_onev_005, 0, 4);
	tcase_add_loop_test (tc_send_onev, test_send_onev_005, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_loop_test (tc_send_onev, test_send_onev_006, 0, 4);
	tcase_add_loop_test (tc_send_onev, test_send_onev_006, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_test (tc_send_onev, test_send_onev_fail);

/* multi-packet api */
	suite_add_tcase (s, tc_send);
	tcase_add_loop_test (tc_send, test_send_000, 0, 4);
	tcase_add_loop_test (tc_send, test_send_000, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_loop_test (tc_send, test_send_000, g_max_tsdu + 1, g_max_tsdu + 4);
	tcase_add_loop_test (tc_send, test_send_000, g_max_apdu - 4, g_max_apdu);
	tcase_add_test (tc_send, test_send_fail);

	suite_add_tcase (s, tc_sendv);
	tcase_add_loop_test (tc_sendv, test_sendv_000, 0, 4);
	tcase_add_loop_test (tc_sendv, test_sendv_000, g_max_apdu - 4, g_max_apdu);
	tcase_add_test (tc_sendv, test_sendv_001);
	tcase_add_test (tc_sendv, test_sendv_002);
	tcase_add_test (tc_sendv, test_sendv_003);
	tcase_add_loop_test (tc_sendv, test_sendv_004, 0, 4);
	tcase_add_loop_test (tc_sendv, test_sendv_004, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_loop_test (tc_sendv, test_sendv_004, g_max_tsdu + 1, g_max_tsdu + 4);
	tcase_add_loop_test (tc_sendv, test_sendv_004, g_max_apdu - 4, g_max_apdu);
	tcase_add_loop_test (tc_sendv, test_sendv_005, 0, 4);
	tcase_add_loop_test (tc_sendv, test_sendv_005, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_loop_test (tc_sendv, test_sendv_005, g_max_tsdu + 1, g_max_tsdu + 4);
	tcase_add_loop_test (tc_sendv, test_sendv_005, g_max_apdu - 4, g_max_apdu);
	tcase_add_loop_test (tc_sendv, test_sendv_006, 0, 4);
	tcase_add_loop_test (tc_sendv, test_sendv_006, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_loop_test (tc_sendv, test_sendv_006, g_max_tsdu + 1, g_max_tsdu + 4);
	tcase_add_loop_test (tc_sendv, test_sendv_006, g_max_apdu - 4, g_max_apdu);
	tcase_add_loop_test (tc_sendv, test_sendv_007, 0, pgm_power2_log2 (PGM_TXW_SQNS));
	tcase_add_test (tc_sendv, test_sendv_fail);

/* zero-copy api */
	suite_add_tcase (s, tc_send_packetv);
	tcase_add_loop_test (tc_send_packetv, test_send_packetv_000, 0, 4);
	tcase_add_loop_test (tc_send_packetv, test_send_packetv_000, g_max_tsdu - 4, g_max_tsdu);
	tcase_add_test (tc_send_packetv, test_send_packetv_001);
	tcase_add_test (tc_send_packetv, test_send_packetv_002);
	tcase_add_loop_test (tc_send_packetv, test_send_packetv_003, 0, PGM_TXW_SQNS);
	tcase_add_test (tc_send_packetv, test_send_packetv_fail_000);
	tcase_add_test (tc_send_packetv, test_send_packetv_fail_001);

	return s;
}

/* eof */
