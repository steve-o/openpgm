/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Asynchronous receive thread helper
 *
 * Copyright (c) 2006-2009 Miru Limited.
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

#ifndef __PGM_ASYNC_H__
#define __PGM_ASYNC_H__

#include <errno.h>
#include <glib.h>
#include <pgm/pgm.h>


#define PGM_ASYNC_ERROR		pgm_async_error_quark ()

typedef enum
{
	/* Derived from errno */
	PGM_ASYNC_ERROR_FAULT,
	PGM_ASYNC_ERROR_MFILE,
	PGM_ASYNC_ERROR_NFILE,
	PGM_ASYNC_ERROR_OVERFLOW,
	PGM_ASYNC_ERROR_FAILED
} PGMAsyncError;

typedef struct pgm_async_t pgm_async_t;

struct pgm_async_t {
	pgm_sock_t*		sock;
	GThread*		thread;
	GAsyncQueue*		commit_queue;
#ifndef _WIN32
	int			commit_pipe[2];
	int			destroy_pipe[2];
#else
	WSAEVENT		commit_event;
	WSAEVENT		destroy_event;
#endif
	gboolean		is_destroyed;
	gboolean		is_nonblocking;
};

typedef int (*pgm_eventfn_t)(gpointer, guint, gpointer);


G_BEGIN_DECLS

int pgm_async_create (pgm_async_t**, pgm_sock_t* const, GError**);
int pgm_async_destroy (pgm_async_t* const);
GIOStatus pgm_async_recv (pgm_async_t* const, gpointer, const gsize, gsize* const, const int, GError**);
gboolean pgm_async_set_nonblocking (pgm_async_t* const, const gboolean);
GSource* pgm_async_create_watch (pgm_async_t* const) G_GNUC_WARN_UNUSED_RESULT;
int pgm_async_add_watch_full (pgm_async_t*, gint, pgm_eventfn_t, gpointer, GDestroyNotify);
int pgm_async_add_watch (pgm_async_t*, pgm_eventfn_t, gpointer);
GQuark pgm_async_error_quark (void);

#ifndef _WIN32
static inline int pgm_async_get_fd (pgm_async_t* async)
{
	g_return_val_if_fail (async != NULL, -EINVAL);
	return async->commit_pipe[0];
}
#else
static inline HANDLE pgm_async_get_event (pgm_async_t* async)
{
	g_return_val_if_fail (async != NULL, NULL);
	return async->commit_event;
}
#endif /* _WIN32 */

G_END_DECLS

#endif /* __PGM_ASYNC_H__ */
