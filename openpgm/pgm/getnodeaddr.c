/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable function to return the nodes IP address.
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

#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <errno.h>
#include <netdb.h>
#include <pgm/framework.h>


//#define GETNODEADDR_DEBUG


/* globals */

static const char* pgm_family_string (const sa_family_t);


/* return node primary address on multi-address family interfaces.
 *
 * returns TRUE on success, returns FALSE on failure.
 */

bool
pgm_if_getnodeaddr (
	const sa_family_t	family,	/* requested address family, AF_INET, AF_INET6, or AF_UNSPEC */
	struct sockaddr*	addr,
	const socklen_t		cnt,	/* size of address pointed to by addr */
	pgm_error_t**		error
	)
{
	pgm_return_val_if_fail (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family, FALSE);
	pgm_return_val_if_fail (NULL != addr, FALSE);
	if (AF_INET == family || AF_UNSPEC == family)
		pgm_return_val_if_fail (cnt >= sizeof(struct sockaddr_in), FALSE);
	else
		pgm_return_val_if_fail (cnt >= sizeof(struct sockaddr_in6), FALSE);

	pgm_debug ("pgm_if_getnodeaddr (family:%s addr:%p cnt:%d error:%p)",
		pgm_family_string (family), (const void*)addr, cnt, (const void*)error);

	char hostname[NI_MAXHOST + 1];
	struct hostent* he;

	if (0 != gethostname (hostname, sizeof(hostname))) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_errno (errno),
			     _("Resolving hostname: %s"),
#ifndef _WIN32
			     strerror (errno)
#else
			     pgm_wsastrerror (WSAGetLastError())
#endif
				);
		return FALSE;
	}

	addr->sa_family = family;
	struct addrinfo hints = {
		.ai_family	= family,
		.ai_socktype	= SOCK_STREAM,		/* not really */
		.ai_protocol	= IPPROTO_TCP,		/* not really */
		.ai_flags	= AI_ADDRCONFIG,
	}, *res;

	int e = getaddrinfo (hostname, NULL, &hints, &res);
	if (0 == e) {
		const socklen_t addrlen = res->ai_addrlen;
		memcpy (addr, res->ai_addr, addrlen);
		freeaddrinfo (res);
		return TRUE;
	} else if (EAI_NONAME != e) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_eai_errno (e, errno),
			     _("Resolving hostname address: %s"),
			     gai_strerror (e));
		return FALSE;
	} else if (AF_UNSPEC == family) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     PGM_ERROR_NONAME,
			     _("Resolving hostname address family."));
		return FALSE;
	}

/* Common case a dual stack host has incorrect IPv6 configuration, i.e.
 * hostname is only IPv4 and despite one or more IPv6 addresses.  Workaround
 * for this case is to resolve the IPv4 hostname, find the matching interface
 * and from that interface find an active IPv6 address taking global scope as
 * preference over link scoped addresses.
 */
	he = gethostbyname (hostname);
	if (NULL == he) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_h_errno (h_errno),
#ifndef _WIN32
			     _("Resolving IPv4 hostname address: %s"),
			     hstrerror (h_errno)
#else
			     _("Resolving IPv4 hostname address: %s"),
			     pgm_wsastrerror (WSAGetLastError())
#endif
				);
		return FALSE;
	}

	struct pgm_ifaddrs *ifap, *ifa, *ifa6;
	if (!pgm_getifaddrs (&ifap, error)) {
		pgm_prefix_error (error,
			     _("Enumerating network interfaces: "));
		return FALSE;
	}

/* hunt for IPv4 interface */
	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		if (NULL == ifa->ifa_addr ||
		    AF_INET != ifa->ifa_addr->sa_family)
			continue;
		if (((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr == ((struct in_addr*)(he->h_addr_list[0]))->s_addr)
		{
			goto ipv4_found;
		}
	}
	pgm_freeifaddrs (ifap);
	pgm_set_error (error,
		     PGM_ERROR_DOMAIN_IF,
		     PGM_ERROR_NONET,
		     _("Discovering primary IPv4 network interface."));
	return FALSE;
ipv4_found:

/* hunt for IPv6 interface */
	for (ifa6 = ifap; ifa6; ifa6 = ifa6->ifa_next)
	{
		if (AF_INET6 != ifa6->ifa_addr->sa_family)
			continue;
		if (0 == strcmp (ifa->ifa_name, ifa6->ifa_name))
		{
			goto ipv6_found;
		}
	}
	pgm_freeifaddrs (ifap);
	pgm_set_error (error,
		     PGM_ERROR_DOMAIN_IF,
		     PGM_ERROR_NONET,
		     _("Discovering primary IPv6 network interface."));
	return FALSE;
ipv6_found:

	memcpy (addr, ifa6->ifa_addr, pgm_sockaddr_len (ifa6->ifa_addr));
	pgm_freeifaddrs (ifap);
	return TRUE;
}

static
const char*
pgm_family_string (
	const sa_family_t	family
	)
{
	const char* c;

	switch (family) {
	case AF_UNSPEC:		c = "AF_UNSPEC"; break;
	case AF_INET:		c = "AF_INET"; break;
	case AF_INET6:		c = "AF_INET6"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

/* eof */
