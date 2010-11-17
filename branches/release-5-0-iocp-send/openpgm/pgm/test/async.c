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


#include <stdio.h>
#include <time.h>
#ifndef _WIN32
#	include <unistd.h>
#	include <fcntl.h>
#endif
#include <glib.h>
#include <pgm/pgm.h>
#ifndef _WIN32
#	include <glib/gi18n-lib.h>
#else
/* Windows libintl borken */
#	define _(String)	(String)
#endif
#include "async.h"


//#define ASYNC_DEBUG

#ifndef ASYNC_DEBUG
#       define g_trace(...)           while (0)
#else
#include <ctype.h>
#       define g_trace(...)           g_debug(__VA_ARGS__)
#endif


/* globals */

#ifndef MSG_DONTWAIT
#	define MSG_DONTWAIT		0
#endif


/* global locals */

typedef struct pgm_event_t pgm_event_t;

struct pgm_event_t {
	gpointer		data;
	guint			len;
};


/* external: Glib event loop GSource of pgm contiguous data */
struct pgm_watch_t {
	GSource		source;
	GPollFD		pollfd;
	pgm_async_t*	async;
};

typedef struct pgm_watch_t pgm_watch_t;


static gboolean pgm_src_prepare (GSource*, gint*);
static gboolean pgm_src_check (GSource*);
static gboolean pgm_src_dispatch (GSource*, GSourceFunc, gpointer);

static GSourceFuncs g_pgm_watch_funcs = {
	.prepare		= pgm_src_prepare,
	.check			= pgm_src_check,
	.dispatch		= pgm_src_dispatch,
	.finalize		= NULL,
	.closure_callback	= NULL
};


static inline gpointer pgm_event_alloc (pgm_async_t* const) G_GNUC_MALLOC;
static PGMAsyncError pgm_async_error_from_errno (const gint);


static inline
gpointer
pgm_event_alloc (
	pgm_async_t* const	async
	)
{
	g_return_val_if_fail (async != NULL, NULL);
	return g_slice_alloc (sizeof(pgm_event_t));
}

/* release event memory for custom async queue dispatch handlers
 */

static inline
void
pgm_event_unref (
        pgm_async_t* const	async,
        pgm_event_t* const	event
        )
{
	g_return_if_fail (async != NULL);
	g_return_if_fail (event != NULL);
	g_slice_free1 (sizeof(pgm_event_t), event);
}

/* internal receiver thread, sits in a loop processing incoming packets
 */

static
gpointer
pgm_receiver_thread (
	gpointer	data
	)
{
	g_assert (NULL != data);

	pgm_async_t* async = (pgm_async_t*)data;
	g_async_queue_ref (async->commit_queue);

/* incoming message buffer */
	struct pgm_msgv_t msgv;
	gsize bytes_read = 0;
	struct timeval tv;
#ifdef _WIN32
	SOCKET recv_sock, pending_sock;
	DWORD cEvents = 3;
	WSAEVENT waitEvents[ cEvents ];
        DWORD dwTimeout, dwEvents;
        socklen_t socklen = sizeof (SOCKET);

	waitEvents[0] = async->destroy_event;
	waitEvents[1] = WSACreateEvent();
	waitEvents[2] = WSACreateEvent();
        pgm_getsockopt (async->sock, IPPROTO_PGM, PGM_RECV_SOCK, &recv_sock, &socklen);
        WSAEventSelect (recv_sock, waitEvents[1], FD_READ);
        pgm_getsockopt (async->sock, IPPROTO_PGM, PGM_PENDING_SOCK, &pending_sock, &socklen);
        WSAEventSelect (pending_sock, waitEvents[2], FD_READ);
#endif

	do {
/* blocking read */
		const int status = pgm_recvmsg (async->sock, &msgv, 0, &bytes_read, NULL);
		switch (status) {
		case PGM_IO_STATUS_NORMAL:
		{
/* queue a copy to receiver */
			pgm_event_t* event = pgm_event_alloc (async);
			event->data = bytes_read > 0 ? g_malloc (bytes_read) : NULL;
			event->len  = bytes_read;
			gpointer dst = event->data;
			guint i = 0;
			while (bytes_read) {
				const struct pgm_sk_buff_t* skb = msgv.msgv_skb[i++];
				g_assert (NULL != skb);
				g_assert (skb->len > 0);
				g_assert (skb->len <= bytes_read);
				memcpy (dst, skb->data, skb->len);
				dst = (char*)dst + skb->len;
				bytes_read -= skb->len;
			}
/* prod pipe on edge */
			g_async_queue_lock (async->commit_queue);
			g_async_queue_push_unlocked (async->commit_queue, event);
			if (g_async_queue_length_unlocked (async->commit_queue) == 1) {
#ifndef _WIN32
				const char one = '1';
				const size_t writelen = write (async->commit_pipe[1], &one, sizeof(one));
				g_assert (sizeof(one) == writelen);
#else
				WSASetEvent (async->commit_event);
#endif
			}
			g_async_queue_unlock (async->commit_queue);
			break;
		}

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
/* fall through */
		case PGM_IO_STATUS_WOULD_BLOCK:
block:
		{
#ifndef _WIN32
			fd_set readfds;
			int fd = async->destroy_pipe[0], n_fds = 1 + fd;
			FD_ZERO(&readfds);
			FD_SET(fd, &readfds);
			if (-1 == pgm_select_info (async->sock, &readfds, NULL, &n_fds)) {
				g_trace ("select_info returned errno=%i",errno);
				goto cleanup;
			}
			const int ready = select (n_fds, &readfds, NULL, NULL, PGM_IO_STATUS_WOULD_BLOCK == status ? NULL : &tv);
			if (-1 == ready) {
				g_trace ("block returned errno=%i",errno);
				goto cleanup;
			}
			if (ready > 0 && FD_ISSET(fd, &readfds))
				goto cleanup;
#else
			dwTimeout = PGM_IO_STATUS_WOULD_BLOCK == status ? WSA_INFINITE : (DWORD)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
                        dwEvents = WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, dwTimeout, FALSE);
                        switch (dwEvents) {
                        case WSA_WAIT_EVENT_0+1: WSAResetEvent (waitEvents[1]); break;
                        case WSA_WAIT_EVENT_0+2: WSAResetEvent (waitEvents[2]); break;
                        default: break;
                        }
#endif
			break;
		}

		case PGM_IO_STATUS_ERROR:
		case PGM_IO_STATUS_EOF:
			goto cleanup;

		case PGM_IO_STATUS_RESET:
		{
			int is_abort_on_reset;
			socklen_t optlen = sizeof (is_abort_on_reset);
			pgm_getsockopt (async->sock, IPPROTO_PGM, PGM_ABORT_ON_RESET, &is_abort_on_reset, &optlen);
			if (is_abort_on_reset)
				goto cleanup;
			break;
		}

/* TODO: report to user */
		case PGM_IO_STATUS_FIN:
			break;

		default:
			g_assert_not_reached();
		}
	} while (!async->is_destroyed);

cleanup:
#ifdef _WIN32
	WSACloseEvent (waitEvents[1]);
	WSACloseEvent (waitEvents[2]);
#endif
	g_async_queue_unref (async->commit_queue);
	return NULL;
}

/* create asynchronous thread handler
 *
 * on success, 0 is returned.  on error, -1 is returned, and errno set appropriately.
 * on invalid parameters, -EINVAL is returned.
 */

gboolean
pgm_async_create (
	pgm_async_t**		async,
	pgm_sock_t* const	sock,
	GError**		error
	)
{
	pgm_async_t* new_async;

	g_return_val_if_fail (NULL != async, FALSE);
	g_return_val_if_fail (NULL != sock, FALSE);

	g_trace ("create (async:%p sock:%p error:%p)",
		 (gpointer)async, (gpointer)sock, (gpointer)error);

	if (!g_thread_supported())
		g_thread_init (NULL);

	new_async = g_new0 (pgm_async_t, 1);
	new_async->sock = sock;
#ifndef _WIN32
	int e;
	e = pipe (new_async->commit_pipe);
	const int flags = fcntl (new_async->commit_pipe[0], F_GETFL);
        fcntl (new_async->commit_pipe[0], F_SETFL, flags | O_NONBLOCK);
	g_assert (0 == e);
        e = pipe (new_async->destroy_pipe);
	g_assert (0 == e);
#else
	new_async->commit_event = WSACreateEvent ();
	new_async->destroy_event = WSACreateEvent ();
#endif /* _WIN32 */
	new_async->commit_queue = g_async_queue_new();
/* setup new thread */
	new_async->thread = g_thread_create_full (pgm_receiver_thread,
						  new_async,
						  0,
						  TRUE,
						  TRUE,
						  G_THREAD_PRIORITY_HIGH,
						  error);
	if (NULL == new_async->thread) {
		g_async_queue_unref (new_async->commit_queue);
#ifndef _WIN32
		close (new_async->destroy_pipe[0]);
		close (new_async->destroy_pipe[1]);
		close (new_async->commit_pipe[0]);
		close (new_async->commit_pipe[1]);
#else
		WSACloseEvent (new_async->destroy_event);
		WSACloseEvent (new_async->commit_event);
#endif
		g_free (new_async);
		return FALSE;
	}

/* return new object */
	*async = new_async;
	return TRUE;
}

/* tell async thread to stop, wait for it to stop, then cleanup.
 *
 * on success, 0 is returned.  if async is invalid, -EINVAL is returned.
 */

gboolean
pgm_async_destroy (
	pgm_async_t* const	async
	)
{
	g_return_val_if_fail (NULL != async, FALSE);
	g_return_val_if_fail (!async->is_destroyed, FALSE);

	async->is_destroyed = TRUE;
#ifndef _WIN32
	const char one = '1';
        const size_t writelen = write (async->destroy_pipe[1], &one, sizeof(one));
        g_assert (sizeof(one) == writelen);
#else
	WSASetEvent (async->destroy_event);
#endif
	if (async->thread)
		g_thread_join (async->thread);
	if (async->commit_queue) {
		g_async_queue_unref (async->commit_queue);
		async->commit_queue = NULL;
	}
#ifndef _WIN32
	close (async->destroy_pipe[0]);
        close (async->destroy_pipe[1]);
        close (async->commit_pipe[0]);
        close (async->commit_pipe[1]);
#else
	WSACloseEvent (async->destroy_event);
        WSACloseEvent (async->commit_event);
#endif
	g_free (async);
	return TRUE;
}

/* queue to GSource and GMainLoop */

GSource*
pgm_async_create_watch (
        pgm_async_t*        async
        )
{
        g_return_val_if_fail (async != NULL, NULL);

        GSource *source = g_source_new (&g_pgm_watch_funcs, sizeof(pgm_watch_t));
        pgm_watch_t *watch = (pgm_watch_t*)source;

        watch->async = async;
#ifndef _WIN32
        watch->pollfd.fd = pgm_async_get_fd (async);
#else
        watch->pollfd.fd = pgm_async_get_event (async);
#endif
        watch->pollfd.events = G_IO_IN;

        g_source_add_poll (source, &watch->pollfd);

        return source;
}

/* pgm transport attaches to the callees context: the default context instead of
 * any internal contexts.
 */

int
pgm_async_add_watch_full (
        pgm_async_t*		async,
        gint                    priority,
        pgm_eventfn_t           function,
        gpointer                user_data,
        GDestroyNotify          notify
        )
{
        g_return_val_if_fail (async != NULL, -EINVAL);
        g_return_val_if_fail (function != NULL, -EINVAL);

        GSource* source = pgm_async_create_watch (async);

        if (priority != G_PRIORITY_DEFAULT)
                g_source_set_priority (source, priority);

        g_source_set_callback (source, (GSourceFunc)function, user_data, notify);

        guint id = g_source_attach (source, NULL);
        g_source_unref (source);

        return id;
}

int
pgm_async_add_watch (
        pgm_async_t*		async,
        pgm_eventfn_t           function,
        gpointer                user_data
        )
{
        return pgm_async_add_watch_full (async, G_PRIORITY_HIGH, function, user_data, NULL);
}

/* returns TRUE if source has data ready, i.e. async queue is not empty
 *
 * called before event loop poll()
 */

static
gboolean
pgm_src_prepare (
        GSource*                source,
        gint*                   timeout
        )
{
        pgm_watch_t* watch = (pgm_watch_t*)source;

/* infinite timeout */
        *timeout = -1;

        return ( g_async_queue_length(watch->async->commit_queue) > 0 );
}

/* called after event loop poll()
 *
 * return TRUE if ready to dispatch.
 */

static
gboolean
pgm_src_check (
        GSource*                source
        )
{
        pgm_watch_t* watch = (pgm_watch_t*)source;

        return ( g_async_queue_length(watch->async->commit_queue) > 0 );
}

/* called when TRUE returned from prepare or check
 */

static gboolean
pgm_src_dispatch (
        GSource*                source,
        GSourceFunc             callback,
        gpointer                user_data
        )
{
        g_trace ("pgm_src_dispatch (source:%p callback:() user-data:%p)",
		(gpointer)source, user_data);

        const pgm_eventfn_t function = (pgm_eventfn_t)callback;
        pgm_watch_t* watch = (pgm_watch_t*)source;
        pgm_async_t* async = watch->async;

/* empty pipe */
#ifndef _WIN32
	char tmp;
	while (sizeof(tmp) == read (async->commit_pipe[0], &tmp, sizeof(tmp)));
#else
	WSAResetEvent (async->commit_event);
#endif

/* purge only one message from the asynchronous queue */
	pgm_event_t* event = g_async_queue_try_pop (async->commit_queue);
	if (event)
	{
/* important that callback occurs out of lock to allow PGM layer to add more messages */
		(*function) (event->data, event->len, user_data);

/* return memory to receive window */
		if (event->len) g_free (event->data);
		pgm_event_unref (async, event);
        }

        return TRUE;
}

/* synchronous reading from the queue.
 *
 * returns GIOStatus with success, error, again, or eof.
 */

GIOStatus
pgm_async_recv (
	pgm_async_t* const	async,
	gpointer		data,
	const gsize		len,
	gsize* const		bytes_read,
	const int		flags,		/* MSG_DONTWAIT for non-blocking */
	GError**		error
	)
{
        g_return_val_if_fail (NULL != async, G_IO_STATUS_ERROR);
	if (len) g_return_val_if_fail (NULL != data, G_IO_STATUS_ERROR);

	g_trace ("pgm_async_recv (async:%p data:%p len:%" G_GSIZE_FORMAT" bytes-read:%p flags:%d error:%p)",
		(gpointer)async, data, len, (gpointer)bytes_read, flags, (gpointer)error);

	pgm_event_t* event = NULL;
	g_async_queue_lock (async->commit_queue);
	if (g_async_queue_length_unlocked (async->commit_queue) == 0)
	{
		g_async_queue_unlock (async->commit_queue);
		if (flags & MSG_DONTWAIT || async->is_nonblocking)
			return G_IO_STATUS_AGAIN;
#ifndef _WIN32
		fd_set readfds;
		int n_fds, ready, fd = async->commit_pipe[0];
		do {
			FD_ZERO(&readfds);
			FD_SET(fd, &readfds);
			n_fds = fd + 1;
			ready = select (n_fds, &readfds, NULL, NULL, NULL);
			if (-1 == ready || async->is_destroyed)	/* errno = EINTR */
				return G_IO_STATUS_ERROR;
		} while (ready <= 0);
		char tmp;
		while (sizeof(tmp) == read (async->commit_pipe[0], &tmp, sizeof(tmp)));
#else
		DWORD cEvents = 1;
		WSAEVENT waitEvents[ cEvents ];
		waitEvents[0] = async->commit_event;
		WSAWaitForMultipleEvents (cEvents, waitEvents, FALSE, WSA_INFINITE, FALSE);
		WSAResetEvent (async->commit_event);
#endif
		g_async_queue_lock (async->commit_queue);
	}
	event = g_async_queue_pop_unlocked (async->commit_queue);
	g_async_queue_unlock (async->commit_queue);

/* pass data back to callee */
	if (event->len > len) {
		*bytes_read = len;
		memcpy (data, event->data, *bytes_read);
		g_set_error (error,
			     PGM_ASYNC_ERROR,
			     PGM_ASYNC_ERROR_OVERFLOW,
			     _("Message too large to be stored in buffer."));
		pgm_event_unref (async, event);
		return G_IO_STATUS_ERROR;
	}

	if (bytes_read)
		*bytes_read = event->len;
	memcpy (data, event->data, event->len);

/* cleanup */
	if (event->len) g_free (event->data);
	pgm_event_unref (async, event);
	return G_IO_STATUS_NORMAL;
}

gboolean
pgm_async_set_nonblocking (
	pgm_async_t* const	async,
	const gboolean		nonblocking
	)
{
        g_return_val_if_fail (NULL != async, FALSE);
	async->is_nonblocking = nonblocking;
	return TRUE;
}

GQuark
pgm_async_error_quark (void)
{
        return g_quark_from_static_string ("pgm-async-error-quark");
}

static
PGMAsyncError
pgm_async_error_from_errno (
	const gint		err_no
	)
{
        switch (err_no) {
#ifdef EFAULT
	case EFAULT:
		return PGM_ASYNC_ERROR_FAULT;
		break;
#endif

#ifdef EMFILE
	case EMFILE:
		return PGM_ASYNC_ERROR_MFILE;
		break;
#endif

#ifdef ENFILE
	case ENFILE:
		return PGM_ASYNC_ERROR_NFILE;
		break;
#endif

	default :
                return PGM_ASYNC_ERROR_FAILED;
                break;
        }
}

/* eof */
