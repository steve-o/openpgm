/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Portable interface index to socket address function.  The underlying
 * socket implementation may manage separate numerical spaces for each
 * address family.
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
#include <impl/i18n.h>
#include <impl/framework.h>


//#define INDEXTOADDR_DEBUG


/* interfaces indexes refer to the link layer, we want to find the internet layer address.
 * the big problem is that multiple IPv6 addresses can be bound to one link - called scopes.
 * we can just pick the first scope and let IP routing handle the rest.
 */

PGM_GNUC_INTERNAL
bool
pgm_if_indextoaddr (
	const unsigned		  ifindex,
	const sa_family_t	  iffamily,
	const uint32_t		  ifscope,
	struct sockaddr* restrict ifsa,
	pgm_error_t**	 restrict error
        )
{
	pgm_return_val_if_fail (NULL != ifsa, FALSE);

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
			pgm_return_val_if_reached (FALSE);
			break;
		}
		return TRUE;
	}

	struct pgm_ifaddrs_t *ifap, *ifa;
	if (!pgm_getifaddrs (&ifap, error)) {
		pgm_prefix_error (error,
			     _("Enumerating network interfaces: "));
		return FALSE;
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		if (NULL == ifa->ifa_addr ||
		    ifa->ifa_addr->sa_family != iffamily)
			continue;

		const unsigned i = pgm_if_nametoindex (iffamily, ifa->ifa_name);
		pgm_assert (0 != i);
		if (i == ifindex)
		{
			if (ifscope && ifscope != pgm_sockaddr_scope_id (ifa->ifa_addr))
				continue;
			memcpy (ifsa, ifa->ifa_addr, pgm_sockaddr_len(ifa->ifa_addr));
			pgm_freeifaddrs (ifap);
			return TRUE;
		}
	}

	pgm_set_error (error,
		     PGM_ERROR_DOMAIN_IF,
		     PGM_ERROR_NODEV,
		     _("No matching network interface index: %i"),
		     ifindex);
	pgm_freeifaddrs (ifap);
	return FALSE;
}

/* eof */
