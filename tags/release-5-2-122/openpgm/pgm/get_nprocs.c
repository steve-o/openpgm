/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable function to return number of available, online, or
 * configured processors.
 *
 * Copyright (c) 2011 Miru Limited.
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

#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

#ifndef _WIN32
#	include <sched.h>
#	include <unistd.h>
#	if defined( __sun )
#		include <sys/pset.h>
#	endif
#	include <sys/types.h>
#	if defined( __APPLE__ )
#		include <sys/sysctl.h>
#	endif
#endif
#include <impl/framework.h>


//#define GET_NPROCS_DEBUG


#ifdef __APPLE__
/* processor binding is not supported, as per 10.5 Affinity API release notes.
 *
 * sysctl may be compatible with *BSD platforms.
 */

static
int
_pgm_apple_get_nprocs (void)
{
	int online = 0, configured = 0;
	int mib[2];
	size_t len;

	mib[0] = CTL_HW;
	mib[1] = HW_AVAILCPU;
	len = sizeof (int);
	sysctl (mib, 2, &online, &len, NULL, 0);
	mib[1] = HW_NCPU;
	len = sizeof (int);
	sysctl (mib, 2, &configured, &len, NULL, 0);

	if (online > configured)
		online = configured;

	pgm_minor (_("Detected %d online %d configured CPUs."),
		online, configured);

	return (online > 0) ? online : configured;
}
#endif

#ifdef _WIN32
static
int
_pgm_win32_get_nprocs (void)
{
	int available = 0, online = 0, configured = 0;
	DWORD_PTR process_mask, system_mask;
	DWORD_PTR mask;
	SYSTEM_INFO si;

	if (GetProcessAffinityMask (GetCurrentProcess(), &process_mask, &system_mask)) {
		for (mask = process_mask; mask != 0; mask >>= 1)
			if (mask & 1)
				available++;
	}

	GetSystemInfo (&si);
	configured = (int)si.dwNumberOfProcessors;
	for (mask = si.dwActiveProcessorMask; mask != 0; mask >>= 1)
		if (mask & 1)
			online++;

	if (online > configured)
		online = configured;
	if (available > online)
		available = online;

	pgm_minor (_("Detected %d available %d online %d configured CPUs."),
		available, online, configured);

	return (available > 0) ? available :
	       ((online > 0) ? online : configured);
}
#endif

#ifdef PS_MYID
/* Solaris processor set API
 */

static
int
_pgm_pset_get_nprocs (void)
{
	uint_t available = 0;

	pset_info (PS_MYID, NULL, &available, NULL);
	return (int)available;
}
#endif

#if defined( CPU_SETSIZE )

/* returns the caller's thread ID (TID).  In a single-threaded process, the
 * thread ID is equal to the process ID (PID).
 */

static
pid_t
_pgm_gettid (void)
{
#	if defined ( SYS_gettid )
	return (pid_t) syscall (SYS_gettid);
#	else
	return getpid();
#	endif
}

/* Linux CPU affinity system calls, also consider pthread_getaffinity_np().
 */

static
int
_pgm_sched_get_nprocs (void)
{
	int available = 0;
	cpu_set_t cpu_set;

	if (0 == sched_getaffinity (_pgm_gettid(), sizeof (cpu_set), &cpu_set))
		for (int i = 0; i < CPU_SETSIZE; i++)
			if (CPU_ISSET (i, &cpu_set))
				available++;

	return available;
}
#endif

#if defined( _SC_NPROCESSORS_ONLN ) || defined( _SC_NPROCESSORS_CONF )
static
int
_pgm_sysconf_get_nprocs (void)
{
	int available = 0, online = 0, configured = 0;

#	ifdef _SC_NPROCESSORS_ONLN
	online = (int)sysconf (_SC_NPROCESSORS_ONLN);
#	endif
#	ifdef _SC_NPROCESSORS_CONF
	configured = (int)sysconf (_SC_NPROCESSORS_CONF);
#	endif

	if (online > configured)
		online = configured;

#if defined( PS_MYID )
	available = _pgm_pset_get_nprocs();
#elif defined( CPU_SETSIZE )
	available = _pgm_sched_get_nprocs();
#endif

	if (available > online)
		available = online;

	pgm_minor (_("Detected %d available %d online %d configured CPUs."),
		available, online, configured);

	return (available > 0) ? available :
	       ((online > 0) ? online : configured);
}
#endif

/* returns number of processors
 */

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
#if defined( __APPLE__ )
	return _pgm_apple_get_nprocs();
#elif defined( _WIN32 )
	return _pgm_win32_get_nprocs();
#elif defined( _SC_NPROCESSORS_ONLN ) || defined( _SC_NPROCESSORS_CONF )
	return _pgm_sysconf_get_nprocs();
#else
#	error "Unsupported processor enumeration on this platform."
#endif
}

/* eof */
