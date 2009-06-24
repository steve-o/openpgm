
#include <stdlib.h>
#include <check.h>

#define TXW_DEBUG
#include "txwi.c"


/* generate valid skb, data pointer pointing to PGM payload
 */
static
struct pgm_sk_buff_t*
generate_valid_skb (void)
{
	const guint16 tsdu_length = 1000;
	const guint16 header_length = sizeof(struct pgm_header) + sizeof(struct pgm_data);
	struct pgm_sk_buff_t* skb = pgm_alloc_skb (1500);
/* fake but valid transport and timestamp */
	skb->transport = (pgm_transport_t*)0x1;
	skb->tstamp = 1;
/* header */
	pgm_skb_reserve (skb, header_length);
	memset (skb->head, 0, header_length);
	skb->pgm_header = (struct pgm_header*)skb->head;
	skb->pgm_data   = (struct pgm_data*)(skb->pgm_header + 1);
	skb->pgm_header->pgm_type = PGM_ODATA;
	skb->pgm_header->pgm_tsdu_length = g_htons (tsdu_length);
/* DATA */
	pgm_skb_put (skb, tsdu_length);
	return skb;
}

/* target:
 *	pgm_txw_t*
 *	pgm_txw_init (
 *		const guint16		tpdu_size,
 *		const guint32		sqns,
 *		const guint		secs,
 *		const guint		max_rte
 *		)
 */

/* vanilla sequence count window */
START_TEST (test_init_pass_001)
{
	fail_if (NULL == pgm_txw_init (0, 100, 0, 0));
}
END_TEST

/* vanilla time based window */
START_TEST (test_init_pass_002)
{
	fail_if (NULL == pgm_txw_init (1500, 0, 60, 800000));
}
END_TEST

/* jumbo frame */
START_TEST (test_init_pass_003)
{
	fail_if (NULL == pgm_txw_init (9000, 0, 60, 800000));
}
END_TEST

/* max frame */
START_TEST (test_init_pass_004)
{
	fail_if (NULL == pgm_txw_init (UINT16_MAX, 0, 60, 800000));
}
END_TEST

/* invalid tpdu size */
START_TEST (test_init_fail_001)
{
	fail_unless (NULL == pgm_txw_init (0, 0, 60, 800000));
}
END_TEST

/* no specified sequence count or time value */
START_TEST (test_init_fail_002)
{
	fail_unless (NULL == pgm_txw_init (0, 0, 0, 800000));
}
END_TEST

/* no specified rate */
START_TEST (test_init_fail_003)
{
	fail_unless (NULL == pgm_txw_init (0, 0, 60, 0));
}
END_TEST

/* all invalid */
START_TEST (test_init_fail_004)
{
	fail_unless (NULL == pgm_txw_init (0, 0, 0, 0));
}
END_TEST

/* target:
 *	int
 *	pgm_txw_shutdown (
 *		pgm_txw_t* const	window
 *		)
 */

START_TEST (test_shutdown_pass_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

START_TEST (test_shutdown_fail_001)
{
	fail_unless (-1 == pgm_txw_shutdown (NULL));
}
END_TEST

/* target:
 *	void
 *	pgm_txw_add (
 *		pgm_txw_t* const		window,
 *		struct pgm_sk_buff_t* const	skb
 *		)
 * failures raise assert errors and stop process execution.
 */

START_TEST (test_add_pass_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

/* null skb */
START_TEST (test_add_fail_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	pgm_txw_add (window, NULL);
	fail ();
}
END_TEST

/* null window */
START_TEST (test_add_fail_002)
{
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (NULL, skb);
	fail ();
}
END_TEST

/* null skb content */
START_TEST (test_add_fail_003)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	char buffer[1500];
	memset (buffer, 0, sizeof(buffer));
	pgm_txw_add (window, (struct pgm_sk_buff_t*)buffer);
	fail ();
}
END_TEST

/* target:
 *	struct pgm_sk_buff_t*
 *	pgm_txw_peek (
 *		pgm_txw_t* const	window,
 *		const guint32		sequence
 *		)
 */

START_TEST (test_peek_pass_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_unless (skb == pgm_txw_peek (window, window->trail));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

/* null window */
START_TEST (test_peek_fail_001)
{
	fail_unless (NULL == pgm_txw_peek (NULL, 0));
}
END_TEST

/* empty window */
START_TEST (test_peek_fail_002)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	fail_unless (NULL == pgm_txw_peek (window, window->trail));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

/** inline function tests **/
/* pgm_txw_max_length () 
 */
START_TEST (test_max_length_pass_001)
{
	const guint window_length = 100;
	pgm_txw_t* window = pgm_txw_init (0, window_length, 0, 0);
	fail_if (NULL == window);
	fail_unless (window_length == pgm_txw_max_length (window));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

START_TEST (test_max_length_fail_001)
{
	pgm_txw_max_length (NULL);
	fail ();
}
END_TEST

/* pgm_txw_length () 
 */
START_TEST (test_length_pass_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	fail_unless (0 == pgm_txw_length (window));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_unless (1 == pgm_txw_length (window));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

START_TEST (test_length_fail_001)
{
	pgm_txw_length (NULL);
	fail ();
}
END_TEST

/* pgm_txw_size () 
 */
START_TEST (test_size_pass_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	fail_unless (0 == pgm_txw_size (window));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_unless (1000 == pgm_txw_size (window));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

START_TEST (test_size_fail_001)
{
	pgm_txw_size (NULL);
	fail ();
}
END_TEST

/* pgm_txw_is_empty
 */
START_TEST (test_is_empty_pass_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	fail_unless (pgm_txw_is_empty (window));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_if (pgm_txw_is_empty (window));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

START_TEST (test_is_empty_fail_001)
{
	pgm_txw_is_empty (NULL);
	fail ();
}
END_TEST

/* pgm_txw_is_full
 */
START_TEST (test_is_full_pass_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 1, 0, 0);
	fail_if (NULL == window);
	fail_if (pgm_txw_is_full (window));
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_unless (pgm_txw_is_full (window));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

START_TEST (test_is_full_fail_001)
{
	pgm_txw_is_full (NULL);
	fail ();
}
END_TEST

/* pgm_txw_lead
 */
START_TEST (test_lead_pass_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 100, 0, 0);
	fail_if (NULL == window);
	guint32 lead = pgm_txw_lead (window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_unless (lead + 1 == pgm_txw_lead (window));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

START_TEST (test_lead_fail_001)
{
	pgm_txw_lead (NULL);
	fail ();
}
END_TEST

/* pgm_txw_next_lead
 */
START_TEST (test_next_lead_pass_001)
{
	const guint window_length = 100;
	pgm_txw_t* window = pgm_txw_init (0, window_length, 0, 0);
	fail_if (NULL == window);
	guint32 next_lead = pgm_txw_next_lead (window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_unless (next_lead == pgm_txw_lead (window));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

START_TEST (test_next_lead_fail_001)
{
	pgm_txw_next_lead (NULL);
	fail ();
}
END_TEST

/* pgm_txw_trail
 */
START_TEST (test_trail_pass_001)
{
	pgm_txw_t* window = pgm_txw_init (0, 1, 0, 0);
	fail_if (NULL == window);
/* does not advance with adding skb */
	guint32 trail = pgm_txw_trail (window);
	struct pgm_sk_buff_t* skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_unless (trail == pgm_txw_trail (window));
/* does advance when filling up window */
	skb = generate_valid_skb ();
	fail_if (NULL == skb);
	pgm_txw_add (window, skb);
	fail_if (trail == pgm_txw_trail (window));
	fail_unless (0 == pgm_txw_shutdown (window));
}
END_TEST

START_TEST (test_trail_fail_001)
{
	pgm_txw_trail (NULL);
	fail ();
}
END_TEST

static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_init = tcase_create ("init");
	suite_add_tcase (s, tc_init);
	tcase_add_test (tc_init, test_init_pass_001);
	tcase_add_test (tc_init, test_init_pass_002);
	tcase_add_test (tc_init, test_init_pass_003);
	tcase_add_test (tc_init, test_init_pass_004);
	tcase_add_test (tc_init, test_init_fail_001);
	tcase_add_test (tc_init, test_init_fail_002);
	tcase_add_test (tc_init, test_init_fail_003);
	tcase_add_test (tc_init, test_init_fail_004);

	TCase* tc_shutdown = tcase_create ("shutdown");
	suite_add_tcase (s, tc_shutdown);
	tcase_add_test (tc_shutdown, test_shutdown_pass_001);
	tcase_add_test (tc_shutdown, test_shutdown_fail_001);

	TCase* tc_add = tcase_create ("add");
	suite_add_tcase (s, tc_add);
	tcase_add_test (tc_add, test_add_pass_001);
	tcase_add_test_raise_signal (tc_add, test_add_fail_001, SIGABRT);
	tcase_add_test_raise_signal (tc_add, test_add_fail_002, SIGABRT);
	tcase_add_test_raise_signal (tc_add, test_add_fail_003, SIGABRT);

	TCase* tc_peek = tcase_create ("peek");
	suite_add_tcase (s, tc_peek);
	tcase_add_test (tc_peek, test_peek_pass_001);
	tcase_add_test (tc_peek, test_peek_fail_001);
	tcase_add_test (tc_peek, test_peek_fail_002);

	TCase* tc_max_length = tcase_create ("max-length");
	suite_add_tcase (s, tc_max_length);
	tcase_add_test (tc_max_length, test_max_length_pass_001);
	tcase_add_test_raise_signal (tc_max_length, test_max_length_fail_001, SIGABRT);

	TCase* tc_length = tcase_create ("length");
	suite_add_tcase (s, tc_length);
	tcase_add_test (tc_length, test_length_pass_001);
	tcase_add_test_raise_signal (tc_length, test_length_fail_001, SIGABRT);

	TCase* tc_size = tcase_create ("size");
	suite_add_tcase (s, tc_size);
	tcase_add_test (tc_size, test_size_pass_001);
	tcase_add_test_raise_signal (tc_size, test_size_fail_001, SIGABRT);

	TCase* tc_is_empty = tcase_create ("is-empty");
	suite_add_tcase (s, tc_is_empty);
	tcase_add_test (tc_is_empty, test_is_empty_pass_001);
	tcase_add_test_raise_signal (tc_is_empty, test_is_empty_fail_001, SIGABRT);

	TCase* tc_is_full = tcase_create ("is-full");
	suite_add_tcase (s, tc_is_full);
	tcase_add_test (tc_is_full, test_is_full_pass_001);
	tcase_add_test_raise_signal (tc_is_full, test_is_full_fail_001, SIGABRT);

	TCase* tc_lead = tcase_create ("lead");
	suite_add_tcase (s, tc_lead);
	tcase_add_test (tc_lead, test_lead_pass_001);
	tcase_add_test_raise_signal (tc_lead, test_lead_fail_001, SIGABRT);

	TCase* tc_next_lead = tcase_create ("next-lead");
	suite_add_tcase (s, tc_next_lead);
	tcase_add_test (tc_next_lead, test_next_lead_pass_001);
	tcase_add_test_raise_signal (tc_next_lead, test_next_lead_fail_001, SIGABRT);

	TCase* tc_trail = tcase_create ("trail");
	suite_add_tcase (s, tc_trail);
	tcase_add_test (tc_trail, test_trail_pass_001);
	tcase_add_test_raise_signal (tc_trail, test_trail_fail_001, SIGABRT);

	TCase* tc_retransmit_push = tcase_create ("retransmit-push");
	suite_add_tcase (s, tc_retransmit_push);

	TCase* tc_retransmit_try_peek = tcase_create ("retransmit-try-peek");
	suite_add_tcase (s, tc_retransmit_try_peek);

	TCase* tc_retransmit_pop = tcase_create ("retransmit-pop");
	suite_add_tcase (s, tc_retransmit_pop);

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
