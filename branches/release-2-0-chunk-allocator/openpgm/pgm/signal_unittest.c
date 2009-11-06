/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for re-entrant safe signal handling.
 *
 * Copyright (c) 2006-2009 Miru Limited.
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


/* mock state */

static
void
on_sigusr1 (
	int		signum,
	gpointer	user_data
	)
{
	g_assert (SIGUSR1 == signum);
	g_assert (NULL    != user_data);
	GMainLoop* loop = (GMainLoop*)user_data;
	g_debug ("on_sigusr1 (signum:%d)", signum);
	g_main_loop_quit (loop);
}

/* mock functions for external references */

#define SIGNAL_DEBUG
#include "signal.c"


/* target:
 *	pgm_sighandler_t
 *	pgm_signal_install (
 *		int			signum,
		pgm_sighandler_t	handler
 *	)
 */

static
gboolean
on_startup (
	gpointer	data
	)
{
	g_assert (NULL != data);
	const int signum = *(const int*)data;
	fail_unless (0 == raise (signum));
	return FALSE;
}

START_TEST (test_install_pass_001)
{
	const int signum = SIGUSR1;
	GMainLoop* loop = g_main_loop_new (NULL, FALSE);
	fail_unless (TRUE == pgm_signal_install (signum, on_sigusr1, loop));
	g_timeout_add (0, (GSourceFunc)on_startup, &signum);
	g_main_loop_run (loop);
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_install = tcase_create ("install");
	suite_add_tcase (s, tc_install);
	tcase_add_test (tc_install, test_install_pass_001);
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
