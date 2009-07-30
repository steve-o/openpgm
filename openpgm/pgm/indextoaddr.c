/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable interface index to socket address function.
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

#include <string.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "pgm/sockaddr.h"
#include "pgm/getifaddrs.h"
#include "pgm/indextoaddr.h"

//#define INDEXTOADDR_DEBUG

#ifndef INDEXTOADDR_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* interfaces indexes refer to the link layer, we want to find the internet layer address.
 * the big problem is that multiple IPv6 addresses can be bound to one link - called scopes.
 * we can just pick the first scope and let IP routing handle the rest.
 */

gboolean
_pgm_if_indextoaddr (
	const unsigned int	ifindex,
	const int		iffamily,
	const unsigned		ifscope,
	struct sockaddr*	ifsa,
	GError**		error
        )
{
/* pre-conditions */
	g_assert (NULL != ifsa);

	if (0 == ifindex)		/* any interface or address */
	{
		ifsa->sa_family = iffamily;
		switch (iffamily) {
		case AF_INET:
			((struct sockaddr_in*)ifsa)->sin_addr.s_addr = INADDR_ANY;
			break;

		case AF_INET6:
			((struct sockaddr_in6*)ifsa)->sin6_addr = in6addr_any;
			break;

		default:
			g_assert_not_reached();
			break;
		}
		return TRUE;
	}

	struct ifaddrs *ifap, *ifa;
	if (0 != getifaddrs (&ifap)) {
		g_set_error (error,
			     PGM_IF_ERROR,
			     pgm_if_error_from_errno (errno),
			     _("Enumerating network interfaces: %s"),
			     g_strerror (errno));
		return FALSE;
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr->sa_family != iffamily)
			continue;

		const unsigned i = if_nametoindex(ifa->ifa_name);
		g_assert (0 != i);
		if (i == ifindex)
		{
			if (ifscope && ifscope != pgm_sockaddr_scope_id (ifa->ifa_addr))
				continue;
			memcpy (ifsa, ifa->ifa_addr, pgm_sockaddr_len(ifa->ifa_addr));
			freeifaddrs (ifap);
			return TRUE;
		}
	}

	g_set_error (error,
		     PGM_IF_ERROR,
		     PGM_IF_ERROR_NODEV,
		     _("No matching network interface index: %i"),
		     ifindex);
	freeifaddrs (ifap);
	return FALSE;
}

/* eof */
