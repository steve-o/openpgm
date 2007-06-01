/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test CPU/thread drift.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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


#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "pgm/timer.h"


/* globals */

static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	puts ("test_time");

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "h")) != -1)
	{
		switch (c) {

		case 'h':
		case '?': usage (binary_name);
		}
	}

/* setup signal handlers */
	signal(SIGHUP, SIG_IGN);

	time_init();

	guint64 start, end;

	guint64 min_diff = UINT32_MAX;
	guint64 max_diff = 0;

	for (int i = 0; i < 1000; i++)
	{
		start = time_update_now();

		time_sleep (7);

		end = time_update_now();

		guint64 elapsed = end - start;

		if (elapsed > max_diff) max_diff = elapsed;
		else if (elapsed < min_diff) min_diff = elapsed;
	}

	printf ("time elapsed max %lu us, min %lu us\n", max_diff, min_diff);

	time_destroy();	

	puts ("finished.");
	return 0;
}

/* eof */
