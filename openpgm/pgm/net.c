/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * network send wrapper.
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

#include <errno.h>
#ifdef CONFIG_HAVE_POLL
#	include <poll.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pgm/i18n.h>
#include <pgm/framework.h>
#include "pgm/net.h"


//#define NET_DEBUG


#if !defined(ENETUNREACH) && defined(WSAENETUNREACH)
#	define ENETUNREACH	WSAENETUNREACH
#endif
#if !defined(EHOSTUNREACH) && defined(WSAEHOSTUNREACH)
#	define EHOSTUNREACH	WSAEHOSTUNREACH
#endif
#if !defined(ENOBUFS) && defined(WSAENOBUFS)
#	define ENOBUFS		WSAENOBUFS
#endif


/* locked and rate regulated sendto
 *
 * on success, returns number of bytes sent.  on error, -1 is returned, and
 * errno set appropriately.
 */

ssize_t
pgm_sendto (
	pgm_transport_t*		transport,
	bool				use_rate_limit,
	bool				use_router_alert,
	const void*	       restrict	buf,
	size_t				len,
	const struct sockaddr* restrict	to,
	socklen_t			tolen
	)
{
	pgm_assert( NULL != transport );
	pgm_assert( NULL != buf );
	pgm_assert( len > 0 );
	pgm_assert( NULL != to );
	pgm_assert( tolen > 0 );

#ifdef NET_DEBUG
	char saddr[INET_ADDRSTRLEN];
	pgm_sockaddr_ntop (to, saddr, sizeof(saddr));
	pgm_debug ("pgm_sendto (transport:%p use_rate_limit:%s use_router_alert:%s buf:%p len:%d to:%s [toport:%d] tolen:%d)",
		(gpointer)transport,
		use_rate_limit ? "TRUE" : "FALSE",
		use_router_alert ? "TRUE" : "FALSE",
		(gpointer)buf,
		len,
		saddr,
		((struct sockaddr_in*)to)->sin_port,
		tolen);
#endif

	const int sock = use_router_alert ? transport->send_with_router_alert_sock : transport->send_sock;

	if (use_rate_limit && 
	    !pgm_rate_check (&transport->rate_control, len, transport->is_nonblocking))
	{
		errno = ENOBUFS;
		return (const ssize_t)-1;
	}

	if (!use_router_alert && transport->can_send_data)
		pgm_mutex_lock (&transport->send_mutex);

	ssize_t sent = sendto (sock, buf, len, 0, to, (socklen_t)tolen);
	pgm_debug ("sendto returned %zu", sent);
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
				pgm_warn (_("sendto %s failed: %s"),
						inet_ntoa( ((const struct sockaddr_in*)to)->sin_addr ),
						strerror (errno));
			}
		}
		else if (ready == 0)
		{
			pgm_warn (_("sendto %s failed: socket timeout."),
					 inet_ntoa( ((const struct sockaddr_in*)to)->sin_addr ));
		}
		else
		{
			pgm_warn (_("blocked socket failed: %s"),
					strerror (errno));
		}
	}

	if (!use_router_alert && transport->can_send_data)
		pgm_mutex_unlock (&transport->send_mutex);
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
	pgm_assert (fd[0]);
	pgm_assert (fd[1]);

	pgm_sockaddr_nonblocking (fd[0], TRUE);
	pgm_sockaddr_nonblocking (fd[1], TRUE);
	return 0;
}		

/* eof */
