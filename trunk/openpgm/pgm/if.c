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

#include "if.h"


/* globals */

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
if_print_all (void)
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
					&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr :
					&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr,
				s,
				sizeof(s));
		printf ("name %-5.5s IPv%i %-46.46s status %s loop %s b/c %s m/c %s\n",
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
if_inet_network (
	const char* s,
	struct in_addr* in
	)
{
printf ("moo[%s]\n", s);
	in->s_addr = INADDR_ANY;

	char *p = s;
	char *e = p + strlen(s);
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

printf ("elem %i\n", val);
			
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
printf ("elem %i\n", val);
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
printf ("bit mask %i\n", val);

/* zero out host bits */
			in->s_addr = htonl(in->s_addr);
			while (val < 32) {
printf ("s_addr=%s &= ~(1 << %i)\n",
	inet_ntoa(*in),
	val);
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
if_inet6_network (
	const char* s,
	struct in6_addr* in6
	)
{
printf ("moo6[%s]\n", s);

	char s2[INET6_ADDRSTRLEN];
	char *p = s, *p2 = s2;
	char *e = p + strlen(s);
	while (*p) {
		if (*p == '/') break;
		*p2++ = *p++;
	}
	if (p == e) {
		if (inet_pton (AF_INET6, s, in6)) return 0;
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}

printf ("net part %s\n", s2);
	if (!inet_pton (AF_INET6, s2, in6)) {
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}

		char s3[INET6_ADDRSTRLEN];
		printf ("IPv6 network address: %s\n", inet_ntop(AF_INET6, in6, s3, sizeof(s3)));

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
printf ("bit mask %i\n", val);

/* zero out host bits */
	while (val < 128) {
		int byte_index = val / 8;
		int bit_index  = val % 8;

		in6->in6_u.u6_addr8[byte_index] &= ~(1 << bit_index);
		val++;
	}

		printf ("IPv6 network address: %s\n", inet_ntop(AF_INET6, in6, s3, sizeof(s3)));

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
if_parse_interface (
	const char* s
	)
{
	struct ifaddrs *ifap, *ifa;
	int e = getifaddrs (&ifap);
	if (e < 0) {
		perror("getifaddrs");
		return -1;
	}

/* search for interface name */
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
				printf ("%s: not an IP interface, sa_family=0x%x\n", ifa->ifa_name, ifa->ifa_addr->sa_family);
				return -1;
			}

/* just ignore other non-IP interfaces */
			continue;
		}

/* we have an IP interface, check its name, IP and network addresses */
		if ( strcmp (s, ifa->ifa_name ) == 0 )
		{
			int i = if_nametoindex (ifa->ifa_name);
			if (i > 0) {
				printf ("interface %i %s\n", i, ifa->ifa_name);
			}
			return (i > 0);
		}
	}

/* check if a valid ipv4 or ipv6 address */
	struct sockaddr_in addr;
	struct sockaddr_in6 addr6;
	int valid_ipv4 = 0, valid_ipv6 = 0;
	int valid_net4 = 0, valid_net6 = 0;

	if (inet_pton (AF_INET, s, &addr.sin_addr))
	{
		valid_ipv4 = 1;
	}
	else if (inet_pton (AF_INET6, s, &addr6.sin6_addr))
	{
		valid_ipv6 = 1;
	}

/* IPv6 friendly version??? */
	struct in_addr in;
#if 0
	in.s_addr = inet_network (s);
	if (in.s_addr != -1) {
		printf ("network address calculated: %s\n", inet_ntoa (in));
	}
#else
	e = if_inet_network (s, &in);
	if (e != -1) {
		printf ("IPv4 network address calculated: %s\n", inet_ntoa (in));
		valid_net4 = 1;
	}

	struct in6_addr in6;
	e = if_inet6_network (s, &in6);
	if (e != -1) {
		char s[INET6_ADDRSTRLEN];
		printf ("IPv6 network address calculated: %s\n", inet_ntop(AF_INET6, &in6, s, sizeof(s)));
		valid_net6 = 1;
	}

#endif

	if (! (valid_ipv4 || valid_ipv6 || valid_net4 || valid_net6) )
	{

/* check NSS networks for a network name */
		struct netent* ne = getnetbyname (s);
		if (ne) {
			printf ("found network by NSS: %s\n", ne->n_name);
	
			addr.sin_addr.s_addr = ne->n_net;
			printf ("address %s\n", inet_ntoa (addr.sin_addr));
			valid_ipv4 = 1;
		}
	}

	if (! (valid_ipv4 || valid_ipv6 || valid_net4 || valid_net6) )
	{

/* DNS resolve to see if valid hostname */
		struct addrinfo hints;
		struct addrinfo *res = NULL;

		memset (&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;	/* NULL */
		hints.ai_flags  = AI_ADDRCONFIG | AI_CANONNAME;
		int err = getaddrinfo (s, NULL, &hints, &res);

		if (!err) {
			if (res->ai_family == AF_INET) {
				addr.sin_addr = ((struct sockaddr_in*)(res->ai_addr))->sin_addr;
				valid_ipv4 = 1;
			} else {
				addr6.sin6_addr = ((struct sockaddr_in6*)(res->ai_addr))->sin6_addr;
				valid_ipv6 = 1;
			}

			freeaddrinfo (res);
		} else {
			return -1;
		}
	}

/* iterate through interface list again to match ip or net address */
	if (valid_ipv4 || valid_ipv6 || valid_net4 || valid_net6)
	{
		for (ifa = ifap; ifa; ifa = ifa->ifa_next)
		{
			switch (ifa->ifa_addr->sa_family) {
			case AF_INET:
				if (memcmp (&((struct sockaddr_in*)ifa->ifa_addr)->sin_addr,
						&addr.sin_addr,
						sizeof(struct in_addr)) == 0)
				{
					printf ("IPv4 address on %i:%s\n",
							if_nametoindex (ifa->ifa_name),
							ifa->ifa_name );
				}

/* check network address */
				if (valid_net4)
				{
					struct in_addr netaddr;
					netaddr.s_addr = ((struct sockaddr_in*)ifa->ifa_netmask)->sin_addr.s_addr &
							((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;

					if (in.s_addr == netaddr.s_addr)
					{
						printf ("IPv4 net address on %i:%s\n",
							if_nametoindex (ifa->ifa_name),
							ifa->ifa_name );
					}
				}
				break;

			case AF_INET6:
				if (memcmp (&((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr,
						&addr6.sin6_addr,
						sizeof(struct in6_addr)) == 0)
				{
					printf ("IPv6 address on %i:%s\n",
							if_nametoindex (ifa->ifa_name),
							ifa->ifa_name );
				}

/* check network address */
				if (valid_net6)
				{
					struct in6_addr ipaddr6;
					struct in6_addr netaddr6;
					memcpy (&netaddr6,
						((struct sockaddr_in6*)ifa->ifa_netmask)->sin6_addr.s6_addr,
						sizeof(in6addr_any));
					memcpy (&ipaddr6,
						((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr.s6_addr,
						sizeof(in6addr_any));

					int invalid = 0;
					for (int i = 0; i < 16; i++)
					{
						if (netaddr6.s6_addr[i] & ipaddr6.s6_addr[i] != in6.s6_addr[i])
						{
							invalid = 1;
							break;
						}
					}
					if (!invalid)
					{
					printf ("IPv6 net address on %i:%s\n",
							if_nametoindex (ifa->ifa_name),
							ifa->ifa_name );
					}
				}
				break;

			default: continue;
			}
		}
	}

	return (e > 0);
}

/* parse multicast address
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
if_parse_multicast (
	const char* s
	)
{
	struct sockaddr_in addr;
	struct sockaddr_in6 addr6;
	char s2[INET6_ADDRSTRLEN];

/* IPv4 address */
	if (inet_pton (AF_INET, s, &addr.sin_addr))
	{
		printf ("IPv4 ");

		if (IN_MULTICAST(htonl(addr.sin_addr.s_addr))) {
			printf ("multicast");
		} else {
			printf ("unicast");
		}

		char* e = inet_ntop(AF_INET, &addr.sin_addr, s2, sizeof(s2));
		if (!e) return -1;
	}
	else if (inet_pton (AF_INET6, s, &addr6.sin6_addr))
	{
		printf ("IPv6 ");

		if (IN6_IS_ADDR_MULTICAST(&addr6.sin6_addr)) {
			printf ("multicast");
		} else {
			printf ("unicast");
		}

		char* e = inet_ntop(AF_INET6, &addr6.sin6_addr, s2, sizeof(s2));
		if (!e) return -1;
	}
	else
	{
/* try to resolve the name instead */
		struct addrinfo hints;
		struct addrinfo *res = NULL;

		memset (&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;	/* NULL */
		hints.ai_flags  = AI_ADDRCONFIG | AI_CANONNAME;
		int err = getaddrinfo (s, NULL, &hints, &res);

		if (!err) {
			printf ("DNS hostname: (A) %s address %s\n",
				res->ai_canonname,
				inet_ntop (res->ai_family, 
						res->ai_family == AF_INET ?
							&((struct sockaddr_in*)(res->ai_addr))->sin_addr :
							&((struct sockaddr_in6*)(res->ai_addr))->sin6_addr,
						s2, sizeof(s2)) );
			if (IN_MULTICAST(htonl(((struct sockaddr_in*)(res->ai_addr))->sin_addr.s_addr))) {
				printf ("multicast");
			} else {
				printf ("unicast");
			}
		}
		else
		{
			return -1;
		}

		freeaddrinfo (res);
	}

	printf (" %s\n", s2);

	return 0;
}

/* parse network parameter
 *
 * interface list; receive multicast group list; send multicast group
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
if_parse_network (
	const char* s
	)
{
	char *p = s;
	char *e = p + strlen(s);
	enum { ENTITY_INTERFACE, ENTITY_RECEIVE, ENTITY_SEND } ec = ENTITY_INTERFACE;
	char *b = p;		/* begin of entity */
	int ret = 0;

	while (p < e) {
		if (!IS_HOSTNAME(*p) && !IS_IP(*p) && !IS_IP6(*p) && !IS_NETPARAM(*p))
		{
			printf ("invalid character 0x%x\n", *p);
			return -1;
		}

		if (*p == ';')		/* end of entity */
		{
			if (b == p)	/* empty entity */
			{
				puts ("null");
				ec++;

				b = ++p;
				continue;
			}

			/* entity from b to p-1 */
			char *dup = malloc (p - b + 1);
			strncpy (dup, b, p - b);
			dup[p - b] = 0;

			printf ("entity '%s'\n", dup);
			switch (ec++) {
			case ENTITY_INTERFACE:	ret = if_parse_interface (dup); break;
			case ENTITY_RECEIVE:
			case ENTITY_SEND:	ret = if_parse_multicast (dup); break;
			default: puts ("invalid state"); break;
			}
				
			free (dup);

			b = ++p;
			continue;
		}

		p++;
	}

	if (b < e) {
		printf ("entity '%s'\n", b);
		switch (ec++) {
		case ENTITY_INTERFACE:	ret = if_parse_interface (b); break;
		case ENTITY_RECEIVE:
		case ENTITY_SEND:	ret = if_parse_multicast (b); break;
		default: puts ("invalid state"); break;
		}
	}
	else
	{
		puts ("null");
	}

	return ret;
}


/* eof */
