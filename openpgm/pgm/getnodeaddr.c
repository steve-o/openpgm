/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable function to return the nodes IP address.
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

#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <glib.h>

#include "pgm/getifaddrs.h"
#include "pgm/getnodeaddr.h"

//#define GETNODEADDR_DEBUG

#ifndef GETNODEADDR_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* return node primary address on multi-address family interfaces.
 *
 * returns > 0 on success, or -1 on error and sets errno appropriately,
 * 			   or -2 on NS lookup error and sets h_errno appropriately.
 */

int
_pgm_if_getnodeaddr (
	int			af,	/* requested address family, AF_INET, or AF_INET6 */
	struct sockaddr*	addr,
	socklen_t		cnt	/* size of address pointed to by addr */
	)
{
	g_return_val_if_fail (af == AF_INET || af == AF_INET6, -EINVAL);
	g_return_val_if_fail (NULL != addr, -EINVAL);

	char hostname[NI_MAXHOST + 1];
	struct hostent* he;

	gethostname (hostname, sizeof(hostname));

	if (AF_INET == af)
	{
		g_return_val_if_fail (cnt >= sizeof(struct sockaddr_in), -EINVAL);

		((struct sockaddr_in*)addr)->sin_family = af;

		he = gethostbyname (hostname);
		if (NULL == he) {
			g_trace ("gethostbyname failed on local hostname: %s", hstrerror (h_errno));
			return -2;
		}
		((struct sockaddr_in*)addr)->sin_addr.s_addr = ((struct in_addr*)(he->h_addr_list[0]))->s_addr;
		cnt = sizeof(struct sockaddr_in);
	}
	else
	{
		g_return_val_if_fail (cnt >= sizeof(struct sockaddr_in6), -EINVAL);

		((struct sockaddr_in6*)addr)->sin6_family = af;

		struct addrinfo hints = {
			.ai_family	= af,
			.ai_socktype	= SOCK_STREAM,		/* not really */
			.ai_protocol	= IPPROTO_TCP,		/* not really */
			.ai_flags	= 0,
		}, *res;

		int e = getaddrinfo (hostname, NULL, &hints, &res);
		if (0 == e)
		{
			const struct sockaddr_in6* res_sin6 = (const struct sockaddr_in6*)res->ai_addr;
			((struct sockaddr_in6*)addr)->sin6_addr     = res_sin6->sin6_addr;
			((struct sockaddr_in6*)addr)->sin6_scope_id = res_sin6->sin6_scope_id;
			freeaddrinfo (res);
		}
		else
		{
/* try link scope via IPv4 nodename */
			he = gethostbyname (hostname);
			if (NULL == he)
			{
				g_trace ("gethostbyname2 and gethostbyname failed on local hostname: %s", hstrerror (h_errno));
				return -2;
			}

			struct ifaddrs *ifap, *ifa, *ifa6;
			e = getifaddrs (&ifap);
			if (e < 0) {
				g_trace ("getifaddrs failed when trying to resolve link scope interfaces");
				return -1;
			}

/* hunt for IPv4 interface */
			for (ifa = ifap; ifa; ifa = ifa->ifa_next)
			{
				if (AF_INET != ifa->ifa_addr->sa_family) {
					continue;
				}
				if (((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr == ((struct in_addr*)(he->h_addr_list[0]))->s_addr)
				{
					goto ipv4_found;
				}
			}
			g_trace ("node IPv4 interface not found!");
			freeifaddrs (ifap);
			errno = ENONET;
			return -1;
ipv4_found:

/* hunt for IPv6 interface */
			for (ifa6 = ifap; ifa6; ifa6 = ifa6->ifa_next)
			{
				if (AF_INET6 != ifa6->ifa_addr->sa_family) {
					continue;
				}
				if (0 == strcmp(ifa->ifa_name, ifa6->ifa_name))
				{
					goto ipv6_found;
				}
			}
			g_trace ("node IPv6 interface not found!");
			freeifaddrs (ifap);
			errno = ENONET;
			return -1;
ipv6_found:
			((struct sockaddr_in6*)addr)->sin6_addr = ((struct sockaddr_in6 *)ifa6->ifa_addr)->sin6_addr;
			freeifaddrs (ifap);
		}

		cnt = sizeof(struct sockaddr_in6);
	}

	return cnt;
}

/* eof */
