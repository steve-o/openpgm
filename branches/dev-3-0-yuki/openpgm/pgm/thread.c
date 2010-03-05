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
#include <string.h>

#include <glib.h>

#ifdef G_OS_UNIX
#	include <pthread.h>
#endif

#include "pgm/thread.h"

//#define THREAD_DEBUG

#ifndef THREAD_DEBUG
#	define g_trace(...)		while (0)
#else
#	define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* Globals */

#if defined(G_OS_WIN) && !defined(CONFIG_HAVE_WIN_COND)
static DWORD g_cond_event_tls = TLS_OUT_OF_INDEXES;
#endif

#ifdef G_OS_UNIX
#	define posix_check_err(err, name) G_STMT_START{			\
  int error = (err); 							\
  if (error)	 		 		 			\
    g_error ("file %s: line %d (%s): error '%s' during '%s'",		\
           __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,			\
           strerror (error), name);					\
  }G_STMT_END
#	define posix_check_cmd(cmd) posix_check_err ((cmd), #cmd)
#elif defined G_OS_WIN32
#define win32_check_err(err, name) G_STMT_START{			\
  int error = (err); 							\
  if (!error)	 		 		 			\
    g_error ("file %s: line %d (%s): error '%s' during '%s'",		\
	     __FILE__, __LINE__, G_GNUC_PRETTY_FUNCTION,		\
	     g_win32_error_message (GetLastError ()), #what);		\
  }G_STMT_END
#	define win32_check_cmd(cmd) win32_check_err ((cmd), #cmd)
#endif /* !G_OS_UNIX */


void
pgm_mutex_init (
	PGMMutex*	mutex
	)
{
	g_assert (NULL != mutex);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_mutex_init (&mutex->pthread_mutex, NULL));
#else
	InitializeCriticalSection (&mutex->win32_mutex);
#endif /* !G_OS_UNIX */
}

void
pgm_mutex_lock (
	PGMMutex*	mutex
	)
{
	g_assert (NULL != mutex);
#ifdef G_OS_UNIX
	pthread_mutex_lock (&mutex->pthread_mutex);
#else
	EnterCriticalSection (&mutex->win32_mutex);
#endif /* !G_OS_UNIX */
}

gboolean
pgm_mutex_trylock (
	PGMMutex*	mutex
	)
{
	g_assert (NULL != mutex);
#ifdef G_OS_UNIX
	const int result = pthread_mutex_trylock (&mutex->pthread_mutex);
	if (EBUSY == result)
		return FALSE;
	posix_check_err (result, "pthread_mutex_trylock");
	return TRUE;
#else
	return TryEnterCriticalSection (&mutex->win32_mutex);
#endif /* !G_OS_UNIX */
}

void
pgm_mutex_unlock (
	PGMMutex*	mutex
	)
{
	g_assert (NULL != mutex);
#ifdef G_OS_UNIX
	pthread_mutex_unlock (&mutex->pthread_mutex);
#else
	LeaveCriticalSection (&mutex->win32_mutex);
#endif /* !G_OS_UNIX */
}

void
pgm_mutex_free (
	PGMMutex*	mutex
	)
{
	g_assert (NULL != mutex);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_mutex_destroy (&mutex->pthread_mutex));
#else
	DeleteCriticalSection (&mutex->win32_mutex);
#endif /* !G_OS_UNIX */
}

void
pgm_cond_init (
	PGMCond*	cond
	)
{
	g_assert (NULL != cond);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_cond_init (&cond->pthread_cond, NULL));
#elif defined(CONFIG_HAVE_WIN_COND)
	InitializeConditionVariable (&cond->win32_cond);
#else
	if (TLS_OUT_OF_INDEXES == g_cond_event_tls) {
		win32_check_err (TLS_OUT_OF_INDEXES != (g_cond_event_tls = TlsAlloc ()));
	}
	cond->array = g_ptr_array_new ();
	InitializeCriticalSection (&cond->win32_mutex);
#endif /* !G_OS_UNIX */
}

void
pgm_cond_signal (
	PGMCond*	cond
	)
{
	g_assert (NULL != cond);
#ifdef G_OS_UNIX
	pthread_cond_signal (&cond->pthread_cond);
#elif defined(CONFIG_HAVE_WIN_COND)
	WakeConditionVariable (&cond->win32_cond);
#else
	EnterCriticalSection (&cond->win32_mutex);
	if (cond->array->len > 0) {
		SetEvent (g_ptr_array_index (cond->array, 0));
		g_ptr_array_remove_index (cond->array, 0);
	}
	LeaveCriticalSection (&cond->win32_mutex);
#endif /* !G_OS_UNIX */
}

void
pgm_cond_broadcast (
	PGMCond*	cond
	)
{
	g_assert (NULL != cond);
#ifdef G_OS_UNIX
	pthread_cond_broadcast (&cond->pthread_cond);
#elif defined(CONFIG_HAVE_WIN_COND)
	WakeAllConditionVariable (&cond->win32_cond);
#else
	EnterCriticalSection (&cond->win32_mutex);
	for (unsigned i = 0; i < cond->array->len; i++)
		SetEvent (g_ptr_array_index (cond->array, i));
	g_ptr_array_set_size (cond->array, 0);
	LeaveCriticalSection (&mutex->win32_cond);
#endif /* !G_OS_UNIX */
}

void
pgm_cond_wait (
	PGMCond*	cond,
	PGMMutex*	mutex
	)
{
	g_assert (NULL != cond);
	g_assert (NULL != mutex);
#ifdef G_OS_UNIX
	pthread_cond_wait (&cond->pthread_cond, &mutex->pthread_mutex);
#elif defined(CONFIG_HAVE_WIN_COND)
	SleepConditionVariableCS (&cond->win32_cond, &mutex->win32_mutex, INFINITE);
#else
	gulong status;
	HANDLE event = TlsGetValue (g_cond_event_tls);

	if (!event) {
		win32_check_err (event = CreateEvent (0, FALSE, FALSE, NULL));
		TlsSetValue (g_cond_event_tls, event);
	}

	EnterCriticalSection (&cond->win32_mutex);
	g_assert (WAIT_TIMEOUT == WaitForSingleObject (event, 0));
	g_ptr_array_add (cond->array, event);
	LeaveCriticalSection (&cond->win32_mutex);

	EnterCriticalSection (mutex);
	win32_check_for_error (WAIT_FAILED != (status = WaitForSingleObject (event, INFINITE)));
	LeaveCriticalSection (mutex);

	if (WAIT_TIMEOUT == retval) {
		EnterCriticalSection (&cond->lock);
		g_ptr_array_remove (cond->array, event);
		win32_check_err (WAIT_FAILED != (status = WaitForSingleObject (event, 0)));
		LeaveCriticalSection (&cond->win32_mutex);
	}

	return WAIT_TIMEOUT != status;
#endif /* !G_OS_UNIX */
}

void
pgm_cond_free (
	PGMCond*	cond
	)
{
	g_assert (NULL != cond);
#ifdef G_OS_UNIX
	posix_check_cmd (pthread_cond_destroy (&cond->pthread_cond));
#elif defined(CONFIG_HAVE_WIN_COND)
	/* nop */
#else
	DeleteCriticalSection (&cond->win32_mutex);
	g_ptr_array_free (cond->array, TRUE);
#endif /* !G_OS_UNIX */
}

void
pgm_rw_lock_init (
	PGMRWLock*	rw_lock
	)
{
	g_assert (NULL != rw_lock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	InitializeSRWLock (&rw_lock->win32_lock);
#else
	pgm_mutex_init (&rw_lock->mutex);
	pgm_cond_init (&rw_lock->read_cond);
	pgm_cond_init (&rw_lock->write_cond);
	rw_lock->read_counter	= 0;
	rw_lock->have_writer	= FALSE;
	rw_lock->want_to_read	= 0;
	rw_lock->want_to_write	= 0;
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

static inline
void
pgm_rw_lock_wait (
	PGMCond*	cond,
	PGMMutex*	mutex
	)
{
	pgm_cond_wait (cond, mutex);
}

static inline
void
pgm_rw_lock_signal (
	PGMRWLock*	rw_lock
	)
{
	if (rw_lock->want_to_write)
		pgm_cond_signal (&rw_lock->write_cond);
	else if (rw_lock->want_to_read)
		pgm_cond_broadcast (&rw_lock->read_cond);
}

void
pgm_rw_lock_reader_lock (
	PGMRWLock*	rw_lock
	)
{
	g_assert (NULL != rw_lock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	AcquireSRWLockShared (&rw_lock->win32_lock);
#else
	pgm_mutex_lock (&rw_lock->mutex);
	rw_lock->want_to_read++;
	while (rw_lock->have_writer || rw_lock->want_to_write)
		pgm_rw_lock_wait (&rw_lock->read_cond, &rw_lock->mutex);
	rw_lock->want_to_read--;
	rw_lock->read_counter++;
	pgm_mutex_unlock (&rw_lock->mutex);
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

gboolean
pgm_rw_lock_reader_trylock (
	PGMRWLock*	rw_lock
	)
{
	g_assert (NULL != rw_lock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	return TryAcquireSRWLockShared (&rw_lock->win32_lock);
#else
	gboolean status;
	pgm_mutex_lock (&rw_lock->mutex);
	if (!rw_lock->have_writer && !rw_lock->want_to_write) {
		rw_lock->read_counter++;
		status = TRUE;
	} else
		status = FALSE;
	pgm_mutex_unlock (&rw_lock->mutex);
	return status;
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

void
pgm_rw_lock_reader_unlock(
	PGMRWLock*	rw_lock
	)
{
	g_assert (NULL != rw_lock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	ReleaseSRWLockShared (&rw_lock->win32_lock);
#else
	pgm_mutex_lock (&rw_lock->mutex);
	rw_lock->read_counter--;
	if (rw_lock->read_counter == 0)
		pgm_rw_lock_signal (rw_lock);
	pgm_mutex_unlock (&rw_lock->mutex);
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

void
pgm_rw_lock_writer_lock (
	PGMRWLock*	rw_lock
	)
{
	g_assert (NULL != rw_lock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	AcquireSRWLockExclusive (&rw_lock->win32_lock);
#else
	pgm_mutex_lock (&rw_lock->mutex);
	rw_lock->want_to_write++;
	while (rw_lock->have_writer || rw_lock->read_counter)
		pgm_rw_lock_wait (&rw_lock->write_cond, &rw_lock->mutex);
	rw_lock->want_to_write--;
	rw_lock->have_writer = TRUE;
	pgm_mutex_unlock (&rw_lock->mutex);
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

gboolean
pgm_rw_lock_writer_trylock (
	PGMRWLock*	rw_lock
	)
{
	g_assert (NULL != rw_lock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	return AcquireSRWLockExclusive (&rw_lock->win32_lock);
#else
	gboolean status;
	g_assert (NULL != rw_lock);
	pgm_mutex_lock (&rw_lock->mutex);
	if (!rw_lock->have_writer && !rw_lock->read_counter) {
		rw_lock->have_writer = TRUE;
		status = TRUE;
	} else
		status = FALSE;
	pgm_mutex_unlock (&rw_lock->mutex);
	return status;
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

void
pgm_rw_lock_writer_unlock (
	PGMRWLock*	rw_lock
	)
{
	g_assert (NULL != rw_lock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	ReleaseSRWLockExclusive (&rw_lock->win32_lock);
#else
	pgm_mutex_lock (&rw_lock->mutex);
	rw_lock->have_writer = FALSE;
	pgm_rw_lock_signal (rw_lock);
	pgm_mutex_unlock (&rw_lock->mutex);
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}

void
pgm_rw_lock_free (
	PGMRWLock*	rw_lock
	)
{
	g_assert (NULL != rw_lock);
#ifdef CONFIG_HAVE_WIN_SRW_LOCK
	/* nop */
#else
	pgm_cond_free (&rw_lock->read_cond);
	pgm_cond_free (&rw_lock->write_cond);
	pgm_mutex_free (&rw_lock->mutex);
#endif /* !CONFIG_HAVE_WIN_SRW_LOCK */
}


/* eof */
