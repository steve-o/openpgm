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
#include <pgm/pgm.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct async_event_t;

struct async_t {
	pgm_transport_t*	transport;
#ifndef _WIN32
	pthread_t		thread;
	int			notify_pipe[2];
	int			destroy_pipe[2];
	pthread_mutex_t		pthread_mutex;
#else
	HANDLE			thread;
	HANDLE			notify_event;
	HANDLE			destroy_event;
	HANDLE			win32_mutex;
#endif
	struct async_event_t    *head, *tail;
	unsigned		length;
	bool			is_destroyed;
};
typedef struct async_t async_t;

int async_create (async_t**, pgm_transport_t* const);
int async_destroy (async_t* const);
ssize_t async_recv (async_t* const, void*, size_t);
ssize_t async_recvfrom (async_t* const, void*, size_t, pgm_tsi_t*);

#ifndef _WIN32
static inline int async_get_fd (async_t* async)
{
	if (NULL == async) {
		errno = EINVAL;
		return -1;
	}
	return async->notify_pipe[0];
}
#else
static inline HANDLE async_get_event (async_t* async)
{
	if (NULL == async) {
		errno = EINVAL;
		return NULL;
	}
	return async->notify_event;
}
#endif /* _WIN32 */

#ifdef  __cplusplus
}
#endif

#endif /* __PGM_ASYNC_H__ */
