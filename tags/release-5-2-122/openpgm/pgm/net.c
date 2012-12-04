/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * network send wrapper.
 *
 * Copyright (c) 2006-2011 Miru Limited.
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

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <errno.h>
#ifdef HAVE_POLL
#	include <poll.h>
#endif
#ifndef _WIN32
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/net.h>
#include <impl/socket.h>


//#define NET_DEBUG


/* locked and rate regulated sendto
 *
 * on success, returns number of bytes sent.  on error, -1 is returned, and
 * errno set appropriately.
 */

PGM_GNUC_INTERNAL
ssize_t
pgm_sendto_hops (
	pgm_sock_t*	       restrict	sock,
	bool				use_rate_limit,
	pgm_rate_t*	       restrict	minor_rate_control,
	bool				use_router_alert,
	int				hops,			/* -1 == system default */
	const void*	       restrict	buf,
	size_t				len,
	const struct sockaddr* restrict	to,
	socklen_t			tolen
	)
{
	pgm_assert( NULL != sock );
	pgm_assert( NULL != buf );
	pgm_assert( len > 0 );
	pgm_assert( NULL != to );
	pgm_assert( tolen > 0 );

#ifdef NET_DEBUG
	char saddr[INET_ADDRSTRLEN];
	pgm_sockaddr_ntop (to, saddr, sizeof(saddr));
	pgm_debug ("pgm_sendto (sock:%p use_rate_limit:%s minor_rate_control:%p use_router_alert:%s buf:%p len:%" PRIzu " to:%s [toport:%d] tolen:%d)",
		(const void*)sock,
		use_rate_limit ? "TRUE" : "FALSE",
		(const void*)minor_rate_control,
		use_router_alert ? "TRUE" : "FALSE",
		(const void*)buf,
		len,
		saddr,
		ntohs (((const struct sockaddr_in*)to)->sin_port),
		(int)tolen);
#endif

	const SOCKET send_sock = use_router_alert ? sock->send_with_router_alert_sock : sock->send_sock;

	if (use_rate_limit)
	{
		if (NULL == minor_rate_control)
		{
			if (!pgm_rate_check (&sock->rate_control, len, sock->is_nonblocking))
			{
				pgm_set_last_sock_error (PGM_SOCK_ENOBUFS);
				return (const ssize_t)-1;
			}
		}
		else
		{
			if (!pgm_rate_check2 (&sock->rate_control, minor_rate_control, len, sock->is_nonblocking))
			{
				pgm_set_last_sock_error (PGM_SOCK_ENOBUFS);
				return (const ssize_t)-1;
			}
		}
	}

	if (!use_router_alert && sock->can_send_data)
		pgm_mutex_lock (&sock->send_mutex);
	if (-1 != hops)
		pgm_sockaddr_multicast_hops (send_sock, sock->send_gsr.gsr_group.ss_family, hops);

	ssize_t sent = sendto (send_sock, buf, len, 0, to, (socklen_t)tolen);
	pgm_debug ("sendto returned %" PRIzd, sent);
	if (sent < 0) {
		int save_errno = pgm_get_last_sock_error();
		if (PGM_UNLIKELY(save_errno != PGM_SOCK_ENETUNREACH &&	/* Network is unreachable */
		 		 save_errno != PGM_SOCK_EHOSTUNREACH &&	/* No route to host */
		    		 save_errno != PGM_SOCK_EAGAIN))	/* would block on non-blocking send */
		{
#ifdef HAVE_POLL
/* poll for cleared socket */
			struct pollfd p = {
				.fd		= send_sock,
				.events		= POLLOUT,
				.revents	= 0
			};
			const int ready = poll (&p, 1, 500 /* ms */);
#else
			fd_set writefds;
			FD_ZERO(&writefds);
			FD_SET(send_sock, &writefds);
#	ifndef _WIN32
			const int n_fds = send_sock + 1;	/* largest fd + 1 */
#	else
			const int n_fds = 1;			/* count of fds */
#	endif
			struct timeval tv = {
				.tv_sec  = 0,
				.tv_usec = 500 /* ms */ * 1000
			};
			const int ready = select (n_fds, NULL, &writefds, NULL, &tv);
#endif /* HAVE_POLL */
			if (ready > 0)
			{
				sent = sendto (send_sock, buf, len, 0, to, (socklen_t)tolen);
				if ( sent < 0 )
				{
					char errbuf[1024];
					char toaddr[INET6_ADDRSTRLEN];
					save_errno = pgm_get_last_sock_error();
					pgm_sockaddr_ntop (to, toaddr, sizeof(toaddr));
					pgm_warn (_("sendto() %s failed: %s"),
						toaddr,
						pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
				}
			}
			else if (ready == 0)
			{
				char toaddr[INET6_ADDRSTRLEN];
				pgm_sockaddr_ntop (to, toaddr, sizeof(toaddr));
				pgm_warn (_("sendto() %s failed: socket timeout."), toaddr);
			}
			else
			{
				char errbuf[1024];
				save_errno = pgm_get_last_sock_error();
				pgm_warn (_("blocked socket failed: %s"),
					  pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno));
			}
		}
	}

/* revert to default value hop limit */
	if (-1 != hops)
		pgm_sockaddr_multicast_hops (send_sock, sock->send_gsr.gsr_group.ss_family, sock->hops);
	if (!use_router_alert && sock->can_send_data)
		pgm_mutex_unlock (&sock->send_mutex);
	return sent;
}

/* socket helper, for setting pipe ends non-blocking
 *
 * on success, returns 0.  on error, returns -1, and sets errno appropriately.
 */

PGM_GNUC_INTERNAL
int
pgm_set_nonblocking (
	SOCKET		fd[2]
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
