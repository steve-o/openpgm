
#include <stdlib.h>
#include <check.h>

#include "txwi.c"

/* target:
 *	pgm_txw_t*
 *	pgm_txw_init (
 *		const guint16		tpdu_size,
 *		const guint32		sqns,
 *		const guint		secs,
 *		const guint		max_rte
 *		)
 *
 * pre-conditions:
 *	none.
 *
 * post-conditions:
 *	created transmit window.
 */

/* vanilla sequence count window */
START_TEST (test_init_pass_001)
{
	fail_unless (NULL != pgm_txw_init (0, 100, 0, 0));
}
END_TEST

/* vanilla time based window */
START_TEST (test_init_pass_002)
{
	fail_unless (NULL != pgm_txw_init (1500, 0, 60, 800000));
}
END_TEST

/* jumbo frame */
START_TEST (test_init_pass_003)
{
	fail_unless (NULL != pgm_txw_init (9000, 0, 60, 800000));
}
END_TEST

/* max frame */
START_TEST (test_init_pass_004)
{
	fail_unless (NULL != pgm_txw_init (UINT16_MAX, 0, 60, 800000));
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

static
Suite*
make_test_suite (void)
{
	Suite* s;
	TCase* tc;

	s  = suite_create (__FILE__);
	tc = tcase_create ("init");
	suite_add_tcase (s, tc);

	tcase_add_test (tc, test_init_pass_001);
	tcase_add_test (tc, test_init_pass_002);
	tcase_add_test (tc, test_init_pass_003);
	tcase_add_test (tc, test_init_pass_004);
	tcase_add_test (tc, test_init_fail_001);
	tcase_add_test (tc, test_init_fail_002);
	tcase_add_test (tc, test_init_fail_003);
	tcase_add_test (tc, test_init_fail_004);

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
