/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Asynchronous queue for receiving packets in a separate managed thread.
 *
 * Copyright (c) 2006-2008 Miru Limited.
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
#include <unistd.h>

#include <glib.h>

#include "pgm/async.h"


//#define ASYNC_DEBUG

#ifndef ASYNC_DEBUG
#       define g_trace(m,...)           while (0)
#else
#include <ctype.h>
#       define g_trace(m,...)           g_debug(__VA_ARGS__)
#endif


/* globals */


/* global locals */

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


static inline gpointer
pgm_event_alloc (
	pgm_async_t*	async
	)
{
	g_return_val_if_fail (async != NULL, NULL);

	gpointer p;
	g_static_mutex_lock (&async->trash_mutex);
	if (async->trash_event) {
		p = g_trash_stack_pop (&async->trash_event);
	} else {
		p = g_slice_alloc (sizeof(pgm_event_t));
	}
	g_static_mutex_unlock (&async->trash_mutex);
	return p;
}

/* release event memory for custom async queue dispatch handlers
 */

static int
pgm_event_unref (
        pgm_async_t*	async,
        pgm_event_t*	event
        )
{
        g_static_mutex_lock (&async->trash_mutex);
        g_trash_stack_push (&async->trash_event, event);
        g_static_mutex_unlock (&async->trash_mutex);
        return TRUE;
}

/* internal receiver thread, sits in a loop processing incoming packets
 */

static gpointer
pgm_receiver_thread (
	gpointer	data
	)
{
	pgm_async_t* async = (pgm_async_t*)data;
	g_async_queue_ref (async->commit_queue);

/* incoming message buffer */
	pgm_msgv_t msgv;

	do {
		int len = pgm_transport_recvmsg (async->transport, &msgv, 0 /* blocking */);
		if (len >= 0)
		{
/* append to queue */
			pgm_event_t* event = pgm_event_alloc (async);
			event->data = len > 0 ? g_malloc (len) : NULL;
			event->len  = len;

			gpointer dst = event->data;
			struct iovec* src = msgv.msgv_iov;
			while (len)
			{
				memcpy (dst, src->iov_base, src->iov_len);
				dst = (char*)dst + src->iov_len;
				len -= src->iov_len;
				src++;
			}

/* prod pipe on edge */
			g_async_queue_lock (async->commit_queue);
			g_async_queue_push_unlocked (async->commit_queue, event);
			if (g_async_queue_length_unlocked (async->commit_queue) == 1)
			{
				const char one = '1';
				if (1 != write (async->commit_pipe[1], &one, sizeof(one))) {
					g_critical ("write to pipe failed :(");
				}
			}
			g_async_queue_unlock (async->commit_queue);
		}
		else if (ECONNRESET == errno && async->transport->will_close_on_failure)
		{
			break;
		}
	} while (!async->quit);

/* cleanup */
	g_async_queue_unref (async->commit_queue);
	return NULL;
}

/* create asynchronous thread handler
 *
 * on success, 0 is returned.  on error, -1 is returned, and errno set appropriately.
 * on invalid parameters, -EINVAL is returned.
 */
int
pgm_async_create (
	pgm_async_t**		async_,
	pgm_transport_t*	transport,
	guint			preallocate
	)
{
	g_return_val_if_fail (async_ != NULL, -EINVAL);
	g_return_val_if_fail (transport != NULL, -EINVAL);

	int pgm_errno = 0;
	pgm_async_t* async;

	async = g_malloc0 (sizeof(pgm_async_t));
	async->transport = transport;
	async->event_preallocate = preallocate;
	g_trace ("INFO","preallocate event queue.");
	for (guint32 i = 0; i < async->event_preallocate; i++)
	{
		gpointer event = g_slice_alloc (sizeof(pgm_event_t));
		g_trash_stack_push (&async->trash_event, event);
	}
	g_static_mutex_init (&async->trash_mutex);

	g_trace ("INFO","create asynchronous commit queue.");
	async->commit_queue = g_async_queue_new();

	g_trace ("INFO","create commit pipe.");
	int e = pipe (async->commit_pipe);
	if (e < 0) {
		pgm_errno = errno;
		goto err_destroy;
	}

	e = pgm_set_nonblocking (async->commit_pipe);
	if (e) {
		pgm_errno = errno;
		goto err_destroy;
	}

/* setup new thread */
	GError* err;
	async->thread = g_thread_create_full (pgm_receiver_thread,
						async,
						0,
						TRUE,
						TRUE,
						G_THREAD_PRIORITY_HIGH,
						&err);
	if (!async->thread) {
		pgm_errno = errno;
		g_error ("g_thread_create_full failed errno %i: \"%s\"", err->code, err->message);
		goto err_destroy;
	}

/* return new object */
	*async_ = async;

	return 0;

err_destroy:
	if (async->commit_queue) {
		g_async_queue_unref (async->commit_queue);
		async->commit_queue = NULL;
	}

	if (async->commit_pipe[0]) {
		close (async->commit_pipe[0]);
		async->commit_pipe[0] = 0;
	}
	if (async->commit_pipe[1]) {
		close (async->commit_pipe[1]);
		async->commit_pipe[1] = 0;
	}

	if (async->trash_event) {
		gpointer* p = NULL;
		while ( (p = g_trash_stack_pop (&async->trash_event)) )
		{
			g_slice_free1 (sizeof(pgm_event_t), p);
		}
		g_assert (async->trash_event == NULL);
	}
	g_static_mutex_free (&async->trash_mutex);

	g_free (async);
	async = NULL;

	errno = pgm_errno;
	return -1;
}

/* tell async thread to stop, wait for it to stop, then cleanup.
 *
 * on success, 0 is returned.  if async is invalid, -EINVAL is returned.
 */
int
pgm_async_destroy (
	pgm_async_t*		async
	)
{
	g_return_val_if_fail (async != NULL, -EINVAL);

	if (async->thread) {
		async->quit = TRUE;
		g_thread_join (async->thread);
	}

	if (async->commit_queue) {
		g_async_queue_unref (async->commit_queue);
		async->commit_queue = NULL;
	}
	if (async->commit_pipe[0]) {
                close (async->commit_pipe[0]);
                async->commit_pipe[0] = 0;
        }
        if (async->commit_pipe[1]) {
                close (async->commit_pipe[1]);
                async->commit_pipe[1] = 0;
        }
        if (async->trash_event) {
                gpointer *p = NULL;
                while ( (p = g_trash_stack_pop (&async->trash_event)) )
                {
                        g_slice_free1 (sizeof(pgm_event_t), p);
                }
                g_assert (async->trash_event == NULL);
        }
	g_static_mutex_free (&async->trash_mutex);

	g_free (async);
	return 0;
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
        watch->pollfd.fd = async->commit_pipe[0];
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

static gboolean
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

static gboolean
pgm_src_check (
        GSource*                source
        )
{
//      g_trace ("INFO","pgm_src_check");

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
        g_trace ("INFO","pgm_src_dispatch");

        pgm_eventfn_t function = (pgm_eventfn_t)callback;
        pgm_watch_t* watch = (pgm_watch_t*)source;
        pgm_async_t* async = watch->async;

/* empty pipe */
        char buf;
        while (1 == read (async->commit_pipe[0], &buf, sizeof(buf)));

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
 * on success, returns number of bytes read.  on error, -1 is returned, and errno set
 * appropriately.  on invalid parameters, -EINVAL is returned.
 */

gssize
pgm_async_recv (
	pgm_async_t*		async,
	gpointer		data,
	gsize			len,
	int			flags		/* MSG_DONTWAIT for non-blocking */
	)
{
        g_return_val_if_fail (async != NULL, -EINVAL);
	if (len)
	        g_return_val_if_fail (len > 0 && data != NULL, -EINVAL);

	pgm_event_t* event;

	if (flags & MSG_DONTWAIT)
	{
		g_async_queue_lock (async->commit_queue);
		if (g_async_queue_length_unlocked (async->commit_queue) == 0)
		{
			g_async_queue_unlock (async->commit_queue);
			errno = EAGAIN;
			return -1;
		}

		event = g_async_queue_pop_unlocked (async->commit_queue);
		g_async_queue_unlock (async->commit_queue);
	}
	else
	{
		event = g_async_queue_pop (async->commit_queue);
	}

/* pass data back to callee */
	gsize bytes_read = event->len;
	if (bytes_read > len) bytes_read = len;

	memcpy (data, event->data, bytes_read);

/* cleanup */
	if (event->len) g_free (event->data);
	pgm_event_unref (async, event);

	return (gssize)bytes_read;	
}

/* eof */
