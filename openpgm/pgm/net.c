/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * network send wrapper.
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

#ifdef CONFIG_HAVE_POLL
#	include <poll.h>
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>

#ifdef G_OS_WIN32
#	include <ws2tcpip.h>
#endif

#include "pgm/transport.h"
#include "pgm/rate_control.h"
#include "pgm/net.h"

//#define NET_DEBUG

#ifndef NET_DEBUG
#	define g_trace(m,...)		while (0)
#else
#	define g_trace(m,...)		g_debug(__VA_ARGS__)
#endif


#ifndef ENETUNREACH
#	define ENETUNREACH	WSAENETUNREACH
#endif
#ifndef EHOSTUNREACH
#	define EHOSTUNREACH	WSAEHOSTUNREACH
#endif
#ifndef ENOBUFS
#	define ENOBUFS		WSAENOBUFS
#endif


/* locked and rate regulated sendto
 *
 * on success, returns number of bytes sent.  on error, -1 is returned, and
 * errno set appropriately.
 */

gssize
pgm_sendto (
	pgm_transport_t*	transport,
	gboolean		use_rate_limit,
	gboolean		use_router_alert,
	const void*		buf,
	gsize			len,
	const struct sockaddr*	to,
	gsize			tolen
	)
{
	g_assert( transport );
	g_assert( buf );
	g_assert( len > 0 );
	g_assert( to );
	g_assert( tolen > 0 );

	int sock = use_router_alert ? transport->send_with_router_alert_sock : transport->send_sock;

	if (use_rate_limit &&
	    transport->rate_control && 
	    !pgm_rate_check (transport->rate_control, len, transport->is_nonblocking))
	{
		errno = ENOBUFS;
		return (const gssize)-1;
	}

	if (!use_router_alert && transport->can_send_data)
		g_static_mutex_lock (&transport->send_mutex);

	ssize_t sent = sendto (sock, buf, len, 0, to, (socklen_t)tolen);
	if (	sent < 0 &&
		errno != ENETUNREACH &&		/* Network is unreachable */
		errno != EHOSTUNREACH &&	/* No route to host */
		errno != EAGAIN 		/* would block on non-blocking send */
	   )
	{
#ifdef CONFIG_HAVE_POLL
/* poll for cleared socket */
		struct pollfd p = {
			.fd		= transport->send_sock,
			.events		= POLLOUT,
			.revents	= 0
		};
		const int ready = poll (&p, 1, 500 /* ms */);
#else
		fd_set writefds;
		FD_ZERO(&writefds);
		FD_SET(transport->send_sock, &writefds);
		struct timeval tv = {
			.tv_sec  = 0,
			.tv_usec = 500 /* ms */ * 1000
		};
		const int ready = select (1, NULL, &writefds, NULL, &tv);
#endif /* CONFIG_HAVE_POLL */
		if (ready > 0)
		{
			sent = sendto (sock, buf, len, 0, to, (socklen_t)tolen);
			if ( sent < 0 )
			{
				g_warning (_("sendto %s failed: %s"),
						inet_ntoa( ((const struct sockaddr_in*)to)->sin_addr ),
						g_strerror (errno));
			}
		}
		else if (ready == 0)
		{
			g_warning (_("sendto %s failed: socket timeout."),
					 inet_ntoa( ((const struct sockaddr_in*)to)->sin_addr ));
		}
		else
		{
			g_warning (_("blocked socket failed: %s"),
					g_strerror (errno));
		}
	}

	if (!use_router_alert && transport->can_send_data)
		g_static_mutex_unlock (&transport->send_mutex);
	return sent;
}

/* socket helper, for setting pipe ends non-blocking
 *
 * on success, returns 0.  on error, returns -1, and sets errno appropriately.
 */

int
pgm_set_nonblocking (
	int		fd[2]
	)
{
/* pre-conditions */
	g_assert (fd);
	g_assert (fd[0]);
	g_assert (fd[1]);

	pgm_sockaddr_nonblocking (fd[0], TRUE);
	pgm_sockaddr_nonblocking (fd[1], TRUE);
	return 0;
}		

/* eof */
