/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * high resolution timers.
 *
 * Copyright (c) 2006-2010 Miru Limited.
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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <errno.h>
#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <stdlib.h>
#ifndef _WIN32
#	include <sys/select.h>
#else
#	define WIN32_LEAN_AND_MEAN
#	include "windows.h"
#endif

#include <pgm/framework.h>

//#define TIME_DEBUG


/* globals */

pgm_time_update_func		pgm_time_update_now;
pgm_time_sleep_func		pgm_time_sleep;
pgm_time_since_epoch_func	pgm_time_since_epoch;


/* locals */

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
#define fsecs_to_nsecs(t)	( (t) / 1000000UL )
#define fsecs_to_usecs(t)	( (t) / 1000000000UL )

static volatile int32_t		time_ref_count = 0;
static pgm_time_t		rel_offset = 0;

static void			pgm_time_conv (const pgm_time_t*const restrict, time_t*restrict);
static void			pgm_time_conv_from_reset (const pgm_time_t*const restrict, time_t*restrict);
static pgm_time_t		pgm_select_sleep (const uint32_t);

#if defined(CONFIG_HAVE_CLOCK_GETTIME) || defined(CONFIG_HAVE_CLOCK_NANOSLEEP)
#	include <time.h>
#	ifdef CONFIG_HAVE_CLOCK_GETTIME
static pgm_time_t		pgm_clock_update (void);
#	endif
#	ifdef CONFIG_HAVE_CLOCK_NANOSLEEP
static clockid_t		clock_id;
static bool			pgm_clock_init (pgm_error_t**);
static pgm_time_t		pgm_clock_nanosleep (const uint32_t);
#	endif
#endif
#ifdef CONFIG_HAVE_FTIME
#	include <sys/timeb.h>
static pgm_time_t		pgm_ftime_update (void);
#endif
#ifdef CONFIG_HAVE_GETTIMEOFDAY
#	include <sys/time.h>
static pgm_time_t		pgm_gettimeofday_update (void);
#endif
#ifdef CONFIG_HAVE_HPET
#	include <fcntl.h>
#	include <sys/types.h>
#	include <sys/stat.h>
#	include <unistd.h>
#	include <sys/mman.h>
#	define HPET_MMAP_SIZE			0x400
#	define HPET_GENERAL_CAPS_REGISTER	0x00
#	define HPET_COUNTER_CLK_PERIOD		0x004
#	define HPET_MAIN_COUNTER_REGISTER	0x0f0
#	define HPET_COUNT_SIZE_CAP		(1 << 13)
/* HPET counter size maybe 64-bit or 32-bit */
#	if defined(__x86_64__)
typedef uint64_t hpet_counter_t;
#else
typedef uint32_t hpet_counter_t;
#endif
static int			hpet_fd = -1;
static char*			hpet_ptr;
static uint64_t			hpet_offset = 0;
static uint64_t			hpet_wrap;
static hpet_counter_t		hpet_last = 0;

#	define HPET_NS_SCALE	22
#	define HPET_US_SCALE	34
static uint_fast32_t		hpet_ns_mul = 0;
static uint_fast32_t		hpet_us_mul = 0;

static inline
void
set_hpet_mul (
	const uint32_t		hpet_period
	)
{
	hpet_ns_mul = fsecs_to_nsecs((uint64_t)hpet_period << HPET_NS_SCALE);
	hpet_us_mul = fsecs_to_usecs((uint64_t)hpet_period << HPET_US_SCALE);
}

static inline
uint64_t
hpet_to_ns (
	const uint64_t		hpet
	)
{
	return (hpet * hpet_ns_mul) >> HPET_NS_SCALE;
}

static inline
uint64_t
hpet_to_us (
	const uint64_t		hpet
	)
{
	return (hpet * hpet_us_mul) >> HPET_US_SCALE;
}

static bool			pgm_hpet_init (pgm_error_t**);
static bool			pgm_hpet_shutdown (void);
static pgm_time_t		pgm_hpet_update (void);
static pgm_time_t		pgm_hpet_sleep (const uint32_t);
#endif
#ifdef CONFIG_HAVE_NANOSLEEP
#	include <time.h>
static pgm_time_t		pgm_nanosleep (const uint32_t);
#endif
#ifdef CONFIG_HAVE_RTC
#	include <fcntl.h>
#	include <sys/types.h>
#	include <sys/stat.h>
#	include <unistd.h>
#	include <sys/ioctl.h>
#	include <linux/rtc.h>
static int			rtc_fd = -1;
static int			rtc_frequency = 8192;
static pgm_time_t		rtc_count = 0;
static bool			pgm_rtc_init (pgm_error_t**);
static bool			pgm_rtc_shutdown (void);
static pgm_time_t		pgm_rtc_update (void);
static pgm_time_t		pgm_rtc_sleep (const uint32_t);
#endif
#ifdef CONFIG_HAVE_TSC
#	include <stdio.h>
#	include <string.h>
#	define TSC_NS_SCALE	10 /* 2^10, carefully chosen */
#	define TSC_US_SCALE	20
static uint_fast32_t		tsc_mhz = 0;
static uint_fast32_t		tsc_ns_mul = 0;
static uint_fast32_t		tsc_us_mul = 0;

static inline
void
set_tsc_mul (
	const unsigned		khz
	)
{
	tsc_ns_mul = (1000000 << TSC_NS_SCALE) / khz;
	tsc_us_mul = (1000 << TSC_US_SCALE) / khz;
}

static inline
uint64_t
tsc_to_ns (
	const uint64_t		tsc
	)
{
	return (tsc * tsc_ns_mul) >> TSC_NS_SCALE;
}

static inline
uint64_t
tsc_to_us (
	const uint64_t		tsc
	)
{
	return (tsc * tsc_us_mul) >> TSC_US_SCALE;
}

static bool			pgm_tsc_init (pgm_error_t**);
static pgm_time_t		pgm_tsc_update (void);
static pgm_time_t		pgm_tsc_sleep (const uint32_t);
#endif
#ifdef CONFIG_HAVE_USLEEP
#	include <unistd.h>
static pgm_time_t		pgm_usleep (const uint32_t);
#endif
#ifdef _WIN32
static pgm_time_t		pgm_msleep (const uint32_t);
#endif


/* initialize time system.
 *
 * returns TRUE on success, returns FALSE on error such as being unable to open
 * the RTC device, an unstable TSC, or system already initialized.
 */

bool
pgm_time_init (
	pgm_error_t**	error
	)
{
	if (pgm_atomic_int32_exchange_and_add (&time_ref_count, 1) > 0)
		return TRUE;

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
#ifdef CONFIG_HAVE_FTIME
	case 'F':
		pgm_minor (_("Using ftime() timer."));
		pgm_time_update_now	= pgm_ftime_update;
		break;
#endif
#ifdef CONFIG_HAVE_CLOCK_GETTIME
	case 'C':
		pgm_minor (_("Using clock_gettime() timer."));
		pgm_time_update_now	= pgm_clock_update;
		break;
#endif
#ifdef CONFIG_HAVE_RTC
	case 'R':
		pgm_minor (_("Using /dev/rtc timer."));
		pgm_time_update_now	= pgm_rtc_update;
		pgm_time_since_epoch	= pgm_time_conv_from_reset;
		break;
#endif
#ifdef CONFIG_HAVE_TSC
	case 'T':
		pgm_minor (_("Using TSC timer."));
		pgm_time_update_now	= pgm_tsc_update;
		pgm_time_since_epoch	= pgm_time_conv_from_reset;
		break;
#endif
#ifdef CONFIG_HAVE_HPET
	case 'H':
		pgm_minor (_("Using HPET timer."));
		pgm_time_update_now	= pgm_hpet_update;
		pgm_time_since_epoch	= pgm_time_conv_from_reset;
		break;
#endif

	default:
	case 'G':
		pgm_minor (_("Using gettimeofday() timer."));
		pgm_time_update_now	= pgm_gettimeofday_update;
		break;
	}

/* sleeping */
	cfg = getenv ("PGM_SLEEP");
	if (cfg == NULL) cfg = "MSLEEP";

	switch (cfg[0]) {
#ifdef CONFIG_HAVE_CLOCK_NANOSLEEP
	case 'C':
		pgm_minor (_("Using clock_nanosleep() sleep."));
		pgm_time_sleep		= pgm_clock_nanosleep;
		break;
#endif
#ifdef CONFIG_HAVE_NANOSLEEP
	case 'N':
		pgm_minor (_("Using nanosleep() sleep."));
		pgm_time_sleep		= pgm_nanosleep;
		break;
#endif
#ifdef CONFIG_HAVE_RTC
	case 'R':
		pgm_minor (_("Using /dev/rtc sleep."));
		pgm_time_sleep		= pgm_rtc_sleep;
		break;
#endif
#ifdef CONFIG_HAVE_TSC
	case 'T':
		pgm_minor (_("Using TSC sleep."));
		pgm_time_sleep		= pgm_tsc_sleep;
		break;
#endif
#ifdef CONFIG_HAVE_HPET
	case 'H':
		pgm_minor (_("Using HPET sleep."));
		pgm_time_sleep		= pgm_hpet_sleep;
		break;
#endif

	default:
#ifdef CONFIG_HAVE_USLEEP
	case 'M':
	case 'U':
		pgm_minor (_("Using usleep() sleep."));
		pgm_time_sleep		= pgm_usleep;
		break;
#elif defined(_WIN32)
	case 'M':
		pgm_minor (_("Using msleep() sleep."));
		pgm_time_sleep		= pgm_msleep;
		break;
#endif /* CONFIG_HAVE_USLEEP */
	case 'S':
		pgm_minor (_("Using select() sleep."));
		pgm_time_sleep		= pgm_select_sleep;
		break;
	}

#ifdef CONFIG_HAVE_RTC
	if (pgm_time_update_now == pgm_rtc_update || pgm_time_sleep == pgm_rtc_sleep)
	{
		pgm_error_t* sub_error = NULL;
		if (!pgm_rtc_init (&sub_error)) {
			pgm_propagate_error (error, sub_error);
			return FALSE;
		}
	}
#endif
#ifdef CONFIG_HAVE_TSC
	if (pgm_time_update_now == pgm_tsc_update || pgm_time_sleep == pgm_tsc_sleep)
	{
#ifdef CONFIG_HAVE_PROC
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
					const char *p = strchr (buffer, ':');
					if (p) tsc_mhz = atoi (p + 1);
					break;
				}
			}
			fclose (fp);
		}
#elif defined(_WIN32)
		uint64_t frequency;
		if (QueryPerformanceFrequency ((LARGE_INTEGER*)&frequency))
		{
			tsc_mhz = frequency / 1000;
		}
#endif /* !_WIN32 */

/* e.g. export RDTSC_FREQUENCY=3200.000000
 *
 * Value can be used to override kernel tick rate as well as internal calibration
 */
		const char *env_mhz = getenv ("RDTSC_FREQUENCY");
		if (env_mhz)
			tsc_mhz = atoi (env_mhz);

/* calibrate */
		if (0 >= tsc_mhz) {
			pgm_error_t* sub_error = NULL;
			if (!pgm_tsc_init (&sub_error)) {
				pgm_propagate_error (error, sub_error);
#ifdef CONFIG_HAVE_RTC
				if (pgm_time_update_now == pgm_rtc_update || pgm_time_sleep == pgm_rtc_sleep)
					pgm_rtc_shutdown ();
#endif
				return FALSE;
			}
		}
		set_tsc_mul (tsc_mhz * 1000);
	}
#endif /* CONFIG_HAVE_TSC */

#ifdef CONFIG_HAVE_HPET
	if (pgm_time_update_now == pgm_hpet_update || pgm_time_sleep == pgm_hpet_sleep)
	{
		pgm_error_t* sub_error = NULL;
		if (!pgm_hpet_init (&sub_error)) {
			pgm_propagate_error (error, sub_error);
#ifdef CONFIG_HAVE_RTC
			if (pgm_time_update_now == pgm_rtc_update || pgm_time_sleep == pgm_rtc_sleep)
				pgm_rtc_shutdown ();
#endif
			return FALSE;
		}
	}
#endif

#ifdef CONFIG_HAVE_CLOCK_NANOSLEEP
	if (pgm_time_sleep == pgm_clock_nanosleep)
	{
		pgm_error_t* sub_error = NULL;
		if (!pgm_clock_init (&sub_error)) {
			pgm_propagate_error (error, sub_error);
#ifdef CONFIG_HAVE_RTC
			if (pgm_time_update_now == pgm_rtc_update || pgm_time_sleep == pgm_rtc_sleep)
				pgm_rtc_shutdown ();
#endif
#ifdef CONFIG_HAVE_HPET
			if (pgm_time_update_now == pgm_hpet_update || pgm_time_sleep == pgm_hpet_sleep)
				pgm_hpet_shutdown ();
#endif
			return FALSE;
		}
	}
#endif

	pgm_time_update_now();

/* calculate relative time offset */
#if defined(CONFIG_HAVE_RTC) || defined(CONFIG_HAVE_TSC)
	if (	0
#	ifdef CONFIG_HAVE_RTC
		|| pgm_time_update_now == pgm_rtc_update
#	endif
#	ifdef CONFIG_HAVE_TSC
		|| pgm_time_update_now == pgm_tsc_update
#	endif
	   )
	{
#	ifndef CONFIG_HAVE_GETTIMEOFDAY
#		error "gettimeofday() required to calculate counter offset"
#	endif
		rel_offset = pgm_gettimeofday_update() - pgm_time_update_now();
	}
#else
	rel_offset = 0;
#endif

	return TRUE;
}

/* returns TRUE if shutdown succeeded, returns FALSE on error.
 */

bool
pgm_time_shutdown (void)
{
	pgm_return_val_if_fail (pgm_atomic_int32_get (&time_ref_count) > 0, FALSE);

	if (!pgm_atomic_int32_dec_and_test (&time_ref_count))
		return TRUE;

	bool success = TRUE;
#ifdef CONFIG_HAVE_RTC
	if (pgm_time_update_now == pgm_rtc_update || pgm_time_sleep == pgm_rtc_sleep)
		success = pgm_rtc_shutdown ();
#endif
#ifdef CONFIG_HAVE_HPET
	if (pgm_time_update_now == pgm_hpet_update || pgm_time_sleep == pgm_hpet_sleep)
		success = pgm_hpet_shutdown ();
#endif
	return success;
}

#ifdef CONFIG_HAVE_GETTIMEOFDAY
static
pgm_time_t
pgm_gettimeofday_update (void)
{
	struct timeval gettimeofday_now;
	static pgm_time_t last = 0;
	gettimeofday (&gettimeofday_now, NULL);
	const pgm_time_t now = secs_to_usecs (gettimeofday_now.tv_sec) + gettimeofday_now.tv_usec;
	if (PGM_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}
#endif /* CONFIG_HAVE_GETTIMEOFDAY */

#ifdef CONFIG_HAVE_CLOCK_GETTIME
static
pgm_time_t
pgm_clock_update (void)
{
	struct timespec clock_now;
	static pgm_time_t last = 0;
	clock_gettime (CLOCK_MONOTONIC, &clock_now);
	const pgm_time_t now = secs_to_usecs (clock_now.tv_sec) + nsecs_to_usecs (clock_now.tv_nsec);
	if (PGM_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}
#endif /* CONFIG_HAVE_CLOCK_GETTIME */

#ifdef CONFIG_HAVE_FTIME
static
pgm_time_t
pgm_ftime_update (void)
{
	struct timeb ftime_now;
	static pgm_time_t last = 0;
	ftime (&ftime_now);
	const pgm_time_t now = secs_to_usecs (ftime_now.time) + msecs_to_usecs (ftime_now.millitm);
	if (PGM_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}
#endif /* CONFIG_HAVE_FTIME */

#ifdef CONFIG_HAVE_RTC
/* Old PC/AT-Compatible driver:  /dev/rtc
 *
 * Not so speedy 8192 Hz timer, thats 122us resolution.
 *
 * WARNING: time is relative to start of timer.
 * WARNING: only one process is allowed to access the RTC.
 */

static
bool
pgm_rtc_init (
	pgm_error_t**	error
	)
{
	pgm_return_val_if_fail (rtc_fd == -1, FALSE);

	rtc_fd = open ("/dev/rtc", O_RDONLY);
	if (-1 == rtc_fd) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Cannot open /dev/rtc for reading: %s"),
			     strerror(errno));
		return FALSE;
	}
	if (-1 == ioctl (rtc_fd, RTC_IRQP_SET, rtc_frequency)) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Cannot set RTC frequency to %i Hz: %s"),
			     rtc_frequency,
			     strerror(errno));
		return FALSE;
	}
	if (-1 == ioctl (rtc_fd, RTC_PIE_ON, 0)) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Cannot enable periodic interrupt (PIE) on RTC: %s"),
			     strerror(errno));
		return FALSE;
	}
	return TRUE;
}

/* returns TRUE on success even if RTC device cannot be closed or had an IO error,
 * returns FALSE if the RTC file descriptor is not set.
 */

static
bool
pgm_rtc_shutdown (void)
{
	pgm_return_val_if_fail (rtc_fd, FALSE);
	pgm_warn_if_fail (0 == close (rtc_fd));
	rtc_fd = -1;
	return TRUE;
}

/* RTC only indicates passed ticks therefore is by definition monotonic, we do not
 * need to check the difference with respect to the last value.
 */

static
pgm_time_t
pgm_rtc_update (void)
{
	uint32_t data;

/* returned value contains interrupt type and count of interrupts since last read */
	pgm_warn_if_fail (sizeof(data) == read (rtc_fd, &data, sizeof(data)));
	rtc_count += data >> 8;
	return rtc_count * 1000000UL / rtc_frequency;
}

/* use a select to check if we have to clear the current interrupt count
 */

static
pgm_time_t
pgm_rtc_sleep (
	const uint32_t	usec
	)
{
	uint32_t data;

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
	if (pgm_time_update_now == pgm_rtc_update)
		return rtc_count * 1000000UL / rtc_frequency;
	else
		return pgm_time_update_now();
}
#endif /* CONFIG_HAVE_RTC */

#ifdef CONFIG_HAVE_TSC
/* read time stamp counter (TSC), count of ticks from processor reset.
 *
 * NB: On Windows this will usually be HPET or PIC timer interpolated with TSC.
 */

static inline
pgm_time_t
rdtsc (void)
{
#ifndef _WIN32
	uint32_t lo, hi;

/* We cannot use "=A", since this would use %rax on x86_64 */
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));

	return (pgm_time_t)hi << 32 | lo;
#else
	uint64_t counter;
	QueryPerformanceCounter ((LARGE_INTEGER*)&counter);
	return (pgm_time_t)counter;
#endif
}

/* determine ratio of ticks to nano-seconds, use /dev/rtc for high accuracy
 * millisecond timer and convert.
 *
 * WARNING: time is relative to start of timer.
 */

static
bool
pgm_tsc_init (
	PGM_GNUC_UNUSED pgm_error_t**	error
	)
{
#ifdef CONFIG_HAVE_PROC
/* Test for constant TSC from kernel
 */
	FILE* fp = fopen ("/proc/cpuinfo", "r");
	char buffer[1024], *flags = NULL;
	if (fp)
	{
		while (!feof(fp) && fgets (buffer, sizeof(buffer), fp))
		{
			if (strstr (buffer, "flags"))
			{
				flags = strchr (buffer, ':');
				break;
			}
		}
		fclose (fp);
	}
	if (!flags || !strstr (flags, " tsc")) {
		pgm_warn (_("Linux kernel reports no Time Stamp Counter (TSC)."));
/* force both to stable clocks even though one might be OK */
		pgm_time_update_now	= pgm_gettimeofday_update;
		pgm_time_sleep		= pgm_usleep;
		return TRUE;
	}
	if (!strstr (flags, " constant_tsc")) {
		pgm_warn (_("Linux kernel reports non-constant Time Stamp Counter (TSC)."));
/* force both to stable clocks even though one might be OK */
		pgm_time_update_now	= pgm_gettimeofday_update;
		pgm_time_sleep		= pgm_usleep;
		return TRUE;
	}
#endif /* CONFIG_HAVE_PROC */
	pgm_time_t start, stop;
	const pgm_time_t calibration_usec = 4000 * 1000;

	pgm_info (_("Running a benchmark to measure system clock frequency..."));

	start = rdtsc();
	pgm_time_sleep (calibration_usec);
	stop = rdtsc();

	if (stop < start)
	{
		pgm_warn (_("Finished RDTSC test.  Unstable TSC detected.  The benchmark resulted in a "
			   "non-monotonic time response rendering the TSC unsuitable for high resolution "
			   "timing.  To prevent the start delay from this benchmark and use a stable clock "
			   "source set the environment variables PGM_TIMER to GTOD and PGM_SLEEP to USLEEP."));
/* force both to stable clocks even though one might be OK */
		pgm_time_update_now	= pgm_gettimeofday_update;
		pgm_time_sleep		= pgm_usleep;
		return TRUE;
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

	pgm_info (_("Finished RDTSC test. To prevent the startup delay from this benchmark, "
		   "set the environment variable RDTSC_FREQUENCY to %" PRIuFAST32 " on this "
		   "system. This value is dependent upon the CPU clock speed and "
		   "architecture and should be determined separately for each server."),
		   tsc_mhz);
	return TRUE;
}

/* TSC is monotonic on the same core but we do neither force the same core or save the count
 * for each core as if the counter is unstable system wide another timing mechanism should be
 * used, preferably HPET on x86/AMD64 or gettimeofday() on SPARC.
 */

static
pgm_time_t
pgm_tsc_update (void)
{
	static pgm_time_t last = 0;
	const pgm_time_t now = tsc_to_us (rdtsc());
	if (PGM_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}	

static
pgm_time_t
pgm_tsc_sleep (
	const uint32_t	usec
	)
{
	pgm_time_t now;
	const pgm_time_t start = tsc_to_us (rdtsc());
	const pgm_time_t end   = start + usec;

	for (;;) {
		now = tsc_to_us (rdtsc());
		if (now < end) {
			pgm_thread_yield ();
		} else break;
	}

	if (pgm_time_update_now == pgm_tsc_update)
		return now;
	else
		return pgm_time_update_now();
}
#endif /* CONFIG_HAVE_TSC */

#ifdef CONFIG_HAVE_HPET
/* High Precision Event Timer (HPET) created as a system wide stable high resolution timer
 * to replace dependency on core specific counters (TSC).
 *
 * NB: Only available on x86/AMD64 hardware post 2007
 */

static
bool
pgm_hpet_init (
	pgm_error_t**	error
	)
{
	pgm_return_val_if_fail (hpet_fd == -1, FALSE);

	hpet_fd = open("/dev/hpet", O_RDONLY);
	if (hpet_fd < 0) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Cannot open /dev/hpet for reading: %s"),
			     strerror(errno));
		return FALSE;
	}

	hpet_ptr = mmap(NULL, HPET_MMAP_SIZE, PROT_READ, MAP_SHARED, hpet_fd, 0);
	if (MAP_FAILED == hpet_ptr) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Error mapping HPET device: %s"),
			     strerror(errno));
		close (hpet_fd);
		hpet_fd = -1;
		return FALSE;
	}

/* HPET counter tick period is in femto-seconds, a value of 0 is not permitted,
 * the value must be <= 0x05f5e100 or 100ns.
 */
	const uint32_t hpet_period = *((uint32_t*)(hpet_ptr + HPET_COUNTER_CLK_PERIOD));
	set_hpet_mul (hpet_period);
#if defined(__x86_64__)
	const uint32_t hpet_caps = *((uint32_t*)(hpet_ptr + HPET_GENERAL_CAPS_REGISTER));
	hpet_wrap = hpet_caps & HPET_COUNT_SIZE_CAP ? 0 : (1ULL << 32);
#else
	hpet_wrap = 1ULL << 32;
#endif

	return TRUE;
}

static
bool
pgm_hpet_shutdown (void)
{
	pgm_return_val_if_fail (hpet_fd, FALSE);
	pgm_warn_if_fail (0 == close (hpet_fd));
	hpet_fd = -1;
	return TRUE;
}

static
pgm_time_t
pgm_hpet_update (void)
{
	const hpet_counter_t hpet_count = *((hpet_counter_t*)(hpet_ptr + HPET_MAIN_COUNTER_REGISTER));
/* 32-bit HPET counters wrap after ~4 minutes */
	if (PGM_UNLIKELY(hpet_count < hpet_last))
		hpet_offset += hpet_wrap;
	hpet_last = hpet_count;
	return hpet_to_us (hpet_offset + hpet_count);
}

static
pgm_time_t
pgm_hpet_sleep (
	const uint32_t	usec
	)
{
	pgm_time_t now;
	const pgm_time_t start = pgm_hpet_update();
	const pgm_time_t end   = start + usec;

	for (;;) {
		now = pgm_hpet_update();
		if (now < end) {
			pgm_thread_yield ();
		} else break;
	}

	if (pgm_time_update_now == pgm_hpet_update)
		return now;
	else
		return pgm_time_update_now();
}
#endif /* CONFIG_HAVE_HPET */

#if defined(CONFIG_HAVE_CLOCK_NANOSLEEP)
/* Allegedly a high performance set of flexible timers, but in reality less then
 * meh performance, not even portable among Linux versions.
 */

static
bool
pgm_clock_init (
	PGM_GNUC_UNUSED pgm_error_t**	error
	)
{
	clock_id = CLOCK_REALTIME;
//	clock_id = CLOCK_MONOTONIC;
//	clock_id = CLOCK_PROCESS_CPUTIME_ID;
//	clock_id = CLOCK_THREAD_CPUTIME_ID;

#if 0
//	clock_getcpuclockid (0, &clock_id);
//	pthread_getcpuclockid (pthread_self(), &clock_id);

	struct timespec ts;
	if (clock_getres (clock_id, &ts) > 0) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("clock_getres() failed: %s"),
			     strerror (errno));
		return FALSE;
	}
	pgm_minor (_("Clock resolution %lu.%.9lu"), ts.tv_sec, ts.tv_nsec);
#endif
	return TRUE;
}

static
pgm_time_t
pgm_clock_nanosleep (
	const uint32_t	usec
	)
{
	struct timespec ts;
#if 0
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	clock_nanosleep (clock_id, 0, &ts, NULL);
#else
	const pgm_time_t abs_usec = usec + pgm_time_update_now ();
	ts.tv_sec	= abs_usec / 1000000UL;
	ts.tv_nsec	= (abs_usec % 1000000UL) * 1000;
	clock_nanosleep (clock_id, TIMER_ABSTIME, &ts, NULL);
#endif
	return pgm_time_update_now ();
}
#endif /* CONFIG_HAVE_CLOCK_NANOSLEEP */

#ifdef CONFIG_HAVE_NANOSLEEP
static
pgm_time_t
pgm_nanosleep (
	const uint32_t	usec
	)
{
	struct timespec ts;
	ts.tv_sec	= usec / 1000000UL;
	ts.tv_nsec	= (usec % 1000000UL) * 1000;
	nanosleep (&ts, NULL);
	return pgm_time_update_now ();
}
#endif /* CONFIG_HAVE_NANOSLEEP */

#ifdef _WIN32
static
pgm_time_t
pgm_msleep (
	const uint32_t	usec
	)
{
	Sleep (usecs_to_msecs (usec));
	return pgm_time_update_now ();
}
#endif

#ifdef CONFIG_HAVE_USLEEP
static
pgm_time_t
pgm_usleep (
	const uint32_t	usec
	)
{
	usleep (usec);
	return pgm_time_update_now ();
}
#endif /* CONFIG_HAVE_USLEEP */

static
pgm_time_t
pgm_select_sleep (
	const uint32_t	usec
	)
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

/* convert from pgm_time_t to time_t with pgm_time_t in microseconds since the epoch.
 */
static
void
pgm_time_conv (
	const pgm_time_t* const restrict pgm_time_t_time,
	time_t*	                restrict time_t_time
	)
{
	*time_t_time = pgm_to_secs (*pgm_time_t_time);
}

/* convert from pgm_time_t to time_t with pgm_time_t in microseconds since the core started.
 */
static
void
pgm_time_conv_from_reset (
	const pgm_time_t* const restrict pgm_time_t_time,
	time_t*			restrict time_t_time
	)
{
	*time_t_time = pgm_to_secs (*pgm_time_t_time + rel_offset);
}

/* eof */
