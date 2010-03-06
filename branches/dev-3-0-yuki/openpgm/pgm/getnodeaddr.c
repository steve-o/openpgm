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
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <glib.h>

#ifdef G_OS_UNIX
#	include <netdb.h>
#	include <sys/socket.h>
#endif

#include "pgm/sockaddr.h"
#include "pgm/getifaddrs.h"
#include "pgm/getnodeaddr.h"

//#define GETNODEADDR_DEBUG

#ifndef GETNODEADDR_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* globals */

static const char* pgm_family_string (const int);


/* return node primary address on multi-address family interfaces.
 *
 * returns TRUE on success, returns FALSE on failure.
 */

gboolean
pgm_if_getnodeaddr (
	const int		family,	/* requested address family, AF_INET, AF_INET6, or AF_UNSPEC */
	struct sockaddr*	addr,
	const socklen_t		cnt,	/* size of address pointed to by addr */
	pgm_error_t**		error
	)
{
	g_return_val_if_fail (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family, FALSE);
	g_return_val_if_fail (NULL != addr, FALSE);
	if (AF_INET == family || AF_UNSPEC == family)
		g_return_val_if_fail (cnt >= sizeof(struct sockaddr_in), FALSE);
	else
		g_return_val_if_fail (cnt >= sizeof(struct sockaddr_in6), FALSE);

	g_trace ("pgm_if_getnodeaddr (family:%s addr:%p cnt:%d error:%p)",
		pgm_family_string (family), (gpointer)addr, cnt, (gpointer)error);

	char hostname[NI_MAXHOST + 1];
	struct hostent* he;

	if (0 != gethostname (hostname, sizeof(hostname))) {
		pgm_set_error (error,
			     PGM_IF_ERROR,
			     pgm_if_error_from_errno (errno),
			     _("Resolving hostname: %s"),
			     strerror (errno));
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
		const gsize addrlen = res->ai_addrlen;
		memcpy (addr, res->ai_addr, addrlen);
		freeaddrinfo (res);
		return TRUE;
	} else if (EAI_NONAME != e) {
		pgm_set_error (error,
			     PGM_IF_ERROR,
			     pgm_if_error_from_eai_errno (e),
			     _("Resolving hostname address: %s"),
			     gai_strerror (e));
		return FALSE;
	} else if (AF_UNSPEC == family) {
		pgm_set_error (error,
			     PGM_IF_ERROR,
			     PGM_IF_ERROR_NONAME,
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
			     PGM_IF_ERROR,
			     pgm_if_error_from_h_errno (h_errno),
#ifdef G_OS_UNIX
			     _("Resolving IPv4 hostname address: %s"),
			     hstrerror (h_errno));
#else
			     _("Resolving IPv4 hostname address: %d"),
			     WSAGetLastError());
#endif
		return FALSE;
	}

	struct ifaddrs *ifap, *ifa, *ifa6;
	e = getifaddrs (&ifap);
	if (e < 0) {
		pgm_set_error (error,
			     PGM_IF_ERROR,
			     pgm_if_error_from_errno (errno),
			     _("Enumerating network interfaces: %s"),
			     strerror (errno));
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
	freeifaddrs (ifap);
	pgm_set_error (error,
		     PGM_IF_ERROR,
		     PGM_IF_ERROR_NONET,
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
	freeifaddrs (ifap);
	pgm_set_error (error,
		     PGM_IF_ERROR,
		     PGM_IF_ERROR_NONET,
		     _("Discovering primary IPv6 network interface."));
	return FALSE;
ipv6_found:

	memcpy (addr, ifa6->ifa_addr, pgm_sockaddr_len (ifa6->ifa_addr));
	freeifaddrs (ifap);
	return TRUE;
}

static
const char*
pgm_family_string (
	const int	family
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
