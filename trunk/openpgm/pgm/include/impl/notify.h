/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Low kernel overhead event notify mechanism, or standard pipes.
 *
 * Copyright (c) 2008-2010 Miru Limited.
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
#       error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_NOTIFY_H__
#define __PGM_IMPL_NOTIFY_H__

typedef struct pgm_notify_t pgm_notify_t;

#ifndef _WIN32
#	include <fcntl.h>
#	include <unistd.h>
#	ifdef HAVE_EVENTFD
#		include <sys/eventfd.h>
#	endif
#else /* _WIN32 */
#	include <memory.h>
#	include <ws2tcpip.h>
#endif
#include <pgm/types.h>
#include <impl/messages.h>
#include <impl/sockaddr.h>

PGM_BEGIN_DECLS

struct pgm_notify_t {
#if defined( HAVE_EVENTFD )
	int eventfd;
#elif !defined( _WIN32 )
	int pipefd[2];
#else
	SOCKET s[2];
#endif /* _WIN32 */
};

#if defined( HAVE_EVENTFD )
#	define PGM_NOTIFY_INIT		{ -1 }
#elif !defined( _WIN32 )
#	define PGM_NOTIFY_INIT		{ { -1, -1 } }
#else
#	define PGM_NOTIFY_INIT		{ { INVALID_SOCKET, INVALID_SOCKET } }
#endif


static inline
bool
pgm_notify_is_valid (
	pgm_notify_t*	notify
	)
{
	if (PGM_UNLIKELY(NULL == notify))
		return FALSE;
#if defined( HAVE_EVENTFD )
	if (PGM_UNLIKELY(-1 == notify->eventfd))
		return FALSE;
#elif !defined( _WIN32 )
	if (PGM_UNLIKELY(-1 == notify->pipefd[0] || -1 == notify->pipefd[1]))
		return FALSE;
#else
	if (PGM_UNLIKELY(INVALID_SOCKET == notify->s[0] || INVALID_SOCKET == notify->s[1]))
		return FALSE;
#endif /* _WIN32 */
	return TRUE;
}

static inline
int
pgm_notify_init (
	pgm_notify_t*	notify
	)
{
#if defined( HAVE_EVENTFD )
	pgm_assert (NULL != notify);
	notify->eventfd = -1;
	int retval = eventfd (0, 0);
	if (-1 == retval)
		return retval;
	notify->eventfd = retval;
	const int fd_flags = fcntl (notify->eventfd, F_GETFL);
	if (-1 != fd_flags)
		retval = fcntl (notify->eventfd, F_SETFL, fd_flags | O_NONBLOCK);
	return 0;
#elif !defined( _WIN32 )
	pgm_assert (NULL != notify);
	notify->pipefd[0] = notify->pipefd[1] = -1;
	int retval = pipe (notify->pipefd);
	pgm_assert (0 == retval);
/* set non-blocking */
/* write-end */
	int fd_flags = fcntl (notify->pipefd[1], F_GETFL);
	if (fd_flags != -1)
		retval = fcntl (notify->pipefd[1], F_SETFL, fd_flags | O_NONBLOCK);
	pgm_assert (notify->pipefd[1]);
/* read-end */
	fd_flags = fcntl (notify->pipefd[0], F_GETFL);
	if (fd_flags != -1)
		retval = fcntl (notify->pipefd[0], F_SETFL, fd_flags | O_NONBLOCK);
	pgm_assert (notify->pipefd[0]);
	return retval;
#else
/* use loopback sockets to simulate a pipe suitable for win32/select() */
	struct sockaddr_in addr;
	SOCKET listener;
	int sockerr;
	int addrlen = sizeof (addr);
	unsigned long one = 1;

	pgm_assert (NULL != notify);
	notify->s[0] = notify->s[1] = INVALID_SOCKET;

	listener = socket (AF_INET, SOCK_STREAM, 0);
	pgm_assert (listener != INVALID_SOCKET);

	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr ("127.0.0.1");
	pgm_assert (addr.sin_addr.s_addr != INADDR_NONE);

	sockerr = bind (listener, (const struct sockaddr*)&addr, sizeof (addr));
	pgm_assert (sockerr != SOCKET_ERROR);

	sockerr = getsockname (listener, (struct sockaddr*)&addr, &addrlen);
	pgm_assert (sockerr != SOCKET_ERROR);

// Listen for incoming connections.
	sockerr = listen (listener, 1);
	pgm_assert (sockerr != SOCKET_ERROR);

// Create the socket.
	notify->s[1] = WSASocket (AF_INET, SOCK_STREAM, 0, NULL, 0, 0);
	pgm_assert (notify->s[1] != INVALID_SOCKET);

// Connect to the remote peer.
	sockerr = connect (notify->s[1], (struct sockaddr*)&addr, addrlen);
/* Failure may be delayed from bind and may be due to socket exhaustion as explained
 * in MSDN(bind Function).
 */
	pgm_assert (sockerr != SOCKET_ERROR);

// Accept connection.
	notify->s[0] = accept (listener, NULL, NULL);
	pgm_assert (notify->s[0] != INVALID_SOCKET);

// Set read-end to non-blocking mode
	sockerr = ioctlsocket (notify->s[0], FIONBIO, &one);
	pgm_assert (sockerr != SOCKET_ERROR);

// We don't need the listening socket anymore. Close it.
	sockerr = closesocket (listener);
	pgm_assert (sockerr != SOCKET_ERROR);

	return 0;
#endif /* HAVE_EVENTFD */
}

static inline
int
pgm_notify_destroy (
	pgm_notify_t*	notify
	)
{
	pgm_assert (NULL != notify);

#if defined( HAVE_EVENTFD )
	if (-1 != notify->eventfd) {
		close (notify->eventfd);
		notify->eventfd = -1;
	}
#elif !defined( _WIN32 )
	if (-1 != notify->pipefd[0]) {
		close (notify->pipefd[0]);
		notify->pipefd[0] = -1;
	}
	if (-1 != notify->pipefd[1]) {
		close (notify->pipefd[1]);
		notify->pipefd[1] = -1;
	}
#else
	if (INVALID_SOCKET != notify->s[0]) {
		closesocket (notify->s[0]);
		notify->s[0] = INVALID_SOCKET;
	}
	if (INVALID_SOCKET != notify->s[1]) {
		closesocket (notify->s[1]);
		notify->s[1] = INVALID_SOCKET;
	}
#endif /* HAVE_EVENTFD */
	return 0;
}

static inline
int
pgm_notify_send (
	pgm_notify_t*	notify
	)
{
#if defined( HAVE_EVENTFD )
	uint64_t u = 1;
	pgm_assert (NULL != notify);
	pgm_assert (-1 != notify->eventfd);
	ssize_t s = write (notify->eventfd, &u, sizeof(u));
	return (s == sizeof(u));
#elif !defined( _WIN32 )
	const char one = '1';
	pgm_assert (NULL != notify);
	pgm_assert (-1 != notify->pipefd[1]);
	return (1 == write (notify->pipefd[1], &one, sizeof(one)));
#else
	const char one = '1';
	pgm_assert (NULL != notify);
	pgm_assert (INVALID_SOCKET != notify->s[1]);
	return (1 == send (notify->s[1], &one, sizeof(one), 0));
#endif /* HAVE_EVENTFD */
}

static inline
int
pgm_notify_read (
	pgm_notify_t*	notify
	)
{
#if defined( HAVE_EVENTFD )
	uint64_t u;
	pgm_assert (NULL != notify);
	pgm_assert (-1 != notify->eventfd);
	return (sizeof(u) == read (notify->eventfd, &u, sizeof(u)));
#elif !defined( _WIN32 )
	char buf;
	pgm_assert (NULL != notify);
	pgm_assert (-1 != notify->pipefd[0]);
	return (sizeof(buf) == read (notify->pipefd[0], &buf, sizeof(buf)));
#else
	char buf;
	pgm_assert (NULL != notify);
	pgm_assert (INVALID_SOCKET != notify->s[0]);
	return (sizeof(buf) == recv (notify->s[0], &buf, sizeof(buf), 0));
#endif /* HAVE_EVENTFD */
}

static inline
void
pgm_notify_clear (
	pgm_notify_t*	notify
	)
{
#if defined( HAVE_EVENTFD )
	uint64_t u;
	pgm_assert (NULL != notify);
	pgm_assert (-1 != notify->eventfd);
	while (sizeof(u) == read (notify->eventfd, &u, sizeof(u)));
#elif !defined( _WIN32 )
	char buf;
	pgm_assert (NULL != notify);
	pgm_assert (-1 != notify->pipefd[0]);
	while (sizeof(buf) == read (notify->pipefd[0], &buf, sizeof(buf)));
#else
	char buf;
	pgm_assert (NULL != notify);
	pgm_assert (INVALID_SOCKET != notify->s[0]);
	while (sizeof(buf) == recv (notify->s[0], &buf, sizeof(buf), 0));
#endif /* HAVE_EVENTFD */
}

static inline
SOCKET
pgm_notify_get_socket (
	pgm_notify_t*	notify
	)
{
	pgm_assert (NULL != notify);

#if defined( HAVE_EVENTFD )
	pgm_assert (-1 != notify->eventfd);
	return notify->eventfd;
#elif !defined( _WIN32 )
	pgm_assert (-1 != notify->pipefd[0]);
	return notify->pipefd[0];
#else
	pgm_assert (INVALID_SOCKET != notify->s[0]);
	return notify->s[0];
#endif /* HAVE_EVENTFD */
}

PGM_END_DECLS

#endif /* __PGM_IMPL_NOTIFY_H__ */
