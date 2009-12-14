/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * high resolution timers.
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/timeb.h>
#ifdef CONFIG_HAVE_POLL
#	include <poll.h>
#endif
#ifdef CONFIG_HAVE_RTC
#	include <sys/ioctl.h>
#	include <linux/rtc.h>
#endif

#include <glib.h>
#include "pgm/glib-compat.h"

#ifdef G_OS_WIN32
#	define WIN32_LEAN_AND_MEAN
#	include "windows.h"
#	include "winsock2.h"
#endif

#include "pgm/time.h"


/* globals */

#define msecs_to_secs(t)	( (t) / 1000 )
#define usecs_to_secs(t)	( (t) / 1000000UL )
#define nsecs_to_secs(t)	( (t) / 1000000000UL )
#define secs_to_msecs(t)	( (pgm_time_t)(t) * 1000 )
#define secs_to_usecs(t)	( (pgm_time_t)(t) * 1000000UL )
#define secs_to_nsecs(t)	( (pgm_time_t)(t) * 1000000000UL )
#define msecs_to_usecs(t)	( (pgm_time_t)(t) * 1000 )
#define msecs_to_nsecs(t)	( (pgm_time_t)(t) * 1000000UL )
#define usecs_to_msecs(t)	( (t) / 1000 )
#define usecs_to_nsecs(t)	( (pgm_time_t)(t) * 1000 )
#define nsecs_to_msecs(t)	( (t) / 1000000UL )
#define nsecs_to_usecs(t)	( (t) / 1000 )

pgm_time_update_func pgm_time_update_now;
pgm_time_sleep_func pgm_time_sleep;
pgm_time_since_epoch_func pgm_time_since_epoch;

static gboolean time_got_initialized = FALSE;
static pgm_time_t rel_offset = 0;

static pgm_time_t gettimeofday_update (void);
#ifdef CONFIG_HAVE_CLOCK_GETTIME
static pgm_time_t clock_update (void);
#endif
static pgm_time_t ftime_update (void);
#ifdef CONFIG_HAVE_CLOCK_NANOSLEEP
static int clock_init (void);
static pgm_time_t clock_nano_sleep (gulong);
#endif
#ifdef CONFIG_HAVE_NANOSLEEP
static pgm_time_t nano_sleep (gulong);
#endif
#ifdef G_OS_WIN32
static pgm_time_t msleep (gulong);
#endif
#ifdef CONFIG_HAVE_USLEEP
static pgm_time_t usleep_sleep (gulong);
#endif

static pgm_time_t select_sleep (gulong);

#ifdef CONFIG_HAVE_RTC
static int rtc_init (void);
static int rtc_shutdown (void);
static pgm_time_t rtc_update (void);
static pgm_time_t rtc_sleep (gulong);
#endif

#ifdef CONFIG_HAVE_TSC
#define TSC_NS_SCALE	10 /* 2^10, carefully chosen */
#define TSC_US_SCALE	20
static guint32 tsc_mhz = 0;
static guint32 tsc_ns_mul = 0;
static guint32 tsc_us_mul = 0;
static int tsc_init (void);
static pgm_time_t tsc_update (void);
static pgm_time_t tsc_sleep (gulong);
#endif

#ifdef CONFIG_HAVE_PPOLL
static pgm_time_t poll_sleep (gulong);
#endif

static void pgm_time_conv (pgm_time_t*, time_t*);
static void pgm_time_conv_from_reset (pgm_time_t*, time_t*);

/* TSC helper functions */
#ifdef CONFIG_HAVE_TSC
static inline
void
set_tsc_mul (
	const guint32		khz
	)
{
	tsc_ns_mul = (1000000 << TSC_NS_SCALE) / khz;
	tsc_us_mul = (1000 << TSC_US_SCALE) / khz;
}

/* convert from clocks (64 bits) to nanoseconds (64 bits)
 */
static
guint64
tsc_to_ns (
	const guint64		tsc
	)
{
	return (tsc * tsc_ns_mul) >> TSC_NS_SCALE;
}

static
guint64
tsc_to_us (
	const guint64		tsc
	)
{
	return (tsc * tsc_us_mul) >> TSC_US_SCALE;
}
#endif /* CONFIG_HAVE_TSC */

/* initialize time system.
 *
 * returns TRUE on success, returns FALSE on error such as being unable to open
 * the RTC device, an unstable TSC, or system already initialized.
 */

gboolean
pgm_time_init (void)
{
	g_return_val_if_fail (FALSE == time_got_initialized, FALSE);

/* current time */
	const char *cfg = getenv ("PGM_TIMER");
	if (cfg == NULL) {
#ifdef CONFIG_HAVE_TSC
		cfg = "TSC";
#else
		cfg = "GTOD";
#endif
	}

	pgm_time_since_epoch = pgm_time_conv;

	switch (cfg[0]) {
	case 'F':	pgm_time_update_now = ftime_update; break;

#ifdef CONFIG_HAVE_CLOCK_GETTIME
	case 'C':	pgm_time_update_now = clock_update; break;
#endif
#ifdef CONFIG_HAVE_RTC
	case 'R':	pgm_time_update_now = rtc_update;
			pgm_time_since_epoch = pgm_time_conv_from_reset;
			break;
#endif
#ifdef CONFIG_HAVE_TSC
	case 'T':	pgm_time_update_now = tsc_update;
			pgm_time_since_epoch = pgm_time_conv_from_reset;
			break;
#endif

	default:
	case 'G':	pgm_time_update_now = gettimeofday_update; break;
	}

/* sleeping */
	cfg = getenv ("PGM_SLEEP");
	if (cfg == NULL) cfg = "MSLEEP";

	switch (cfg[0]) {
#ifdef CONFIG_HAVE_CLOCK_NANOSLEEP
	case 'C':	pgm_time_sleep = clock_nano_sleep; break;
#endif
#ifdef CONFIG_HAVE_NANOSLEEP
	case 'N':	pgm_time_sleep = nano_sleep; break;
#endif
#ifdef CONFIG_HAVE_RTC
	case 'R':	pgm_time_sleep = rtc_sleep; break;
#endif
#ifdef CONFIG_HAVE_TSC
	case 'T':	pgm_time_sleep = tsc_sleep; break;
#endif
#ifdef CONFIG_HAVE_PPOLL
	case 'P':	pgm_time_sleep = poll_sleep; break;
#endif

	default:
#ifdef CONFIG_HAVE_USLEEP
	case 'M':
	case 'U':
			pgm_time_sleep = usleep_sleep; break;

	case 'S':	pgm_time_sleep = select_sleep; break;
#elif defined(G_OS_WIN32)
	case 'M':	pgm_time_sleep = msleep; break;
	case 'S':	pgm_time_sleep = select_sleep; break;
#else
	case 'S':	pgm_time_sleep = select_sleep; break;
#endif /* CONFIG_HAVE_USLEEP */
	}

#ifdef CONFIG_HAVE_RTC
	if (pgm_time_update_now == rtc_update || pgm_time_sleep == rtc_sleep)
	{
		rtc_init();
	}
#endif
#ifdef CONFIG_HAVE_TSC
	if (pgm_time_update_now == tsc_update || pgm_time_sleep == tsc_sleep)
	{
/* attempt to parse clock ticks from kernel
 */
		FILE* fp = fopen ("/proc/cpuinfo", "r");
		char buffer[1024];

		if (fp)
		{
			while (!feof(fp) && fgets (buffer, sizeof(buffer), fp))
			{
				if (strstr (buffer, "cpu MHz"))
				{
					char *p = strchr (buffer, ':');
					if (p) tsc_mhz = atoi (p + 1);
					break;
				}
			}
			fclose (fp);
		}

/* e.g. export RDTSC_FREQUENCY=3200.000000
 *
 * Value can be used to override kernel tick rate as well as internal calibration
 */
		const char *env_mhz = getenv ("RDTSC_FREQUENCY");
		if (env_mhz)
			tsc_mhz = atoi (env_mhz);

/* calibrate */
		if (0 >= tsc_mhz)
			tsc_init();
		set_tsc_mul (tsc_mhz * 1000);
	}
#endif /* CONFIG_HAVE_TSC */

#ifdef CONFIG_HAVE_CLOCK_NANOSLEEP
	if (pgm_time_sleep == clock_nano_sleep)
	{
		clock_init();
	}
#endif

	pgm_time_update_now();

/* calculate relative time offset */
	if (	0
#ifdef CONFIG_HAVE_RTC
		|| pgm_time_update_now == rtc_update
#endif
#ifdef CONFIG_HAVE_TSC
		|| pgm_time_update_now == tsc_update
#endif
	   )
	{
		rel_offset = gettimeofday_update() - pgm_time_update_now();
	}

	time_got_initialized = TRUE;
	return TRUE;
}

gboolean
pgm_time_supported (void)
{
	return ( time_got_initialized == TRUE );
}

/* returns TRUE if shutdown succeeded, returns FALSE on error.
 */

gboolean
pgm_time_shutdown (void)
{
	g_return_val_if_fail (TRUE == time_got_initialized, FALSE);

	gboolean success = TRUE;
#ifdef CONFIG_HAVE_RTC
	if (pgm_time_update_now == rtc_update || pgm_time_sleep == rtc_sleep)
		success = rtc_shutdown ();
#endif
	time_got_initialized = !success;
	return TRUE;
}

static
pgm_time_t
gettimeofday_update (void)
{
	struct timeval gettimeofday_now;
	static pgm_time_t last = 0;
	gettimeofday (&gettimeofday_now, NULL);
	const pgm_time_t now = secs_to_usecs (gettimeofday_now.tv_sec) + gettimeofday_now.tv_usec;
	if (G_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}

#ifdef CONFIG_HAVE_CLOCK_GETTIME
static
pgm_time_t
clock_update (void)
{
	struct timespec clock_now;
	static pgm_time_t last = 0;
	clock_gettime (CLOCK_MONOTONIC, &clock_now);
	const pgm_time_t now = secs_to_usecs (clock_now.tv_sec) + nsecs_to_usecs (clock_now.tv_nsec);
	if (G_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}
#endif /* CONFIG_HAVE_CLOCK_GETTIME */

static
pgm_time_t
ftime_update (void)
{
	struct timeb ftime_now;
	static pgm_time_t last = 0;
	ftime (&ftime_now);
	const pgm_time_t now = secs_to_usecs (ftime_now.time) + msecs_to_usecs (ftime_now.millitm);
	if (G_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}

/* Old PC/AT-Compatible driver:  /dev/rtc
 *
 * Not so speedy 8192 Hz timer, thats 122us resolution.
 *
 * WARNING: time is relative to start of timer.
 */

#ifdef CONFIG_HAVE_RTC
static int rtc_fd = -1;
static int rtc_frequency = 8192;
static pgm_time_t rtc_count = 0;

static
int
rtc_init (void)
{
	g_return_val_if_fail (rtc_fd == -1, -1);

	rtc_fd = open ("/dev/rtc", O_RDONLY);
	if (rtc_fd < 0) {
		g_error ("Cannot open /dev/rtc for reading.");
		return -1;
	}
	if ( ioctl (rtc_fd, RTC_IRQP_SET, rtc_frequency) < 0 ) {
		g_error ("Cannot set RTC frequency to %i Hz.", rtc_frequency);
		return -1;
	}
	if ( ioctl (rtc_fd, RTC_PIE_ON, 0) < 0 ) {
		g_error ("Cannot enable periodic interrupt (PIE) on RTC.");
		return -1;
	}
	return 0;
}

/* returns 0 on success even if RTC device cannot be closed or had an IO error,
 * returns -1 if the RTC file descriptor is not set.
 */

static
gboolean
rtc_shutdown (void)
{
	g_return_val_if_fail (rtc_fd, FALSE);
	g_warn_if_fail (0 == close (rtc_fd));
	rtc_fd = -1;
	return FALSE;
}

static
pgm_time_t
rtc_update (void)
{
	unsigned long data;
/* returned value contains interrupt type and count of interrupts since last read */
	read (rtc_fd, &data, sizeof(data));
	rtc_count += data >> 8;
	return rtc_count * 1000000UL / rtc_frequency;
}

/* use a select to check if we have to clear the current interrupt count
 */

static
pgm_time_t
rtc_sleep (gulong usec)
{
	unsigned long data;

	struct timeval zero_tv = {0, 0};
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(rtc_fd, &readfds);
	const int retval = select (rtc_fd + 1, &readfds, NULL, NULL, &zero_tv);
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
	if (pgm_time_update_now == rtc_update)
		return rtc_count * 1000000UL / rtc_frequency;
	else
		return pgm_time_update_now();
}
#endif /* CONFIG_HAVE_RTC */

/* read time stamp counter, count of ticks from processor reset.
 */

#ifdef CONFIG_HAVE_TSC
static inline
pgm_time_t
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

static
int
tsc_init (void)
{
	pgm_time_t start, stop;
	const gulong calibration_usec = 4000 * 1000;

	g_message ("Running a benchmark to measure system clock frequency...");

	start = rdtsc();
	pgm_time_sleep (calibration_usec);
	stop = rdtsc();

	if (stop < start)
	{
		g_warning ("Finished RDTSC test.  Unstable TSC detected.  The benchmark resulted in a "
			   "non-monotonic time response rendering the TSC unsuitable for high resolution "
			   "timing.  To prevent the start delay from this benchmark and use a stable clock "
			   "source set the environment variables PGM_TIMER to GTOD and PGM_SLEEP to USLEEP.");

/* force both to stable clocks even though one might be OK */
		pgm_time_update_now = gettimeofday_update;
		pgm_time_sleep = (pgm_time_sleep_func)usleep;
		return -1;
	}

/* TODO: this math needs to be scaled to reduce rounding errors */
	const pgm_time_t tsc_diff = stop - start;
	if (tsc_diff > calibration_usec) {
/* cpu > 1 Ghz */
		tsc_mhz = tsc_diff / calibration_usec;
	} else {
/* cpu < 1 Ghz */
		tsc_mhz = -( calibration_usec / tsc_diff );
	}

	g_warning ("Finished RDTSC test. To prevent the startup delay from this benchmark, "
		   "set the environment variable RDTSC_FREQUENCY to %i on this "
		   "system. This value is dependent upon the CPU clock speed and "
		   "architecture and should be determined separately for each server.",
		   tsc_mhz);
	return 0;
}

static
pgm_time_t
tsc_update (void)
{
	static pgm_time_t last = 0;
	const pgm_time_t now = tsc_to_us (rdtsc());
	if (G_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}	

static
pgm_time_t
tsc_sleep (gulong usec)
{
	guint64 now;
	const guint64 start = tsc_to_us (rdtsc());
	const guint64 end   = start + usec;

	for (;;) {
		now = tsc_to_us (rdtsc());
		if (now < end) g_thread_yield();
		else break;
	}

	if (pgm_time_update_now == tsc_update)
		return now;
	else
		return pgm_time_update_now();
}
#endif /* CONFIG_HAVE_TSC */

#ifdef CONFIG_HAVE_CLOCK_NANOSLEEP
static clockid_t g_clock_id;

static
int
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

static
pgm_time_t
clock_nano_sleep (gulong usec)
{
	struct timespec ts;
#if 0
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	clock_nanosleep (g_clock_id, 0, &ts, NULL);
#else
	usec += pgm_time_update_now ();
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	clock_nanosleep (g_clock_id, TIMER_ABSTIME, &ts, NULL);
#endif
	return pgm_time_update_now ();
}
#endif /* CONFIG_HAVE_CLOCK_NANOSLEEP */

#ifdef CONFIG_HAVE_NANOSLEEP
static
pgm_time_t
nano_sleep (gulong usec)
{
	struct timespec ts;
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	nanosleep (&ts, NULL);
	return pgm_time_update_now ();
}
#endif /* CONFIG_HAVE_NANOSLEEP */

#ifdef G_OS_WIN32
static
pgm_time_t
msleep (gulong usec)
{
	Sleep (usecs_to_msecs(usec));
	return pgm_time_update_now ();
}
#endif

#ifdef CONFIG_HAVE_USLEEP
static
pgm_time_t
usleep_sleep (gulong usec)
{
	usleep (usec);
	return pgm_time_update_now ();
}
#endif /* CONFIG_HAVE_USLEEP */

static
pgm_time_t
select_sleep (gulong usec)
{
#ifdef CONFIG_HAVE_PSELECT
	struct timespec ts;
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	pselect (0, NULL, NULL, NULL, &ts, NULL);
#else
	struct timeval tv;
	tv.tv_sec	= usec / 1000000UL;
	tv.tv_usec	= usec % 1000000UL;
	select (0, NULL, NULL, NULL, &tv);
#endif
	return pgm_time_update_now ();
}

#ifdef CONFIG_HAVE_PPOLL
static
pgm_time_t
poll_sleep (gulong usec)
{
	struct timespec ts;
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	ppoll (NULL, 0, &ts, NULL);
	return pgm_time_update_now();
}
#endif

/* convert from pgm_time_t to time_t with pgm_time_t in microseconds since the epoch.
 */
static
void
pgm_time_conv (
	pgm_time_t*	pgm_time_t_time,
	time_t*		time_t_time
	)
{
	*time_t_time = pgm_to_secs (*pgm_time_t_time);
}

/* convert from pgm_time_t to time_t with pgm_time_t in microseconds since the core started.
 */
static
void
pgm_time_conv_from_reset (
	pgm_time_t*	pgm_time_t_time,
	time_t*		time_t_time
	)
{
	*time_t_time = pgm_to_secs (*pgm_time_t_time + rel_offset);
}

/* eof */
