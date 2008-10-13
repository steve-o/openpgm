/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * network interface handling.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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
#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>

#include "pgm/if.h"

#define IF_DEBUG

#ifndef IF_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* globals */

#define IF_DEFAULT_GROUP	((in_addr_t) 0xefc00001) /* 239.192.0.1 */

/* ff08::1 */
#define IF6_DEFAULT_INIT { { { 0xff,8,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }
const struct in6_addr if6_default_group = IF6_DEFAULT_INIT;


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

int
pgm_if_print_all (void)
{
	struct ifaddrs *ifap, *ifa;

	int e = getifaddrs (&ifap);
	if (e < 0) {
		perror("getifaddrs");
		return -1;
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		if ( ifa->ifa_addr->sa_family != AF_INET && 
			ifa->ifa_addr->sa_family != AF_INET6)
		{
			continue;
		}

		char s[INET6_ADDRSTRLEN];
		inet_ntop (ifa->ifa_addr->sa_family,
				ifa->ifa_addr->sa_family == AF_INET ?
					(struct sockaddr*)&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr :
					(struct sockaddr*)&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr,
				s,
				sizeof(s));
		g_message ("name %-5.5s IPv%i %-46.46s status %s loop %s b/c %s m/c %s",
			ifa->ifa_name,
			ifa->ifa_addr->sa_family == AF_INET ? 4 : 6,
			s,
			ifa->ifa_flags & IFF_UP ? "UP  " : "DOWN",
			ifa->ifa_flags & IFF_LOOPBACK ? "YES" : "NO ",
			ifa->ifa_flags & IFF_BROADCAST ? "YES" : "NO ",
			ifa->ifa_flags & IFF_MULTICAST ? "YES" : "NO "
			);
	}

	freeifaddrs (ifap);
	return 0;
}

/* 127		=> 127.0.0.0
 * 127.1/8	=> 127.0.0.0
 */

int
pgm_if_inet_network (
	const char* s,
	struct in_addr* in
	)
{
	g_return_val_if_fail (s != NULL, -EINVAL);

	g_trace ("if_inet_network (\"%s\")", s);
	in->s_addr = INADDR_ANY;

	const char *p = s;
	const char *e = p + strlen(s);
	int val = 0;
	int shift = 24;

	while (p <= e)
	{
		if (isdigit(*p)) {
			val = 10 * val + (*p - '0');
		} else if (*p == '.' || *p == 0) {
			if (val > 0xff) {
				in->s_addr = INADDR_NONE;
				return -1;
			}

//g_trace ("elem %i", val);
			
			in->s_addr |= val << shift;
			val = 0;
			shift -= 8;
			if (shift < 0 && *p != 0) {
				in->s_addr = INADDR_NONE;
				return -1;
			}

		} else if (*p == '/') {
			if (val > 0xff) {
				in->s_addr = INADDR_NONE;
				return -1;
			}
//g_trace ("elem %i", val);
			in->s_addr |= val << shift;
			p++; val = 0;
			while (p < e)
			{
				if (isdigit(*p)) {
					val = 10 * val + (*p - '0');
				} else {
					in->s_addr = INADDR_NONE;
					return -1;
				}
				p++;
			}
			if (val == 0 || val > 32) {
				in->s_addr = INADDR_NONE;
				return -1;
			}
//g_trace ("bit mask %i", val);

/* zero out host bits */
			in->s_addr = htonl(in->s_addr);
			while (val < 32) {
//g_trace ("s_addr=%s &= ~(1 << %i)", inet_ntoa(*in), val);
				in->s_addr &= ~(1 << val++);
			}
			return 0;
		
		} else if (*p == 'x' || *p == 'X') {	/* skip number, e.g. 1.x.x.x */
			if (val > 0) {	
				in->s_addr = INADDR_NONE;
				return -1;
			}
			
		} else {
			in->s_addr = INADDR_NONE;
			return -1;
		}
		p++;
	}

	in->s_addr = htonl(in->s_addr);

	return 0;
}

/* ::1/128	=> 0:0:0:0:0:0:0:1
 * ::1          => 0:0:0:0:0:0:0:1
 * ::1.2.3.4	=> 0:0:0:0:1.2.3.4
 */

int
pgm_if_inet6_network (
	const char* s,
	struct in6_addr* in6
	)
{
	g_return_val_if_fail (s != NULL, -EINVAL);

	g_trace ("if_inet6_network (\"%s\")", s);

	char s2[INET6_ADDRSTRLEN];
	const char *p = s;
	char* p2 = s2;
	const char *e = p + strlen(s);
	while (*p) {
		if (*p == '/') break;
		*p2++ = *p++;
	}
	if (p == e) {
		if (inet_pton (AF_INET6, s, in6)) return 0;
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}

//	g_trace ("net part %s", s2);
	if (!inet_pton (AF_INET6, s2, in6)) {
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}

#ifdef IF_DEBUG
	char sdebug[INET6_ADDRSTRLEN];
	g_trace ("IPv6 network address: %s", inet_ntop(AF_INET6, in6, sdebug, sizeof(sdebug)));
#endif

	p++;
	int val = 0;
	while (p < e)
	{
		if (isdigit(*p)) {
			val = 10 * val + (*p - '0');
		} else {
			memcpy (in6, &in6addr_any, sizeof(in6addr_any));
			return -1;
		}
		p++;
	}
	if (val == 0 || val > 128) {
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}
//	g_trace ("bit mask %i", val);

/* zero out host bits */
	while (val < 128) {
		int byte_index = val / 8;
		int bit_index  = val % 8;

		in6->in6_u.u6_addr8[byte_index] &= ~(1 << bit_index);
		val++;
	}

	g_trace ("IPv6 network address: %s", inet_ntop(AF_INET6, in6, s2, sizeof(s2)));

	return 0;
}

/* parse if name/address
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
 */

int
pgm_if_parse_interface (
	const char*		s,
	int			ai_family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	struct group_req*	interface
	)
{
	g_return_val_if_fail (s != NULL, -EINVAL);

	g_trace ("if_parse_interface (\"%s\", %s [%i])", 
		s, 
		ai_family == AF_UNSPEC ? "AF_UNSPEC" :
			( ai_family == AF_INET ? "AF_INET" :
				( ai_family == AF_INET6 ? "AF_INET6" : "UNKNOWN" )
			),
		ai_family);

	int retval = 0;
	struct ifaddrs *ifap, *ifa;
	struct sockaddr* sa_interface = (struct sockaddr*)&interface->gr_group;

	int e = getifaddrs (&ifap);
	if (e < 0) {
		g_critical ("getifaddrs failed: %s", strerror(e));
		return -EINVAL;
	}

/* search for interface name */
	int interface_matches = 0;
	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
/* ignore raw entries */
		if ( ifa->ifa_addr->sa_family == AF_PACKET ) continue;

		if (	ifa->ifa_addr->sa_family != AF_INET && 
			ifa->ifa_addr->sa_family != AF_INET6	)
		{
/* warn if not an IP interface */
			if ( strcmp (s, ifa->ifa_name ) == 0 )
			{
				g_warning ("%s: not an IP interface, sa_family=0x%x", ifa->ifa_name, ifa->ifa_addr->sa_family);

				retval = -EINVAL;
				goto out;
			}

/* just ignore other non-IP interfaces */
			continue;
		}

/* we have an IP interface, check its name, IP and network addresses */
		if ( strcmp (s, ifa->ifa_name ) == 0 )
		{
			int i = if_nametoindex (ifa->ifa_name);
			if (i > 0) {
#ifdef IF_DEBUG
				if (ai_family == AF_UNSPEC) {
					g_trace ("match on interface #%i, %s %s [%i]",
							i, ifa->ifa_name,
							ifa->ifa_addr->sa_family == AF_INET ? "AF_INET" : "AF_INET6",
							ifa->ifa_addr->sa_family);
				} else if (ai_family == ifa->ifa_addr->sa_family) {
					g_trace ("match on interface #%i, %s", i, ifa->ifa_name);
				} else {
					continue;
				}
#endif /* IF_DEBUG */

				interface_matches++;
				interface->gr_interface = i;
				memcpy (&interface->gr_group, ifa->ifa_addr, sizeof(struct sockaddr_storage));

/* check for multiple interfaces */
				continue;
			}

			retval = -EINVAL;
			goto out;
		}
	}

	if (interface_matches == 1) {
		retval = 0;
		goto out;
	} else if (interface_matches > 1) {
		retval = -ERANGE;
		goto out;
	}

/* check if a valid ipv4 or ipv6 address */
	struct sockaddr addr;
	int valid_ipv4 = 0, valid_ipv6 = 0;
	int valid_net4 = 0, valid_net6 = 0;

	if (inet_pton (AF_INET, s, &((struct sockaddr_in*)&addr)->sin_addr))
	{
		valid_ipv4 = 1;
	}
	else if (inet_pton (AF_INET6, s, &((struct sockaddr_in6*)&addr)->sin6_addr))
	{
		valid_ipv6 = 1;
	}

/* IPv6 friendly version??? */
	struct in_addr in;
#if 0
	in.s_addr = inet_network (s);
	if (in.s_addr != -1) {
		g_trace ("network address calculated: %s", inet_ntoa (in));
	}
#else
	e = pgm_if_inet_network (s, &in);
	if (e != -1) {
		g_trace ("IPv4 network address calculated: %s", inet_ntoa (in));
		valid_net4 = 1;
	}

	struct in6_addr in6;
	e = pgm_if_inet6_network (s, &in6);
	if (e != -1) {
#ifdef IF_DEBUG
		char sdebug[INET6_ADDRSTRLEN];
		g_trace ("IPv6 network address calculated: %s", inet_ntop(AF_INET6, &in6, sdebug, sizeof(sdebug)));
#endif
		valid_net6 = 1;
	}

#endif

	if (! (valid_ipv4 || valid_ipv6 || valid_net4 || valid_net6) )
	{

/* check IP NSS networks for a network name */
		struct netent* ne = getnetbyname (s);
		if (ne) {
			switch (ne->n_addrtype) {
			case AF_INET:
			{
				g_trace ("found IPv4 network by NSS: %s", ne->n_name);
				((struct sockaddr_in*)&addr)->sin_addr.s_addr = ne->n_net;
				g_trace ("address %s", inet_ntoa (((struct sockaddr_in*)&addr)->sin_addr));
				valid_ipv4 = 1;
			}
			break;

			case AF_INET6:
			{
				g_trace ("found IPv6 network by NSS: %s", ne->n_name);
				memcpy (&((struct sockaddr_in6*)&addr)->sin6_addr,
					&ne->n_net,
					sizeof(struct in6_addr));
#ifdef IF_DEBUG
				char sdebug[INET6_ADDRSTRLEN];
				g_trace ("address %s", inet_ntop (ne->n_addrtype, &((struct sockaddr_in6*)&addr)->sin6_addr, sdebug, sizeof(sdebug)));
#endif
				valid_ipv6 = 1;
			}
			break;

			default:
				g_warning ("unknown network address type %i from getnetbyname().", ne->n_addrtype);
				break;
			}
		}
	}

	if (! (valid_ipv4 || valid_ipv6 || valid_net4 || valid_net6) )
	{

/* DNS resolve to see if valid hostname */
		struct addrinfo hints;
		struct addrinfo *res = NULL;

		memset (&hints, 0, sizeof(hints));
		hints.ai_family = ai_family;
/*		hints.ai_protocol = IPPROTO_PGM; */
/*		hints.ai.socktype = SOCK_RAW; */
		hints.ai_flags  = AI_ADDRCONFIG | AI_CANONNAME; /* AI_V4MAPPED is probably stupid here */
		int err = getaddrinfo (s, NULL, &hints, &res);

		if (!err) {
			switch (res->ai_family) {
			case AF_INET:
				((struct sockaddr_in*)&addr)->sin_addr = ((struct sockaddr_in*)(res->ai_addr))->sin_addr;
				valid_ipv4 = 1;
				break;

			case AF_INET6:
				((struct sockaddr_in6*)&addr)->sin6_addr = ((struct sockaddr_in6*)(res->ai_addr))->sin6_addr;
				valid_ipv6 = 1;
				break;

			default:
				g_trace ("getaddrinfo returned %i on ai_family", res->ai_family);
				g_assert_not_reached();
			}

			freeaddrinfo (res);
		} else {
			return -EINVAL;
		}
	}

/* iterate through interface list again to match ip or net address */
	if (valid_ipv4 || valid_ipv6 || valid_net4 || valid_net6)
	{
		for (ifa = ifap; ifa; ifa = ifa->ifa_next)
		{
			switch (ifa->ifa_addr->sa_family) {
			case AF_INET:
				if (valid_ipv4 &&
					((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr == ((struct sockaddr_in*)&addr)->sin_addr.s_addr )
				{
					g_trace ("IPv4 address on %i:%s",
							if_nametoindex (ifa->ifa_name),
							ifa->ifa_name );

/* copy interface ip address */
					interface->gr_interface = if_nametoindex (ifa->ifa_name);
					sa_interface->sa_family = ifa->ifa_addr->sa_family;
					((struct sockaddr_in*)sa_interface)->sin_port = 0;
					((struct sockaddr_in*)sa_interface)->sin_addr.s_addr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;
					retval = 0;
					goto out;
				}

/* check network address */
				if (valid_net4)
				{
					struct in_addr netaddr;
					netaddr.s_addr = ((struct sockaddr_in*)ifa->ifa_netmask)->sin_addr.s_addr &
							((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;

					if (in.s_addr == netaddr.s_addr)
					{
						g_trace ("IPv4 net address on %i:%s",
							if_nametoindex (ifa->ifa_name),
							ifa->ifa_name );

						interface->gr_interface = if_nametoindex (ifa->ifa_name);
						sa_interface->sa_family = AF_INET;
						((struct sockaddr_in*)sa_interface)->sin_port = 0;
						((struct sockaddr_in*)sa_interface)->sin_addr.s_addr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;
						retval = 0;
						goto out;
					}
				}
				break;

			case AF_INET6:
				if (valid_ipv6 &&
					memcmp (&((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr,
						&((struct sockaddr_in6*)&addr)->sin6_addr,
						sizeof(struct in6_addr)) == 0)
				{
					g_trace ("IPv6 address on %i:%s",
							if_nametoindex (ifa->ifa_name),
							ifa->ifa_name );

					interface->gr_interface = if_nametoindex (ifa->ifa_name);
					sa_interface->sa_family = AF_INET6;
					((struct sockaddr_in6*)sa_interface)->sin6_port = 0;
					((struct sockaddr_in6*)sa_interface)->sin6_flowinfo = 0;
					((struct sockaddr_in6*)sa_interface)->sin6_addr = ((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
					((struct sockaddr_in6*)sa_interface)->sin6_scope_id = 0;
					retval = 0;
					goto out;
				}

/* check network address */
				if (valid_net6)
				{
					struct in6_addr ipaddr6  = ((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
					struct in6_addr netaddr6 = ((struct sockaddr_in6*)ifa->ifa_netmask)->sin6_addr;

					int invalid = 0;
					for (int i = 0; i < 16; i++)
					{
						if ((netaddr6.s6_addr[i] & ipaddr6.s6_addr[i]) != in6.s6_addr[i])
						{
							invalid = 1;
							break;
						}
					}
					if (!invalid)
					{
						g_trace ("IPv6 net address on %i:%s",
							if_nametoindex (ifa->ifa_name),
							ifa->ifa_name );

						interface->gr_interface = if_nametoindex (ifa->ifa_name);
						sa_interface->sa_family = AF_INET6;
						((struct sockaddr_in6*)sa_interface)->sin6_port = 0;
						((struct sockaddr_in6*)sa_interface)->sin6_flowinfo = 0;
						((struct sockaddr_in6*)sa_interface)->sin6_addr = ((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
						((struct sockaddr_in6*)sa_interface)->sin6_scope_id = 0;
						retval = 0;
						goto out;
					}
				}
				break;

			default: continue;
			}
		}
	}

	retval = -EINVAL;

out:

/* cleanup after getifaddrs() */
	freeifaddrs (ifap);

	return retval;
}

/* parse one multicast address
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

int
pgm_if_parse_multicast (
	const char*		s,
	int			ai_family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	struct sockaddr*	addr
	)
{
	g_return_val_if_fail (s != NULL, -EINVAL);

	g_trace ("if_parse_multicast (\"%s\", %s [%i])", 
		s, 
		ai_family == AF_UNSPEC ? "AF_UNSPEC" :
			( ai_family == AF_INET ? "AF_INET" :
				( ai_family == AF_INET6 ? "AF_INET6" : "UNKNOWN" )
			),
		ai_family);

	int retval = 0;

/* IPv4 address */
	if (inet_pton (AF_INET, s, &((struct sockaddr_in*)addr)->sin_addr))
	{
		addr->sa_family = AF_INET;
		
		if (IN_MULTICAST(htonl(((struct sockaddr_in*)addr)->sin_addr.s_addr)))
		{
			g_trace ("IPv4 multicast: %s", s);
		}
		else
		{
			g_trace ("IPv4 unicast: %s", s);
			retval = -EINVAL;
		}
	}
	else if (inet_pton (AF_INET6, s, &((struct sockaddr_in6*)addr)->sin6_addr))
	{
		addr->sa_family = AF_INET6;

		if (IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)addr)->sin6_addr))
		{
			g_trace ("IPv6 multicast: %s", s);
		}
		else
		{
			g_trace ("IPv6 unicast: %s", s);
			retval = -EINVAL;
		}
	}
	else
	{
/* try to resolve the name instead */
		struct addrinfo hints;
		struct addrinfo *res = NULL;

		memset (&hints, 0, sizeof(hints));
		hints.ai_family = ai_family;
/*		hints.ai_protocol = IPPROTO_PGM; */
/*		hints.ai.socktype = SOCK_RAW; */
		hints.ai_flags  = AI_ADDRCONFIG | AI_CANONNAME; /* AI_V4MAPPED is probably stupid here */
		int err = getaddrinfo (s, NULL, &hints, &res);

		if (!err) {
#ifdef IF_DEBUG
			char s2[INET6_ADDRSTRLEN];
			g_trace ("DNS hostname: (A) %s address %s",
				res->ai_canonname,
				inet_ntop (res->ai_family, 
						res->ai_family == AF_INET ?
							(const void*)&((struct sockaddr_in*)(res->ai_addr))->sin_addr :
							(const void*)&((struct sockaddr_in6*)(res->ai_addr))->sin6_addr,
						s2, sizeof(s2)) );
#endif

			if (res->ai_family == AF_INET)
			{
				addr->sa_family = AF_INET;

				if (IN_MULTICAST(htonl(((struct sockaddr_in*)(res->ai_addr))->sin_addr.s_addr)))
				{
					g_trace ("IPv4 multicast");
					((struct sockaddr_in*)addr)->sin_addr.s_addr = 
						((struct sockaddr_in*)(res->ai_addr))->sin_addr.s_addr;
				}
				else
				{
					g_trace ("IPv4 unicast");
					retval = -EINVAL;
				}
			}
			else
			{
				addr->sa_family = AF_INET6;

				if (IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)res->ai_addr)->sin6_addr))
				{
					g_trace ("IPv6 multicast");

					((struct sockaddr_in6*)addr)->sin6_port = 0;
					((struct sockaddr_in6*)addr)->sin6_flowinfo = 0;
					memcpy (&((struct sockaddr_in6*)addr)->sin6_addr,
						&((struct sockaddr_in6*)(res->ai_addr))->sin6_addr,
						sizeof(struct in6_addr));
					((struct sockaddr_in6*)addr)->sin6_scope_id = 0;
				}
				else
				{
					g_trace ("IPv6 unicast");
					retval = -EINVAL;
				}
			}
		}
		else
		{
			g_trace ("DNS resolution failed.");
			retval = -EINVAL;
		}

		freeaddrinfo (res);
	}

	return retval;
}

/* parse an interface entity from a network parameter.  for every multi-family interface return
 * the name through multiprotocol_list.
 *
 * examples:  "eth0",  "hme0,hme1".
 */
static
int
pgm_if_parse_entity_interface (
	const char*		s,		/* null = empty entity */
	int			ai_family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	struct group_req*	devices,	/* sockaddr + idx */
	GList**			multiprotocol_list
	)
{
	int retval = 0;

	if (s == NULL)
	{
		g_trace ("interface = (nul)");
		memset (&devices[0], 0, sizeof(struct group_req));
		((struct sockaddr*)&devices[0].gr_group)->sa_family = ai_family;
		goto out;
	}

	gchar** tokens = g_strsplit (s, ",", 10);
	int j = 0;
	while (tokens && tokens[j])
	{
		retval = pgm_if_parse_interface (tokens[j], ai_family, &devices[j]);
/* mark multiple interfaces for later decision based on group families */
		if (retval == -ERANGE) {
			((struct sockaddr*)&devices[j].gr_group)->sa_family = AF_BRIDGE;
		} else if (retval != 0) {
			g_strfreev (tokens);
			goto out;
		}

		*multiprotocol_list = g_list_prepend (*multiprotocol_list, g_strdup (tokens[j]));
		++j;
	}

	g_strfreev (tokens);
	((struct sockaddr*)&devices[j].gr_group)->sa_family = 0;

out:
	return retval;
}

static
int
pgm_if_parse_entity_receive (
	const char*		s,		/* null = empty entity */
	int			ai_family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	struct group_req*	devices,
	struct sockaddr*	receive_groups,
	GList**			multiprotocol_list
	)
{
	int retval = 0;

	if (s == NULL)
	{
		g_trace ("receive group = (nul)");
		switch (ai_family) {
		case AF_INET6:
			receive_groups[0].sa_family = ai_family;
			memcpy (&((struct sockaddr_in6*)&receive_groups[0])->sin6_addr,
				&if6_default_group,
				sizeof(struct in6_addr));
			break;

		case AF_INET:
		case AF_UNSPEC:	/* fixme */
			receive_groups[0].sa_family = AF_INET;
			((struct sockaddr_in*)&receive_groups[0])->sin_addr.s_addr = htonl(IF_DEFAULT_GROUP);
			break;

		default:
			g_assert_not_reached();
		}

		if (((struct sockaddr*)&devices[0].gr_group)->sa_family == 0)
		{
			g_trace ("re-evaluate default interface to family %s [%i]",
				receive_groups[0].sa_family == AF_INET ? "AF_INET" : "AF_INET6",
				receive_groups[0].sa_family);
			((struct sockaddr*)&devices[0].gr_group)->sa_family = receive_groups[0].sa_family;
		}

		if (*multiprotocol_list && ((struct sockaddr*)&devices[0].gr_group)->sa_family == AF_BRIDGE)
		{
			g_trace ("re-evaluate interface entity to family %s [%i]",
				receive_groups[0].sa_family == AF_INET ? "AF_INET" : "AF_INET6",
				receive_groups[0].sa_family);
			gchar* interface_token = ( g_list_last(*multiprotocol_list) )->data;
			retval = pgm_if_parse_interface (interface_token, receive_groups[0].sa_family, &devices[0]);
			g_free (interface_token);
			*multiprotocol_list = g_list_delete_link (*multiprotocol_list, g_list_last(*multiprotocol_list));
			if (retval != 0) {
				goto out;
			}
		}

		receive_groups[1].sa_family = 0;
		goto out;
	}

	gchar** tokens = g_strsplit (s, ",", 10);
	int j = 0;
	while (tokens && tokens[j])
	{
		retval = pgm_if_parse_multicast (tokens[j], ai_family, &receive_groups[j]);
		if (retval != 0) {
			g_strfreev (tokens);
			goto out;
		}

/* reparse interfaces if undecided */
		if (((struct sockaddr*)&devices[j].gr_group)->sa_family == 0)
		{
			g_trace ("re-evaluate default interface to family %s [%i]",
				receive_groups[j].sa_family == AF_INET ? "AF_INET" : "AF_INET6",
				receive_groups[j].sa_family);
			((struct sockaddr*)&devices[j].gr_group)->sa_family = receive_groups[j].sa_family;
		}

		if (*multiprotocol_list && ((struct sockaddr*)&devices[j].gr_group)->sa_family == AF_BRIDGE)
		{
			g_trace ("re-evaluate interface entity to family %s [%i]",
				receive_groups[j].sa_family == AF_INET ? "AF_INET" : "AF_INET6",
				receive_groups[j].sa_family);
			gchar* interface_token = ( g_list_last(*multiprotocol_list) )->data;
			retval = pgm_if_parse_interface (interface_token, receive_groups[j].sa_family, &devices[j]);
			g_free (interface_token);
			*multiprotocol_list = g_list_delete_link (*multiprotocol_list, g_list_last(*multiprotocol_list));
			if (retval != 0) {
				goto out;
			}
		}

		++j;
	}

	g_strfreev (tokens);
	receive_groups[j].sa_family = 0;

out:
	return retval;
}

static
int
pgm_if_parse_entity_send (
	const char*		s,		/* null = empty entity */
	int			ai_family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	struct sockaddr*	receive_groups,
	struct sockaddr*	send_group
	)
{
	int retval = 0;

	if (s == NULL)
	{
		g_trace ("send group = (nul)");
/* 2 or more receive groups defined */
		if (receive_groups[1].sa_family != 0)
		{
			g_critical ("send group needs to be explicitly defined when requesting multiple receive groups.");
			retval = -EINVAL;
			goto out;
		}

		send_group->sa_family = receive_groups[0].sa_family;
		switch (send_group->sa_family) {
		case AF_INET6:
			memcpy (&((struct sockaddr_in6*)send_group)->sin6_addr,
				&((struct sockaddr_in6*)&receive_groups[0])->sin6_addr,
				sizeof(struct in6_addr));
			break;

		case AF_INET:
			((struct sockaddr_in*)send_group)->sin_addr.s_addr =
				((struct sockaddr_in*)&receive_groups[0])->sin_addr.s_addr;
			break;

		default:
		case AF_UNSPEC:
			g_assert_not_reached();
		}

		goto out;
	}

	retval = pgm_if_parse_multicast (s, ai_family, send_group);

out:
	return retval;
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
#define IS_NETPARAM(x) ( \
				((x) == ',') || \
				((x) == ';') \
			)

int
pgm_if_parse_network (
	const char*		s,
	int			ai_family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	struct group_req*	devices,
	struct sockaddr*	receive_groups,
	struct sockaddr*	send_group,
	G_GNUC_UNUSED int	len			/* length of device & receive_groups fields */
	)
{
	g_trace ("if_parse_network (\"%s\", %s [%i], ..., %i)", 
		s, 
		ai_family == AF_UNSPEC ? "AF_UNSPEC" :
			( ai_family == AF_INET ? "AF_INET" :
				( ai_family == AF_INET6 ? "AF_INET6" : "UNKNOWN" )
			),
		ai_family, len);

	const char *p = s;
	const char *e = p + strlen(s);
	enum { ENTITY_INTERFACE, ENTITY_RECEIVE, ENTITY_SEND, ENTITY_ERROR } ec = ENTITY_INTERFACE;
	const char *b = p;		/* begin of entity */
	int retval = 0;

	GList* multiprotocol_list = NULL;

	while (p < e)
	{
		if (!IS_HOSTNAME(*p) && !IS_IP(*p) && !IS_IP6(*p) && !IS_NETPARAM(*p))
		{
			g_trace ("invalid character 0x%x", *p);
			retval = -EINVAL;
			goto out;
		}

		if (*p == ';')		/* end of entity */
		{
			if (b == p)	/* empty entity */
			{
				switch (ec++) {
				case ENTITY_INTERFACE:
					retval = pgm_if_parse_entity_interface (NULL, ai_family, devices, &multiprotocol_list);
					break;

				case ENTITY_RECEIVE:
					retval = pgm_if_parse_entity_receive (NULL, ai_family, devices, receive_groups, &multiprotocol_list);
					break;

				case ENTITY_SEND:
					retval = pgm_if_parse_entity_send (NULL, ai_family, receive_groups, send_group);
					break;

				default:
					g_assert_not_reached();
					break;
				}

				if (retval != 0) goto out;

				b = ++p;
				continue;
			}

			/* entity from b to p-1 */
			char dup[1024];
			strncpy (dup, b, sizeof(dup));

//			char *dup = malloc (p - b + 1);
//			strncpy (dup, b, p - b);

			dup[p - b] = 0;

			g_trace ("entity '%s'", dup);
			switch (ec++) {
			case ENTITY_INTERFACE:
				retval = pgm_if_parse_entity_interface (dup, ai_family, devices, &multiprotocol_list);
				if (!(retval == 0 || retval == -ERANGE)) goto out;
				break;

			case ENTITY_RECEIVE:
				retval = pgm_if_parse_entity_receive (dup, ai_family, devices, receive_groups, &multiprotocol_list);
				if (retval != 0) goto out;
				break;

			case ENTITY_SEND:
				retval = pgm_if_parse_entity_send (dup, ai_family, receive_groups, send_group);
				if (retval != 0) goto out;
				break;

			default:
				g_assert_not_reached();
				break;
			}
				
//			free (dup);

			b = ++p;
			continue;
		}

		p++;
	}

	if (b < e) {
		g_trace ("entity '%s'", b);
		switch (ec++) {
		case ENTITY_INTERFACE:
			retval = pgm_if_parse_entity_interface (b, ai_family, devices, &multiprotocol_list);
			if (!(retval == 0 || retval == -ERANGE)) goto out;
			break;

		case ENTITY_RECEIVE:
			retval = pgm_if_parse_entity_receive (b, ai_family, devices, receive_groups, &multiprotocol_list);
			if (retval != 0) goto out;
			break;

		case ENTITY_SEND:
			retval = pgm_if_parse_entity_send (b, ai_family, receive_groups, send_group);
			if (retval != 0) goto out;
			break;

		default:
			g_assert_not_reached();
			break;
		}
	}


	while (ec < (ENTITY_SEND+1))
	{
		switch (ec++) {
		case ENTITY_INTERFACE:
			retval = pgm_if_parse_entity_interface (NULL, ai_family, devices, &multiprotocol_list);
			break;

		case ENTITY_RECEIVE:
			retval = pgm_if_parse_entity_receive (NULL, ai_family, devices, receive_groups, &multiprotocol_list);
			break;

		case ENTITY_SEND:
			retval = pgm_if_parse_entity_send (NULL, ai_family, receive_groups, send_group);
			break;

		default:
			g_assert_not_reached();
			break;
		}

		if (retval != 0) goto out;
	}

out:

/* cleanup any list */
	while (multiprotocol_list) {
		g_free (multiprotocol_list->data);
		multiprotocol_list = g_list_delete_link (multiprotocol_list, multiprotocol_list);
	}

	return retval;
}

/* create group_source_req as used by pgm_transport_create which specify port, address & interface.
 */

int
pgm_if_parse_transport (
	const char*			s,
	int				ai_family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	struct group_source_req*	send_gsr,
	struct group_source_req*	recv_gsr,
	int*				len		/* length of incoming mreq and filled in returning */
	)
{
	g_return_val_if_fail (s != NULL, -EINVAL);
	g_return_val_if_fail (ai_family == AF_UNSPEC || ai_family == AF_INET || ai_family == AF_INET6, -EINVAL);
	g_return_val_if_fail (send_gsr != NULL, -EINVAL);
	g_return_val_if_fail (recv_gsr != NULL, -EINVAL);
	g_return_val_if_fail (len != NULL || *len > 0, -EINVAL);

	g_trace ("if_parse_transport (\"%s\", %s [%i], ..., %i)", 
		s, 
		ai_family == AF_UNSPEC ? "AF_UNSPEC" :
			( ai_family == AF_INET ? "AF_INET" :
				( ai_family == AF_INET6 ? "AF_INET6" : "UNKNOWN" )
			),
		ai_family, *len);
	int retval = 0;

/* resolve network string */
	struct group_req* devices = g_malloc0 ( sizeof(struct group_req) * (1 + *len) );
	struct sockaddr* receive_groups = g_malloc0 ( sizeof(struct sockaddr_storage) * (1 + *len) );
	struct sockaddr* send_group = g_malloc0 ( sizeof(struct sockaddr_storage) );
	retval = pgm_if_parse_network (s, ai_family, devices, receive_groups, send_group, *len);
	if (retval == 0)
	{
/* receive groups: possible confusion over mismatch length of devices & receive_groups
 *
 * all ports are ignored because this is a new IP protocol.
 */
		int i = 0;
		while (receive_groups[i].sa_family)
		{
			memcpy (&recv_gsr[i].gsr_group, &receive_groups[i], pgm_sockaddr_len(&receive_groups[i]));

			if (((struct sockaddr*)&devices[0].gr_group)->sa_family) {
				recv_gsr[i].gsr_interface = devices[0].gr_interface;
				memcpy (&recv_gsr[i].gsr_source, &devices[0].gr_group, pgm_sockaddr_len(&devices[0].gr_group));
			} else {
				recv_gsr[i].gsr_interface = 0;
				((struct sockaddr_in*)&recv_gsr[i].gsr_source)->sin_family = AF_INET;
				((struct sockaddr_in*)&recv_gsr[i].gsr_source)->sin_addr.s_addr = INADDR_ANY;
			}
			i++;
		}

/* send group */
		memcpy (&send_gsr[0].gsr_group, &send_group[0], pgm_sockaddr_len(&send_group[0]));

		if (((struct sockaddr*)&devices[0].gr_group)->sa_family) {
			send_gsr[0].gsr_interface = devices[0].gr_interface;
			memcpy (&send_gsr[0].gsr_source, &devices[0].gr_group, pgm_sockaddr_len(&devices[0].gr_group));
		} else {
			send_gsr[0].gsr_interface = 0;
			((struct sockaddr_in*)&send_gsr[0].gsr_source)->sin_family = AF_INET;
			((struct sockaddr_in*)&send_gsr[0].gsr_source)->sin_addr.s_addr = INADDR_ANY;
		}

/* pass back number of filled in gsr objects */
		*len = i;
	}

	g_free (devices);
	g_free (receive_groups);
	g_free (send_group);
	return retval;
}

/* eof */
