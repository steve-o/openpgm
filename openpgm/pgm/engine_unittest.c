/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for PGM engine.
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

/* getprotobyname_r */
#define _BSD_SOURCE	1

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <glib.h>
#include <check.h>

#ifdef _WIN32
#	define PGM_CHECK_NOFORK		1
#endif


/* mock state */

struct pgm_rwlock_t;
struct pgm_slist_t;

static gint mock_time_init = 0;
static struct pgm_rwlock_t mock_pgm_sock_list_lock;
static struct pgm_slist_t* mock_pgm_sock_list = NULL;

#define pgm_time_init		mock_pgm_time_init
#define pgm_time_shutdown	mock_pgm_time_shutdown
#define pgm_close		mock_pgm_close
#define pgm_sock_list_lock	mock_pgm_sock_list_lock
#define pgm_sock_list		mock_pgm_sock_list

#define ENGINE_DEBUG
#include "engine.c"


static
void
mock_setup (void)
{
	mock_time_init = FALSE;
}

static
void
mock_teardown (void)
{
// null
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

PGM_GNUC_INTERNAL
bool
mock_pgm_time_init (
	pgm_error_t**	error
	)
{
	mock_time_init++;
	return TRUE;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_time_shutdown (void)
{
	if (!mock_time_init)
		return FALSE;
	mock_time_init--;
	return TRUE;
}

bool
mock_pgm_close (
	pgm_sock_t*		sock,
	bool			flush
        )
{
	return TRUE;
}


/* target:
 *	bool
 *	pgm_init (pgm_error_t** error)
 */

/* reference counting on init */
START_TEST (test_init_pass_001)
{
	fail_unless (TRUE == pgm_init (NULL), "init failed");
	fail_unless (TRUE == pgm_init (NULL), "init failed");

#ifdef PGM_CHECK_NOFORK
/* clean up state */
	fail_unless (TRUE == pgm_shutdown (), "shutdown failed");
	fail_unless (TRUE == pgm_shutdown (), "shutdown failed");
	fail_unless (FALSE == pgm_shutdown (), "shutdown failed");
#endif
}
END_TEST

/* timing module already init */
START_TEST (test_init_pass_003)
{
	pgm_error_t* err = NULL;
	fail_unless (TRUE == pgm_time_init (&err), "time-init failed: %s", (err && err->message) ? err->message : "(null)");
	fail_unless (TRUE == pgm_init (&err), "init failed: %s", (err && err->message) ? err->message : "(null)");
	fail_unless (TRUE == pgm_init (&err), "init failed: %s", (err && err->message) ? err->message : "(null)");

#ifdef PGM_CHECK_NOFORK
/* clean up state */
	fail_unless (TRUE == pgm_shutdown (), "shutdown failed");
	fail_unless (TRUE == pgm_shutdown (), "shutdown failed");
	fail_unless (FALSE == pgm_shutdown (), "shutdown failed");
	fail_unless (TRUE == pgm_time_shutdown (), "time-shutdown failed");
	fail_unless (FALSE == pgm_time_shutdown (), "time-shutdown failed");
#endif
}
END_TEST

/* target:
 *	bool
 *	pgm_shutdown (void)
 */

START_TEST (test_shutdown_pass_001)
{
	fail_unless (TRUE == pgm_init (NULL), "init failed");
	fail_unless (TRUE == pgm_shutdown (), "shutdown failed");

#ifdef PGM_CHECK_NOFORK
	fail_unless (FALSE == pgm_shutdown (), "shutdown failed");
#endif
}
END_TEST

/* no init */
START_TEST (test_shutdown_pass_002)
{
	fail_unless (FALSE == pgm_shutdown (), "shutdown failed");
}
END_TEST

/* double call */
START_TEST (test_shutdown_pass_003)
{
	fail_unless (TRUE == pgm_init (NULL), "init failed");
	fail_unless (TRUE == pgm_shutdown (), "shutdown failed");
	fail_unless (FALSE == pgm_shutdown (), "shutdown failed");
}
END_TEST

/* check reference counting */
START_TEST (test_shutdown_pass_004)
{
	fail_unless (TRUE == pgm_init (NULL), "init failed");
	fail_unless (TRUE == pgm_init (NULL), "init failed");
	fail_unless (TRUE == pgm_shutdown (), "shutdown failed");
	fail_unless (TRUE == pgm_shutdown (), "shutdown failed");
	fail_unless (FALSE == pgm_shutdown (), "shutdown failed");
}
END_TEST

/* target:
 *	bool
 *	pgm_supported (void)
 */

START_TEST (test_supported_pass_001)
{
	fail_unless (FALSE == pgm_supported(), "supported failed");
	fail_unless (TRUE == pgm_init (NULL), "init failed");
	fail_unless (TRUE == pgm_supported(), "supported failed");
	fail_unless (TRUE == pgm_shutdown (), "shutdown failed");
	fail_unless (FALSE == pgm_supported(), "supported failed");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_init = tcase_create ("init");
	tcase_add_checked_fixture (tc_init, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_init);
	tcase_add_test (tc_init, test_init_pass_001);
	tcase_add_test (tc_init, test_init_pass_003);

	TCase* tc_shutdown = tcase_create ("shutdown");
	tcase_add_checked_fixture (tc_shutdown, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_shutdown);
	tcase_add_test (tc_shutdown, test_shutdown_pass_001);
	tcase_add_test (tc_shutdown, test_shutdown_pass_002);
	tcase_add_test (tc_shutdown, test_shutdown_pass_003);
	tcase_add_test (tc_shutdown, test_shutdown_pass_004);
	
	TCase* tc_supported = tcase_create ("supported");
	tcase_add_checked_fixture (tc_supported, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_supported);
	tcase_add_test (tc_supported, test_supported_pass_001);

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
