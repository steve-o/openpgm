/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * network interface handling.
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

#define _GNU_SOURCE
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#ifndef _WIN32
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netdb.h>		/* _GNU_SOURCE for EAI_NODATA */
#endif
#include <pgm/i18n.h>
#include <pgm/framework.h>


//#define IF_DEBUG

/* temporary structure to contain interface name whilst address family
 * has not been resolved.
 */
struct interface_req {
	char			ir_name[IF_NAMESIZE];
	unsigned int		ir_interface;		/* interface index */
	struct sockaddr_storage ir_addr;		/* interface address */
};


/* locals */

#ifndef _WIN32
#	define IF_DEFAULT_GROUP	((in_addr_t)0xefc00001) /* 239.192.0.1 */
#else
#	define IF_DEFAULT_GROUP	((u_long)0xefc00001)
#endif

/* ff08::1 */
#define IF6_DEFAULT_INIT { { { 0xff,8,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }
const struct in6_addr if6_default_group_addr = IF6_DEFAULT_INIT;


static inline bool is_in_net (const struct in_addr*restrict, const struct in_addr*restrict, const struct in_addr*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
static inline bool is_in_net6 (const struct in6_addr*restrict, const struct in6_addr*restrict, const struct in6_addr*restrict) PGM_GNUC_WARN_UNUSED_RESULT;
static inline bool is_network_char (const int, const char) PGM_GNUC_CONST;
static const char* pgm_family_string (const int) PGM_GNUC_CONST;


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
	struct pgm_ifaddrs *ifap, *ifa;

	if (!pgm_getifaddrs (&ifap, NULL))
		return;

	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		const unsigned int i = NULL == ifa->ifa_addr ? 0 : pgm_if_nametoindex (ifa->ifa_addr->sa_family, ifa->ifa_name);
		char rname[IF_NAMESIZE * 2 + 3];
		char b[IF_NAMESIZE * 2 + 3];

		pgm_if_indextoname (i, rname);
		sprintf (b, "%s (%s)",
			ifa->ifa_name ? ifa->ifa_name : "(null)", rname);

		if (NULL == ifa->ifa_addr ||
		     (ifa->ifa_addr->sa_family != AF_INET && 
		      ifa->ifa_addr->sa_family != AF_INET6) )
		{
			pgm_info (_("#%d name %-15.15s ---- %-46.46s scope 0 status %s loop %s b/c %s m/c %s"),
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
		pgm_info (_("#%d name %-15.15s IPv%i %-46.46s scope %u status %s loop %s b/c %s m/c %s"),
			i,
			b,
			ifa->ifa_addr->sa_family == AF_INET ? 4 : 6,
			s,
			(unsigned)pgm_sockaddr_scope_id(ifa->ifa_addr),
			ifa->ifa_flags & IFF_UP ? "UP  " : "DOWN",
			ifa->ifa_flags & IFF_LOOPBACK ? "YES" : "NO ",
			ifa->ifa_flags & IFF_BROADCAST ? "YES" : "NO ",
			ifa->ifa_flags & IFF_MULTICAST ? "YES" : "NO "
			);
	}

	pgm_freeifaddrs (ifap);
}

static inline
bool
is_in_net (
	const struct in_addr* restrict	addr,		/* host byte order */
	const struct in_addr* restrict	netaddr,
	const struct in_addr* restrict	netmask
	)
{
	pgm_assert (NULL != addr);
	pgm_assert (NULL != netaddr);
	pgm_assert (NULL != netmask);

#ifdef IF_DEBUG
	const struct in_addr taddr    = { .s_addr = htonl (addr->s_addr) };
	const struct in_addr tnetaddr = { .s_addr = htonl (netaddr->s_addr) };
	const struct in_addr tnetmask = { .s_addr = htonl (netmask->s_addr) };
	char saddr[INET_ADDRSTRLEN], snetaddr[INET_ADDRSTRLEN], snetmask[INET_ADDRSTRLEN];
	pgm_debug ("is_in_net (addr:%s netaddr:%s netmask:%s)",
		 pgm_inet_ntop (AF_INET, &taddr,    saddr,    sizeof(saddr)),
		 pgm_inet_ntop (AF_INET, &tnetaddr, snetaddr, sizeof(snetaddr)),
		 pgm_inet_ntop (AF_INET, &tnetmask, snetmask, sizeof(snetmask)));
#endif

	if ((addr->s_addr & netmask->s_addr) == (netaddr->s_addr & netmask->s_addr))
		return TRUE;
	return FALSE;
}

static
bool
is_in_net6 (
	const struct in6_addr* restrict	addr,
	const struct in6_addr* restrict	netaddr,
	const struct in6_addr* restrict	netmask
	)
{
	pgm_assert (NULL != addr);
	pgm_assert (NULL != netaddr);
	pgm_assert (NULL != netmask);

#ifdef IF_DEBUG
	char saddr[INET6_ADDRSTRLEN], snetaddr[INET6_ADDRSTRLEN], snetmask[INET6_ADDRSTRLEN];
	pgm_debug ("is_in_net6 (addr:%s netaddr:%s netmask:%s)",
		 pgm_inet_ntop (AF_INET6, addr, saddr, sizeof(saddr)),
		 pgm_inet_ntop (AF_INET6, netaddr, snetaddr, sizeof(snetaddr)),
		 pgm_inet_ntop (AF_INET6, netmask, snetmask, sizeof(snetmask)));
#endif

	for (unsigned i = 0; i < 16; i++)
		if ((addr->s6_addr[i] & netmask->s6_addr[i]) != (netaddr->s6_addr[i] & netmask->s6_addr[i]))
			return FALSE;
	return TRUE;
}

/* parse interface entity into an interface-request structure.
 *
 * e.g.  eth0
 *       1.2.3.4
 *       1.2
 *       abcd::
 *       [abcd::]
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
 * returns TRUE on success, FALSE on error and sets error appropriately.
 */

static
bool
parse_interface (
	int				family,			/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*	      restrict	ifname,			/* NULL terminated */
	struct interface_req* restrict	ir,			/* location to write interface details to */
	pgm_error_t**	      restrict	error
	)
{
	bool check_inet_network = FALSE, check_inet6_network = FALSE;
	bool check_addr = FALSE;
	bool check_ifname = FALSE;
	char literal[1024];
	struct in_addr in_addr;
	struct in6_addr in6_addr;
	struct pgm_ifaddrs *ifap, *ifa;
	struct sockaddr_storage addr;
	unsigned interface_matches = 0;

/* pre-conditions */
	pgm_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	pgm_assert (NULL != ifname);
	pgm_assert (NULL != ir);

	pgm_debug ("parse_interface (family:%s ifname:%s%s%s ir:%p error:%p)",
		pgm_family_string (family),
		ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "",
		(const void*)ir,
		(const void*)error);

/* strip any square brackets for IPv6 early evaluation */
	if (AF_INET != family &&
	    '[' == ifname[0])
	{
		const size_t ifnamelen = strlen(ifname);
		if (']' == ifname[ ifnamelen - 1 ]) {
			strncpy (literal, ifname + 1, ifnamelen - 2);
			literal[ ifnamelen - 2 ] = 0;
			family = AF_INET6;		/* force IPv6 evaluation */
			check_inet6_network = TRUE;	/* may be a network IP or CIDR block */
			check_addr = TRUE;		/* cannot be not a name */
			ifname = literal;
		}
	}

/* network address: in_addr in host byte order */
	if (AF_INET6 != family && 0 == pgm_inet_network (ifname, &in_addr))
	{
#ifdef IF_DEBUG
		struct in_addr t = { .s_addr = htonl (in_addr.s_addr) };
		pgm_debug ("IPv4 network address: %s", inet_ntoa (t));
#endif
		if (IN_MULTICAST(in_addr.s_addr)) {
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_IF,
				     PGM_ERROR_XDEV,
				     _("Expecting network interface address, found IPv4 multicast network %s%s%s"),
				     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
			return FALSE;
		}
		struct sockaddr_in s4;
		memset (&s4, 0, sizeof(s4));
		s4.sin_family = AF_INET;
		s4.sin_addr.s_addr = htonl (in_addr.s_addr);
		memcpy (&addr, &s4, sizeof(s4));

		check_inet_network = TRUE;
		check_addr = TRUE;
	}
	if (AF_INET  != family && 0 == pgm_inet6_network (ifname, &in6_addr))
	{
		if (IN6_IS_ADDR_MULTICAST(&in6_addr)) {
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_IF,
				     PGM_ERROR_XDEV,
				     _("Expecting network interface address, found IPv6 multicast network %s%s%s"),
				     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
			return FALSE;
		}
		struct sockaddr_in6 s6;
		memset (&s6, 0, sizeof(s6));
		s6.sin6_family = AF_INET6;
		s6.sin6_addr = in6_addr;
		memcpy (&addr, &s6, sizeof(s6));

		check_inet6_network = TRUE;
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
		switch (eai) {
		case 0:
			if (AF_INET == res->ai_family &&
			    IN_MULTICAST(ntohl (((struct sockaddr_in*)(res->ai_addr))->sin_addr.s_addr)))
			{
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_IF,
					     PGM_ERROR_XDEV,
					     _("Expecting interface address, found IPv4 multicast address %s%s%s"),
					     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
				freeaddrinfo (res);
				return FALSE;
			}
			else if (AF_INET6 == res->ai_family &&
				 IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)res->ai_addr)->sin6_addr))
			{
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_IF,
					     PGM_ERROR_XDEV,
					     _("Expecting interface address, found IPv6 multicast address %s%s%s"),
					     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
				freeaddrinfo (res);
				return FALSE;
			}

			memcpy (&addr, res->ai_addr, pgm_sockaddr_len (res->ai_addr));
			freeaddrinfo (res);
			check_addr = TRUE;
			break;

#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
		case EAI_NODATA:
#endif
		case EAI_NONAME:
			break;

		default:
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_IF,
				     pgm_error_from_eai_errno (eai, errno),
				     _("Numeric host resolution: %s"),
				     gai_strerror (eai));
			return FALSE;
		}
	}

#ifndef _WIN32
/* network name into network address, can be expensive with NSS network lookup
 *
 * Only Class A, B or C networks are supported, partitioned networks
 * (i.e. network/26 or network/28) are not supported by this facility.
 */
	if (!(check_inet_network || check_inet6_network))
	{
		const struct netent* ne = getnetbyname (ifname);
/* ne::n_net in host byte order */

		if (ne) {
			switch (ne->n_addrtype) {
			case AF_INET:
				if (AF_INET6 == family) {
					pgm_set_error (error,
						     PGM_ERROR_DOMAIN_IF,
						     PGM_ERROR_NODEV,
						     _("IP address family conflict when resolving network name %s%s%s, found AF_INET when AF_INET6 expected."),
						     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
					return FALSE;
				}
/* ne->n_net in network order */
				in_addr.s_addr = ne->n_net;
				if (IN_MULTICAST(in_addr.s_addr)) {
					pgm_set_error (error,
						     PGM_ERROR_DOMAIN_IF,
						     PGM_ERROR_XDEV,
						     _("Network name %s%s%s resolves to IPv4 mulicast address."),
						     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
					return FALSE;
				}
				check_inet_network = TRUE;
				break;
			case AF_INET6:
#ifndef CONFIG_HAVE_IP6_NETWORKS
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_IF,
					     PGM_ERROR_NODEV,
					     _("Not configured for IPv6 network name support, %s%s%s is an IPv6 network name."),
					     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
				return FALSE;
#else
				if (AF_INET == family) {
					pgm_set_error (error,
						     PGM_ERROR_DOMAIN_IF,
						     PGM_ERROR_NODEV,
						     _("IP address family conflict when resolving network name %s%s%s, found AF_INET6 when AF_INET expected."),
						     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
					return FALSE;
				}
				if (IN6_IS_ADDR_MULTICAST(&ne->n_net)) {
					pgm_set_error (error,
						     PGM_ERROR_DOMAIN_IF,
						     PGM_ERROR_XDEV,
						     _("Network name resolves to IPv6 mulicast address %s%s%s"),
						     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
					return FALSE;
				}
				in6_addr = *(const struct in6_addr*)&ne->n_net;
				check_inet6_network = TRUE;
				break;
#endif
			default:
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_IF,
					     PGM_ERROR_NODEV,
					     _("Network name resolves to non-internet protocol address family %s%s%s"),
					     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
				return FALSE;
			}
		}
	}
#endif /* _WIN32 */

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
		switch (eai) {
			if (AF_INET == res->ai_family &&
			    IN_MULTICAST(ntohl (((struct sockaddr_in*)(res->ai_addr))->sin_addr.s_addr)))
			{
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_IF,
					     PGM_ERROR_XDEV,
					     _("Expecting interface address, found IPv4 multicast name %s%s%s"),
					     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
				freeaddrinfo (res);
				return FALSE;
			}
			else if (AF_INET6 == res->ai_family &&
				 IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)res->ai_addr)->sin6_addr))
			{
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_IF,
					     PGM_ERROR_XDEV,
					     _("Expecting interface address, found IPv6 multicast name %s%s%s"),
					     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
				freeaddrinfo (res);
				return FALSE;
			}
			memcpy (&addr, res->ai_addr, pgm_sockaddr_len (res->ai_addr));
			freeaddrinfo (res);
			check_addr = TRUE;
			break;

#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
		case EAI_NODATA:
#endif
		case EAI_NONAME:
			check_ifname = TRUE;
			break;

		default:
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_IF,
				     pgm_error_from_eai_errno (eai, errno),
				     _("Internet host resolution: %s(%d)"),
				     gai_strerror (eai), eai);
			return FALSE;
		}
	}

/* iterate through interface list and match device name, ip or net address */
	if (!pgm_getifaddrs (&ifap, error)) {
		pgm_prefix_error (error,
				_("Enumerating network interfaces: "));
		return FALSE;
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next)
	{
		if (NULL == ifa->ifa_addr)
			continue;

		switch (ifa->ifa_addr->sa_family) {
/* ignore raw entries on Linux */
#ifdef AF_PACKET
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

		const unsigned ifindex = pgm_if_nametoindex (ifa->ifa_addr->sa_family, ifa->ifa_name);
		pgm_assert (0 != ifindex);

/* check numeric host */
		if (check_addr &&
		    (0 == pgm_sockaddr_cmp (ifa->ifa_addr, (const struct sockaddr*)&addr)))
		{
			strcpy (ir->ir_name, ifa->ifa_name);
			ir->ir_interface = ifindex;
			memcpy (&ir->ir_addr, ifa->ifa_addr, pgm_sockaddr_len (ifa->ifa_addr));
			pgm_freeifaddrs (ifap);
			return TRUE;
		}

/* check network address */
		if (check_inet_network &&
		    AF_INET == ifa->ifa_addr->sa_family)
		{
			const struct in_addr ifaddr  = { .s_addr = ntohl (((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr) };
			const struct in_addr netmask = { .s_addr = ntohl (((struct sockaddr_in*)ifa->ifa_netmask)->sin_addr.s_addr) };
			if (is_in_net (&ifaddr, &in_addr, &netmask)) {
				strcpy (ir->ir_name, ifa->ifa_name);
				ir->ir_interface = ifindex;
				memcpy (&ir->ir_addr, ifa->ifa_addr, pgm_sockaddr_len (ifa->ifa_addr));
				pgm_freeifaddrs (ifap);
				return TRUE;
			}
		}
		if (check_inet6_network &&
		    AF_INET6 == ifa->ifa_addr->sa_family)
		{
			const struct in6_addr ifaddr = ((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
			const struct in6_addr netmask = ((struct sockaddr_in6*)ifa->ifa_netmask)->sin6_addr;
			if (is_in_net6 (&ifaddr, &in6_addr, &netmask)) {
				strcpy (ir->ir_name, ifa->ifa_name);
				ir->ir_interface = ifindex;
				memcpy (&ir->ir_addr, ifa->ifa_addr, pgm_sockaddr_len (ifa->ifa_addr));
				pgm_freeifaddrs (ifap);
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
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_IF,
					     PGM_ERROR_NOTUNIQ,
					     _("Network interface name not unique %s%s%s"),
					     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
				pgm_freeifaddrs (ifap);
				return FALSE;
			}

			ir->ir_interface = ifindex;
			strcpy (ir->ir_name, ifa->ifa_name);
			memcpy (&ir->ir_addr, ifa->ifa_addr, pgm_sockaddr_len (ifa->ifa_addr));
			continue;
		}

	}

	if (0 == interface_matches) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     PGM_ERROR_NODEV,
			     _("No matching network interface %s%s%s"),
			     ifname ? "\"" : "", ifname ? ifname : "(null)", ifname ? "\"" : "");
		pgm_freeifaddrs (ifap);
		return FALSE;
	}

	pgm_freeifaddrs (ifap);
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
 *
 * returns TRUE on success, FALSE on error and sets error appropriately.
 */

static
bool
parse_group (
	const int		  family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*	 restrict group,	/* NULL terminated */
	struct sockaddr* restrict addr,		/* pointer to sockaddr_storage for writing */
	pgm_error_t**	 restrict error
	)
{
/* pre-conditions */
	pgm_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	pgm_assert (NULL != group);
	pgm_assert (NULL != addr);

	pgm_debug ("parse_group (family:%s group:%s%s%s addr:%p error:%p)",
		pgm_family_string (family),
		group ? "\"" : "", group ? group : "(null)", group ? "\"" : "",
		(const void*)addr,
		(const void*)error);

/* strip any square brackets for early IPv6 literal evaluation */
	if (AF_INET != family &&
	    '[' == group[0])
	{
		const size_t grouplen = strlen(group);
		if (']' == group[ grouplen - 1 ]) {
			char literal[1024];
			strncpy (literal, group + 1, grouplen - 2);
			literal[ grouplen - 2 ] = 0;
			if (pgm_inet_pton (AF_INET6, literal, &((struct sockaddr_in6*)addr)->sin6_addr) &&
			    IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)addr)->sin6_addr))
			{
				addr->sa_family = AF_INET6;
				((struct sockaddr_in6*)addr)->sin6_port = 0;
				((struct sockaddr_in6*)addr)->sin6_flowinfo = 0;
				((struct sockaddr_in6*)addr)->sin6_scope_id = 0;
				return TRUE;
			}
		}
	}

/* IPv4 address */
	if (AF_INET6 != family &&
	    pgm_inet_pton (AF_INET, group, &((struct sockaddr_in*)addr)->sin_addr) &&
	    IN_MULTICAST(ntohl (((struct sockaddr_in*)addr)->sin_addr.s_addr)))
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

#ifndef _WIN32
/* NSS network */
	const struct netent* ne = getnetbyname (group);
/* ne::n_net in host byte order */
	if (ne) {
		switch (ne->n_addrtype) {
		case AF_INET:
			if (AF_INET6 == family) {
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_IF,
					     PGM_ERROR_NODEV,
					     _("IP address family conflict when resolving network name %s%s%s, found IPv4 when IPv6 expected."),
					     group ? "\"" : "", group ? group : "(null)", group ? "\"" : "");
				return FALSE;
			}
			if (IN_MULTICAST(ne->n_net)) {
				addr->sa_family = AF_INET;
				((struct sockaddr_in*)addr)->sin_addr.s_addr = htonl (ne->n_net);
				return TRUE;
			}
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_IF,
				     PGM_ERROR_NODEV,
				     _("IP address class conflict when resolving network name %s%s%s, expected IPv4 multicast."),
				     group ? "\"" : "", group ? group : "(null)", group ? "\"" : "");
			return FALSE;
		case AF_INET6:
#ifndef CONFIG_HAVE_IP6_NETWORKS
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_IF,
				     PGM_ERROR_NODEV,
				     _("Not configured for IPv6 network name support, %s%s%s is an IPv6 network name."),
				     group ? "\"" : "", group ? group : "(null)", group ? "\"" : "");
			return FALSE;
#else
			if (AF_INET == family) {
				pgm_set_error (error,
					     PGM_ERROR_DOMAIN_IF,
					     PGM_ERROR_NODEV,
					     _("IP address family conflict when resolving network name %s%s%s, found IPv6 when IPv4 expected."),
					     group ? "\"" : "", group ? group : "(null)", group ? "\"" : "");
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
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_IF,
				     PGM_ERROR_NODEV,
				     _("IP address class conflict when resolving network name %s%s%s, expected IPv6 multicast."),
				     group ? "\"" : "", group ? group : "(null)", group ? "\"" : "");
			return FALSE;
#endif /* CONFIG_HAVE_IP6_NETWORKS */
		default:
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_IF,
				     PGM_ERROR_NODEV,
				     _("Network name resolves to non-internet protocol address family %s%s%s"),
				     group ? "\"" : "", group ? group : "(null)", group ? "\"" : "");
			return FALSE;
		}
	}
#endif /* _WIN32 */

/* lookup group through name service */
	struct addrinfo hints = {
		.ai_family	= family,
		.ai_socktype	= SOCK_STREAM,		/* not really, SOCK_RAW */
		.ai_protocol	= IPPROTO_TCP,		/* not really, IPPROTO_PGM */
		.ai_flags	= AI_ADDRCONFIG,	/* AI_V4MAPPED is unhelpful */
	}, *res;

	const int eai = getaddrinfo (group, NULL, &hints, &res);
	if (0 != eai) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_eai_errno (eai, errno),
			     _("Resolving receive group: %s"),
			     gai_strerror (eai));
		return FALSE;
	}

	if ((AF_INET6 != family && IN_MULTICAST(ntohl (((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr))) ||
	    (AF_INET  != family && IN6_IS_ADDR_MULTICAST(&((struct sockaddr_in6*)res->ai_addr)->sin6_addr)))
	{
		memcpy (addr, res->ai_addr, res->ai_addrlen);
		freeaddrinfo (res);
		return TRUE;
	}

	pgm_set_error (error,
		     PGM_ERROR_DOMAIN_IF,
		     PGM_ERROR_INVAL,
		     _("Unresolvable receive group %s%s%s"),
		     group ? "\"" : "", group ? group : "(null)", group ? "\"" : "");
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
 * returns TRUE on success with device_list containing double linked list of devices as
 * sockaddr/idx pairs.  returns FALSE on error, including multiple matching adapters.
 *
 * memory ownership of linked list is passed to caller and must be freed with pgm_free
 * and the pgm_list_free* api.
 */

static
bool
parse_interface_entity (
	int			family,	/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*   restrict	entity,	/* NULL terminated */
	pgm_list_t**  restrict	interface_list,	/* <struct interface_req*> */
	pgm_error_t** restrict	error
	)
{
	struct interface_req* ir;
	pgm_list_t* source_list = NULL;

/* pre-conditions */
	pgm_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	pgm_assert (NULL != interface_list);
	pgm_assert (NULL == *interface_list);
	pgm_assert (NULL != error);

	pgm_debug ("parse_interface_entity (family:%s entity:%s%s%s interface_list:%p error:%p)",
		pgm_family_string (family),
		entity ? "\"":"", entity ? entity : "(null)", entity ? "\"":"",
		(const void*)interface_list,
		(const void*)error);

/* the empty entity, returns in_addr_any for both receive and send interfaces */
	if (NULL == entity)
	{
		ir = pgm_new0 (struct interface_req, 1);
		ir->ir_addr.ss_family = family;
		*interface_list = pgm_list_append (*interface_list, ir);
		return TRUE;
	}

/* check interface name length limit */
	char** tokens = pgm_strsplit (entity, ",", 10);
	int j = 0;
	while (tokens && tokens[j])
	{
		pgm_error_t* sub_error = NULL;
		ir = pgm_new (struct interface_req, 1);
		if (!parse_interface (family, tokens[j], ir, &sub_error))
		{
/* mark multiple interfaces for later decision based on group families */
			if (sub_error && PGM_ERROR_NOTUNIQ == sub_error->code)
			{
				ir->ir_addr.ss_family = AF_UNSPEC;
				pgm_error_free (sub_error);
			}
/* bail out on first interface with an error */
			else
			{
				pgm_propagate_error (error, sub_error);
				pgm_free (ir);
				pgm_strfreev (tokens);
				while (source_list) {
					pgm_free (source_list->data);
					source_list = pgm_list_delete_link (source_list, source_list);
				}
				return FALSE;
			}
		}

		source_list = pgm_list_append (source_list, ir);
		++j;
	}

	pgm_strfreev (tokens);
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
 *
 * returns TRUE on success, returns FALSE on error and sets error appropriately.
 */

static
bool
parse_receive_entity (
	int			family,		/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*   restrict	entity,		/* NULL terminated */
	pgm_list_t**  restrict	interface_list,	/* <struct interface_req*> */
	pgm_list_t**  restrict	recv_list,	/* <struct group_source_req*> */
	pgm_error_t** restrict	error
	)
{
/* pre-conditions */
	pgm_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	pgm_assert (NULL != recv_list);
	pgm_assert (NULL == *recv_list);
	pgm_assert (NULL != error);

	pgm_debug ("parse_receive_entity (family:%s entity:%s%s%s interface_list:%p recv_list:%p error:%p)",
		pgm_family_string (family),
		entity ? "\"":"", entity ? entity : "(null)", entity ? "\"":"",
		(const void*)interface_list,
		(const void*)recv_list,
		(const void*)error);

	struct group_source_req* recv_gsr;
	struct interface_req* primary_interface = (struct interface_req*)pgm_memdup ((*interface_list)->data, sizeof(struct interface_req));

/* the empty entity */
	if (NULL == entity)
	{
/* default receive object */
		recv_gsr = pgm_new0 (struct group_source_req, 1);
		recv_gsr->gsr_interface = primary_interface->ir_interface;
		recv_gsr->gsr_group.ss_family = family;

/* track IPv6 scope from any resolved interface */
		unsigned scope_id = 0;

/* if using unspec default group check the interface for address family
 */
		if (AF_UNSPEC == recv_gsr->gsr_group.ss_family)
		{
			if (AF_UNSPEC == primary_interface->ir_addr.ss_family)
			{
				struct sockaddr_storage addr;
				if (!pgm_if_getnodeaddr (AF_UNSPEC, (struct sockaddr*)&addr, sizeof(addr), error))
				{
					pgm_prefix_error (error,
							_("Node primary address family cannot be determined: "));
					pgm_free (recv_gsr);
					pgm_free (primary_interface);
					return FALSE;
				}
				recv_gsr->gsr_group.ss_family = addr.ss_family;
				scope_id = pgm_sockaddr_scope_id ((struct sockaddr*)&addr);

/* was an interface actually specified */
				if (primary_interface->ir_name[0] != '\0')
				{
					struct interface_req ir;
					if (!parse_interface (recv_gsr->gsr_group.ss_family, primary_interface->ir_name, &ir, error))
					{
						pgm_prefix_error (error,
								_("Unique address cannot be determined for interface %s%s%s: "),
								primary_interface->ir_name ? "\"" : "", primary_interface->ir_name ? primary_interface->ir_name : "(null)", primary_interface->ir_name ? "\"" : "");
						pgm_free (recv_gsr);
						pgm_free (primary_interface);
						return FALSE;
					}

					recv_gsr->gsr_interface = ir.ir_interface;
					memcpy (&primary_interface->ir_addr, &ir.ir_addr, pgm_sockaddr_len ((struct sockaddr*)&ir.ir_addr));
					scope_id = pgm_sockaddr_scope_id ((struct sockaddr*)&ir.ir_addr);
				}
			}
			else
			{
/* use interface address family for multicast group */
				recv_gsr->gsr_group.ss_family = primary_interface->ir_addr.ss_family;
				scope_id = pgm_sockaddr_scope_id ((struct sockaddr*)&primary_interface->ir_addr);
			}
		}


		pgm_assert (AF_UNSPEC != recv_gsr->gsr_group.ss_family);
		if (AF_UNSPEC != primary_interface->ir_addr.ss_family)
		{
			pgm_assert (recv_gsr->gsr_group.ss_family == primary_interface->ir_addr.ss_family);
		}
		else
		{
/* check if we can now resolve the interface by address family of the receive group */
			if (primary_interface->ir_name[0] != '\0')
			{
				struct interface_req ir;
				if (!parse_interface (recv_gsr->gsr_group.ss_family, primary_interface->ir_name, &ir, error))
				{
					pgm_prefix_error (error,
							_("Unique address cannot be determined for interface %s%s%s: "),
							primary_interface->ir_name ? "\"" : "", primary_interface->ir_name ? primary_interface->ir_name : "(null)", primary_interface->ir_name ? "\"" : "");
					pgm_free (recv_gsr);
					pgm_free (primary_interface);
					return FALSE;
				}

				recv_gsr->gsr_interface = ir.ir_interface;
				scope_id = pgm_sockaddr_scope_id ((struct sockaddr*)&ir.ir_addr);
			}
		}

/* copy default PGM multicast group */
		switch (recv_gsr->gsr_group.ss_family) {
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
			pgm_assert_not_reached();
		}

/* ASM: source = group */
		memcpy (&recv_gsr->gsr_source, &recv_gsr->gsr_group, pgm_sockaddr_len ((struct sockaddr*)&recv_gsr->gsr_group));
		*recv_list = pgm_list_append (*recv_list, recv_gsr);
		pgm_free (primary_interface);
		return TRUE;
	}

/* parse one or more multicast receive groups.
 */

	int j = 0;	
	char** tokens = pgm_strsplit (entity, ",", 10);
	while (tokens && tokens[j])
	{
/* default receive object */
		recv_gsr = pgm_new0 (struct group_source_req, 1);
		recv_gsr->gsr_interface = primary_interface->ir_interface;
		recv_gsr->gsr_group.ss_family = family;

		if (AF_UNSPEC == recv_gsr->gsr_group.ss_family)
		{
			if (AF_UNSPEC == primary_interface->ir_addr.ss_family)
			{
				pgm_debug ("Address family of receive group cannot be determined from interface.");
			}
			else
			{
				recv_gsr->gsr_group.ss_family = primary_interface->ir_addr.ss_family;
				((struct sockaddr_in6*)&recv_gsr->gsr_group)->sin6_scope_id = pgm_sockaddr_scope_id ((struct sockaddr*)&primary_interface->ir_addr);
			}
		}

		if (!parse_group (recv_gsr->gsr_group.ss_family, tokens[j], (struct sockaddr*)&recv_gsr->gsr_group, error))
		{
			pgm_prefix_error (error,
					_("Unresolvable receive entity %s%s%s: "),
					tokens[j] ? "\"" : "", tokens[j] ? tokens[j] : "(null)", tokens[j] ? "\"" : "");
			pgm_free (recv_gsr);
			pgm_strfreev (tokens);
			pgm_free (primary_interface);
			return FALSE;
		}

/* check if we can now resolve the source interface by address family of the receive group */
		if (AF_UNSPEC == primary_interface->ir_addr.ss_family)
		{
			if (primary_interface->ir_name[0] != '\0')
			{
				struct interface_req ir;
				if (!parse_interface (recv_gsr->gsr_group.ss_family, primary_interface->ir_name, &ir, error))
				{
					pgm_prefix_error (error,
							_("Unique address cannot be determined for interface %s%s%s: "),
							primary_interface->ir_name ? "\"" : "", primary_interface->ir_name ? primary_interface->ir_name : "(null)", primary_interface->ir_name ? "\"" : "");
					pgm_free (recv_gsr);
					pgm_free (primary_interface);
					return FALSE;
				}

				recv_gsr->gsr_interface = ir.ir_interface;
				((struct sockaddr_in6*)&recv_gsr->gsr_group)->sin6_scope_id = pgm_sockaddr_scope_id ((struct sockaddr*)&ir.ir_addr);
			}
		}

/* ASM: source = group */
		memcpy (&recv_gsr->gsr_source, &recv_gsr->gsr_group, pgm_sockaddr_len ((struct sockaddr*)&recv_gsr->gsr_group));
		*recv_list = pgm_list_append (*recv_list, recv_gsr);
		++j;
	}

	pgm_strfreev (tokens);
	pgm_free (primary_interface);
	return TRUE;
}

static
bool
parse_send_entity (
	int			family,		/* AF_UNSPEC | AF_INET | AF_INET6 */
	const char*   restrict	entity,		/* null = empty entity */
	pgm_list_t**  restrict	interface_list,	/* <struct interface_req*> */
	pgm_list_t**  restrict	recv_list,	/* <struct group_source_req*> */
	pgm_list_t**  restrict	send_list,	/* <struct group_source_req*> */
	pgm_error_t** restrict	error
	)
{
/* pre-conditions */
	pgm_assert (AF_INET == family || AF_INET6 == family || AF_UNSPEC == family);
	pgm_assert (NULL != recv_list);
	pgm_assert (NULL != *recv_list);
	pgm_assert (NULL != send_list);
	pgm_assert (NULL == *send_list);
	pgm_assert (NULL != error);

	pgm_debug ("parse_send_entity (family:%s entity:%s%s%s interface_list:%p recv_list:%p send_list:%p error:%p)",
		pgm_family_string (family),
		entity ? "\"":"", entity ? entity : "(null)", entity ? "\"":"",
		(const void*)interface_list,
		(const void*)recv_list,
		(const void*)send_list,
		(const void*)error);

	struct group_source_req* send_gsr;
	const struct interface_req* primary_interface = (struct interface_req*)(*interface_list)->data;

	if (entity == NULL)
	{
		send_gsr = pgm_memdup ((*recv_list)->data, sizeof(struct group_source_req));
		*send_list = pgm_list_append (*send_list, send_gsr);
		return TRUE;
	}

/* default send object */
	send_gsr = pgm_new0 (struct group_source_req, 1);
	send_gsr->gsr_interface = primary_interface->ir_interface;
	if (!parse_group (family, entity, (struct sockaddr*)&send_gsr->gsr_group, error))
	{
		pgm_prefix_error (error,
				_("Unresolvable send entity %s%s%s: "),
				entity ? "\"":"", entity ? entity : "(null)", entity ? "\"":"");
		pgm_free (send_gsr);
		return FALSE;
	}

/* check if we can now resolve the source interface by address family of the send group */
	if (AF_UNSPEC == primary_interface->ir_addr.ss_family)
	{
		if (primary_interface->ir_name[0] != '\0')
		{
			struct interface_req ir;
			if (!parse_interface (send_gsr->gsr_group.ss_family, primary_interface->ir_name, &ir, error))
			{
				pgm_prefix_error (error,
						_("Unique address cannot be determined for interface %s%s%s: "),
						primary_interface->ir_name ? "\"":"", primary_interface->ir_name ? primary_interface->ir_name : "(null)", primary_interface->ir_name ? "\"":"");
				pgm_free (send_gsr);
				return FALSE;
			}

			send_gsr->gsr_interface = ir.ir_interface;
			((struct sockaddr_in6*)&send_gsr->gsr_group)->sin6_scope_id = pgm_sockaddr_scope_id ((struct sockaddr*)&ir.ir_addr);
		}
	}

/* ASM: source = group */
	memcpy (&send_gsr->gsr_source, &send_gsr->gsr_group, pgm_sockaddr_len ((struct sockaddr*)&send_gsr->gsr_group));
	*send_list = pgm_list_append (*send_list, send_gsr);
	return TRUE;
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
				((x) == '.') || \
				((x) == '[') || \
				((x) == ']') \
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
bool
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
bool
network_parse (
	const char*   restrict	network,		/* NULL terminated */
	int			family,			/* AF_UNSPEC | AF_INET | AF_INET6 */
	pgm_list_t**  restrict	recv_list,		/* <struct group_source_req*> */
	pgm_list_t**  restrict	send_list,		/* <struct group_source_req*> */
	pgm_error_t** restrict	error
	)
{
	bool retval = FALSE;
	const char *p = network;
	const char *e = p + strlen(network);
	enum { ENTITY_INTERFACE, ENTITY_RECEIVE, ENTITY_SEND, ENTITY_ERROR } ec = ENTITY_INTERFACE;
	const char *b = p;		/* begin of entity */
	pgm_list_t* source_list = NULL;
	pgm_error_t* sub_error = NULL;

/* pre-conditions */
	pgm_assert (NULL != network);
	pgm_assert (AF_UNSPEC == family || AF_INET == family || AF_INET6 == family);
	pgm_assert (NULL != recv_list);
	pgm_assert (NULL != send_list);

	pgm_debug ("network_parse (network:%s%s%s family:%s recv_list:%p send_list:%p error:%p)",
		network ? "\"" : "", network ? network : "(null)", network ? "\"" : "",
		pgm_family_string (family),
		(const void*)recv_list,
		(const void*)send_list,
		(const void*)error);

	while (p < e)
	{
		if (!is_network_char (family, *p))
		{
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_IF,
				     PGM_ERROR_INVAL,
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
					pgm_assert_not_reached();
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
				if (!(sub_error && PGM_ERROR_XDEV == sub_error->code))
				{
/* fall through on multicast */
					if (!(sub_error && PGM_ERROR_NOTUNIQ == sub_error->code))
					{
						pgm_propagate_error (error, sub_error);
						goto free_lists;
					}
					pgm_clear_error (&sub_error);
/* FIXME: too many interfaces */
					if (pgm_list_length (source_list) > 1) {
						pgm_set_error (error,
				   			     PGM_ERROR_DOMAIN_IF,
							     PGM_ERROR_INVAL,
							     _("Send group list contains more than one entity."));
						goto free_lists;
					}
					break;
				}
				pgm_clear_error (&sub_error);
				while (source_list) {
					pgm_free (source_list->data);
					source_list = pgm_list_delete_link (source_list, source_list);
				}
				if (!parse_interface_entity (family, NULL, &source_list, &sub_error) &&
				    !(sub_error && PGM_ERROR_NOTUNIQ == sub_error->code))
				{
					pgm_propagate_error (error, sub_error);
					goto free_lists;
				}
				pgm_clear_error (&sub_error);
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
				pgm_assert_not_reached();
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
			if (!(sub_error && PGM_ERROR_XDEV == sub_error->code))
			{
/* fall through on multicast */
				if (!(sub_error && PGM_ERROR_NOTUNIQ == sub_error->code))
				{
					pgm_propagate_error (error, sub_error);
					goto free_lists;
				}
				pgm_clear_error (&sub_error);

/* FIXME: too many interfaces */
				if (pgm_list_length (source_list) > 1) {
					pgm_set_error (error,
			   			     PGM_ERROR_DOMAIN_IF,
						     PGM_ERROR_INVAL,
						     _("Send group list contains more than one entity."));
					goto free_lists;
				}
				break;
			}
			pgm_clear_error (&sub_error);
			while (source_list) {
				pgm_free (source_list->data);
				source_list = pgm_list_delete_link (source_list, source_list);
			}
			if (!parse_interface_entity (family, NULL, &source_list, &sub_error) &&
			    !(sub_error && PGM_ERROR_NOTUNIQ == sub_error->code))
			{
				pgm_propagate_error (error, sub_error);
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
			pgm_assert_not_reached();
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
			pgm_assert_not_reached();
			break;
		}
	}

	if (pgm_list_length (source_list) > 1)
		goto free_lists;

/* cleanup source interface list */
	while (source_list) {
		pgm_free (source_list->data);
		source_list = pgm_list_delete_link (source_list, source_list);
	}

	return TRUE;

free_lists:
	while (source_list) {
		pgm_free (source_list->data);
		source_list = pgm_list_delete_link (source_list, source_list);
	}
	while (*recv_list) {
		pgm_free ((*recv_list)->data);
		*recv_list = pgm_list_delete_link (*recv_list, *recv_list);
	}
	while (*send_list) {
		pgm_free ((*send_list)->data);
		*send_list = pgm_list_delete_link (*send_list, *send_list);
	}
	return FALSE;
}

/* create group_source_req as used by pgm_transport_create which specify port, address & interface.
 * gsr_source is copied from gsr_group for ASM, caller needs to populate gsr_source for SSM.
 *
 * returns TRUE on success, returns FALSE on error and sets error appropriately.
 */

bool
pgm_if_get_transport_info (
	const char*				 restrict network,
	const struct pgm_transport_info_t* const restrict hints,
	struct pgm_transport_info_t**		 restrict res,
	pgm_error_t**			         restrict error
	)
{
	struct pgm_transport_info_t* ti;
	const int family = hints ? hints->ti_family : AF_UNSPEC;
	pgm_list_t* recv_list = NULL;	/* <struct group_source_req*> */
	pgm_list_t* send_list = NULL;	/* <struct group_source_req*> */

	pgm_return_val_if_fail (NULL != network, FALSE);
	pgm_return_val_if_fail (AF_UNSPEC == family || AF_INET == family || AF_INET6 == family, FALSE);
	pgm_return_val_if_fail (NULL != res, FALSE);

	if (hints) {
		pgm_debug ("get_transport_info (network:%s%s%s hints: {family:%s} res:%p error:%p)",
			network ? "\"" : "", network ? network : "(null)", network ? "\"" : "",
			pgm_family_string (family),
			(const void*)res,
			(const void*)error);
	} else {
		pgm_debug ("get_transport_info (network:%s%s%s hints:%p res:%p error:%p)",
			network ? "\"" : "", network ? network : "(null)", network ? "\"" : "",
			(const void*)hints,
			(const void*)res,
			(const void*)error);
	}

	if (!network_parse (network, family, &recv_list, &send_list, error))
		return FALSE;
	const size_t recv_list_len = pgm_list_length (recv_list);
	const size_t send_list_len = pgm_list_length (send_list);
	ti = pgm_malloc0 (sizeof(struct pgm_transport_info_t) + 
			 (recv_list_len + send_list_len) * sizeof(struct group_source_req));
	ti->ti_recv_addrs_len = recv_list_len;
	ti->ti_recv_addrs = (void*)((char*)ti + sizeof(struct pgm_transport_info_t));
	ti->ti_send_addrs_len = send_list_len;
	ti->ti_send_addrs = (void*)((char*)ti->ti_recv_addrs + recv_list_len * sizeof(struct group_source_req));
	ti->ti_dport = DEFAULT_DATA_DESTINATION_PORT;
	ti->ti_sport = DEFAULT_DATA_SOURCE_PORT;
			
	size_t i = 0;
	while (recv_list) {
		memcpy (&ti->ti_recv_addrs[i++], recv_list->data, sizeof(struct group_source_req));
		pgm_free (recv_list->data);
		recv_list = pgm_list_delete_link (recv_list, recv_list);
	}
	i = 0;
	while (send_list) {
		memcpy (&ti->ti_send_addrs[i++], send_list->data, sizeof(struct group_source_req));
		pgm_free (send_list->data);
		send_list = pgm_list_delete_link (send_list, send_list);
	}
	*res = ti;
	return TRUE;
}

void
pgm_if_free_transport_info (
	struct pgm_transport_info_t*	res
	)
{
	pgm_free (res);
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
