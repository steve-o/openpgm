/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for SNMP.
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

#include "pgm/transport.h"


/* mock state */
static const guint mock_pgm_major_version = 0;
static const guint mock_pgm_minor_version = 0;
static const guint mock_pgm_micro_version = 0;
static GStaticRWLock mock_pgm_transport_list_lock = G_STATIC_RW_LOCK_INIT;
static GSList* mock_pgm_transport_list = NULL;

static
gboolean
mock_pgm_tsi_equal (
	gconstpointer	v1,
	gconstpointer	v2
	)
{
	return memcmp (v1, v2, sizeof(struct pgm_tsi_t)) == 0;
}

static
void
mock_pgm_time_since_epoch (
	pgm_time_t*	pgm_time_t_time,
	time_t*		time_t_time
	)
{
	*time_t_time = pgm_to_secs (*pgm_time_t_time + 0);
}

static
gboolean
mock_pgm_mib_init (
	GError**	error
	)
{
	return TRUE;
}

/* mock functions for external references */

#define pgm_major_version	mock_pgm_major_version
#define pgm_minor_version	mock_pgm_minor_version
#define pgm_micro_version	mock_pgm_micro_version
#define pgm_transport_list_lock	mock_pgm_transport_list_lock
#define pgm_transport_list	mock_pgm_transport_list
#define pgm_tsi_equal		mock_pgm_tsi_equal
#define pgm_time_since_epoch	mock_pgm_time_since_epoch
#define pgm_mib_init		mock_pgm_mib_init


#define SNMP_DEBUG
#include "snmp.c"


/* target:
 *	gboolean
 *	pgm_snmp_init (
 *		GError**	error
 *	)
 */

START_TEST (test_init_pass_001)
{
	GError* err = NULL;
	fail_unless (TRUE == pgm_snmp_init (&err));
	fail_unless (NULL == err);
}
END_TEST

/* duplicate servers */
START_TEST (test_init_fail_001)
{
	GError* err = NULL;
	fail_unless (TRUE == pgm_snmp_init (&err));
	fail_unless (FALSE == pgm_snmp_init (&err));
}
END_TEST

/* target:
 *	gboolean
 *	pgm_snmp_shutdown (void)
 */

START_TEST (test_shutdown_pass_001)
{
	GError* err = NULL;
	fail_unless (TRUE == pgm_snmp_init (&err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_snmp_shutdown ());
}
END_TEST

/* repeatability
 */
START_TEST (test_shutdown_pass_002)
{
	GError* err = NULL;
	fail_unless (TRUE == pgm_snmp_init (&err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_snmp_shutdown ());
	fail_unless (TRUE == pgm_snmp_init (&err));
	fail_unless (NULL == err);
	fail_unless (TRUE == pgm_snmp_shutdown ());
}
END_TEST

/* no running server */
START_TEST (test_shutdown_fail_001)
{
	fail_unless (FALSE == pgm_snmp_shutdown ());
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
	tcase_add_test (tc_init, test_init_fail_001);

	TCase* tc_shutdown = tcase_create ("shutdown");
	suite_add_tcase (s, tc_shutdown);
	tcase_add_test (tc_shutdown, test_shutdown_pass_001);
	tcase_add_test (tc_shutdown, test_shutdown_pass_002);
	tcase_add_test (tc_shutdown, test_shutdown_fail_001);

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
