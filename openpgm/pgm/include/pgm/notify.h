/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Low kernel overhead event notify mechanism, or standard pipes.
 *
 * Copyright (c) 2008 Miru Limited.
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

#ifndef __PGM_NOTIFY_H__
#define __PGM_NOTIFY_H__

#include <unistd.h>
#include <fcntl.h>

#include <glib.h>

#ifdef CONFIG_HAVE_EVENTFD
#	include <sys/eventfd.h>
#endif


G_BEGIN_DECLS

struct pgm_notify_t {
#ifdef CONFIG_HAVE_EVENTFD
	int eventfd;
#elif defined(G_OS_UNIX)
	int pipefd[2];
#else
	SOCKET s[2];
#endif /* CONFIG_HAVE_EVENTFD */
};

typedef struct pgm_notify_t pgm_notify_t;


static inline gboolean pgm_notify_is_valid (pgm_notify_t* notify)
{
	if (NULL == notify) return FALSE;
#ifdef CONFIG_HAVE_EVENTFD
	if (0 == notify->eventfd) return FALSE;
#elif defined(G_OS_UNIX)
	if (0 == notify->pipefd[0] || 0 == notify->pipefd[1]) return FALSE;
#else
	if (INVALID_SOCKET == notify->s[0] || INVALID_SOCKET == notify->s[1]) return FALSE;
#endif /* CONFIG_HAVE_EVENTFD */
	return TRUE;
}

static inline int pgm_notify_init (pgm_notify_t* notify)
{
	g_assert (notify);

#ifdef CONFIG_HAVE_EVENTFD
	int retval = eventfd (0, 0);
	if (-1 == retval)
		return retval;
	notify->eventfd = retval;
	const int fd_flags = fcntl (notify->eventfd, F_GETFL);
	if (-1 != fd_flags)
		retval = fcntl (notify->eventfd, F_SETFL, fd_flags | O_NONBLOCK);
	return 0;
#elif defined(G_OS_UNIX)
	int retval = pipe (notify->pipefd);
	g_assert (0 == retval);
/* set non-blocking */
/* write-end */
	int fd_flags = fcntl (notify->pipefd[1], F_GETFL);
	if (fd_flags != -1)
		retval = fcntl (notify->pipefd[1], F_SETFL, fd_flags | O_NONBLOCK);
	g_assert (notify->pipefd[1]);
/* read-end */
	fd_flags = fcntl (notify->pipefd[0], F_GETFL);
	if (fd_flags != -1)
		retval = fcntl (notify->pipefd[0], F_SETFL, fd_flags | O_NONBLOCK);
	g_assert (notify->pipefd[0]);
	return retval;
#else
/* use loopback sockets to simulate a pipe suitable for win32/select() */
	struct sockaddr_in addr;
	SOCKET listener;
	int addrlen = sizeof (addr);

	notify->s[0] = notify->s[1] = INVALID_SOCKET;

	listener = socket (AF_INET, SOCK_STREAM, 0);
	g_assert (listener != INVALID_SOCKET);

	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr ("127.0.0.1");
	g_assert (addr.sin_addr.s_addr != INADDR_NONE);

	int rc = bind (listener, (const struct sockaddr*)&addr, sizeof (addr));
	g_assert (rc != SOCKET_ERROR);

	rc = getsockname (listener, (struct sockaddr*)&addr, &addrlen);
	g_assert (rc != SOCKET_ERROR);

// Listen for incoming connections.
	rc = listen (listener, 1);
	g_assert (rc != SOCKET_ERROR);

// Create the socket.
	notify->s[1] = WSASocket (AF_INET, SOCK_STREAM, 0, NULL, 0, 0);
	g_assert (notify->s[1] != INVALID_SOCKET);

// Connect to the remote peer.
	rc = connect (notify->s[1], (struct sockaddr*)&addr, addrlen);
	g_assert (rc != SOCKET_ERROR);

// Accept connection.
	notify->s[0] = accept (listener, NULL, NULL);
	g_assert (notify->s[0] != INVALID_SOCKET);

// We don't need the listening socket anymore. Close it.
	rc = closesocket (listener);
	g_assert (rc != SOCKET_ERROR);

	return 0;
#endif
}

static inline int pgm_notify_destroy (pgm_notify_t* notify)
{
	g_assert (notify);

#ifdef CONFIG_HAVE_EVENTFD
	if (notify->eventfd) {
		close (notify->eventfd);
		notify->eventfd = 0;
	}
#elif defined(G_OS_UNIX)
	if (notify->pipefd[0]) {
		close (notify->pipefd[0]);
		notify->pipefd[0] = 0;
	}
	if (notify->pipefd[1]) {
		close (notify->pipefd[1]);
		notify->pipefd[1] = 0;
	}
#else
	if (notify->s[0]) {
		closesocket (notify->s[0]);
		notify->s[0] = INVALID_SOCKET;
	}
	if (notify->s[1]) {
		closesocket (notify->s[1]);
		notify->s[1] = INVALID_SOCKET;
	}
#endif
	return 0;
}

static inline int pgm_notify_send (pgm_notify_t* notify)
{
	g_assert (notify);

#ifdef CONFIG_HAVE_EVENTFD
	g_assert (notify->eventfd);
	uint64_t u = 1;
	ssize_t s = write (notify->eventfd, &u, sizeof(u));
	return (s == sizeof(u));
#elif defined(G_OS_UNIX)
	g_assert (notify->pipefd[1]);
	const char one = '1';
	return (1 == write (notify->pipefd[1], &one, sizeof(one)));
#else
	g_assert (notify->s[1]);
	const char one = '1';
	return (1 == send (notify->s[1], &one, sizeof(one), 0));
#endif
}

static inline int pgm_notify_read (pgm_notify_t* notify)
{
	g_assert (notify);

#ifdef CONFIG_HAVE_EVENTFD
	g_assert (notify->eventfd);
	uint64_t u;
	return (sizeof(u) == read (notify->eventfd, &u, sizeof(u)));
#elif defined(G_OS_UNIX)
	g_assert (notify->pipefd[0]);
	char buf;
	return (sizeof(buf) == read (notify->pipefd[0], &buf, sizeof(buf)));
#else
	g_assert (notify->s[0]);
	char buf;
	return (sizeof(buf) == recv (notify->s[0], &buf, sizeof(buf), 0));
#endif
}

static inline void pgm_notify_clear (pgm_notify_t* notify)
{
	g_assert (notify);

#ifdef CONFIG_HAVE_EVENTFD
	g_assert (notify->eventfd);
	uint64_t u;
	while (sizeof(u) == read (notify->eventfd, &u, sizeof(u)));
#elif defined(G_OS_UNIX)
	g_assert (notify->pipefd[0]);
	char buf;
	while (sizeof(buf) == read (notify->pipefd[0], &buf, sizeof(buf)));
#else
	g_assert (notify->s[0]);
	char buf;
	while (sizeof(buf) == recv (notify->s[0], &buf, sizeof(buf), 0));
#endif
}

static inline int pgm_notify_get_fd (pgm_notify_t* notify)
{
	g_assert (notify);

#ifdef CONFIG_HAVE_EVENTFD
	g_assert (notify->eventfd);
	return notify->eventfd;
#elif defined(G_OS_UNIX)
	g_assert (notify->pipefd[0]);
	return notify->pipefd[0];
#else
	g_assert (notify->s[0]);
	return notify->s[0];
#endif
}

G_END_DECLS

#endif /* __PGM_NOTIFY_H__ */
