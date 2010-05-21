/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * mutexes and locks.
 *
 * Copyright (c) 2010 Miru Limited.
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

#include <errno.h>
#include <pgm/framework.h>

//#define THREAD_DEBUG


/* Globals */

#if defined(_WIN32) && !defined(CONFIG_HAVE_WIN_COND)
static DWORD cond_event_tls = TLS_OUT_OF_INDEXES;
#endif

static volatile uint32_t thread_ref_count = 0;


#ifndef _WIN32
#	define posix_check_err(err, name) \
		do { \
			const int save_error = (err); \
			if (PGM_UNLIKELY(save_error)) { \
				pgm_error ("file %s: line %d (%s): error '%s' during '%s'", \
					__FILE__, __LINE__, __PRETTY_FUNCTION__, \
					strerror (save_error), name); \
			} \
		} while (0)
#	define posix_check_cmd(cmd) posix_check_err ((cmd), #cmd)
#else
#	define win32_check_err(err, name) \
		do { \
			const bool save_error = (err); \
			if (PGM_UNLIKELY(!save_error)) { \
				pgm_error ("file %s: line %d (%s): error '%s' during '%s'", \
					__FILE__, __LINE__, __PRETTY_FUNCTION__, \
					pgm_wsastrerror (GetLastError ()), name); \
			} \
		} while (0)
#	define win32_check_cmd(cmd) win32_check_err ((cmd), #cmd)
#endif /* !_WIN32 */


/* only needed for Win32 pre-Vista read-write locks
 */
void
pgm_thread_init (void)
{
	if (pgm_atomic_exchange_and_add32 (&thread_ref_count, 1) > 0)
		return;

#if defined(_WIN32) && !defined(CONFIG_HAVE_WIN_COND)
	win32_check_cmd (TLS_OUT_OF_INDEXES != (cond_event_tls = TlsAlloc ()));
#endif
}

void
pgm_thread_shutdown (void)
{
	pgm_return_if_fail (pgm_atomic_read32 (&thread_ref_count) > 0);

	if (pgm_atomic_exchange_and_add32 (&thread_ref_count, (uint32_t)-1) != 1)
		return;

#if defined(_WIN32) && !defined(CONFIG_HAVE_WIN_COND)
	TlsFree (cond_event_tls);
#endif
}

void
pgm_mutex_init (
	pgm_mutex_t*	mutex
	)
{
	pgm_assert (NULL != mutex);
#ifndef _WIN32
	posix_check_cmd (pthread_mutex_init (&mutex->pthread_mutex, NULL));
#else
	HANDLE handle;
	win32_check_cmd (handle = CreateMutex (NULL, FALSE, NULL));
	mutex->win32_mutex = handle;
#endif /* !_WIN32 */
}

bool
pgm_mutex_trylock (
	pgm_mutex_t*	mutex
	)
{
	pgm_assert (NULL != mutex);
#ifndef _WIN32
	const int result = pthread_mutex_trylock (&mutex->pthread_mutex);
	if (EBUSY == result)
		return FALSE;
	posix_check_err (result, "pthread_mutex_trylock");
	return TRUE;
#else
	DWORD result;
	win32_check_cmd (WAIT_FAILED != (result = WaitForSingleObject (mutex->win32_mutex, 0)));
	return WAIT_TIMEOUT != result;
#endif /* !_WIN32 */
}

void
pgm_mutex_free (
	pgm_mutex_t*	mutex
	)
{
	pgm_assert (NULL != mutex);
#ifndef _WIN32
	posix_check_cmd (pthread_mutex_destroy (&mutex->pthread_mutex));
#else
	win32_check_cmd (CloseHandle (mutex->win32_mutex));
#endif /* !_WIN32 */
}

void
pgm_spinlock_init (
	pgm_spinlock_t*	spinlock
	)
{
	pgm_assert (NULL != spinlock);
#ifndef _WIN32
	posix_check_cmd (pthread_spin_init (&spinlock->pthread_spinlock, PTHREAD_PROCESS_PRIVATE));
#else
	InitializeCriticalSection (&spinlock->win32_spinlock);
#endif /* !_WIN32 */
}

bool
pgm_spinlock_trylock (
	pgm_spinlock_t*	spinlock
	)
{
	pgm_assert (NULL != spinlock);
#ifndef _WIN32
	const int result = pthread_spin_trylock (&spinlock->pthread_spinlock);
	if (EBUSY == result)
		return FALSE;
	posix_check_err (result, "pthread_spinlock_trylock");
	return TRUE;
#else
	return TryEnterCriticalSection (&spinlock->win32_spinlock);
#endif /* !_WIN32 */
}

void
pgm_spinlock_free (
	pgm_spinlock_t*	spinlock
	)
{
	pgm_assert (NULL != spinlock);
#ifndef _WIN32
/* ignore return value */
	pthread_spin_destroy (&spinlock->pthread_spinlock);
#else
	DeleteCriticalSection (&spinlock->win32_spinlock);
#endif /* !_WIN32 */
}

void
pgm_cond_init (
	pgm_cond_t*	cond
	)
{
	pgm_assert (NULL != cond);
#ifndef _WIN32
	posix_check_cmd (pthread_cond_init (&cond->pthread_cond, NULL));
#elif defined(CONFIG_HAVE_WIN_COND)
	InitializeConditionVariable (&cond->win32_cond);
#else
	cond->len = 0;
	cond->allocated_len = pgm_nearest_power (1, 2 + 1);
	cond->phandle = pgm_new (HANDLE, cond->allocated_len);
	InitializeCriticalSection (&cond->win32_spinlock);
#endif /* !_WIN32 */
}

void
pgm_cond_signal (
	pgm_cond_t*	cond
	)
{
	pgm_assert (NULL != cond);
#ifndef _WIN32
	pthread_cond_signal (&cond->pthread_cond);
#elif defined(CONFIG_HAVE_WIN_COND)
	WakeConditionVariable (&cond->win32_cond);
#else
	EnterCriticalSection (&cond->win32_spinlock);
	if (cond->len > 0) {
		SetEvent (cond->phandle[ 0 ]);
		memmove (&cond->phandle[ 0 ], &cond->phandle[ 1 ], cond->len - 1);
		cond->len--;
	}
	LeaveCriticalSection (&cond->win32_spinlock);
#endif /* !_WIN32 */
}

void
pgm_cond_broadcast (
	pgm_cond_t*	cond
	)
{
	pgm_assert (NULL != cond);
#ifndef _WIN32
	pthread_cond_broadcast (&cond->pthread_cond);
#elif defined(CONFIG_HAVE_WIN_COND)
	WakeAllConditionVariable (&cond->win32_cond);
#else
	EnterCriticalSection (&cond->win32_spinlock);
	for (unsigned i = 0; i < cond->len; i++)
		SetEvent (cond->phandle[ i ]);
	cond->len = 0;
	LeaveCriticalSection (&cond->win32_spinlock);
#endif /* !_WIN32 */
}

#ifndef _WIN32
void
pgm_cond_wait (
	pgm_cond_t*		cond,
	pthread_mutex_t*	mutex
	)
{
	pgm_assert (NULL != cond);
	pgm_assert (NULL != mutex);
	pthread_cond_wait (&cond->pthread_cond, mutex);
}
#else
void
pgm_cond_wait (
	pgm_cond_t*		cond,
	CRITICAL_SECTION*	spinlock
	)
{
	pgm_assert (NULL != cond);
	pgm_assert (NULL != spinlock);
#	if defined(CONFIG_HAVE_WIN_COND)
	SleepConditionVariableCS (&cond->win32_cond, spinlock, INFINITE);
#	else
	DWORD status;
	HANDLE event = TlsGetValue (cond_event_tls);

	if (!event) {
		win32_check_cmd (event = CreateEvent (0, FALSE, FALSE, NULL));
		TlsSetValue (cond_event_tls, event);
	}

	EnterCriticalSection (&cond->win32_spinlock);
	pgm_assert (WAIT_TIMEOUT == WaitForSingleObject (event, 0));
	if ((cond->len + 1) > cond->allocated_len) {
		cond->allocated_len = pgm_nearest_power (1, cond->len + 1 + 1);
		cond->phandle	    = pgm_realloc (cond->phandle, cond->allocated_len);
	}
	cond->phandle[ cond->len++ ] = event;
	LeaveCriticalSection (&cond->win32_spinlock);

	EnterCriticalSection (spinlock);
	win32_check_cmd (WAIT_FAILED != (status = WaitForSingleObject (event, INFINITE)));
	LeaveCriticalSection (spinlock);

	if (WAIT_TIMEOUT == status) {
		EnterCriticalSection (&cond->win32_spinlock);
		for (unsigned i = 0; i < cond->len; i++) {
			if (cond->phandle[ i ] == event) {
				if (i != cond->len - 1)
					memmove (&cond->phandle[ i ], &cond->phandle[ i + 1 ], sizeof(HANDLE) * (cond->len - i - 1));
				cond->len--;
				break;
			}
		}
		win32_check_cmd (WAIT_FAILED != (status = WaitForSingleObject (event, 0)));
		LeaveCriticalSection (&cond->win32_spinlock);
	}
#	endif /* !CONFIG_HAVE_WIN_COND */
}
#endif /* !_WIN32 */

void
pgm_cond_free (
	pgm_cond_t*	cond
	)
{
	pgm_assert (NULL != cond);
#ifndef _WIN32
	posix_check_cmd (pthread_cond_destroy (&cond->pthread_cond));
#elif defined(CONFIG_HAVE_WIN_COND)
	/* nop */
#else
	DeleteCriticalSection (&cond->win32_spinlock);
	pgm_free (cond->phandle);
#endif /* !_WIN32 */
}

void
pgm_rwlock_init (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	InitializeSRWLock (&rwlock->win32_lock);
#elif !defined(_WIN32)
	posix_check_cmd (pthread_rwlock_init (&rwlock->pthread_rwlock, NULL));
#else
	InitializeCriticalSection (&rwlock->win32_spinlock);
	pgm_cond_init (&rwlock->read_cond);
	pgm_cond_init (&rwlock->write_cond);
	rwlock->read_counter	= 0;
	rwlock->have_writer	= FALSE;
	rwlock->want_to_read	= 0;
	rwlock->want_to_write	= 0;
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

void
pgm_rwlock_free (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	/* nop */
#elif !defined(_WIN32)
	pthread_rwlock_destroy (&rwlock->pthread_rwlock);
#else
	pgm_cond_free (&rwlock->read_cond);
	pgm_cond_free (&rwlock->write_cond);
	DeleteCriticalSection (&rwlock->win32_spinlock);
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

#if !defined(CONFIG_HAVE_WIN_SRW_LOCK) && defined(_WIN32)
static inline
void
_pgm_rwlock_signal (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
	if (rwlock->want_to_write)
		pgm_cond_signal (&rwlock->write_cond);
	else if (rwlock->want_to_read)
		pgm_cond_broadcast (&rwlock->read_cond);
}

void
pgm_rwlock_reader_lock (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
	EnterCriticalSection (&rwlock->win32_spinlock);
	rwlock->want_to_read++;
	while (rwlock->have_writer || rwlock->want_to_write)
		pgm_cond_wait (&rwlock->read_cond, &rwlock->win32_spinlock);
	rwlock->want_to_read--;
	rwlock->read_counter++;
	LeaveCriticalSection (&rwlock->win32_spinlock);
}

bool
pgm_rwlock_reader_trylock (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
	bool status;
	EnterCriticalSection (&rwlock->win32_spinlock);
	if (!rwlock->have_writer && !rwlock->want_to_write) {
		rwlock->read_counter++;
		status = TRUE;
	} else
		status = FALSE;
	LeaveCriticalSection (&rwlock->win32_spinlock);
	return status;
}

void
pgm_rwlock_reader_unlock(
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
	EnterCriticalSection (&rwlock->win32_spinlock);
	rwlock->read_counter--;
	if (rwlock->read_counter == 0)
		_pgm_rwlock_signal (rwlock);
	LeaveCriticalSection (&rwlock->win32_spinlock);
}

void
pgm_rwlock_writer_lock (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
	EnterCriticalSection (&rwlock->win32_spinlock);
	rwlock->want_to_write++;
	while (rwlock->have_writer || rwlock->read_counter)
		pgm_cond_wait (&rwlock->write_cond, &rwlock->win32_spinlock);
	rwlock->want_to_write--;
	rwlock->have_writer = TRUE;
	LeaveCriticalSection (&rwlock->win32_spinlock);
}

bool
pgm_rwlock_writer_trylock (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
	bool status;
	EnterCriticalSection (&rwlock->win32_spinlock);
	if (!rwlock->have_writer && !rwlock->read_counter) {
		rwlock->have_writer = TRUE;
		status = TRUE;
	} else
		status = FALSE;
	LeaveCriticalSection (&rwlock->win32_spinlock);
	return status;
}

void
pgm_rwlock_writer_unlock (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
	EnterCriticalSection (&rwlock->win32_spinlock);
	rwlock->have_writer = FALSE;
	_pgm_rwlock_signal (rwlock);
	LeaveCriticalSection (&rwlock->win32_spinlock);
}
#endif /* !_WIN32 && !CONFIG_HAVE_WIN_SRW_LOCK */


/* eof */
