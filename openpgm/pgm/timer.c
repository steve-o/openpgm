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

#include "pgm/timer.h"


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
#define usecs_to_nsecs(t)	( (t) * 1000 )
#define nsecs_to_msecs(t)	( (t) / 1000000UL )
#define nsecs_to_usecs(t)	( (t) / 1000 )

pgm_time_t pgm_time_now = 0;
pgm_time_update_func pgm_time_update_now;
pgm_time_sleep_func pgm_time_sleep;

static gboolean time_got_initialized = FALSE;

static pgm_time_t gettimeofday_update (void);
static pgm_time_t clock_update (void);
static pgm_time_t ftime_update (void);
static int rtc_init (void);
static int rtc_destroy (void);
static pgm_time_t rtc_update (void);
static int tsc_init (void);
static pgm_time_t tsc_update (void);
static int clock_init (void);

static void clock_nano_sleep (gulong);
static void nano_sleep (gulong);
static void rtc_sleep (gulong);
static void tsc_sleep (gulong);

int
pgm_time_init ( void )
{
	g_return_val_if_fail (time_got_initialized == FALSE, -1);

/* current time */
	char *cfg = getenv ("PGM_TIMER");
	if (cfg == NULL) cfg = "TSC";

	switch (cfg[0]) {
	case 'C':	pgm_time_update_now = clock_update; break;
	case 'F':	pgm_time_update_now = ftime_update; break;
	case 'R':	pgm_time_update_now = rtc_update; break;
	case 'T':	pgm_time_update_now = tsc_update; break;

	default:
	case 'G':	pgm_time_update_now = gettimeofday_update; break;
	}

/* sleeping */
	cfg = getenv ("PGM_SLEEP");
	if (cfg == NULL) cfg = "RTC";

	switch (cfg[0]) {
	case 'C':	pgm_time_sleep = clock_nano_sleep; break;
	case 'N':	pgm_time_sleep = nano_sleep; break;
	case 'R':	pgm_time_sleep = rtc_sleep; break;
	case 'T':	pgm_time_sleep = tsc_sleep; break;

	default:
	case 'M':
	case 'U':	pgm_time_sleep = usleep; break;	/* direct to glibc, function is deprecated */
	}

	if (pgm_time_update_now == rtc_update || pgm_time_sleep == rtc_sleep ||
		pgm_time_update_now == tsc_update || pgm_time_sleep == tsc_sleep)
	{
		rtc_init();
	}

	if (pgm_time_update_now == tsc_update || pgm_time_sleep == tsc_sleep)
	{
		tsc_init();
	}

	if (pgm_time_sleep == clock_nano_sleep)
	{
		clock_init();
	}

	pgm_time_update_now();

	time_got_initialized = TRUE;
	return 0;
}

gboolean
pgm_time_supported (void)
{
	return ( time_got_initialized == TRUE );
}

int
pgm_time_destroy (void)
{
	if (pgm_time_update_now == rtc_update || pgm_time_sleep == rtc_sleep ||
		pgm_time_update_now == tsc_update || pgm_time_sleep == tsc_sleep)
	{
		rtc_destroy();
	}

	return 0;
}

static pgm_time_t
gettimeofday_update (void)
{
	static struct timeval now;
	
	gettimeofday (&now, NULL);
	pgm_time_now = secs_to_usecs(now.tv_sec) + now.tv_usec;

	return pgm_time_now;
}

static pgm_time_t
clock_update (void)
{
	static struct timespec now;

	clock_gettime (CLOCK_MONOTONIC, &now);
	pgm_time_now = secs_to_usecs(now.tv_sec) + nsecs_to_usecs(now.tv_nsec);

	return pgm_time_now;
}

static pgm_time_t
ftime_update (void)
{
	static struct timeb now;

	ftime (&now);
	pgm_time_now = secs_to_usecs(now.time) + msecs_to_usecs(now.millitm);

	return pgm_time_now;
}

/* Old PC/AT-Compatible driver:  /dev/rtc
 *
 * Not so speedy 8192 Hz timer, thats 122us resolution.
 *
 * WARNING: time is relative to start of timer.
 */

static int rtc_fd = -1;
static int rtc_frequency = 8192;
static pgm_time_t rtc_count = 0;

static int
rtc_init (void)
{
	g_return_val_if_fail (rtc_fd == -1, -1);

	rtc_fd = open ("/dev/rtc", O_RDONLY);
	if (rtc_fd < 0) {
		g_critical ("Cannot open /dev/rtc for reading.");
		g_assert_not_reached();
	}

	if ( ioctl (rtc_fd, RTC_IRQP_SET, rtc_frequency) < 0 ) {
		g_critical ("Cannot set RTC frequency to %i Hz.", rtc_frequency);
		g_assert_not_reached();
	}

	if ( ioctl (rtc_fd, RTC_PIE_ON, 0) < 0 ) {
		g_critical ("Cannot enable periodic interrupt (PIE) on RTC.");
		g_assert_not_reached();
	}

	return 0;
}

static int
rtc_destroy (void)
{
	g_return_val_if_fail (rtc_fd, -1);

	close (rtc_fd);
	rtc_fd = -1;

	return 0;
}

static pgm_time_t
rtc_update (void)
{
	unsigned long data;

/* returned value contains interrupt type and count of interrupts since last read */
	read (rtc_fd, &data, sizeof(data));

	rtc_count += data >> 8;
	pgm_time_now = rtc_count * 1000000UL / rtc_frequency;

	return pgm_time_now;
}

/* use a select to check if we have to clear the current interrupt count
 */

static void
rtc_sleep (gulong usec)
{
	unsigned long data;

	struct timeval zero_tv = {0, 0};
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(rtc_fd, &readfds);
	int retval = select (rtc_fd + 1, &readfds, NULL, NULL, &zero_tv);
	if (retval) {
		read (rtc_fd, &data, sizeof(data));
		rtc_count += data >> 8;
	}

	pgm_time_t count = 0;
	do {
		read (rtc_fd, &data, sizeof(data));

		count += data >> 8;

	} while ( (count * 1000000UL) < rtc_frequency * usec );

	rtc_count += count;
}

/* read time stamp counter, count of ticks from processor reset.
 *  */

__inline__ pgm_time_t
rdtsc (void)
{
	guint32 lo, hi;

/* We cannot use "=A", since this would use %rax on x86_64 */
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));

	return (pgm_time_t)hi << 32 | lo;
}

/* determine ratio of ticks to nano-seconds, use /dev/rtc for high accuracy
 * millisecond timer and convert.
 *
 * WARNING: time is relative to start of timer.
 */

static int tsc_us_scaler = 0;

static int
tsc_init (void)
{
	pgm_time_t start, stop;
	gulong calibration_usec = 20 * 1000;

	start = rdtsc();
	rtc_sleep (calibration_usec);
	stop = rdtsc();

	g_assert (stop >= start);

/* TODO: this math needs to be scaled to reduce rounding errors */
	pgm_time_t tsc_diff = stop - start;
	if (tsc_diff > calibration_usec) {
/* cpu > 1 Ghz */
		tsc_us_scaler = tsc_diff / calibration_usec;
	} else {
/* cpu < 1 Ghz */
		tsc_us_scaler = -( calibration_usec / tsc_diff );
	}

	return 0;
}

static pgm_time_t
tsc_update (void)
{
	pgm_time_t count = rdtsc();

	pgm_time_now = tsc_us_scaler > 0 ? (count / tsc_us_scaler) : (count * tsc_us_scaler);

	return pgm_time_now;
}	

static void
tsc_sleep (gulong usec)
{
	pgm_time_t start, now, end;

	start = rdtsc();
	end = start + ( tsc_us_scaler > 0 ? (usec * tsc_us_scaler) : (usec / tsc_us_scaler) );

	do {
		now = rdtsc();

		if (now < end) g_thread_yield();

	} while ( now < end );
}

static clockid_t g_clock_id;

static int
clock_init (void)
{
	g_clock_id = CLOCK_REALTIME;
//	g_clock_id = CLOCK_MONOTONIC;
//	g_clock_id = CLOCK_PROCESS_CPUTIME_ID;
//	g_clock_id = CLOCK_THREAD_CPUTIME_ID;

#if 0
//	clock_getcpuclockid (0, &g_clock_id);
//	pthread_getcpuclockid (pthread_self(), &g_clock_id);

	struct timespec ts;
	if (clock_getres (g_clock_id, &ts) > 0) {
		g_critical ("clock_getres failed on clock id %i", (int)g_clock_id);
		return -1;
	}
	g_message ("clock resolution %lu.%.9lu", ts.tv_sec, ts.tv_nsec);
#endif
	return 0;
}

static void
clock_nano_sleep (gulong usec)
{
	struct timespec ts;
#if 0
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	clock_nanosleep (g_clock_id, 0, &ts, NULL);
#else
	usec += pgm_time_now;
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	clock_nanosleep (g_clock_id, TIMER_ABSTIME, &ts, NULL);
#endif
}

static void
nano_sleep (gulong usec)
{
	struct timespec ts;
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	nanosleep (&ts, NULL);
}


/* eof */
