/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Portable function to return the nodes IP address.  IPv6 addressing
 * is often complicated on dual-stack machines that are configured to
 * only resolve an IPv4 address against the node name.  In such
 * circumstances the primary IPv6 for the interface with that IPv4
 * address is taken as the node address.
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
#ifndef _WIN32
#	include <netdb.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>


//#define GETNODEADDR_DEBUG


/* globals */

static const char* pgm_family_string (const sa_family_t);


/* return node addresses, similar to getaddrinfo('..localmachine") on Win2003.
 *
 * returns TRUE on success, returns FALSE on failure.
 */

PGM_GNUC_INTERNAL
bool
pgm_getnodeaddr (
	const sa_family_t	   family,	/* requested address family, AF_INET, AF_INET6, or AF_UNSPEC */
	struct addrinfo** restrict res,		/* return list of addresses */
	pgm_error_t**	  restrict error
	)
{
	struct addrinfo* na;
	size_t na_len = 0;

	pgm_return_val_if_fail (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family, FALSE);
	pgm_return_val_if_fail (NULL != res, FALSE);
	pgm_debug ("pgm_getnodeaddr (family:%s res:%p error:%p)",
		pgm_family_string (family), (const void*)res, (const void*)error);

	char hostname[NI_MAXHOST];
	struct hostent* he;

	if (0 != gethostname (hostname, sizeof (hostname))) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_sock_errno (save_errno),
			     _("Resolving hostname: %s"),
			     pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno)
				);
		return FALSE;
	}
	hostname[NI_MAXHOST - 1] = '\0';

	struct addrinfo hints = {
		.ai_family	= family,
		.ai_socktype	= SOCK_STREAM,		/* not really */
		.ai_protocol	= IPPROTO_TCP,		/* not really */
		.ai_flags	= AI_ADDRCONFIG,
	}, *result, *ai;

	int e = getaddrinfo (hostname, NULL, &hints, &result);
	if (0 == e) {
/* NB: getaddrinfo may return multiple addresses, the sorting order of the
 * list defined by RFC 3484 and /etc/gai.conf
 */
		for (ai = result; NULL != ai; ai = ai->ai_next)
		{
			if (!(AF_INET == ai->ai_family || AF_INET6 == ai->ai_family))
				continue;
			if (NULL == ai->ai_addr || 0 == ai->ai_addrlen)
				continue;
			na_len += sizeof (struct addrinfo) + ai->ai_addrlen;
		}

		na = pgm_malloc0 (na_len);
		char* p = (char*)na + na_len;	/* point to end of block */
		struct addrinfo* prev = NULL;

		for (ai = result; NULL != ai; ai = ai->ai_next)
		{
			if (!(AF_INET == ai->ai_family || AF_INET6 == ai->ai_family))
				continue;
			if (NULL == ai->ai_addr || 0 == ai->ai_addrlen)
				continue;
			p -= ai->ai_addrlen;
			memcpy (p, ai->ai_addr, ai->ai_addrlen);
			struct addrinfo* t = (struct addrinfo*)(p - sizeof (struct addrinfo));
			t->ai_family	= ai->ai_family;
			t->ai_addrlen	= ai->ai_addrlen;
			t->ai_addr	= (struct sockaddr*)p;
			t->ai_next	= prev;
			prev = t;
			p   -= sizeof (struct addrinfo);
		}
		freeaddrinfo (result);
		*res = na;
		return TRUE;
	} else if (EAI_NONAME != e) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_eai_errno (e, errno),
			     _("Resolving hostname address: %s"),
			     pgm_gai_strerror_s (errbuf, sizeof (errbuf), e));
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

	struct pgm_ifaddrs_t *ifap, *ifa, *ifa6;
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

	na_len = sizeof (struct addrinfo) + pgm_sockaddr_len (ifa6->ifa_addr);
	na = pgm_malloc0 (na_len);
	na->ai_family	= AF_INET6;
	na->ai_addrlen	= pgm_sockaddr_len (ifa6->ifa_addr);
	na->ai_addr	= (struct sockaddr*)((char*)na + sizeof (struct addrinfo));
	memcpy (na->ai_addr, ifa6->ifa_addr, na->ai_addrlen);
	pgm_freeifaddrs (ifap);
	*res = na;
	return TRUE;
}

PGM_GNUC_INTERNAL
void
pgm_freenodeaddr (
	struct addrinfo*	res
	)
{
	pgm_free (res);
}

/* pick a node address that supports mulitcast traffic iff more than one
 * address exists.
 */

PGM_GNUC_INTERNAL
bool
pgm_get_multicast_enabled_node_addr (
	const sa_family_t	   family,	/* requested address family, AF_INET, AF_INET6, or AF_UNSPEC */
	struct sockaddr*  restrict addr,
	const socklen_t		   cnt,		/* size of address pointed to by addr */
	pgm_error_t**	  restrict error
	)
{
	struct addrinfo *result, *res;
	struct pgm_ifaddrs_t *ifap, *ifa;

	if (!pgm_getnodeaddr (family, &result, error)) {
		pgm_prefix_error (error,
				_("Enumerating node address: "));
		return FALSE;
	}
/* iff one address return that independent of multicast support */
	if (NULL == result->ai_next) {
		pgm_return_val_if_fail (cnt >= (socklen_t)result->ai_addrlen, FALSE);
		memcpy (addr, result->ai_addr, result->ai_addrlen);
		pgm_freenodeaddr (result);
		return TRUE;
	}
	if (!pgm_getifaddrs (&ifap, error)) {
		pgm_prefix_error (error,
				_("Enumerating network interfaces: "));
		return FALSE;
	}

	for (res = result; NULL != res; res = res->ai_next)
	{
/* for each node address find matching interface and test flags */
		for (ifa = ifap; ifa; ifa = ifa->ifa_next)
		{
			if (NULL == ifa->ifa_addr ||
			    0 != pgm_sockaddr_cmp (ifa->ifa_addr, res->ai_addr))
				continue;
			if (ifa->ifa_flags & IFF_MULTICAST) {
				pgm_return_val_if_fail (cnt >= (socklen_t)res->ai_addrlen, FALSE);
				memcpy (addr, res->ai_addr, res->ai_addrlen);
				pgm_freenodeaddr (result);
				return TRUE;
			} else
				break;
		}

/* use last address as fallback */
		if (NULL == res->ai_next) {
			pgm_return_val_if_fail (cnt >= (socklen_t)res->ai_addrlen, FALSE);
			memcpy (addr, res->ai_addr, res->ai_addrlen);
			pgm_freenodeaddr (result);
			return TRUE;
		}
	}

	pgm_freeifaddrs (ifap);
	pgm_freenodeaddr (result);
	return FALSE;
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
