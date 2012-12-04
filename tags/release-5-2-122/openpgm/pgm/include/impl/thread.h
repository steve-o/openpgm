/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * mutexes and locks.
 *
 * Copyright (c) 2010-2011 Miru Limited.
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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_THREAD_H__
#define __PGM_IMPL_THREAD_H__

typedef struct pgm_mutex_t pgm_mutex_t;
typedef struct pgm_spinlock_t pgm_spinlock_t;
typedef struct pgm_cond_t pgm_cond_t;
typedef struct pgm_rwlock_t pgm_rwlock_t;

/* spins before yielding, 200 (Linux) - 4,000 (Windows)
 */
#define PGM_ADAPTIVE_MUTEX_SPINCOUNT	200

/* On initialisation the number of available processors is queried to determine
 * whether we should spin in locks or yield to other threads and processes.
 */
extern bool pgm_smp_system;

#ifndef _WIN32
#	include <pthread.h>
#	include <unistd.h>
#	if defined( __sun )
#		include <thread.h>
#	endif
#else
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif
#ifdef __APPLE__
#	include <libkern/OSAtomic.h>
#endif
#include <pgm/types.h>
#if defined( USE_TICKET_SPINLOCK )
#	include <impl/ticket.h>
#endif
#if defined( USE_DUMB_RWSPINLOCK )
#	include <impl/rwspinlock.h>
#endif

PGM_BEGIN_DECLS

struct pgm_mutex_t {
#ifndef _WIN32
/* POSIX mutex */
	pthread_mutex_t		pthread_mutex;
#else
/* Windows process-private adaptive mutex */
	CRITICAL_SECTION	win32_crit;
#endif /* !_WIN32 */
};

struct pgm_spinlock_t {
#if defined( USE_TICKET_SPINLOCK )
/* ticket based spinlock */
	pgm_ticket_t		ticket_lock;
#elif defined( HAVE_PTHREAD_SPINLOCK )
/* POSIX spinlock, not available on OSX */
	pthread_spinlock_t	pthread_spinlock;
#elif defined( __APPLE__ )
/* OSX native spinlock */
	OSSpinLock		darwin_spinlock;
#elif defined( _WIN32 )
/* Win32 friendly atomic-op based spinlock */
	volatile LONG		taken;
#else
/* GCC atomic-op based spinlock */
	volatile uint32_t	taken;
#endif
};

struct pgm_cond_t {
#ifndef _WIN32
/* POSIX condition variable */
	pthread_cond_t		pthread_cond;
#elif ( _WIN32_WINNT >= 0x0600 )
/* Windows Vista+ condition variable */
	CONDITION_VARIABLE	win32_cond;
#else
/* Windows XP condition variable implementation */
	CRITICAL_SECTION	win32_crit;
	size_t			len;
	size_t			allocated_len;
	HANDLE*			phandle;
#endif /* _WIN32 */
};

struct pgm_rwlock_t {
#if defined( USE_DUMB_RWSPINLOCK )
	pgm_rwspinlock_t	rwspinlock;
#elif !defined( _WIN32 )
/* POSIX read-write lock */
	pthread_rwlock_t	pthread_rwlock;
#elif ( _WIN32_WINNT >= 0x0600 )
/* Windows Vista+ user-space slim read-write lock */
	SRWLOCK			win32_rwlock;
#else
/* Windows XP read-write lock implementation */
	CRITICAL_SECTION	win32_crit;
	pgm_cond_t		read_cond;
	pgm_cond_t		write_cond;
	unsigned		read_counter;
	bool			have_writer;
	unsigned		want_to_read;
	unsigned		want_to_write;
#endif /* USE_DUMB_RWSPINLOCK */
};

PGM_GNUC_INTERNAL void pgm_mutex_init (pgm_mutex_t*);
PGM_GNUC_INTERNAL void pgm_mutex_free (pgm_mutex_t*);

static inline bool pgm_mutex_trylock (pgm_mutex_t* mutex) {
#ifndef _WIN32
	const int result = pthread_mutex_trylock (&mutex->pthread_mutex);
	if (EBUSY == result)
		return FALSE;
	return TRUE;
#else
/* WARNING: returns TRUE if in same thread as lock */
	return TryEnterCriticalSection (&mutex->win32_crit);
#endif /* !_WIN32 */
}

/* call to pgm_mutex_lock on locked mutex or non-init pointer is undefined.
 */

static inline void pgm_mutex_lock (pgm_mutex_t* mutex) {
#ifndef _WIN32
	pthread_mutex_lock (&mutex->pthread_mutex);
#else
	EnterCriticalSection (&mutex->win32_crit);
#endif /* !_WIN32 */
}

/* call to pgm_mutex_unlock on unlocked mutex or non-init pointer is undefined.
 */

static inline void pgm_mutex_unlock (pgm_mutex_t* mutex) {
#ifndef _WIN32
	pthread_mutex_unlock (&mutex->pthread_mutex);
#else
	LeaveCriticalSection (&mutex->win32_crit);
#endif /* !_WIN32 */
}

PGM_GNUC_INTERNAL void pgm_spinlock_init (pgm_spinlock_t*);
PGM_GNUC_INTERNAL void pgm_spinlock_free (pgm_spinlock_t*);

static inline bool pgm_spinlock_trylock (pgm_spinlock_t* spinlock) {
#if defined( USE_TICKET_SPINLOCK )
	return pgm_ticket_trylock (&spinlock->ticket_lock);
#elif defined( HAVE_PTHREAD_SPINLOCK )
	const int result = pthread_spin_trylock (&spinlock->pthread_spinlock);
	if (EBUSY == result)
		return FALSE;
	return TRUE;
#elif defined( __APPLE__ )
	return OSSpinLockTry (&spinlock->darwin_spinlock);
#elif defined( _WIN32 )
	const LONG prev = _InterlockedExchange (&spinlock->taken, 1);
	return (0 == prev);
#else   /* GCC atomics */
	const uint32_t prev = __sync_lock_test_and_set (&spinlock->taken, 1);
	return (0 == prev);
#endif
}

static inline void pgm_spinlock_lock (pgm_spinlock_t* spinlock) {
#if defined( USE_TICKET_SPINLOCK )
	pgm_ticket_lock (&spinlock->ticket_lock);
#elif defined( HAVE_PTHREAD_SPINLOCK )
	pthread_spin_lock (&spinlock->pthread_spinlock);
#elif defined( __APPLE__ )
/* Anderson's exponential back-off */
	OSSpinLockLock (&spinlock->darwin_spinlock);
#elif defined( _WIN32 )
/* Segall and Rudolph bus-optimised spinlock acquire with Intel's recommendation
 * for a pause instruction for hyper-threading.
 */
	unsigned spins = 0;
	while (_InterlockedExchange (&spinlock->taken, 1))
		while (spinlock->taken)
			if (!pgm_smp_system || (++spins > PGM_ADAPTIVE_MUTEX_SPINCOUNT))
				SwitchToThread();
			else
				YieldProcessor();
#elif defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
/* GCC atomics with x86 pause */
	unsigned spins = 0;
	while (__sync_lock_test_and_set (&spinlock->taken, 1))
		while (spinlock->taken)
			if (!pgm_smp_system || (++spins > PGM_ADAPTIVE_MUTEX_SPINCOUNT))
				sched_yield();
			else
				__asm volatile ("pause" ::: "memory");
#else
/* GCC atomics */
	while (__sync_lock_test_and_set (&spinlock->taken, 1))
		while (spinlock->taken)
			sched_yield();
#endif
}

static inline void pgm_spinlock_unlock (pgm_spinlock_t* spinlock) {
#if defined( USE_TICKET_SPINLOCK )
	pgm_ticket_unlock (&spinlock->ticket_lock);
#elif defined( HAVE_PTHREAD_SPINLOCK )
	pthread_spin_unlock (&spinlock->pthread_spinlock);
#elif defined( __APPLE__ )
	OSSpinLockUnlock (&spinlock->darwin_spinlock);
#elif defined( _WIN32 )
	_InterlockedExchange (&spinlock->taken, 0);
#else /* GCC atomics */
	__sync_lock_release (&spinlock->taken);
#endif
}

PGM_GNUC_INTERNAL void pgm_cond_init (pgm_cond_t*);
PGM_GNUC_INTERNAL void pgm_cond_signal (pgm_cond_t*);
PGM_GNUC_INTERNAL void pgm_cond_broadcast (pgm_cond_t*);
#ifndef _WIN32
PGM_GNUC_INTERNAL void pgm_cond_wait (pgm_cond_t*, pthread_mutex_t*);
#else
PGM_GNUC_INTERNAL void pgm_cond_wait (pgm_cond_t*, CRITICAL_SECTION*);
#endif
PGM_GNUC_INTERNAL void pgm_cond_free (pgm_cond_t*);

#if defined( _WIN32 ) && !( _WIN32_WINNT >= 0x600 ) && !defined( USE_DUMB_RWSPINLOCK )
/* read-write lock implementation for Windows XP */
PGM_GNUC_INTERNAL void pgm_rwlock_reader_lock (pgm_rwlock_t*);
PGM_GNUC_INTERNAL bool pgm_rwlock_reader_trylock (pgm_rwlock_t*);
PGM_GNUC_INTERNAL void pgm_rwlock_reader_unlock(pgm_rwlock_t*);
PGM_GNUC_INTERNAL void pgm_rwlock_writer_lock (pgm_rwlock_t*);
PGM_GNUC_INTERNAL bool pgm_rwlock_writer_trylock (pgm_rwlock_t*);
PGM_GNUC_INTERNAL void pgm_rwlock_writer_unlock (pgm_rwlock_t*);
#else
static inline void pgm_rwlock_reader_lock (pgm_rwlock_t* rwlock) {
#	if defined( USE_DUMB_RWSPINLOCK )
/* User-space read/write lock */
	pgm_rwspinlock_reader_lock (&rwlock->rwspinlock);
#	elif defined( _WIN32 ) && ( _WIN32_WINNT >= 0x0600 )
/* Vista+ slim read/write lock */
	AcquireSRWLockShared (&rwlock->win32_rwlock);
#	else
/* POSIX read/write lock */
	pthread_rwlock_rdlock (&rwlock->pthread_rwlock);
#	endif
}
static inline bool pgm_rwlock_reader_trylock (pgm_rwlock_t* rwlock) {
#	if defined( USE_DUMB_RWSPINLOCK )
	return pgm_rwspinlock_reader_trylock (&rwlock->rwspinlock);
#	elif defined( _WIN32 ) && ( _WIN32_WINNT >= 0x0600 )
	return TryAcquireSRWLockShared (&rwlock->win32_rwlock);
#	else
	return !pthread_rwlock_tryrdlock (&rwlock->pthread_rwlock);
#	endif
}
static inline void pgm_rwlock_reader_unlock(pgm_rwlock_t* rwlock) {
#	if defined( USE_DUMB_RWSPINLOCK )
	pgm_rwspinlock_reader_unlock (&rwlock->rwspinlock);
#	elif defined( _WIN32 ) && ( _WIN32_WINNT >= 0x0600 )
	ReleaseSRWLockShared (&rwlock->win32_rwlock);
#	else
	pthread_rwlock_unlock (&rwlock->pthread_rwlock);
#	endif
}
static inline void pgm_rwlock_writer_lock (pgm_rwlock_t* rwlock) {
#	if defined( USE_DUMB_RWSPINLOCK )
	pgm_rwspinlock_writer_lock (&rwlock->rwspinlock);
#	elif defined( _WIN32 ) && ( _WIN32_WINNT >= 0x0600 )
	AcquireSRWLockExclusive (&rwlock->win32_rwlock);
#	else
	pthread_rwlock_wrlock (&rwlock->pthread_rwlock);
#	endif
}
static inline bool pgm_rwlock_writer_trylock (pgm_rwlock_t* rwlock) {
#	if defined( USE_DUMB_RWSPINLOCK )
	return pgm_rwspinlock_writer_trylock (&rwlock->rwspinlock);
#	elif defined( _WIN32 ) && ( _WIN32_WINNT >= 0x0600 )
	return TryAcquireSRWLockExclusive (&rwlock->win32_rwlock);
#	else
	return !pthread_rwlock_trywrlock (&rwlock->pthread_rwlock);
#	endif
}
static inline void pgm_rwlock_writer_unlock (pgm_rwlock_t* rwlock) {
#	if defined( USE_DUMB_RWSPINLOCK )
	pgm_rwspinlock_writer_unlock (&rwlock->rwspinlock);
#	elif defined( _WIN32 ) && ( _WIN32_WINNT >= 0x0600 )
	ReleaseSRWLockExclusive (&rwlock->win32_rwlock);
#	else
	pthread_rwlock_unlock (&rwlock->pthread_rwlock);
#	endif
}
#endif

PGM_GNUC_INTERNAL void pgm_rwlock_init (pgm_rwlock_t*);
PGM_GNUC_INTERNAL void pgm_rwlock_free (pgm_rwlock_t*);

PGM_GNUC_INTERNAL void pgm_thread_init (void);
PGM_GNUC_INTERNAL void pgm_thread_shutdown (void);

static inline
void
pgm_thread_yield (void)
{
#if defined( __sun )
	thr_yield();		/* Solaris specific thread API */
#elif !defined( _WIN32 )
	sched_yield();		/* most Unix platforms */
#else
	SwitchToThread();	/* yields only current processor */
#	if 0
	Sleep (1);		/* If you specify 0 milliseconds, the thread will relinquish
				 * the remainder of its time slice to processes of equal priority.
				 *
				 * Specify > Sleep (1) to yield to any process.
				 */
#	endif
#endif /* _WIN32 */
}

PGM_END_DECLS

#endif /* __PGM_IMPL_THREAD_H__ */
