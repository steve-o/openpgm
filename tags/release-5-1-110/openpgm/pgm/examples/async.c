/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Asynchronous queue for receiving packets in a separate managed thread.
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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifndef _WIN32
#	include <fcntl.h>
#	include <unistd.h>
#	include <pthread.h>
#else
#	include <process.h>
#endif
#ifdef __APPLE__
#	include <pgm/in.h>
#endif
#include <pgm/pgm.h>

#include "async.h"


/* locals */

struct async_event_t {
	struct async_event_t   *next, *prev;
	size_t			len;
	struct pgm_sockaddr_t	addr;
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
	char			data[];
#elif defined(__cplusplus)
	char			data[1];
#else
	char			data[0];
#endif
};


static void on_data (async_t*const restrict, const void*restrict, const size_t, const struct pgm_sockaddr_t*restrict, const socklen_t);


/* queued data is stored as async_event_t objects
 */

static inline
struct async_event_t*
async_event_alloc (
	size_t			len
	)
{
	struct async_event_t* event;
	event = (struct async_event_t*)calloc (1, len + sizeof(struct async_event_t));
	event->len = len;
	return event;
}

static inline
void
async_event_unref (
        struct async_event_t* const event
        )
{
	free (event);
}

/* async_t implements a queue
 */

static inline
void
async_push_event (
	async_t*		restrict async,
	struct async_event_t*   restrict event
	)
{
	event->next = async->head;
	if (async->head)
		async->head->prev = event;
	else
		async->tail = event;
	async->head = event;
	async->length++;
}

static inline
struct async_event_t*
async_pop_event (
	async_t*		async
	)
{
	if (async->tail)
	{
		struct async_event_t *event = async->tail;

		async->tail = event->prev;
		if (async->tail)
		{
			async->tail->next = NULL;
			event->prev = NULL;
		}
		else
			async->head = NULL;
		async->length--;

		return event;
	}

	return NULL;
}

/* asynchronous receiver thread, sits in a loop processing incoming packets
 */

static
#ifndef _WIN32
void*
#else
unsigned
__stdcall
#endif
receiver_routine (
	void*		arg
	)
{
	assert (NULL != arg);
	async_t* async = (async_t*)arg;
	assert (NULL != async->sock);
#ifndef _WIN32
	int fds;
	fd_set readfds;
#else
	SOCKET recv_sock, pending_sock;
	DWORD cEvents = PGM_RECV_SOCKET_READ_COUNT + 1;
	WSAEVENT waitEvents[ PGM_RECV_SOCKET_READ_COUNT + 1 ];
	DWORD dwTimeout, dwEvents;
	socklen_t socklen = sizeof (SOCKET);

	waitEvents[0] = async->destroyEvent;
	waitEvents[1] = WSACreateEvent();
	waitEvents[2] = WSACreateEvent();
	assert (2 == PGM_RECV_SOCKET_READ_COUNT);
	pgm_getsockopt (async->sock, IPPROTO_PGM, PGM_RECV_SOCK, &recv_sock, &socklen);
	WSAEventSelect (recv_sock, waitEvents[1], FD_READ);
	pgm_getsockopt (async->sock, IPPROTO_PGM, PGM_PENDING_SOCK, &pending_sock, &socklen);
	WSAEventSelect (pending_sock, waitEvents[2], FD_READ);
#endif /* !_WIN32 */

/* dispatch loop */
	do {
		struct timeval tv;
		char buffer[4096];
		size_t len;
		struct pgm_sockaddr_t from;
		socklen_t fromlen = sizeof (from);
		const int status = pgm_recvfrom (async->sock,
						 buffer,
						 sizeof(buffer),
						 0,
						 &len,
						 &from,
						 &fromlen,
						 NULL);
		switch (status) {
		case PGM_IO_STATUS_NORMAL:
			on_data (async, buffer, len, &from, fromlen);
			break;
		case PGM_IO_STATUS_TIMER_PENDING:
			{
				socklen_t optlen = sizeof (tv);
				pgm_getsockopt (async->sock, IPPROTO_PGM, PGM_TIME_REMAIN, &tv, &optlen);
			}
			goto block;
		case PGM_IO_STATUS_RATE_LIMITED:
			{
				socklen_t optlen = sizeof (tv);
				pgm_getsockopt (async->sock, IPPROTO_PGM, PGM_RATE_REMAIN, &tv, &optlen);
			}
		case PGM_IO_STATUS_WOULD_BLOCK:
/* select for next event */
block:
#ifndef _WIN32
			fds = async->destroy_pipe[0] + 1;
			FD_ZERO(&readfds);
			FD_SET(async->destroy_pipe[0], &readfds);
			pgm_select_info (async->sock, &readfds, NULL, &fds);
			fds = select (fds, &readfds, NULL, NULL, PGM_IO_STATUS_WOULD_BLOCK == status ? NULL : &tv);
#else
			dwTimeout = PGM_IO_STATUS_WOULD_BLOCK == status ? WSA_INFINITE : (DWORD)((tv.tv_sec * 1000) + (tv.
tv_usec / 1000));
			dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, dwTimeout, FALSE);
			switch (dwEvents) {
			case WSA_WAIT_EVENT_0+1: WSAResetEvent (waitEvents[1]); break;
			case WSA_WAIT_EVENT_0+2: WSAResetEvent (waitEvents[2]); break;
			default: break;
			}
#endif /* !_WIN32 */
			break;

		default:
			if (PGM_IO_STATUS_ERROR == status)
				break;
		}
	} while (!async->is_destroyed);

/* cleanup */
#ifndef _WIN32
	return NULL;
#else
	WSACloseEvent (waitEvents[1]);
	WSACloseEvent (waitEvents[2]);
	_endthread();
	return 0;
#endif /* !_WIN32 */
}

/* enqueue a new data event.
 */

static
void
on_data (
	async_t*const		     restrict async,
	const void*		     restrict data,
	const size_t			      len,
	const struct pgm_sockaddr_t* restrict from,
	const socklen_t			      fromlen
	)
{
	struct async_event_t* event = async_event_alloc (len);
	memcpy (&event->addr, from, fromlen);
	memcpy (&event->data, data, len);
#ifndef _WIN32
	pthread_mutex_lock (&async->pthread_mutex);
	async_push_event (async, event);
	if (1 == async->length) {
		const char one = '1';
		const size_t writelen = write (async->notify_pipe[1], &one, sizeof(one));
		assert (sizeof(one) == writelen);
	}
	pthread_mutex_unlock (&async->pthread_mutex);
#else
	WaitForSingleObject (async->win32_mutex, INFINITE);
	async_push_event (async, event);
	if (1 == async->length) {
		WSASetEvent (async->notifyEvent);
	}
	ReleaseMutex (async->win32_mutex);
#endif /* _WIN32 */
}

/* create asynchronous thread handler from bound PGM sock.
 *
 * on success, 0 is returned.  on error, -1 is returned, and errno set appropriately.
 */

int
async_create (
	async_t**         restrict async,
	pgm_sock_t* const restrict sock
	)
{
	async_t* new_async;

	if (NULL == async || NULL == sock) {
		errno = EINVAL;
		return -1;
	}

	new_async = (async_t*)calloc (1, sizeof(async_t));
	new_async->sock = sock;
#ifndef _WIN32
	int e;
	e = pthread_mutex_init (&new_async->pthread_mutex, NULL);
	if (0 != e) goto err_destroy;
	e = pipe (new_async->notify_pipe);
	const int flags = fcntl (new_async->notify_pipe[0], F_GETFL);
	fcntl (new_async->notify_pipe[0], F_SETFL, flags | O_NONBLOCK);
	if (0 != e) goto err_destroy;
	e = pipe (new_async->destroy_pipe);
	if (0 != e) goto err_destroy;
	const int status = pthread_create (&new_async->thread, NULL, &receiver_routine, new_async);
	if (0 != status) goto err_destroy;
#else
	new_async->win32_mutex = CreateMutex (NULL, FALSE, NULL);
	new_async->notifyEvent = WSACreateEvent();
	new_async->destroyEvent = WSACreateEvent();
/* expect warning on MinGW due to lack of native uintptr_t */
	new_async->thread = (HANDLE)_beginthreadex (NULL, 0, &receiver_routine, new_async, 0, NULL);
	if (0 == new_async->thread) goto err_destroy;
#endif /* _WIN32 */

/* return new object */
	*async = new_async;
	return 0;

err_destroy:
#ifndef _WIN32
	close (new_async->destroy_pipe[0]);
	close (new_async->destroy_pipe[1]);
	close (new_async->notify_pipe[0]);
	close (new_async->notify_pipe[1]);
	pthread_mutex_destroy (&new_async->pthread_mutex);
#else
	WSACloseEvent (new_async->destroyEvent);
	WSACloseEvent (new_async->notifyEvent);
	CloseHandle (new_async->win32_mutex);
#endif /* _WIN32 */
	if (new_async)
		free (new_async);
#ifndef _WIN32
	return -1;
#else
	return INVALID_SOCKET;
#endif
}

/* Destroy asynchronous receiver, there must be no active queue consumer.
 *
 * on success, 0 is returned, on error -1 is returned and errno set appropriately.
 */

int
async_destroy (
	async_t* const	async
	)
{
	if (NULL == async || async->is_destroyed) {
		errno = EINVAL;
		return -1;
	}

	async->is_destroyed = TRUE;
#ifndef _WIN32
	const char one = '1';
	const size_t writelen = write (async->destroy_pipe[1], &one, sizeof(one));
	assert (sizeof(one) == writelen);
	pthread_join (async->thread, NULL);
	close (async->destroy_pipe[0]);
	close (async->destroy_pipe[1]);
	close (async->notify_pipe[0]);
	close (async->notify_pipe[1]);
	pthread_mutex_destroy (&async->pthread_mutex);
#else
	WSASetEvent (async->destroyEvent);
	WaitForSingleObject (async->thread, INFINITE);
	CloseHandle (async->thread);
	WSACloseEvent (async->destroyEvent);
	WSACloseEvent (async->notifyEvent);
	CloseHandle (async->win32_mutex);
#endif /* !_WIN32 */
	while (async->head) {
		struct async_event_t *next = async->head->next;
		async_event_unref (async->head);
		async->head = next;
		async->length--;
	}
	free (async);
	return 0;
}

/* synchronous reading from the queue.
 *
 * returns GIOStatus with success, error, again, or eof.
 */

ssize_t
async_recvfrom (
	async_t*  	 const restrict async,
	void*		       restrict	buf,
	size_t				len,
	struct pgm_sockaddr_t* restrict from,
	socklen_t*     	       restrict fromlen
	)
{
	struct async_event_t* event;

	if (NULL == async || NULL == buf || async->is_destroyed) {
#ifndef _WIN32
		errno = EINVAL;
		return -1;
#else
		WSASetLastError (WSAEINVAL);
		return SOCKET_ERROR;
#endif
	}

#ifndef _WIN32
	pthread_mutex_lock (&async->pthread_mutex);
	if (0 == async->length) {
/* flush event pipe */
		char tmp;
		while (sizeof(tmp) == read (async->notify_pipe[0], &tmp, sizeof(tmp)));
		pthread_mutex_unlock (&async->pthread_mutex);
		errno = EAGAIN;
		return -1;
	}
	event = async_pop_event (async);
	pthread_mutex_unlock (&async->pthread_mutex);
#else
	WaitForSingleObject (async->win32_mutex, INFINITE);
	if (0 == async->length) {
/* clear event */
		WSAResetEvent (async->notifyEvent);
		ReleaseMutex (async->win32_mutex);
		WSASetLastError (WSAEWOULDBLOCK);
		return SOCKET_ERROR;
	}
	event = async_pop_event (async);
	ReleaseMutex (async->win32_mutex);
#endif /* _WIN32 */
	assert (NULL != event);

/* pass data back to callee */
	const size_t event_len = MIN(event->len, len);
	if (NULL != from && sizeof(struct pgm_sockaddr_t) == *fromlen) {
		memcpy (from, &event->addr, *fromlen);
	}
	memcpy (buf, event->data, event_len);
	async_event_unref (event);
	return event_len;
}

ssize_t
async_recv (
	async_t* const restrict async,
	void*	       restrict buf,
	size_t			len
	)
{
	return async_recvfrom (async, buf, len, NULL, NULL);
}

/* eof */
