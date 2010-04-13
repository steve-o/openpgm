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
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#ifdef G_OS_UNIX
#	include <pthread.h>
#endif

#include "pgm/messages.h"
#include "pgm/thread.h"

//#define THREAD_DEBUG



/* Globals */

#if defined(G_OS_WIN) && !defined(CONFIG_HAVE_WIN_COND)
static DWORD g_cond_event_tls = TLS_OUT_OF_INDEXES;
#endif

#ifdef G_OS_UNIX
#	define posix_check_err(err, name) G_STMT_START{			\
  int error = (err); 							\
  if (error)	 		 		 			\
    pgm_error ("file %s: line %d (%s): error '%s' during '%s'",		\
           __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,			\
           strerror (error), name);					\
  }G_STMT_END
#	define posix_check_cmd(cmd) posix_check_err ((cmd), #cmd)
#elif defined G_OS_WIN32
#define win32_check_err(err, name) G_STMT_START{			\
  int error = (err); 							\
  if (!error)	 		 		 			\
    pgm_error ("file %s: line %d (%s): error '%s' during '%s'",		\
	     __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,		\
	     g_win32_error_message (GetLastError ()), #what);		\
  }G_STMT_END
#	define win32_check_cmd(cmd) win32_check_err ((cmd), #cmd)
#endif /* !G_OS_UNIX */


/* only needed for Win32 pre-Vista read-write locks
 */
void
pgm_thread_init (void)
{
#if defined(G_OS_WIN) && !defined(CONFIG_HAVE_WIN_COND)
	win32_check_err (TLS_OUT_OF_INDEXES != (g_cond_event_tls = TlsAlloc ()));
#endif
}

void
pgm_thread_shutdown (void)
{
#if defined(G_OS_WIN) && !defined(CONFIG_HAVE_WIN_COND)
	TlsFree (g_cond_event_tls);
#endif
}

void
pgm_mutex_init (
	pgm_mutex_t*	mutex
	)
{
	pgm_assert (NULL != mutex);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_mutex_init (&mutex->pthread_mutex, NULL));
#else
	HANDLE handle = CreateMutex (NULL, FALSE, NULL);
	win32_check_for_error (handle);
	mutex->win32_mutex = handle;
#endif /* !G_OS_UNIX */
}

gboolean
pgm_mutex_trylock (
	pgm_mutex_t*	mutex
	)
{
	pgm_assert (NULL != mutex);
#ifdef G_OS_UNIX
	const int result = pthread_mutex_trylock (&mutex->pthread_mutex);
	if (EBUSY == result)
		return FALSE;
	posix_check_err (result, "pthread_mutex_trylock");
	return TRUE;
#else
	const DWORD result = WaitForSingleObject (mutex->win32_mutex, 0);
	win32_check_for_error (WAIT_FAILED != result);
	return WAIT_TIMEOUT != result;
#endif /* !G_OS_UNIX */
}

void
pgm_mutex_free (
	pgm_mutex_t*	mutex
	)
{
	pgm_assert (NULL != mutex);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_mutex_destroy (&mutex->pthread_mutex));
#else
	CloseHandle (mutex->win32_mutex);
#endif /* !G_OS_UNIX */
}

void
pgm_spinlock_init (
	pgm_spinlock_t*	spinlock
	)
{
	pgm_assert (NULL != spinlock);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_spin_init (&spinlock->pthread_spinlock, PTHREAD_PROCESS_PRIVATE));
#else
	InitializeCriticalSection (&spinlock->win32_spinlock);
#endif /* !G_OS_UNIX */
}

gboolean
pgm_spinlock_trylock (
	pgm_spinlock_t*	spinlock
	)
{
	pgm_assert (NULL != spinlock);
#ifdef G_OS_UNIX
	const int result = pthread_spin_trylock (&spinlock->pthread_spinlock);
	if (EBUSY == result)
		return FALSE;
	posix_check_err (result, "pthread_spinlock_trylock");
	return TRUE;
#else
	return TryEnterCriticalSection (&spinlock->win32_spinlock);
#endif /* !G_OS_UNIX */
}

void
pgm_spinlock_free (
	pgm_spinlock_t*	spinlock
	)
{
	pgm_assert (NULL != spinlock);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_spin_destroy (&spinlock->pthread_spinlock));
#else
	DeleteCriticalSection (&spinlock->win32_spinlock);
#endif /* !G_OS_UNIX */
}

void
pgm_cond_init (
	pgm_cond_t*	cond
	)
{
	pgm_assert (NULL != cond);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_cond_init (&cond->pthread_cond, NULL));
#elif defined(CONFIG_HAVE_WIN_COND)
	InitializeConditionVariable (&cond->win32_cond);
#else
	cond->array = g_ptr_array_new ();
	InitializeCriticalSection (&cond->win32_spinlock);
#endif /* !G_OS_UNIX */
}

void
pgm_cond_signal (
	pgm_cond_t*	cond
	)
{
	pgm_assert (NULL != cond);
#ifdef G_OS_UNIX
	pthread_cond_signal (&cond->pthread_cond);
#elif defined(CONFIG_HAVE_WIN_COND)
	WakeConditionVariable (&cond->win32_cond);
#else
	EnterCriticalSection (&cond->win32_spinlock);
	if (cond->array->len > 0) {
		SetEvent (g_ptr_array_index (cond->array, 0));
		g_ptr_array_remove_index (cond->array, 0);
	}
	LeaveCriticalSection (&cond->win32_spinlock);
#endif /* !G_OS_UNIX */
}

void
pgm_cond_broadcast (
	pgm_cond_t*	cond
	)
{
	pgm_assert (NULL != cond);
#ifdef G_OS_UNIX
	pthread_cond_broadcast (&cond->pthread_cond);
#elif defined(CONFIG_HAVE_WIN_COND)
	WakeAllConditionVariable (&cond->win32_cond);
#else
	EnterCriticalSection (&cond->win32_spinlock);
	for (unsigned i = 0; i < cond->array->len; i++)
		SetEvent (g_ptr_array_index (cond->array, i));
	g_ptr_array_set_size (cond->array, 0);
	LeaveCriticalSection (&mutex->win32_cond);
#endif /* !G_OS_UNIX */
}

#ifdef G_OS_UNIX
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
	gulong status;
	HANDLE event = TlsGetValue (g_cond_event_tls);

	if (!event) {
		win32_check_err (event = CreateEvent (0, FALSE, FALSE, NULL));
		TlsSetValue (g_cond_event_tls, event);
	}

	EnterCriticalSection (&cond->win32_spinlock);
	pgm_assert (WAIT_TIMEOUT == WaitForSingleObject (event, 0));
	g_ptr_array_add (cond->array, event);
	LeaveCriticalSection (&cond->win32_spinlock);

	EnterCriticalSection (spinlock);
	win32_check_for_error (WAIT_FAILED != (status = WaitForSingleObject (event, INFINITE)));
	LeaveCriticalSection (spinlock);

	if (WAIT_TIMEOUT == retval) {
		EnterCriticalSection (&cond->win32_spinlock);
		g_ptr_array_remove (cond->array, event);
		win32_check_err (WAIT_FAILED != (status = WaitForSingleObject (event, 0)));
		LeaveCriticalSection (&cond->win32_spinlock);
	}
#	endif /* !CONFIG_HAVE_WIN_COND */
}
#endif /* !G_OS_UNIX */

void
pgm_cond_free (
	pgm_cond_t*	cond
	)
{
	pgm_assert (NULL != cond);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_cond_destroy (&cond->pthread_cond));
#elif defined(CONFIG_HAVE_WIN_COND)
	/* nop */
#else
	DeleteCriticalSection (&cond->win32_spinlock);
	g_ptr_array_free (cond->array, TRUE);
#endif /* !G_OS_UNIX */
}

void
pgm_rwlock_init (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	InitializeSRWLock (&rwlock->win32_lock);
#elif defined(G_OS_UNIX)
	posix_check_cmd (pthread_rwlock_init (&rwlock->pthread_rwlock, NULL));
#else
	InitializeCriticalSection (&rwlock->win32_spinlock);
	_pgm_cond_init (&rwlock->read_cond);
	_pgm_cond_init (&rwlock->write_cond);
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
#elif defined(G_OS_UNIX)
	pthread_rwlock_destroy (&rwlock->pthread_rwlock);
#else
	_pgm_cond_free (&rwlock->read_cond);
	_pgm_cond_free (&rwlock->write_cond);
	DeleteCriticalSection (&rwlock->win32_spinlock);
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

#if !defined(CONFIG_HAVE_WIN_SRW_LOCK) && !defined(G_OS_UNIX)
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
		_pgm_cond_wait (&rwlock->read_cond, &rwlock->win32_spinlock);
	rwlock->want_to_read--;
	rwlock->read_counter++;
	LeaveCriticalSection (&rwlock->win32_spinlock);
}

gboolean
pgm_rwlock_reader_trylock (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
	gboolean status;
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
		_pgm_cond_wait (&rwlock->write_cond, &rwlock->win32_spinlock);
	rwlock->want_to_write--;
	rwlock->have_writer = TRUE;
	LeaveCriticalSection (&rwlock->win32_spinlock);
}

gboolean
pgm_rwlock_writer_trylock (
	pgm_rwlock_t*	rwlock
	)
{
	pgm_assert (NULL != rwlock);
	gboolean status;
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
#endif /* !G_OS_UNIX && !CONFIG_HAVE_WIN_SRW_LOCK */


/* eof */
