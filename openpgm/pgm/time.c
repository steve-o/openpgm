/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * high resolution timers.
 *
 * NB: RDTSC requires P5 microarchitecture.
 *
 * Copyright (c) 2006-2011 Miru Limited.
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

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <errno.h>
#include <stdlib.h>
#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <mmsystem.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>

//#define TIME_DEBUG


/* globals */

pgm_time_update_func		pgm_time_update_now PGM_GNUC_READ_MOSTLY;
pgm_time_since_epoch_func	pgm_time_since_epoch PGM_GNUC_READ_MOSTLY;


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

static volatile uint32_t	time_ref_count = 0;
static pgm_time_t		rel_offset PGM_GNUC_READ_MOSTLY = 0;

#ifdef _WIN32
static UINT			wTimerRes = 0;
#endif

static void			pgm_time_conv (const pgm_time_t*const restrict, time_t*restrict);
static void			pgm_time_conv_from_reset (const pgm_time_t*const restrict, time_t*restrict);

#if defined(HAVE_CLOCK_GETTIME)
#	include <time.h>
static pgm_time_t		pgm_clock_update (void);
#endif
#ifdef HAVE_FTIME
#	include <sys/timeb.h>
static pgm_time_t		pgm_ftime_update (void);
#endif
#ifdef HAVE_GETTIMEOFDAY
#	include <sys/time.h>
static pgm_time_t		pgm_gettimeofday_update (void);
#endif
#ifdef _WIN32
static pgm_time_t		pgm_mmtime_update (void);
static pgm_time_t		pgm_queryperformancecounter_update (void);
#endif
#ifdef HAVE_DEV_HPET
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
#	else
typedef uint32_t hpet_counter_t;
#	endif
static int			hpet_fd PGM_GNUC_READ_MOSTLY = -1;
static char*			hpet_ptr PGM_GNUC_READ_MOSTLY;
static uint64_t			hpet_offset = 0;
static uint64_t			hpet_wrap PGM_GNUC_READ_MOSTLY;
static hpet_counter_t		hpet_last = 0;

#	define HPET_NS_SCALE	22
#	define HPET_US_SCALE	34
static uint_fast32_t		hpet_ns_mul PGM_GNUC_READ_MOSTLY = 0;
static uint_fast32_t		hpet_us_mul PGM_GNUC_READ_MOSTLY = 0;

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
#endif
#ifdef HAVE_DEV_RTC
#	include <fcntl.h>
#	include <sys/types.h>
#	include <sys/stat.h>
#	include <unistd.h>
#	include <sys/ioctl.h>
#	include <linux/rtc.h>
static int			rtc_fd PGM_GNUC_READ_MOSTLY = -1;
static int			rtc_frequency PGM_GNUC_READ_MOSTLY = 8192;
static pgm_time_t		rtc_count = 0;
static bool			pgm_rtc_init (pgm_error_t**);
static bool			pgm_rtc_shutdown (void);
static pgm_time_t		pgm_rtc_update (void);
#endif
#ifdef HAVE_RDTSC
#	include <stdio.h>
#	include <string.h>
#	if defined(__APPLE__) || defined(__FreeBSD__)
#		include <sys/sysctl.h>
#	elif defined(__sun)
#		include <kstat.h>
#	elif defined(_MSC_VER)
#		include <intrin.h>
#	endif
#	define TSC_NS_SCALE	10 /* 2^10, carefully chosen */
#	define TSC_US_SCALE	20
static uint_fast32_t		tsc_khz PGM_GNUC_READ_MOSTLY = 0;
static uint_fast32_t		tsc_ns_mul PGM_GNUC_READ_MOSTLY = 0;
static uint_fast32_t		tsc_us_mul PGM_GNUC_READ_MOSTLY = 0;

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
ns_to_tsc (
	const uint64_t		ns
	)
{
	return (ns << TSC_NS_SCALE) / tsc_ns_mul;
}

static inline
uint64_t
tsc_to_us (
	const uint64_t		tsc
	)
{
	return (tsc * tsc_us_mul) >> TSC_US_SCALE;
}

static inline
uint64_t
us_to_tsc (
	const uint64_t		us
	)
{
	return (us << TSC_US_SCALE) / tsc_us_mul;
}

#	ifndef _WIN32
static bool			pgm_tsc_init (pgm_error_t**);
#	endif
static pgm_time_t		pgm_tsc_update (void);
#endif


/* initialize time system.
 *
 * returns TRUE on success, returns FALSE on error such as being unable to open
 * the RTC device, an unstable TSC, or system already initialized.
 */

PGM_GNUC_INTERNAL
bool
pgm_time_init (
	pgm_error_t**	error
	)
{
	char	*pgm_timer;
	size_t	 envlen;
	errno_t	 err;

	if (pgm_atomic_exchange_and_add32 (&time_ref_count, 1) > 0)
		return TRUE;

/* user preferred time stamp function */
	err = pgm_dupenv_s (&pgm_timer, &envlen, "PGM_TIMER");
	if (0 != err || 0 == envlen) {
		pgm_timer = pgm_strdup (
/* default time stamp function */
#if defined(_WIN32)
			"MMTIME"
#else
			"GETTIMEOFDAY"
#endif
				);
	}

	pgm_time_since_epoch = pgm_time_conv;

	switch (pgm_timer[0]) {
#ifdef HAVE_FTIME
	case 'F':
		pgm_minor (_("Using ftime() timer."));
		pgm_time_update_now	= pgm_ftime_update;
		break;
#endif
#ifdef HAVE_CLOCK_GETTIME
	case 'C':
		pgm_minor (_("Using clock_gettime() timer."));
		pgm_time_update_now	= pgm_clock_update;
		break;
#endif
#ifdef HAVE_DEV_RTC
	case 'R':
		pgm_minor (_("Using /dev/rtc timer."));
		pgm_time_update_now	= pgm_rtc_update;
		pgm_time_since_epoch	= pgm_time_conv_from_reset;
		break;
#endif
#ifdef HAVE_RDTSC
	case 'T':
		pgm_minor (_("Using TSC timer."));
		pgm_time_update_now	= pgm_tsc_update;
		pgm_time_since_epoch	= pgm_time_conv_from_reset;
		break;
#endif
#ifdef HAVE_DEV_HPET
	case 'H':
		pgm_minor (_("Using HPET timer."));
		pgm_time_update_now	= pgm_hpet_update;
		pgm_time_since_epoch	= pgm_time_conv_from_reset;
		break;
#endif
#ifdef HAVE_GETTIMEOFDAY
	case 'G':
		pgm_minor (_("Using gettimeofday() timer."));
		pgm_time_update_now	= pgm_gettimeofday_update;
		break;
#endif
#ifdef _WIN32
	case 'M':
		pgm_minor (_("Using multi-media timer."));
		pgm_time_update_now	= pgm_mmtime_update;
		pgm_time_since_epoch	= pgm_time_conv_from_reset;
		break;

	case 'Q':
		pgm_minor (_("Using QueryPerformanceCounter() timer."));
		pgm_time_update_now	= pgm_queryperformancecounter_update;
		pgm_time_since_epoch	= pgm_time_conv_from_reset;
		break;
#endif

	default:
		pgm_set_error (error,
			       PGM_ERROR_DOMAIN_TIME,
			       PGM_ERROR_FAILED,
			       _("Unsupported time stamp function: PGM_TIMER=%s"),
			       pgm_timer);
		pgm_free (pgm_timer);
		goto err_cleanup;
	}

/* clean environment copy */
	pgm_free (pgm_timer);

#ifdef HAVE_DEV_RTC
	if (pgm_time_update_now == pgm_rtc_update)
	{
		pgm_error_t* sub_error = NULL;
		if (!pgm_rtc_init (&sub_error)) {
			pgm_propagate_error (error, sub_error);
			goto err_cleanup;
		}
	}
#endif
#ifdef _WIN32
	if (pgm_time_update_now == pgm_queryperformancecounter_update)
	{
/* MSDN statement: The frequency cannot change while the system is running.
 * http://msdn.microsoft.com/en-us/library/ms644905(v=vs.85).aspx
 */
		LARGE_INTEGER frequency;
		if (QueryPerformanceFrequency (&frequency))
		{
			tsc_khz = (uint_fast32_t)(frequency.QuadPart / 1000LL);
			pgm_minor (_("High-resolution performance counter frequency %lld Hz"),
				frequency.QuadPart);
		}
		else
		{
			const DWORD save_errno = GetLastError();
			char winstr[1024];
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_TIME,
				       PGM_ERROR_FAILED,
				       _("No supported high-resolution performance counter: %s"),
				       pgm_win_strerror (winstr, sizeof (winstr), save_errno));
			goto err_cleanup;
		}
		set_tsc_mul (tsc_khz);
	}
#endif /* _WIN32 */
#ifdef HAVE_RDTSC
	if (pgm_time_update_now == pgm_tsc_update)
	{
		char	*rdtsc_frequency;

#ifdef HAVE_PROC_CPUINFO
/* attempt to parse clock ticks from kernel
 */
		FILE	*fp = fopen ("/proc/cpuinfo", "r");
		if (fp)
		{
			char buffer[1024];
			while (!feof(fp) && fgets (buffer, sizeof(buffer), fp))
			{
				if (strstr (buffer, "cpu MHz")) {
					const char *p = strchr (buffer, ':');
					if (p) tsc_khz = atoi (p + 1) * 1000;
					break;
				}
			}
			fclose (fp);
		}
#elif defined(_WIN32)
/* core frequency HKLM/Hardware/Description/System/CentralProcessor/0/~Mhz
 */
		HKEY hKey;
		if (ERROR_SUCCESS == RegOpenKeyExA (HKEY_LOCAL_MACHINE,
					"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
					0,
					KEY_QUERY_VALUE,
					&hKey))
		{
			DWORD dwData = 0;
			DWORD dwDataSize = sizeof (dwData);
			if (ERROR_SUCCESS == RegQueryValueExA (hKey,
						"~MHz",
						NULL,
						NULL,
						(LPBYTE)&dwData,
						&dwDataSize))
			{
				tsc_khz = dwData * 1000;
				pgm_minor (_("Registry reports central processor frequency %u MHz"),
					(unsigned)dwData);
/* dump processor name for comparison aid of obtained frequency */
				char szProcessorBrandString[48];
				dwDataSize = sizeof (szProcessorBrandString);
				if (ERROR_SUCCESS == RegQueryValueExA (hKey,
							"ProcessorNameString",
							NULL,
							NULL,
							(LPBYTE)szProcessorBrandString,
							&dwDataSize))
				{
					pgm_minor (_("Processor Brand String \"%s\""), szProcessorBrandString);
				}
			}
			else
			{
				const DWORD save_errno = GetLastError();
				char winstr[1024];
				pgm_warn (_("Registry query on HKLM\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0\\~MHz failed: %s"),
					pgm_win_strerror (winstr, sizeof (winstr), save_errno));
			}
			RegCloseKey (hKey);
		}
#elif defined(__APPLE__)
/* nb: RDTSC is non-functional on Darwin */
		uint64_t cpufrequency;
		size_t len;
		len = sizeof (cpufrequency);
		if (0 == sysctlbyname ("hw.cpufrequency", &cpufrequency, &len, NULL, 0)) {
			tsc_khz = (uint_fast32_t)(cpufrequency / 1000);
		}
#elif defined(__FreeBSD__)
/* frequency in Mhz */
		unsigned long clockrate;
		size_t len;
		len = sizeof (clockrate);
		if (0 == sysctlbyname ("hw.clockrate", &clockrate, &len, NULL, 0)) {
			tsc_khz = (uint_fast32_t)(clockrate * 1000);
		}
#elif defined(KSTAT_DATA_INT32)
/* ref: http://developers.sun.com/solaris/articles/kstatc.html */
		kstat_ctl_t* kc;
		kstat_t* ksp;
		kstat_named_t* kdata;
		if (NULL != (kc = kstat_open()) &&
			NULL != (ksp = kstat_lookup (kc, "cpu_info", -1, NULL)) &&
			KSTAT_TYPE_NAMED == ksp->ks_type &&
			-1 != kstat_read (kc, ksp, NULL) &&
			NULL != (kdata = kstat_data_lookup (ksp, "clock_MHz")) &&
			KSTAT_DATA_INT32 == kdata->data_type)
		{
			tsc_khz = (uint_fast32_t)(kdata->value.i32 * 1000);
			kstat_close (kc);
		}
#endif /* !_WIN32 */

/* e.g. export RDTSC_FREQUENCY=3200.000000
 *
 * Value can be used to override kernel tick rate as well as internal calibration
 */
		err = pgm_dupenv_s (&rdtsc_frequency, &envlen, "RDTSC_FREQUENCY");
		if (0 == err && envlen > 0) {
			tsc_khz = atoi (rdtsc_frequency) * 1000;
			pgm_free (rdtsc_frequency);
		}

#ifndef _WIN32
/* calibrate */
		if (0 >= tsc_khz) {
			pgm_error_t* sub_error = NULL;
			if (!pgm_tsc_init (&sub_error)) {
				pgm_propagate_error (error, sub_error);
				goto err_cleanup;
			}
		}
#endif
		pgm_minor (_("TSC frequency set at %u KHz"), (unsigned)(tsc_khz));
		set_tsc_mul (tsc_khz);
	}
#endif /* HAVE_RDTSC */

#ifdef HAVE_DEV_HPET
	if (pgm_time_update_now == pgm_hpet_update)
	{
		pgm_error_t* sub_error = NULL;
		if (!pgm_hpet_init (&sub_error)) {
			pgm_propagate_error (error, sub_error);
			goto err_cleanup;
		}
	}
#endif

	pgm_time_update_now();

/* calculate relative time offset */
#if defined(HAVE_DEV_RTC) || defined(HAVE_RDTSC) || defined(HAVE_DEV_HPET) || defined(_WIN32)
	if (	0
#	ifdef HAVE_DEV_RTC
		|| pgm_time_update_now == pgm_rtc_update
#	endif
#	ifdef HAVE_RDTSC
		|| pgm_time_update_now == pgm_tsc_update
#	endif
#	ifdef HAVE_DEV_HPET
		|| pgm_time_update_now == pgm_hpet_update
#	endif
#	ifdef _WIN32
		|| pgm_time_update_now == pgm_mmtime_update
		|| pgm_time_update_now == pgm_queryperformancecounter_update
#	endif
	   )
	{
#	if defined( HAVE_GETTIMEOFDAY )
		rel_offset = pgm_gettimeofday_update() - pgm_time_update_now();
#	elif defined( HAVE_FTIME )
		rel_offset = pgm_ftime_update() - pgm_time_update_now();
#	else
#		error "gettimeofday() or ftime() required to calculate counter offset"
#	endif
	}
#else
	rel_offset = 0;
#endif

/* update Windows timer resolution to 1ms */
#ifdef _WIN32
	TIMECAPS tc;
	if (TIMERR_NOERROR == timeGetDevCaps (&tc, sizeof (TIMECAPS)))
	{
		wTimerRes = min (max (tc.wPeriodMin, 1 /* ms */), tc.wPeriodMax);
		timeBeginPeriod (wTimerRes);
		pgm_debug ("Set timer resolution to %ums.", wTimerRes);
	}
	else
	{
		pgm_warn (_("Unable to determine timer device resolution."));
	}
#endif

	return TRUE;
err_cleanup:
	pgm_atomic_dec32 (&time_ref_count);
	return FALSE;
}

/* returns TRUE if shutdown succeeded, returns FALSE on error.
 */

PGM_GNUC_INTERNAL
bool
pgm_time_shutdown (void)
{
	bool retval = TRUE;

	pgm_return_val_if_fail (pgm_atomic_read32 (&time_ref_count) > 0, FALSE);

	if (pgm_atomic_exchange_and_add32 (&time_ref_count, (uint32_t)-1) != 1)
		return retval;

#ifdef _WIN32
	timeEndPeriod (wTimerRes);
#endif

#ifdef HAVE_DEV_RTC
	if (pgm_time_update_now == pgm_rtc_update)
		retval = pgm_rtc_shutdown ();
#endif
#ifdef HAVE_DEV_HPET
	if (pgm_time_update_now == pgm_hpet_update)
		retval = pgm_hpet_shutdown ();
#endif
	return retval;
}

#ifdef HAVE_GETTIMEOFDAY
static
pgm_time_t
pgm_gettimeofday_update (void)
{
	struct timeval		gettimeofday_now;
	pgm_time_t		now;
	static pgm_time_t	last = 0;

	gettimeofday (&gettimeofday_now, NULL);
	now = secs_to_usecs (gettimeofday_now.tv_sec) + gettimeofday_now.tv_usec;
	if (PGM_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}
#endif /* HAVE_GETTIMEOFDAY */

#ifdef HAVE_CLOCK_GETTIME
static
pgm_time_t
pgm_clock_update (void)
{
	struct timespec		clock_now;
	pgm_time_t		now;
	static pgm_time_t	last = 0;

	clock_gettime (CLOCK_MONOTONIC, &clock_now);
	now = secs_to_usecs (clock_now.tv_sec) + nsecs_to_usecs (clock_now.tv_nsec);
	if (PGM_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}
#endif /* HAVE_CLOCK_GETTIME */

#ifdef HAVE_FTIME
/* Note that _ftime32_s is implemented using GetSystemTimeAsFileTime().
 * ref: CRT source code as shipped with Microsoft Visual Studio: crt/src/ftime.c
 */
static
pgm_time_t
pgm_ftime_update (void)
{
#ifndef _WIN32
	struct timeb		ftime_now;
#elif !defined( _MSC_VER )
	struct _timeb		ftime_now;
#else
	struct __timeb64	ftime_now;
#endif
	pgm_time_t		now;
	static pgm_time_t	last = 0;

	pgm_ftime_s (&ftime_now);
	now = secs_to_usecs (ftime_now.time) + msecs_to_usecs (ftime_now.millitm);
	if (PGM_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}
#endif /* HAVE_FTIME */

#ifdef HAVE_DEV_RTC
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
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Cannot open /dev/rtc for reading: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), errno));
		return FALSE;
	}
	if (-1 == ioctl (rtc_fd, RTC_IRQP_SET, rtc_frequency)) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Cannot set RTC frequency to %i Hz: %s"),
			     rtc_frequency,
			     pgm_strerror_s (errbuf, sizeof (errbuf), errno));
		return FALSE;
	}
	if (-1 == ioctl (rtc_fd, RTC_PIE_ON, 0)) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Cannot enable periodic interrupt (PIE) on RTC: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), errno));
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
#endif /* HAVE_DEV_RTC */

#ifdef _WIN32
/* Multi-media like timer API, returns 100-nanoseconds intervals.
 *
 * Faster than QueryPerformanceCounter and hence deployed for SQL Server.
 * http://blogs.msdn.com/b/psssql/archive/2008/12/16/how-it-works-sql-server-no-longer-uses-rdtsc-for-timings-in-sql-2008-and-sql-2005-service-pack-3-sp3.aspx
 */
static
pgm_time_t
pgm_mmtime_update (void)
{
	FILETIME ft;
	int64_t aligned_ft;

	GetSystemTimeAsFileTime (&ft);
	memcpy (&aligned_ft, &ft, sizeof (int64_t));
	return (pgm_time_t)( aligned_ft / 10 );
}

/* High-resolution performance counter.
 *
 * Microsoft performance note on API cost:
 * http://blogs.msdn.com/b/psssql/archive/2006/11/27/sql-server-2005-sp2-will-introduce-new-messages-to-the-error-log-related-to-timing-activities.aspx
 * "QueryPerformanceCounter is a more stable counter but very costly when called lots of times like SQL Server would do."
 *
 * Microsoft note on TSC drift affect on this API.
 * http://support.microsoft.com/kb/895980
 */
static
pgm_time_t
pgm_queryperformancecounter_update (void)
{
	LARGE_INTEGER counter;

	QueryPerformanceCounter (&counter);
	return (pgm_time_t)counter.QuadPart;
}
#endif /* _WIN32 */

#ifdef HAVE_RDTSC
/* read time stamp counter (TSC), count of ticks from processor reset.
 *
 * Microsoft notes on TSC drift:
 * http://support.microsoft.com/kb/931279
 * http://support.microsoft.com/kb/938448
 */

static inline
pgm_time_t
pgm_rdtsc (void)
{
#	ifndef _MSC_VER

	uint32_t lo, hi;

/* We cannot use "=A", since this would use %rax on x86_64 */
	__asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
	return (pgm_time_t)hi << 32 | lo;

#	else

	return (pgm_time_t)__rdtsc ();

#	endif
}

#	ifndef _WIN32
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
#		ifdef HAVE_PROC_CPUINFO
/* Test for constant TSC from kernel
 */
	FILE	*fp = fopen ("/proc/cpuinfo", "r");
	char	buffer[1024], *flags = NULL;
	if (fp)
	{
		while (!feof(fp) && fgets (buffer, sizeof(buffer), fp))
		{
			if (strstr (buffer, "flags")) {
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
		return TRUE;
	}
	if (!strstr (flags, " constant_tsc")) {
		pgm_warn (_("Linux kernel reports non-constant Time Stamp Counter (TSC)."));
/* force both to stable clocks even though one might be OK */
		pgm_time_update_now	= pgm_gettimeofday_update;
		return TRUE;
	}
#		endif /* HAVE_PROC_CPUINFO */

	pgm_time_t		start, stop, elapsed;
	const pgm_time_t	calibration_usec = secs_to_usecs (4);
	struct timespec		req = {
					.tv_sec  = 4,
					.tv_nsec = 0
				};

	pgm_info (_("Running a benchmark to measure system clock frequency..."));

	start = pgm_rdtsc();
	while (-1 == nanosleep (&req, &req) && EINTR == errno);
	stop = pgm_rdtsc();

	if (stop < start)
	{
		pgm_warn (_("Finished RDTSC test.  Unstable TSC detected.  The benchmark resulted in a "
			   "non-monotonic time response rendering the TSC unsuitable for high resolution "
			   "timing.  To prevent the start delay from this benchmark and use a stable clock "
			   "source set the environment variable PGM_TIMER to GTOD."));
/* force both to stable clocks even though one might be OK */
		pgm_time_update_now = pgm_gettimeofday_update;
		return TRUE;
	}

/* TODO: this math needs to be scaled to reduce rounding errors */
	elapsed = stop - start;
	if (elapsed > calibration_usec) {
/* cpu > 1 Ghz */
		tsc_khz = (elapsed * 1000) / calibration_usec;
	} else {
/* cpu < 1 Ghz */
		tsc_khz = -( (calibration_usec * 1000) / elapsed );
	}

	pgm_info (_("Finished RDTSC test. To prevent the startup delay from this benchmark, "
		   "set the environment variable RDTSC_FREQUENCY to %" PRIuFAST32 " on this "
		   "system. This value is dependent upon the CPU clock speed and "
		   "architecture and should be determined separately for each server."),
		   tsc_khz);
	return TRUE;
}
#	endif

/* TSC is monotonic on the same core but we do neither force the same core or save the count
 * for each core as if the counter is unstable system wide another timing mechanism should be
 * used, preferably HPET on x86/AMD64 or gettimeofday() on SPARC.
 */

static
pgm_time_t
pgm_tsc_update (void)
{
	static pgm_time_t	last = 0;
	const pgm_time_t	now = tsc_to_us (pgm_rdtsc());

	if (PGM_UNLIKELY(now < last))
		return last;
	else
		return last = now;
}
#endif /* HAVE_RDTSC */

#ifdef HAVE_DEV_HPET
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
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Cannot open /dev/hpet for reading: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), errno));
		return FALSE;
	}

	hpet_ptr = mmap(NULL, HPET_MMAP_SIZE, PROT_READ, MAP_SHARED, hpet_fd, 0);
	if (MAP_FAILED == hpet_ptr) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_TIME,
			     PGM_ERROR_FAILED,
			     _("Error mapping HPET device: %s"),
			     pgm_strerror_s (errbuf, sizeof (errbuf), errno));
		close (hpet_fd);
		hpet_fd = -1;
		return FALSE;
	}

/* HPET counter tick period is in femto-seconds, a value of 0 is not permitted,
 * the value must be <= 0x05f5e100 or 100ns.
 */
	const uint32_t hpet_period = *((uint32_t*)(hpet_ptr + HPET_COUNTER_CLK_PERIOD));
	set_hpet_mul (hpet_period);
#if defined( __x86_64__ ) || defined( __amd64 )
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
#endif /* HAVE_DEV_HPET */

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
