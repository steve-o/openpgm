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


#define _GNU_SOURCE


#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>



/* globals */

/* read time stamp counter, count of ticks from processor reset.
 */

__inline__ uint64_t
rdtsc (void)
{
	uint32_t lo, hi;

/* We cannot use "=A", since this would use %rax on x86_64 */
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));

	return (uint64_t)hi << 32 | lo;
}


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
	puts ("test_cpu_timers");

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

/* setup process affinity and maximum priority */
	struct sched_param sp;
	sp.sched_priority = sched_get_priority_max (SCHED_RR);
	int e = sched_setscheduler (0, SCHED_RR, &sp);
	g_assert (!e);

	cpu_set_t cpu0, cpu1, cur;

	e = sched_getaffinity (0, sizeof(cpu_set_t), &cur);
	g_assert (!e);

	CPU_ZERO(&cpu0);
	CPU_ZERO(&cpu1);
	CPU_SET(0, &cpu0);
	CPU_SET(1, &cpu1);

	uint64_t h0 = 0, h1 = 0, diff = 0;

	for (int i = 0; i < 100000; i++)
	{
		e = sched_setaffinity (0, sizeof(cpu_set_t), &cpu0);
		g_assert (!e);

		uint64_t x = rdtsc();

		e = sched_setaffinity (0, sizeof(cpu_set_t), &cpu1);
		g_assert (!e);

		uint64_t y = rdtsc();

		e = sched_setaffinity (0, sizeof(cpu_set_t), &cpu0);
		g_assert (!e);

		uint64_t z = rdtsc();

		if (!i || z - x < diff)
		{
			diff = z - x;
			h0   = x + diff / 2;
			h1   = y;
		}
	}

	printf ("drift %lu ticks.\n", h0 - h1);

	e = sched_setaffinity (0, sizeof(cpu_set_t), &cur);
	g_assert (!e);


	puts ("finished.");
	return 0;
}

/* eof */
