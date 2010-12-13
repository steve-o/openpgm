/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * mutexes and locks.
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

#pragma once
#ifndef __PGM_IMPL_THREAD_H__
#define __PGM_IMPL_THREAD_H__

typedef struct pgm_mutex_t pgm_mutex_t;
typedef struct pgm_spinlock_t pgm_spinlock_t;
typedef struct pgm_cond_t pgm_cond_t;
typedef struct pgm_rwlock_t pgm_rwlock_t;

#ifndef _WIN32
#	include <pthread.h>
#	include <unistd.h>
#else
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif
#include <pgm/types.h>

PGM_BEGIN_DECLS

struct pgm_mutex_t {
#ifndef _WIN32
	pthread_mutex_t		pthread_mutex;
#else
	HANDLE			win32_mutex;
#endif /* !_WIN32 */
};

struct pgm_spinlock_t {
#ifndef _WIN32
	pthread_spinlock_t	pthread_spinlock;
#else
	CRITICAL_SECTION	win32_spinlock;
#endif
};

struct pgm_cond_t {
#ifndef _WIN32
	pthread_cond_t		pthread_cond;
#elif defined(CONFIG_HAVE_WIN_COND)
	CONDITION_VARIABLE	win32_cond;
#else
	CRITICAL_SECTION	win32_spinlock;
	size_t			len;
	size_t			allocated_len;
	HANDLE*			phandle;
#endif /* !_WIN32 */
};

struct pgm_rwlock_t {
#ifndef _WIN32
	pthread_rwlock_t	pthread_rwlock;
#elif CONFIG_HAVE_WIN_SRW_LOCK
	SRWLOCK			win32_rwlock;
#else
	CRITICAL_SECTION	win32_spinlock;
	pgm_cond_t		read_cond;
	pgm_cond_t		write_cond;
	unsigned		read_counter;
	bool			have_writer;
	unsigned		want_to_read;
	unsigned		want_to_write;
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
};

PGM_GNUC_INTERNAL void pgm_mutex_init (pgm_mutex_t*);
PGM_GNUC_INTERNAL void pgm_mutex_free (pgm_mutex_t*);
PGM_GNUC_INTERNAL bool pgm_mutex_trylock (pgm_mutex_t*);

static inline void pgm_mutex_lock (pgm_mutex_t* mutex) {
#ifndef _WIN32
	pthread_mutex_lock (&mutex->pthread_mutex);
#else
	WaitForSingleObject (mutex->win32_mutex, INFINITE);
#endif /* !_WIN32 */
}

static inline void pgm_mutex_unlock (pgm_mutex_t* mutex) {
#ifndef _WIN32
	pthread_mutex_unlock (&mutex->pthread_mutex);
#else
	ReleaseMutex (mutex->win32_mutex);
#endif /* !_WIN32 */
}

PGM_GNUC_INTERNAL void pgm_spinlock_init (pgm_spinlock_t*);
PGM_GNUC_INTERNAL void pgm_spinlock_free (pgm_spinlock_t*);
PGM_GNUC_INTERNAL bool pgm_spinlock_trylock (pgm_spinlock_t*);

static inline void pgm_spinlock_lock (pgm_spinlock_t* spinlock) {
#ifndef _WIN32
	pthread_spin_lock (&spinlock->pthread_spinlock);
#else
	EnterCriticalSection (&spinlock->win32_spinlock);
#endif /* !_WIN32 */
}

static inline void pgm_spinlock_unlock (pgm_spinlock_t* spinlock) {
#ifndef _WIN32
	pthread_spin_unlock (&spinlock->pthread_spinlock);
#else
	LeaveCriticalSection (&spinlock->win32_spinlock);
#endif /* !_WIN32 */
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

#ifndef _WIN32
static inline void pgm_rwlock_reader_lock (pgm_rwlock_t* rwlock) {
	pthread_rwlock_rdlock (&rwlock->pthread_rwlock);
}
static inline bool pgm_rwlock_reader_trylock (pgm_rwlock_t* rwlock) {
	return !pthread_rwlock_tryrdlock (&rwlock->pthread_rwlock);
}
static inline void pgm_rwlock_reader_unlock(pgm_rwlock_t* rwlock) {
	pthread_rwlock_unlock (&rwlock->pthread_rwlock);
}
static inline void pgm_rwlock_writer_lock (pgm_rwlock_t* rwlock) {
	pthread_rwlock_wrlock (&rwlock->pthread_rwlock);
}
static inline bool pgm_rwlock_writer_trylock (pgm_rwlock_t* rwlock) {
	return !pthread_rwlock_trywrlock (&rwlock->pthread_rwlock);
}
static inline void pgm_rwlock_writer_unlock (pgm_rwlock_t* rwlock) {
	pthread_rwlock_unlock (&rwlock->pthread_rwlock);
}
#elif defined(CONFIG_HAVE_WIN_SRW_LOCK)
static inline void pgm_rwlock_reader_lock (pgm_rwlock_t* rwlock) {
	AcquireSRWLockShared (&rwlock->win32_rwlock);
}
static inline bool pgm_rwlock_reader_trylock (pgm_rwlock_t* rwlock) {
	return TryAcquireSRWLockShared (&rwlock->win32_rwlock);
}
static inline void pgm_rwlock_reader_unlock(pgm_rwlock_t* rwlock) {
	ReleaseSRWLockShared (&rwlock->win32_rwlock);
}
static inline void pgm_rwlock_writer_lock (pgm_rwlock_t* rwlock) {
	AcquireSRWLockExclusive (&rwlock->win32_rwlock);
}
static inline bool pgm_rwlock_writer_trylock (pgm_rwlock_t* rwlock) {
	return AcquireSRWLockExclusive (&rwlock->win32_rwlock);
}
static inline void pgm_rwlock_writer_unlock (pgm_rwlock_t* rwlock) {
	ReleaseSRWLockExclusive (&rwlock->win32_rwlock);
}
#else
PGM_GNUC_INTERNAL void pgm_rwlock_reader_lock (pgm_rwlock_t*);
PGM_GNUC_INTERNAL bool pgm_rwlock_reader_trylock (pgm_rwlock_t*);
PGM_GNUC_INTERNAL void pgm_rwlock_reader_unlock(pgm_rwlock_t*);
PGM_GNUC_INTERNAL void pgm_rwlock_writer_lock (pgm_rwlock_t*);
PGM_GNUC_INTERNAL bool pgm_rwlock_writer_trylock (pgm_rwlock_t*);
PGM_GNUC_INTERNAL void pgm_rwlock_writer_unlock (pgm_rwlock_t*);
#endif

PGM_GNUC_INTERNAL void pgm_rwlock_init (pgm_rwlock_t*);
PGM_GNUC_INTERNAL void pgm_rwlock_free (pgm_rwlock_t*);

PGM_GNUC_INTERNAL void pgm_thread_init (void);
PGM_GNUC_INTERNAL void pgm_thread_shutdown (void);

static inline
void
pgm_thread_yield (void)
{
#ifndef _WIN32
#	ifdef _POSIX_PRIORITY_SCHEDULING
		sched_yield();
#	else
		pthread_yield();	/* requires _GNU */
#	endif
#else
	Sleep (0);		/* If you specify 0 milliseconds, the thread will relinquish
				 * the remainder of its time slice but remain ready. 
				 */
#endif /* _WIN32 */
}

PGM_END_DECLS

#endif /* __PGM_IMPL_THREAD_H__ */
