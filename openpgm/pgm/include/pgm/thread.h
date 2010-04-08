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

#ifndef __PGM_THREAD_H__
#define __PGM_THREAD_H__

#ifdef G_OS_UNIX
#	include <pthread.h>
#endif

#include <glib.h>


struct pgm_mutex_t {
#ifdef G_OS_UNIX
	pthread_mutex_t		pthread_mutex;
#else
	HANDLE			win32_mutex;
#endif /* !G_OS_UNIX */
};

typedef struct pgm_mutex_t pgm_mutex_t;

struct pgm_spinlock_t {
#ifdef G_OS_UNIX
	pthread_spinlock_t	pthread_spinlock;
#else
	CRITICAL_SECTION	win32_spinlock;
#endif
};

typedef struct pgm_spinlock_t pgm_spinlock_t;

struct pgm_cond_t {
#ifdef G_OS_UNIX
	pthread_cond_t		pthread_cond;
#elif defined(CONFIG_HAVE_WIN_COND)
	CONDITION_VARIABLE	win32_cond;
#else
	CRITICAL_SECTION	win32_spinlock;
	GPtrArray		array;
#endif /* !G_OS_UNIX */
};

typedef struct pgm_cond_t pgm_cond_t;

struct pgm_rwlock_t {
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	SRWLOCK			win32_lock;
#elif defined(G_OS_UNIX)
	pthread_rwlock_t	pthread_rwlock;
#else
	CRITICAL_SECTION	win32_spinlock;
	pgm_cond_t		read_cond;
	pgm_cond_t		write_cond;
	guint			read_counter;
	gboolean		have_writer;
	guint			want_to_read;
	guint			want_to_write;
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
};

typedef struct pgm_rwlock_t pgm_rwlock_t;

G_BEGIN_DECLS

void pgm_mutex_init (pgm_mutex_t*);
void pgm_mutex_free (pgm_mutex_t*);
gboolean pgm_mutex_trylock (pgm_mutex_t*);

static inline void pgm_mutex_lock (pgm_mutex_t* mutex) {
#ifdef G_OS_UNIX
	pthread_mutex_lock (&mutex->pthread_mutex);
#else
	WaitForSingleObject (mutex->win32_mutex, INFINITE);
#endif /* !G_OS_UNIX */
}

static inline void pgm_mutex_unlock (pgm_mutex_t* mutex) {
#ifdef G_OS_UNIX
	pthread_mutex_unlock (&mutex->pthread_mutex);
#else
	ReleaseMutex (mutex->win32_mutex);
#endif /* !G_OS_UNIX */
}

void pgm_spinlock_init (pgm_spinlock_t*);
void pgm_spinlock_free (pgm_spinlock_t*);
gboolean pgm_spinlock_trylock (pgm_spinlock_t*);

static inline void pgm_spinlock_lock (pgm_spinlock_t* spinlock) {
#ifdef G_OS_UNIX
	pthread_spin_lock (&spinlock->pthread_spinlock);
#else
	EnterCriticalSection (&spinlock->win32_spinlock);
#endif /* !G_OS_UNIX */
}

static inline void pgm_spinlock_unlock (pgm_spinlock_t* spinlock) {
#ifdef G_OS_UNIX
	pthread_spin_unlock (&spinlock->pthread_spinlock);
#else
	LeaveCriticalSection (&spinlock->win32_spinlock);
#endif /* !G_OS_UNIX */
}

void pgm_cond_init (pgm_cond_t*);
void pgm_cond_signal (pgm_cond_t*);
void pgm_cond_broadcast (pgm_cond_t*);
#ifdef G_OS_UNIX
void pgm_cond_wait (pgm_cond_t*, pthread_mutex_t*);
#else
void pgm_cond_wait (pgm_cond_t*, CRITICAL_SECTION*);
#endif
void pgm_cond_free (pgm_cond_t*);

void pgm_rwlock_init (pgm_rwlock_t*);
void pgm_rwlock_free (pgm_rwlock_t*);

#ifdef G_OS_UNIX
static inline void pgm_rwlock_reader_lock (pgm_rwlock_t* rwlock) {
	pthread_rwlock_rdlock (&rwlock->pthread_rwlock);
}
static inline gboolean pgm_rwlock_reader_trylock (pgm_rwlock_t* rwlock) {
	return !pthread_rwlock_tryrdlock (&rwlock->pthread_rwlock);
}
static inline void pgm_rwlock_reader_unlock(pgm_rwlock_t* rwlock) {
	pthread_rwlock_unlock (&rwlock->pthread_rwlock);
}
static inline void pgm_rwlock_writer_lock (pgm_rwlock_t* rwlock) {
	pthread_rwlock_wrlock (&rwlock->pthread_rwlock);
}
static inline gboolean pgm_rwlock_writer_trylock (pgm_rwlock_t* rwlock) {
	return !pthread_rwlock_trywrlock (&rwlock->pthread_rwlock);
}
static inline void pgm_rwlock_writer_unlock (pgm_rwlock_t* rwlock) {
	pthread_rwlock_unlock (&rwlock->pthread_rwlock);
}
#elif defined(CONFIG_HAVE_WIN_SRW_LOCK)
static inline void pgm_rwlock_reader_lock (pgm_rwlock_t* rwlock) {
	AcquireSRWLockShared (&rwlock->win32_lock);
}
static inline gboolean pgm_rwlock_reader_trylock (pgm_rwlock_t* rwlock) {
	return TryAcquireSRWLockShared (&rwlock->win32_lock);
}
static inline void pgm_rwlock_reader_unlock(pgm_rwlock_t* rwlock) {
	ReleaseSRWLockShared (&rwlock->win32_lock);
}
static inline void pgm_rwlock_writer_lock (pgm_rwlock_t* rwlock) {
	AcquireSRWLockExclusive (&rwlock->win32_lock);
}
static inline gboolean pgm_rwlock_writer_trylock (pgm_rwlock_t* rwlock) {
	return AcquireSRWLockExclusive (&rwlock->win32_lock);
}
static inline void pgm_rwlock_writer_unlock (pgm_rwlock_t* rwlock) {
	ReleaseSRWLockExclusive (&rwlock->win32_lock);
}
#else
void pgm_rwlock_init (pgm_rwlock_t*);
void pgm_rwlock_free (pgm_rwlock_t*);
void pgm_rwlock_reader_lock (pgm_rwlock_t*);
gboolean pgm_rwlock_reader_trylock (pgm_rwlock_t*);
void pgm_rwlock_reader_unlock(pgm_rwlock_t*);
void pgm_rwlock_writer_lock (pgm_rwlock_t*);
gboolean pgm_rwlock_writer_trylock (pgm_rwlock_t*);
void pgm_rwlock_writer_unlock (pgm_rwlock_t*);
#endif

void pgm_thread_init (void);
void pgm_thread_shutdown (void);

G_END_DECLS

#endif /* __PGM_THREAD_H__ */
