/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * high resolution timers.
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

#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/timeb.h>

#include <glib.h>

#include "timer.h"


/* globals */

#define msecs_to_secs(t)	( (t) / 1000 )
#define usecs_to_secs(t)	( (t) / 1000000UL )
#define nsecs_to_secs(t)	( (t) / 1000000000UL )
#define secs_to_msecs(t)	( (t) * 1000 )
#define secs_to_usecs(t)	( (t) * 1000000UL )
#define secs_to_nsecs(t)	( (t) * 1000000000UL )
#define msecs_to_usecs(t)	( (t) * 1000 )
#define msecs_to_nsecs(t)	( (t) * 1000000UL )
#define usecs_to_msecs(t)	( (t) / 1000 )
#define usecs_to_nsecs(t)	( (t) / 1000000UL )
#define nsecs_to_msecs(t)	( (t) / 1000000UL )
#define nsecs_to_usecs(t)	( (t) / 1000 )

guint64 time_now = 0;
time_update_func time_update_now;

static void gettimeofday_update (void);
static void clock_update (void);
static void ftime_update (void);

int
time_init ( void )
{
	char *cfg = getenv ("PGM_TIMER");
	if (cfg == NULL) cfg = "GETTIMEOFDAY";

	switch (cfg[0]) {
	case 'C':	time_update_now = clock_update; break;
	case 'F':	time_update_now = ftime_update; break;

	default:
	case 'G':	time_update_now = gettimeofday_update; break;
	}

	time_update_now();
	return 0;
}

int
time_shutdown (void)
{

	return 0;
}

static void
gettimeofday_update (void)
{
	static struct timeval now;
	
	gettimeofday (&now, NULL);
	time_now = secs_to_usecs(now.tv_sec) + now.tv_usec;
}

static void
clock_update (void)
{
	static struct timespec now;

	clock_gettime (CLOCK_MONOTONIC, &now);
	time_now = secs_to_usecs(now.tv_sec) + nsecs_to_usecs(now.tv_nsec);
}

static void
ftime_update (void)
{
	static struct timeb now;

	ftime (&now);
	time_now = secs_to_usecs(now.time) + msecs_to_usecs(now.millitm);
}

/* eof */
