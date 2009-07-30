/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * network interface handling.
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

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "pgm/if.h"
#include "pgm/ip.h"
#include "pgm/sockaddr.h"
#include "pgm/getifaddrs.h"
#include "pgm/getnodeaddr.h"
#include "pgm/inet_network.h"

#define IF_DEBUG

#ifndef IF_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* temporary structure to contain interface name whilst address family
 * has not been resolved.
 */
struct interface_req {
	char			ir_name[IF_NAMESIZE];
	unsigned int		ir_interface;		/* interface index */
	struct sockaddr_storage ir_addr;		/* interface address */
};


/* globals */

#define IF_DEFAULT_GROUP	((in_addr_t) 0xefc00001) /* 239.192.0.1 */

/* ff08::1 */
#define IF6_DEFAULT_INIT { { { 0xff,8,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }
const struct in6_addr if6_default_group_addr = IF6_DEFAULT_INIT;


static const char* pgm_family_string (const int);


/* recommended address space for multicast:
 * rfc4607, rfc3180, rfc2365
 *
 * avoid 5 high-order bit overlap.
 *
 * loopback:  ffx1::/16
 * segment:   ffx2::/16
 * glop:      238/8
 * mysterious admin:  239/8,   ffx6::/16
 * site:      239.252-255/16,  ffx5::/16
 * org:       239.192/14,      ffx8::/16
 *
 * internets: 224.0.1.0-238.255.255.255,  ffxe::/16
 */


/* dump all interfaces to console.
 *
 * note that interface indexes are only in regard to the link layer and hence
 * no 1-1 mapping between adapter name to index back to address.
 */

void
pgm_if_print_all (void)
{
	struct ifaddrs *ifap, *ifa;

	int e = getifaddrs (&ifap);
	if (e < 0)
		return;

	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		int i = if_nametoindex(ifa->ifa_name);
		char rname[IF_NAMESIZE * 2 + 3];
		char b[IF_NAMESIZE * 2 + 3];

		if_indextoname(i, rname);
		sprintf (b, "%s (%s)", ifa->ifa_name, rname);

		if ( ifa->ifa_addr->sa_family != AF_INET && 
			ifa->ifa_addr->sa_family != AF_INET6)
		{
			g_message ("#%d name %-15.15s ---- %-46.46s scope 0 status %s loop %s b/c %s m/c %s",
				i,
				b,
				"",
			ifa->ifa_flags & IFF_UP ? "UP  " : "DOWN",
			ifa->ifa_flags & IFF_LOOPBACK ? "YES" : "NO ",
			ifa->ifa_flags & IFF_BROADCAST ? "YES" : "NO ",
			ifa->ifa_flags & IFF_MULTICAST ? "YES" : "NO "
			);
			continue;
		}

		char s[INET6_ADDRSTRLEN];
		getnameinfo (ifa->ifa_addr, pgm_sockaddr_len(ifa->ifa_addr),
			     s, sizeof(s),
			     NULL, 0,
			     NI_NUMERICHOST);
		g_message ("#%d name %-15.15s IPv%i %-46.46s scope %u status %s loop %s b/c %s m/c %s",
			i,
			b,
			ifa->ifa_addr->sa_family == AF_INET ? 4 : 6,
			s,
			pgm_sockaddr_scope_id(ifa->ifa_addr),
			ifa->ifa_flags & IFF_UP ? "UP  " : "DOWN",
			ifa->ifa_flags & IFF_LOOPBACK ? "YES" : "NO ",
			ifa->ifa_flags & IFF_BROADCAST ? "YES" : "NO ",
			ifa->ifa_flags & IFF_MULTICAST ? "YES" : "NO "
			);
	}

	freeifaddrs (ifap);
}

static inline
gboolean
is_in_net (
	const struct in_addr*	addr,		/* network order */
	const struct in_addr*	netaddr,
	const struct in_addr*	netmask
	)
{
	g_assert (NULL != addr);
	g_assert (NULL != netaddr);
	g_assert (NULL != netmask);

#ifdef IF_DEBUG
	char saddr[INET_ADDRSTRLEN], snetaddr[INET_ADDRSTRLEN], snetmask[INET_ADDRSTRLEN];
	g_trace ("is_in_net (addr:%s netaddr:%s netmask:%s)",
		 pgm_inet_ntop (AF_INET, addr, saddr, sizeof(saddr)),
		 pgm_inet_ntop (AF_INET, netaddr, snetaddr, sizeof(snetaddr)),
		 pgm_inet_ntop (AF_INET, netmask, snetmask, sizeof(snetmask)));
#endif

	if (addr->s_addr != (netaddr->s_addr & netmask->s_addr))
		return FALSE;
	return TRUE;
}

static inline
gboolean
is_in_net6 (
	const struct in6_addr*	addr,
	const struct in6_addr*	netaddr,
	const struct in6_addr*	netmask
	)
{
	g_assert (NULL != addr);
	g_assert (NULL != netaddr);
	g_assert (NULL != netmask);

#ifdef IF_DEBUG
	char saddr[INET6_ADDRSTRLEN], snetaddr[INET6_ADDRSTRLEN], snetmask[INET6_ADDRSTRLEN];
	g_trace ("is_in_net6 (addr:%s netaddr:%s netmask:%s)",
		 pgm_inet_ntop (AF_INET6, addr, saddr, sizeof(saddr)),
		 pgm_inet_ntop (AF_INET6, netaddr, snetaddr, sizeof(snetaddr)),
		 pgm_inet_ntop (AF_INET6, netmask, snetmask, sizeof(snetmask)));
#endif

	for (unsigned i = 0; i < 16; i++)
		if (addr->s6_addr[i] != (netaddr->s6_addr[i] & netmask->s6_addr[i]))
			return FALSE;
	return TRUE;
}

/* parse interface entity into an interface-request structure.
 *
 * e.g.  eth0
 *       1.2.3.4
 *       1.2
 *       abcd::
 *       <hostname>
 *       <nss network name>
 *
 * special addresses should be ignored:
 *
 * local physical link: 169.254.0.0/16, fe80::/64
 * broadcast: 255.255.255.255
 * multicast: 224.0.0.0/4 (224.0.0.0 to 239.255.255.255), ff00::/8
 *
 * We could use if_nametoindex() but we might as well check that the interface is
 * actually UP and capable of multicast traffic.
 *
 * returns 0 on success, -EINVAL on invalid input, -ENODEV on interface not found,
 * -EXDEV if multicast address instead of interface found.
 */

static
gboolean
parse_interface (
	const int		family,			/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*		ifname,			/* NULL terminated */
	struct interface_req*	ir,			/* location to write interface details to */
	GError**		error
	)
{
	gboolean check_inet_network = FALSE, check_inet6_network = FALSE;
	gboolean check_addr = FALSE;
	gboolean check_ifname = FALSE;
	struct in_addr in_addr;
	struct in6_addr in6_addr;
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_storage addr;
	guint interface_matches = 0;

/* pre-conditions */
	g_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	g_assert (NULL != ifname);
	g_assert (NULL != ir);

	g_trace ("parse_interface (family:%s ifname:\"%s\" ir:%p error:%p)",
		 pgm_family_string (family), ifname, (gpointer)ir, (gpointer)error);

/* network address: in_addr in network order */
	if (AF_INET6 != family && 0 == _pgm_inet_network (ifname, &in_addr))
	{
		if (IN_MULTICAST(g_ntohl(in_addr.s_addr))) {
			g_set_error (error,
				     PGM_IF_ERROR,
				     PGM_IF_ERROR_XDEV,
				     _("Expecting network interface address, found IPv4 multicast network: %s"),
				     ifname);
			return FALSE;
		}
		check_inet_network = TRUE;
		((struct sockaddr*)&addr)->sa_family = AF_INET;
		((struct sockaddr_in*)&addr)->sin_addr.s_addr = in_addr.s_addr;
		check_addr = TRUE;
	}
	if (AF_INET  != family && 0 == _pgm_inet6_network (ifname, &in6_addr))
	{
		if (IN6_IS_ADDR_MULTICAST(&in6_addr)) {
			g_set_error (error,
				     PGM_IF_ERROR,
				     PGM_IF_ERROR_XDEV,
				     _("Expecting network interface address, found IPv6 multicast network: %s"),
				     ifname);
			return FALSE;
		}
		check_inet6_network = TRUE;
		((struct sockaddr*)&addr)->sa_family = AF_INET6;
		((struct sockaddr_in6*)&addr)->sin6_addr = in6_addr;
		((struct sockaddr_in6*)&addr)->sin6_port = 0;
		((struct sockaddr_in6*)&addr)->sin6_flowinfo = 0;
		((struct sockaddr_in6*)&addr)->sin6_scope_id = 0;
		check_addr = TRUE;
	}

/* numeric host with scope id */
	if (!check_addr)
	{
		struct addrinfo hints = {
			.ai_family	= family,
			.ai_socktype	= SOCK_STREAM,				/* not really, SOCK_RAW */
			.ai_protocol	= IPPROTO_TCP,				/* not really, IPPROTO_PGM */
			.ai_flags	= AI_ADDRCONFIG | AI_NUMERICHOST	/* AI_V4MAPPED is unhelpful */
		}, *res;
		const int eai = getaddrinfo (ifname, NULL, &hints, &res);
		if (0 == eai) {
			if (AF_INET == res->ai_family && IN_MULTICAST(g_ntohl (((struct sockaddr_in*)(res->ai_addr))->sin_addr.s_addr)))
			{
				g_set_error (error,
					     PGM_IF_ERROR,
					     PGM_IF_ERROR_XDEV,
					     _("Expecting interface address, found IPv4 multicast address: %s"),
					     ifname);
				freeaddrinfo (res);
				return FALSE;
			}
			else if (AF_INET6 == res->ai_family && IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)res->ai_addr)->sin6_addr))
			{
				g_set_error (error,
					     PGM_IF_ERROR,
					     PGM_IF_ERROR_XDEV,
					     _("Expecting interface address, found IPv6 multicast address: %s"),
					     ifname);
				freeaddrinfo (res);
				return FALSE;
			}

			memcpy (&addr, res->ai_addr, pgm_sockaddr_len (res->ai_addr));
			freeaddrinfo (res);
			check_addr = TRUE;
		} else if (EAI_NONAME != eai) {
			g_set_error (error,
				     PGM_IF_ERROR,
				     pgm_if_error_from_eai_errno (eai),
				     _("Numeric host resolution: %s"),
				     gai_strerror (eai));
			return FALSE;
		}
	}

/* network name into network address, can be expensive with NSS network lookup */
	if (!(check_inet_network || check_inet6_network))
	{
		const struct netent* ne = getnetbyname (ifname);
		if (ne) {
			switch (ne->n_addrtype) {
			case AF_INET:
				if (AF_INET6 == family) {
					g_set_error (error,
						     PGM_IF_ERROR,
						     PGM_IF_ERROR_NODEV,
						     _("IP address family conflict when resolving network name \"%s\", found AF_INET when AF_INET6 expected."),
						     ifname);
					return FALSE;
				}
/* ne->n_net in network order */
				in_addr.s_addr = ne->n_net;
g_message ("moooooooo ******************");
				if (IN_MULTICAST(g_ntohl(in_addr.s_addr))) {
g_message ("oooooooo ******************");
					g_set_error (error,
						     PGM_IF_ERROR,
						     PGM_IF_ERROR_XDEV,
						     _("Network name resolves to IPv4 mulicast address: %s"),
						     ifname);
					return FALSE;
				}
				check_inet_network = TRUE;
				break;
			case AF_INET6:
#ifndef CONFIG_HAVE_IP6_NETWORKS
				g_set_error (error,
					     PGM_IF_ERROR,
					     PGM_IF_ERROR_NODEV,
					     _("Not configured for IPv6 network name support, \"%s\" is an IPv6 network name."),
					     ifname);
				return FALSE;
#else
				if (AF_INET == family) {
					g_set_error (error,
						     PGM_IF_ERROR,
						     PGM_IF_ERROR_NODEV,
						     _("IP address family conflict when resolving network name \"%s\", found AF_INET6 when AF_INET expected."),
						     ifname);
					return FALSE;
				}
				if (IN6_IS_ADDR_MULTICAST(&ne->n_net)) {
					g_set_error (error,
						     PGM_IF_ERROR,
						     PGM_IF_ERROR_XDEV,
						     _("Network name resolves to IPv6 mulicast address: %s"),
						     ifname);
					return FALSE;
				}
				in6_addr = *(const struct in6_addr*)&ne->n_net;
				check_inet6_network = TRUE;
				break;
#endif
			default:
				g_set_error (error,
					     PGM_IF_ERROR,
					     PGM_IF_ERROR_NODEV,
					     _("Network name resolves to non-internet protocol address family: \"%s\""),
					     ifname);
				return FALSE;
			}
		}
	}

/* hostname lookup with potential DNS delay or error */
	if (!check_addr)
	{
		struct addrinfo hints = {
			.ai_family	= family,
			.ai_socktype	= SOCK_STREAM,		/* not really, SOCK_RAW */
			.ai_protocol	= IPPROTO_TCP,		/* not really, IPPROTO_PGM */
			.ai_flags	= AI_ADDRCONFIG,	/* AI_V4MAPPED is unhelpful */
		}, *res;

		const int eai = getaddrinfo (ifname, NULL, &hints, &res);
		if (0 == eai) {
			if (AF_INET == res->ai_family && IN_MULTICAST(g_ntohl (((struct sockaddr_in*)(res->ai_addr))->sin_addr.s_addr)))
			{
				g_set_error (error,
					     PGM_IF_ERROR,
					     PGM_IF_ERROR_XDEV,
					     _("Expecting interface address, found IPv4 multicast name: %s"),
					     ifname);
				freeaddrinfo (res);
				return FALSE;
			}
			else if (AF_INET6 == res->ai_family && IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)res->ai_addr)->sin6_addr))
			{
				g_set_error (error,
					     PGM_IF_ERROR,
					     PGM_IF_ERROR_XDEV,
					     _("Expecting interface address, found IPv6 multicast name: %s"),
					     ifname);
				freeaddrinfo (res);
				return FALSE;
			}
			memcpy (&addr, res->ai_addr, pgm_sockaddr_len (res->ai_addr));
			freeaddrinfo (res);
			check_addr = TRUE;
		} else if (EAI_NONAME != eai) {
			g_set_error (error,
				     PGM_IF_ERROR,
				     pgm_if_error_from_eai_errno (eai),
				     _("Internet host resolution: %s"),
				     gai_strerror (eai));
			return FALSE;
		} else
			check_ifname = TRUE;
	}

/* iterate through interface list and match device name, ip or net address */
	if (getifaddrs (&ifap) < 0) {
		g_set_error (error,
			     PGM_IF_ERROR,
			     pgm_if_error_from_errno (errno),
			     _("Enumerating network interfaces: %s"),
			     g_strerror (errno));
		return FALSE;
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		switch (ifa->ifa_addr->sa_family) {
/* ignore raw entries on Linux */
#ifdef CONFIG_HAVE_GETIFADDRS
		case AF_PACKET:
			continue;
#endif
		case AF_INET:
			if (AF_INET6 == family)
				continue;
			break;
		case AF_INET6:
			if (AF_INET == family)
				continue;
			break;
		default:
			continue;
		}

		const unsigned ifindex = if_nametoindex (ifa->ifa_name);
		g_assert (0 != ifindex);

/* check numeric host */
		if (check_addr &&
		    (0 == pgm_sockaddr_cmp (ifa->ifa_addr, (const struct sockaddr*)&addr)))
		{
			strcpy (ir->ir_name, ifa->ifa_name);
			ir->ir_interface = ifindex;
			memcpy (&ir->ir_addr, ifa->ifa_addr, pgm_sockaddr_len (ifa->ifa_addr));
			freeifaddrs (ifap);
			return TRUE;
		}

/* check network address */
		if (check_inet_network)
		{
			const struct in_addr netaddr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
			const struct in_addr netmask = ((struct sockaddr_in*)ifa->ifa_netmask)->sin_addr;
			if (is_in_net (&in_addr, &netaddr, &netmask)) {
				strcpy (ir->ir_name, ifa->ifa_name);
				ir->ir_interface = ifindex;
				memcpy (&ir->ir_addr, ifa->ifa_addr, pgm_sockaddr_len (ifa->ifa_addr));
				freeifaddrs (ifap);
				return TRUE;
			}
		}
		if (check_inet6_network)
		{
			const struct in6_addr netaddr = ((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
			const struct in6_addr netmask = ((struct sockaddr_in6*)ifa->ifa_netmask)->sin6_addr;
			if (is_in_net6 (&in6_addr, &netaddr, &netmask)) {
				strcpy (ir->ir_name, ifa->ifa_name);
				ir->ir_interface = ifindex;
				memcpy (&ir->ir_addr, ifa->ifa_addr, pgm_sockaddr_len (ifa->ifa_addr));
				freeifaddrs (ifap);
				return TRUE;
			}
		}

/* check interface name */
		if (check_ifname)
		{
			if (0 != strcmp (ifname, ifa->ifa_name))
				continue;

/* check for multiple interfaces */
			if (interface_matches++) {
				g_set_error (error,
					     PGM_IF_ERROR,
					     PGM_IF_ERROR_NOTUNIQ,
					     _("Network interface name not unique: %s"),
					     ifname);
				freeifaddrs (ifap);
				return FALSE;
			}

			ir->ir_interface = ifindex;
			strcpy (ir->ir_name, ifa->ifa_name);
			memcpy (&ir->ir_addr, ifa->ifa_addr, pgm_sockaddr_len (ifa->ifa_addr));
			continue;
		}

	}

	if (0 == interface_matches) {
		g_set_error (error,
			     PGM_IF_ERROR,
			     PGM_IF_ERROR_NODEV,
			     _("No matching network interface: %s"),
			     ifname);
		freeifaddrs (ifap);
		return FALSE;
	}

	freeifaddrs (ifap);
	return TRUE;
}

/* parse one multicast address, conflict resolution of multiple address families of DNS multicast names is
 * deferred to libc.
 *
 * Zone indices are ignored as interface specification is already available.
 *
 * reserved addresses may flag warnings:
 *
 * 224.0.0.0/24 for local network control
 * 224.0.1/24 for internetwork control
 * 169.254.255.255, ff02::1 all local nodes on segment
 * ff02::2 all routers
 * ff05::1 all nodes
 * ff0x::fb multicast DNS
 * ff0x::108 NIS
 * ff05::1:3 DHCP
 */

static inline
gboolean
parse_group (
	const int		family,		/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*		group,		/* NULL terminated */
	struct sockaddr*	addr,		/* pointer to sockaddr_storage for writing */
	GError**		error
	)
{
/* pre-conditions */
	g_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	g_assert (NULL != group);
	g_assert (NULL != addr);

	g_trace ("parse_group (family:%s group:\"%s\" addr:%p error:%p)",
		 pgm_family_string (family), group, (gpointer)addr, (gpointer)error);

/* IPv4 address */
	if (AF_INET6 != family &&
	    pgm_inet_pton (AF_INET, group, &((struct sockaddr_in*)addr)->sin_addr) &&
	    IN_MULTICAST(g_ntohl (((struct sockaddr_in*)addr)->sin_addr.s_addr)))
	{
		addr->sa_family = AF_INET;
		return TRUE;
	}
	if (AF_INET  != family &&
	    pgm_inet_pton (AF_INET6, group, &((struct sockaddr_in6*)addr)->sin6_addr) &&
	    IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)addr)->sin6_addr))
	{
		addr->sa_family = AF_INET6;
		((struct sockaddr_in6*)addr)->sin6_port = 0;
		((struct sockaddr_in6*)addr)->sin6_flowinfo = 0;
		((struct sockaddr_in6*)addr)->sin6_scope_id = 0;
		return TRUE;
	}

/* NSS network */
	const struct netent* ne = getnetbyname (group);
	if (ne) {
		switch (ne->n_addrtype) {
		case AF_INET:
			if (AF_INET6 == family) {
				g_set_error (error,
					     PGM_IF_ERROR,
					     PGM_IF_ERROR_NODEV,
					     _("IP address family conflict when resolving network name \"%s\", found IPv4 when IPv6 expected."),
					     group);
				return FALSE;
			}
/* ne->n_net in network order */
			if (IN_MULTICAST(g_ntohl(ne->n_net))) {
				addr->sa_family = AF_INET;
				((struct sockaddr_in*)addr)->sin_addr.s_addr = ne->n_net;
				return TRUE;
			}
			g_set_error (error,
				     PGM_IF_ERROR,
				     PGM_IF_ERROR_NODEV,
				     _("IP address class conflict when resolving network name \"%s\", expected IPv4 multicast."),
				     group);
			return FALSE;
		case AF_INET6:
#ifndef CONFIG_HAVE_IP6_NETWORKS
			g_set_error (error,
				     PGM_IF_ERROR,
				     PGM_IF_ERROR_NODEV,
				     _("Not configured for IPv6 network name support, \"%s\" is an IPv6 network name."),
				     group);
			return FALSE;
#else
			if (AF_INET == family) {
				g_set_error (error,
					     PGM_IF_ERROR,
					     PGM_IF_ERROR_NODEV,
					     _("IP address family conflict when resolving network name \"%s\", found IPv6 when IPv4 expected."),
					     group);
				return FALSE;
			}
			if (IN6_IS_ADDR_MULTICAST(&ne->n_net)) {
				addr->sa_family = AF_INET6;
				((struct sockaddr_in6*)addr)->sin6_addr = *(const struct in6_addr*)ne->n_net;
				((struct sockaddr_in6*)&addr)->sin6_port = 0;
				((struct sockaddr_in6*)&addr)->sin6_flowinfo = 0;
				((struct sockaddr_in6*)&addr)->sin6_scope_id = 0;
				return TRUE;
			}
			g_set_error (error,
				     PGM_IF_ERROR,
				     PGM_IF_ERROR_NODEV,
				     _("IP address class conflict when resolving network name \"%s\", expected IPv6 multicast."),
				     group);
			return FALSE;
#endif /* CONFIG_HAVE_IP6_NETWORKS */
		default:
			g_set_error (error,
				     PGM_IF_ERROR,
				     PGM_IF_ERROR_NODEV,
				     _("Network name resolves to non-internet protocol address family: \"%s\""),
				     group);
			return FALSE;
		}
	}

/* lookup group through name service */
	struct addrinfo hints = {
		.ai_family	= family,
		.ai_socktype	= SOCK_STREAM,		/* not really, SOCK_RAW */
		.ai_protocol	= IPPROTO_TCP,		/* not really, IPPROTO_PGM */
		.ai_flags	= AI_ADDRCONFIG,	/* AI_V4MAPPED is unhelpful */
	}, *res;

	const int eai = getaddrinfo (group, NULL, &hints, &res);
	if (0 != eai) {
		g_set_error (error,
			     PGM_IF_ERROR,
			     pgm_if_error_from_eai_errno (eai),
			     _("Resolving receive group: %s"),
			     gai_strerror (eai));
		return FALSE;
	}

	if ((AF_INET6 != family && IN_MULTICAST(g_ntohl (((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr))) ||
	    (AF_INET  != family && IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)res->ai_addr)->sin6_addr)))
	{
		memcpy (addr, res->ai_addr, res->ai_addrlen);
		freeaddrinfo (res);
		return TRUE;
	}

	g_set_error (error,
		     PGM_IF_ERROR,
		     PGM_IF_ERROR_INVAL,
		     _("Unresolvable receive group: %s"),
		     group);
	freeaddrinfo (res);
	return FALSE;
}

/* parse an interface entity from a network parameter.
 *
 * family can be unspecified - AF_UNSPEC, can return interfaces with the unspecified
 * address family
 *
 * examples:  "eth0"
 * 	      "hme0,hme1"
 * 	      "qe0,qe1,qe2"
 * 	      "qe0,qe2,qe2"	=> valid even though duplicate interface name
 *
 * returns 0 on success with device_list containing double linked list of devices as
 * sockaddr/idx pairs.  returns -ERANGE, device multicast group address family with be
 * AF_UNSPEC when multiple matching adapters have been discovered.  returns -EINVAL on
 * invalid input, -ENODEV if a device could not be found, and -EXDEV if a multicast group
 * is resolved instead of a node address.
 *
 * memory ownership of linked list is passed to caller and must be freed with g_free
 * and the g_list_free* api.
 */

static
gboolean
parse_interface_entity (
	int			family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*		entity,	/* NULL terminated */
	GList**			interface_list,	/* <struct interface_req*> */
	GError**		error
	)
{
	struct interface_req* ir;
	GList* source_list = NULL;

/* pre-conditions */
	g_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	g_assert (NULL != interface_list);
	g_assert (NULL == *interface_list);
	g_assert (NULL != error);

	g_trace ("parse_interface_entity (family:%s entity:%s%s%s interface_list:%p error:%p)",
		pgm_family_string (family),
		entity ? "\"":"", entity, entity ? "\"":"",
		(gpointer)interface_list,
		(gpointer)error);

/* the empty entity, returns in_addr_any for both receive and send interfaces */
	if (NULL == entity)
	{
		ir = g_new0 (struct interface_req, 1);
		((struct sockaddr*)&ir->ir_addr)->sa_family = family;
		*interface_list = g_list_append (*interface_list, ir);
		return TRUE;
	}

/* check interface name length limit */
	gchar** tokens = g_strsplit (entity, ",", 10);
	int j = 0;
	while (tokens && tokens[j])
	{
		GError* sub_error = NULL;
		ir = g_new (struct interface_req, 1);
		if (!parse_interface (family, tokens[j], ir, &sub_error))
		{
/* mark multiple interfaces for later decision based on group families */
			if (sub_error && PGM_IF_ERROR_NOTUNIQ == sub_error->code)
			{
				((struct sockaddr*)&ir->ir_addr)->sa_family = AF_UNSPEC;
				g_error_free (sub_error);
			}
/* bail out on first interface with an error */
			else
			{
				g_propagate_error (error, sub_error);
				g_free (ir);
				g_strfreev (tokens);
				while (source_list) {
					g_free (source_list->data);
					source_list = g_list_delete_link (source_list, source_list);
				}
				return FALSE;
			}
		}

		source_list = g_list_append (source_list, ir);
		++j;
	}

	g_strfreev (tokens);
	*interface_list = source_list;
	return TRUE;
}

/* parse a receive multicast group entity.  can contain more than one multicast group to
 * support asymmetric fan-out.
 *
 * if group is ambiguous, i.e. empty or a name mapping then the address family of the matching
 * interface is queried.  if the interface is also ambiguous, i.e. empty interface and receive group
 * then the hostname will be used to determine the default node address family.  if the hosts
 * node name resolves both IPv4 and IPv6 address families then the first matching value is taken.
 *
 * e.g. "239.192.0.1"
 * 	"239.192.0.100,239.192.0.101"
 *
 * unspecified address family interfaces are forced to AF_INET or AF_INET6.
 */

static
gboolean
parse_receive_entity (
	int			family,		/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*		entity,		/* NULL terminated */
	GList**			interface_list,	/* <struct interface_req*> */
	GList**			recv_list,	/* <struct group_source_req*> */
	GError**		error
	)
{
/* pre-conditions */
	g_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	g_assert (NULL != recv_list);
	g_assert (NULL == *recv_list);
	g_assert (NULL != error);

	g_trace ("parse_receive_entity (family:%s entity:%s%s%s interface_list:%p recv_list:%p error:%p)",
		pgm_family_string (family),
		entity ? "\"":"", entity, entity ? "\"":"",
		(gpointer)interface_list,
		(gpointer)recv_list,
		(gpointer)error);

	struct group_source_req* recv_gsr;
	struct interface_req* primary_interface = (struct interface_req*)g_memdup ((*interface_list)->data, sizeof(struct interface_req));

/* the empty entity */
	if (NULL == entity)
	{
/* default receive object */
		recv_gsr = g_new0 (struct group_source_req, 1);
		recv_gsr->gsr_interface = primary_interface->ir_interface;
		((struct sockaddr*)&recv_gsr->gsr_group)->sa_family = family;

/* track IPv6 scope from any resolved interface */
		unsigned scope_id = 0;

/* if using unspec default group check the interface for address family
 */
		if (AF_UNSPEC == ((struct sockaddr*)&recv_gsr->gsr_group)->sa_family)
		{
			if (AF_UNSPEC == ((struct sockaddr*)&primary_interface->ir_addr)->sa_family)
			{
				struct sockaddr_storage addr;
				if (!_pgm_if_getnodeaddr (AF_UNSPEC, (struct sockaddr*)&addr, sizeof(addr), error))
				{
					g_prefix_error (error,
							_("Node primary address family cannot be determined: "));
					g_free (recv_gsr);
					g_free (primary_interface);
					return FALSE;
				}
				((struct sockaddr*)&recv_gsr->gsr_group)->sa_family = ((struct sockaddr*)&addr)->sa_family;
				if (AF_INET6 == ((struct sockaddr*)&addr)->sa_family)
					scope_id = pgm_sockaddr_scope_id (&addr);

/* was an interface actually specified */
				if (primary_interface->ir_name[0] != '\0')
				{
					struct interface_req ir;
					if (!parse_interface (((struct sockaddr*)&recv_gsr->gsr_group)->sa_family, primary_interface->ir_name, &ir, error))
					{
						g_prefix_error (error,
								_("Unique address cannot be determined for interface \"%s\": "),
								primary_interface->ir_name);
						g_free (recv_gsr);
						g_free (primary_interface);
						return FALSE;
					}

					recv_gsr->gsr_interface = ir.ir_interface;
					memcpy (&primary_interface->ir_addr, &ir.ir_addr, pgm_sockaddr_len (&ir.ir_addr));
					if (AF_INET6 == pgm_sockaddr_family (&ir.ir_addr))
						scope_id = pgm_sockaddr_scope_id (&ir.ir_addr);
				}
			}
			else
			{
/* use interface address family for multicast group */
				((struct sockaddr*)&recv_gsr->gsr_group)->sa_family = ((struct sockaddr*)&primary_interface->ir_addr)->sa_family;
				if (AF_INET6 == pgm_sockaddr_family (&primary_interface->ir_addr)) {
					scope_id = pgm_sockaddr_scope_id (&primary_interface->ir_addr);
				}
			}
		}


		g_assert (AF_UNSPEC != ((struct sockaddr*)&recv_gsr->gsr_group)->sa_family);
		if (AF_UNSPEC != ((struct sockaddr*)&primary_interface->ir_addr)->sa_family)
		{
			g_assert (((struct sockaddr*)&recv_gsr->gsr_group)->sa_family == ((struct sockaddr*)&primary_interface->ir_addr)->sa_family);
		}
		else
		{
/* check if we can now resolve the interface by address family of the receive group */
			if (primary_interface->ir_name[0] != '\0')
			{
				struct interface_req ir;
				if (!parse_interface (((struct sockaddr*)&recv_gsr->gsr_group)->sa_family, primary_interface->ir_name, &ir, error))
				{
					g_prefix_error (error,
							_("Unique address cannot be determined for interface \"%s\": "),
							primary_interface->ir_name);
					g_free (recv_gsr);
					g_free (primary_interface);
					return FALSE;
				}

				recv_gsr->gsr_interface = ir.ir_interface;
				if (AF_INET6 == pgm_sockaddr_family (&ir.ir_addr))
					scope_id = pgm_sockaddr_scope_id (&ir.ir_addr);
			}
		}

/* copy default PGM multicast group */
		switch (((struct sockaddr*)&recv_gsr->gsr_group)->sa_family) {
		case AF_INET6:
			memcpy (&((struct sockaddr_in6*)&recv_gsr->gsr_group)->sin6_addr,
				&if6_default_group_addr,
				sizeof(if6_default_group_addr));
			((struct sockaddr_in6*)&recv_gsr->gsr_group)->sin6_scope_id = scope_id;
			break;

		case AF_INET:
			((struct sockaddr_in*)&recv_gsr->gsr_group)->sin_addr.s_addr = htonl(IF_DEFAULT_GROUP);
			break;
	
		default:
			g_assert_not_reached();
		}

/* ASM: source = group */
		memcpy (&recv_gsr->gsr_source, &recv_gsr->gsr_group, pgm_sockaddr_len (&recv_gsr->gsr_group));
		*recv_list = g_list_append (*recv_list, recv_gsr);
		g_free (primary_interface);
		return TRUE;
	}

/* parse one or more multicast receive groups.
 */

	int j = 0;	
	gchar** tokens = g_strsplit (entity, ",", 10);
	while (tokens && tokens[j])
	{
/* default receive object */
		recv_gsr = g_new0 (struct group_source_req, 1);
		recv_gsr->gsr_interface = primary_interface->ir_interface;
		((struct sockaddr*)&recv_gsr->gsr_group)->sa_family = family;

		if (AF_UNSPEC == ((struct sockaddr*)&recv_gsr->gsr_group)->sa_family)
		{
			if (AF_UNSPEC == ((struct sockaddr*)&primary_interface->ir_addr)->sa_family)
			{
				g_trace ("Address family of receive group cannot be determined from interface.");
			}
			else
			{
				((struct sockaddr*)&recv_gsr->gsr_group)->sa_family = ((struct sockaddr*)&primary_interface->ir_addr)->sa_family;
				if (AF_INET6 == ((struct sockaddr*)&recv_gsr->gsr_group)->sa_family) {
					((struct sockaddr_in6*)&recv_gsr->gsr_group)->sin6_scope_id = pgm_sockaddr_scope_id (&primary_interface->ir_addr);
				}
			}
		}

		if (!parse_group (((struct sockaddr*)&recv_gsr->gsr_group)->sa_family, tokens[j], (struct sockaddr*)&recv_gsr->gsr_group, error))
		{
			g_prefix_error (error,
					_("Unresolvable receive entity \"%s\": "),
					tokens[j]);
			g_free (recv_gsr);
			g_strfreev (tokens);
			g_free (primary_interface);
			return FALSE;
		}

/* check if we can now resolve the source interface by address family of the receive group */
		if (AF_UNSPEC == ((struct sockaddr*)&primary_interface->ir_addr)->sa_family)
		{
			if (primary_interface->ir_name[0] != '\0')
			{
				struct interface_req ir;
				if (!parse_interface (((struct sockaddr*)&recv_gsr->gsr_group)->sa_family, primary_interface->ir_name, &ir, error))
				{
					g_prefix_error (error,
							_("Unique address cannot be determined for interface \"%s\": "),
							primary_interface->ir_name);
					g_free (recv_gsr);
					g_free (primary_interface);
					return FALSE;
				}

				recv_gsr->gsr_interface = ir.ir_interface;
				if (AF_INET6 == pgm_sockaddr_family (&ir.ir_addr))
					((struct sockaddr_in6*)&recv_gsr->gsr_group)->sin6_scope_id = pgm_sockaddr_scope_id (&ir.ir_addr);
			}
		}

/* ASM: source = group */
		memcpy (&recv_gsr->gsr_source, &recv_gsr->gsr_group, pgm_sockaddr_len (&recv_gsr->gsr_group));
		*recv_list = g_list_append (*recv_list, recv_gsr);
		++j;
	}

	g_strfreev (tokens);
	g_free (primary_interface);
	return TRUE;
}

static inline
gboolean
parse_send_entity (
	int			family,		/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*		entity,		/* null = empty entity */
	GList**			interface_list,	/* <struct interface_req*> */
	GList**			recv_list,	/* <struct group_source_req*> */
	GList**			send_list,	/* <struct group_source_req*> */
	GError**		error
	)
{
/* pre-conditions */
	g_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	g_assert (NULL != recv_list);
	g_assert (NULL != *recv_list);
	g_assert (NULL != send_list);
	g_assert (NULL == *send_list);
	g_assert (NULL != error);

	g_trace ("parse_send_entity (family:%s entity:%s%s%s interface_list:%p recv_list:%p send_list:%p error:%p)",
		pgm_family_string (family),
		entity ? "\"":"", entity, entity ? "\"":"",
		(gpointer)interface_list,
		(gpointer)recv_list,
		(gpointer)send_list,
		(gpointer)error);

	struct group_source_req* send_gsr;
	const struct interface_req* primary_interface = (struct interface_req*)(*interface_list)->data;

	if (entity == NULL)
	{
		send_gsr = g_memdup ((*recv_list)->data, sizeof(struct group_source_req));
		*send_list = g_list_append (*send_list, send_gsr);
		return TRUE;
	}

/* default send object */
	send_gsr = g_new0 (struct group_source_req, 1);
	send_gsr->gsr_interface = primary_interface->ir_interface;
	if (!parse_group (family, entity, (struct sockaddr*)&send_gsr->gsr_group, error))
	{
		g_prefix_error (error,
				_("Unresolvable send entity \"%s\": "),
				entity);
		g_free (send_gsr);
		return FALSE;
	}

/* check if we can now resolve the source interface by address family of the send group */
	if (AF_UNSPEC == ((const struct sockaddr*)&primary_interface->ir_addr)->sa_family)
	{
		if (primary_interface->ir_name[0] != '\0')
		{
			struct interface_req ir;
			if (!parse_interface (((struct sockaddr*)&send_gsr->gsr_group)->sa_family, primary_interface->ir_name, &ir, error))
			{
				g_prefix_error (error,
						_("Unique address cannot be determined for interface \"%s\": "),
						primary_interface->ir_name);
				g_free (send_gsr);
				return FALSE;
			}

			send_gsr->gsr_interface = ir.ir_interface;
			if (AF_INET6 == pgm_sockaddr_family (&ir.ir_addr))
				((struct sockaddr_in6*)&send_gsr->gsr_group)->sin6_scope_id = pgm_sockaddr_scope_id (&ir.ir_addr);
		}
	}

/* ASM: source = group */
	memcpy (&send_gsr->gsr_source, &send_gsr->gsr_group, pgm_sockaddr_len (&send_gsr->gsr_group));
	*send_list = g_list_append (*send_list, send_gsr);
	return TRUE;
}

/* parse network parameter
 *
 * interface list; receive multicast group list; send multicast group
 *
 * TODO: reply with linked list of devices & groups.
 * TODO: split receive/send interfaces, ensure matching interface to multicast group.
 *
 * TODO: create function to determine whether this host is default IPv4 or IPv6 and use that instead of IPv4.
 */

#define IS_HOSTNAME(x) ( 				/* RFC 952 */ \
				isalnum(x) || \
				((x) == '-') || \
				((x) == '.') \
			)
#define IS_IP(x) ( \
				isdigit(x) || \
				((x) == '.') || \
				((x) == '/') \
			)
#define IS_IP6(x) ( \
				isxdigit(x) || \
				((x) == ':') || \
				((x) == '/') || \
				((x) == '.') \
			)
/* e.g. fe80::1%eth0.620    vlan tag,
 *      fe80::1%eth0:0      IP alias
 *      fe80::1%qe0_0       Solaris link name
 *
 * The Linux kernel generally doesn't care too much, but everything else falls apart with
 * random characters in interface names.  Hyphen is a popular problematic character.
 */
#define IS_IP6_WITH_ZONE(x) ( \
				IS_IP6(x) || \
				((x) == '%') || \
				isalpha(x) || \
				((x) == '_') \
			    )
#define IS_NETPARAM(x) ( \
				((x) == ',') || \
				((x) == ';') \
			)

static inline
gboolean
is_network_char (
	const int		family,
	const char		c
	)
{
	if (IS_HOSTNAME(c) ||
	    (AF_INET == family && IS_IP(c)) ||
	    ((AF_INET6 == family || AF_UNSPEC == family) && IS_IP6_WITH_ZONE(c)) ||
	    IS_NETPARAM(c))
		return TRUE;
	else
		return FALSE;
}

static
gboolean
network_parse (
	const char*		network,		/* NULL terminated */
	int			family,			/* AF_UNSPEC | AF_INET | AF_INET6 */
	GList**			recv_list,		/* <struct group_source_req*> */
	GList**			send_list,		/* <struct group_source_req*> */
	GError**		error
	)
{
	gboolean retval = FALSE;
	const char *p = network;
	const char *e = p + strlen(network);
	enum { ENTITY_INTERFACE, ENTITY_RECEIVE, ENTITY_SEND, ENTITY_ERROR } ec = ENTITY_INTERFACE;
	const char *b = p;		/* begin of entity */
	GList* source_list = NULL;
	GError* sub_error = NULL;

/* pre-conditions */
	g_assert (NULL != network);
	g_assert (AF_UNSPEC == family || AF_INET == family || AF_INET6 == family);
	g_assert (NULL != recv_list);
	g_assert (NULL != send_list);

	g_trace ("network_parse (network:\"%s\" family:%s recv_list:%p send_list:%p error:%p)",
		network, pgm_family_string (family), (gpointer)recv_list, (gpointer)send_list, (gpointer)error);

	while (p < e)
	{
		if (!is_network_char (family, *p))
		{
			g_set_error (error,
				     PGM_IF_ERROR,
				     PGM_IF_ERROR_INVAL,
				     _("'%c' is not a valid character."),
				     *p);
			goto free_lists;
		}

		if (*p == ';')		/* end of entity */
		{
			if (b == p)	/* empty entity */
			{
				switch (ec++) {
				case ENTITY_INTERFACE:
					retval = parse_interface_entity (family, NULL, &source_list, error);
					break;

				case ENTITY_RECEIVE:
					retval = parse_receive_entity (family, NULL, &source_list, recv_list, error);
					break;

				case ENTITY_SEND:
					retval = parse_send_entity (family, NULL, &source_list, recv_list, send_list, error);
					break;

				default:
					g_assert_not_reached();
					break;
				}

				if (!retval)
					goto free_lists;

				b = ++p;
				continue;
			}

/* entity from b to p-1 */
			char entity[1024];
			strncpy (entity, b, sizeof(entity));
			entity[p - b] = 0;

			switch (ec++) {
			case ENTITY_INTERFACE:
				if (parse_interface_entity (family, entity, &source_list, &sub_error))
					break;
				if (!(sub_error && PGM_IF_ERROR_XDEV == sub_error->code))
				{
/* fall through on multicast */
					if (!(sub_error && PGM_IF_ERROR_NOTUNIQ == sub_error->code))
					{
						g_propagate_error (error, sub_error);
						goto free_lists;
					}
					g_clear_error (&sub_error);
/* FIXME: too many interfaces */
					if (g_list_length (source_list) > 1) {
						g_set_error (error,
				   			     PGM_IF_ERROR,
							     PGM_IF_ERROR_INVAL,
							     _("Send group list contains more than one entity."));
						goto free_lists;
					}
					break;
				}
				g_clear_error (&sub_error);
				while (source_list) {
					g_free (source_list->data);
					source_list = g_list_delete_link (source_list, source_list);
				}
				if (!parse_interface_entity (family, NULL, &source_list, &sub_error) &&
				    !(sub_error && PGM_IF_ERROR_NOTUNIQ == sub_error->code))
				{
					g_propagate_error (error, sub_error);
					goto free_lists;
				}
				g_clear_error (&sub_error);
				ec++;

			case ENTITY_RECEIVE:
				if (!parse_receive_entity (family, entity, &source_list, recv_list, error))
					goto free_lists;
				break;

			case ENTITY_SEND:
				if (!parse_send_entity (family, entity, &source_list, recv_list, send_list, error))
					goto free_lists;
				break;

			default:
				g_assert_not_reached();
				break;
			}

			b = ++p;
			continue;
		}

		p++;
	}

	if (b < e) {
		switch (ec++) {
		case ENTITY_INTERFACE:
			if (parse_interface_entity (family, b, &source_list, &sub_error))
				break;
			if (!(sub_error && PGM_IF_ERROR_XDEV == sub_error->code))
			{
/* fall through on multicast */
				if (!(sub_error && PGM_IF_ERROR_NOTUNIQ == sub_error->code))
				{
					g_propagate_error (error, sub_error);
					goto free_lists;
				}
				g_clear_error (&sub_error);

/* FIXME: too many interfaces */
				if (g_list_length (source_list) > 1) {
					g_set_error (error,
			   			     PGM_IF_ERROR,
						     PGM_IF_ERROR_INVAL,
						     _("Send group list contains more than one entity."));
					goto free_lists;
				}
				break;
			}
			g_clear_error (&sub_error);
			while (source_list) {
				g_free (source_list->data);
				source_list = g_list_delete_link (source_list, source_list);
			}
			if (!parse_interface_entity (family, NULL, &source_list, &sub_error) &&
			    !(sub_error && PGM_IF_ERROR_NOTUNIQ == sub_error->code))
			{
				g_propagate_error (error, sub_error);
				goto free_lists;
			}
			ec++;

		case ENTITY_RECEIVE:
			if (!parse_receive_entity (family, b, &source_list, recv_list, error))
				goto free_lists;
			break;

		case ENTITY_SEND:
			if (!parse_send_entity (family, b, &source_list, recv_list, send_list, error))
				goto free_lists;
			break;

		default:
			g_assert_not_reached();
			break;
		}
	}

	while (ec <= ENTITY_SEND)
	{
		switch (ec++) {
		case ENTITY_INTERFACE:
			if (!parse_interface_entity (family, NULL, &source_list, error))
				goto free_lists;
			break;

		case ENTITY_RECEIVE:
			if (!parse_receive_entity (family, NULL, &source_list, recv_list, error))
				goto free_lists;
			break;

		case ENTITY_SEND:
			if (!parse_send_entity (family, NULL, &source_list, recv_list, send_list, error))
				goto free_lists;
			break;

		default:
			g_assert_not_reached();
			break;
		}
	}

	if (g_list_length (source_list) > 1)
		goto free_lists;

/* cleanup source interface list */
	while (source_list) {
		g_free (source_list->data);
		source_list = g_list_delete_link (source_list, source_list);
	}

	return TRUE;

free_lists:
	while (source_list) {
		g_free (source_list->data);
		source_list = g_list_delete_link (source_list, source_list);
	}
	while (*recv_list) {
		g_free ((*recv_list)->data);
		*recv_list = g_list_delete_link (*recv_list, *recv_list);
	}
	while (*send_list) {
		g_free ((*send_list)->data);
		*send_list = g_list_delete_link (*send_list, *send_list);
	}
	return FALSE;
}

/* create group_source_req as used by pgm_transport_create which specify port, address & interface.
 * gsr_source is copied from gsr_group for ASM, caller needs to populate gsr_source for SSM.
 *
 * returns 0 on success, returns -EINVAL on invalid input, returns -ENOMEM when out of memory.
 */

gboolean
pgm_if_get_transport_info (
	const char*					network,
	const struct pgm_transport_info_t* const	hints,
	struct pgm_transport_info_t**			res,
	GError**					error
	)
{
	struct pgm_transport_info_t* ti;
	int family = hints ? hints->ti_family : AF_UNSPEC;
	GList* recv_list = NULL;	/* <struct group_source_req*> */
	GList* send_list = NULL;	/* <struct group_source_req*> */

	g_return_val_if_fail (NULL != network, FALSE);
	g_return_val_if_fail (AF_UNSPEC == family || AF_INET == family || AF_INET6 == family, FALSE);
	g_return_val_if_fail (NULL != res, FALSE);

	if (hints) {
		g_trace ("get_transport_info (network:\"%s\" hints: {family:%s} res:%p error:%p)",
			network, pgm_family_string (family), (gpointer)res, (gpointer)error);
	} else {
		g_trace ("get_transport_info (network:\"%s\" hints:%p res:%p error:%p)",
			network, (gpointer)hints, (gpointer)res, (gpointer)error);
	}

	if (!network_parse (network, family, &recv_list, &send_list, error))
		return FALSE;
	const int recv_list_len = g_list_length (recv_list);
	const int send_list_len = g_list_length (send_list);
	ti = g_malloc0 (sizeof(struct pgm_transport_info_t) + 
			 (recv_list_len + send_list_len) * sizeof(struct group_source_req));
	ti->ti_recv_addrs_len = recv_list_len;
	ti->ti_recv_addrs = (gpointer)((guint8*)ti + sizeof(struct pgm_transport_info_t));
	ti->ti_send_addrs_len = send_list_len;
	ti->ti_send_addrs = (gpointer)((guint8*)ti->ti_recv_addrs + recv_list_len * sizeof(struct group_source_req));
			
	gsize i = 0;
	while (recv_list) {
		memcpy (&ti->ti_recv_addrs[i++], recv_list->data, sizeof(struct group_source_req));
		g_free (recv_list->data);
		recv_list = g_list_delete_link (recv_list, recv_list);
	}
	i = 0;
	while (send_list) {
		memcpy (&ti->ti_send_addrs[i++], send_list->data, sizeof(struct group_source_req));
		g_free (send_list->data);
		send_list = g_list_delete_link (send_list, send_list);
	}
	*res = ti;
	return TRUE;
}

void
pgm_if_free_transport_info (
	struct pgm_transport_info_t*	res
	)
{
	g_free (res);
}

GQuark
pgm_if_error_quark (void)
{
	return g_quark_from_static_string ("pgm-if-error-quark");
}

PGMIFError
pgm_if_error_from_errno (
	gint		err_no
	)
{
	switch (err_no) {
#ifdef EFAULT
	case EFAULT:
		return PGM_IF_ERROR_FAULT;
		break;
#endif

#ifdef EINVAL
	case EINVAL:
		return PGM_IF_ERROR_INVAL;
		break;
#endif

#ifdef EPERM
	case EPERM:
		return PGM_IF_ERROR_PERM;
		break;
#endif

	default :
		return PGM_IF_ERROR_FAILED;
		break;
	}
}

/* h_errno from gethostbyname.
 */

PGMIFError
pgm_if_error_from_h_errno (
	gint		err_no
	)
{
	switch (err_no) {
#ifdef HOST_NOT_FOUND
	case HOST_NOT_FOUND:
		return PGM_IF_ERROR_NONAME;
		break;
#endif

#ifdef TRY_AGAIN
	case TRY_AGAIN:
		return PGM_IF_ERROR_AGAIN;
		break;
#endif

#ifdef NO_RECOVERY
	case NO_RECOVERY:
		return PGM_IF_ERROR_FAIL;
		break;
#endif

#ifdef NO_DATA
	case NO_DATA:
		return PGM_IF_ERROR_NODATA;
		break;
#endif

	default:
		return PGM_IF_ERROR_FAILED;
		break;
	}
}

/* errno must be preserved before calling to catch correct error
 * status with EAI_SYSTEM.
 */

PGMIFError
pgm_if_error_from_eai_errno (
	gint		err_no
	)
{
	switch (err_no) {
#ifdef EAI_ADDRFAMILY
	case EAI_ADDRFAMILY:
		return PGM_IF_ERROR_ADDRFAMILY;
		break;
#endif

#ifdef EAI_AGAIN
	case EAI_AGAIN:
		return PGM_IF_ERROR_AGAIN;
		break;
#endif

#ifdef EAI_BADFLAGS
	case EAI_BADFLAGS:
		return PGM_IF_ERROR_BADFLAGS;
		break;
#endif

#ifdef EAI_FAIL
	case EAI_FAIL:
		return PGM_IF_ERROR_FAIL;
		break;
#endif

#ifdef EAI_FAMILY
	case EAI_FAMILY:
		return PGM_IF_ERROR_FAMILY;
		break;
#endif

#ifdef EAI_MEMORY
	case EAI_MEMORY:
		return PGM_IF_ERROR_MEMORY;
		break;
#endif

#ifdef EAI_NODATA
	case EAI_NODATA:
		return PGM_IF_ERROR_NODATA;
		break;
#endif

#ifdef EAI_NONAME
	case EAI_NONAME:
		return PGM_IF_ERROR_NONAME;
		break;
#endif

#ifdef EAI_SERVICE
	case EAI_SERVICE:
		return PGM_IF_ERROR_SERVICE;
		break;
#endif

#ifdef EAI_SOCKTYPE
	case EAI_SOCKTYPE:
		return PGM_IF_ERROR_SOCKTYPE;
		break;
#endif

#ifdef EAI_SYSTEM
	case EAI_SYSTEM:
		return pgm_if_error_from_errno (errno);
		break;
#endif

	default :
		return PGM_IF_ERROR_FAILED;
		break;
	}
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
