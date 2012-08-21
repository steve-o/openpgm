/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable implementations of inet_network and inet_network6.
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
#include <ctype.h>
#include <impl/framework.h>


//#define INET_NETWORK_DEBUG

/* locals */

static uint32_t cidr_to_netmask (const unsigned) PGM_GNUC_CONST;


/* calculate IPv4 netmask from network size, returns address in
 * host byte order.
 */

static
uint32_t
cidr_to_netmask (
	const unsigned	cidr
	)
{
	return (cidr == 0) ? 0 : (0xffffffff - (1 << (32 - cidr)) + 1);
}


/* Converts a numbers-and-dots notation string into a network number in
 * host order.
 * Note parameters and return value differs from inet_network().  This
 * function will not interpret octal numbers, preceeded with a 0, or
 * hexadecimal numbers, preceeded by 0x.
 *
 * 127		=> 127.0.0.0
 * 127.1/8	=> 127.0.0.0	-- 127.1.0.0
 *                                 inet_addr() would be 127.0.0.1
 *                                 inet_network() would be 0.0.127.1
 *
 * returns 0 on success, returns -1 on invalid address.
 */

PGM_GNUC_INTERNAL
int					/* return type to match inet_network() */
pgm_inet_network (
	const char*	restrict s,
	struct in_addr*	restrict in
	)
{
	const char	*p = s;
	unsigned	 val = 0;
	int		 shift = 24;

	pgm_return_val_if_fail (NULL != s,  -1);
	pgm_return_val_if_fail (NULL != in, -1);

	pgm_debug ("pgm_inet_network (s:\"%s\" in:%p)",
		 s, (const void*)in);

	in->s_addr = INADDR_ANY;

	while (*p)
	{
		if (isdigit (*p)) {
			val = 10 * val + (*p - '0');
		} else if (*p == '.' || *p == 0) {
			if (val > 0xff)
				goto default_none;

//g_trace ("elem %i", val);
			
			in->s_addr |= val << shift;
			val = 0;
			shift -= 8;
			if (shift < 0 && *p != 0)
				goto default_none;

		} else if (*p == '/') {
			if (val > 0xff)
				goto default_none;
//g_trace ("elem %i", val);
			in->s_addr |= val << shift;
			p++; val = 0;
			while (*p)
			{
				if (isdigit (*p))
					val = 10 * val + (*p - '0');
				else
					goto default_none;
				p++;
			}
			if (val == 0 || val > 32)
				goto default_none;
//g_trace ("bit mask %i", val);

/* zero out host bits */
			const struct in_addr netaddr = { .s_addr = cidr_to_netmask (val) };
#ifdef INET_NETWORK_DEBUG
{
g_debug ("netaddr %s", inet_ntoa (netaddr));
}
#endif
			in->s_addr &= netaddr.s_addr;
			return 0;
		
		} else if (*p == 'x' || *p == 'X') {	/* skip number, e.g. 1.x.x.x */
			if (val > 0)
				goto default_none;
			
		} else {
			goto default_none;
		}
		p++;
	}

	in->s_addr |= val << shift;
	return 0;

default_none:
	in->s_addr = INADDR_NONE;
	return -1;
}

/* Converts a numbers-and-dots notation string into an IPv6 network number.
 *
 * ::1/128	=> 0:0:0:0:0:0:0:1
 * ::1          => 0:0:0:0:0:0:0:1
 * ::1.2.3.4	=> 0:0:0:0:1.2.3.4
 *
 * returns 0 on success, returns -1 on invalid address.
 */

PGM_GNUC_INTERNAL
int
pgm_inet6_network (
	const char*	 restrict s,		/* NULL terminated */
	struct in6_addr* restrict in6
	)
{
	const char	*p = s;
	char		 s2[INET6_ADDRSTRLEN];
	char		*p2 = s2;
	unsigned	 val = 0;

	pgm_return_val_if_fail (NULL != s,   -1);
	pgm_return_val_if_fail (NULL != in6, -1);

	pgm_debug ("pgm_inet6_network (s:\"%s\" in6:%p)",
		 s, (const void*)in6);

/* inet_pton cannot parse IPv6 addresses with subnet declarations, so
 * chop them off.
 *
 * as we are dealing with network addresses IPv6 zone indices are not important
 * so we can use the inet_xtoy functions.
 */
/* first check for scope identifier and return failure */
	while (*p) {
		if (*p == '%') {
			pgm_debug ("pgm_inet_pton(AF_INET6) failed due to presence of scope identifier.");
			goto default_none;
		}
		p++;
	}

	p = s;
	while (*p) {
		if (*p == '/') break;
		*p2++ = *p++;
	}
	if (*p == 0) {
		if (pgm_inet_pton (AF_INET6, s, in6)) return 0;
		pgm_debug ("pgm_inet_pton(AF_INET6) failed on '%s'", s);
		goto default_none;
	}

	*p2 = 0;
	pgm_debug ("net part %s", s2);
	if (!pgm_inet_pton (AF_INET6, s2, in6)) {
		pgm_debug ("pgm_inet_pton(AF_INET) failed parsing network part '%s'", s2);
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		goto default_none;
	}

#ifdef INET_NETWORK_DEBUG
	char sdebug[INET6_ADDRSTRLEN];
	pgm_debug ("IPv6 network address: %s", pgm_inet_ntop(AF_INET6, in6, sdebug, sizeof(sdebug)));
#endif

	p++;
	while (*p)
	{
		if (isdigit(*p)) {
			val = 10 * val + (*p - '0');
		} else {
			pgm_debug ("failed parsing subnet size due to character '%c'", *p);
			goto default_none;
		}
		p++;
	}
	if (val == 0 || val > 128) {
		pgm_debug ("subnet size invalid (%d)", val);
		goto default_none;
	}
	pgm_debug ("subnet size %i", val);

/* zero out host bits */
	const unsigned suffix_length = 128 - val;
	for (int i = suffix_length, j = 15; i > 0; i -= 8, --j)
	{
		in6->s6_addr[ j ] &= i >= 8 ? 0x00 : (unsigned)(( 0xffU << i ) & 0xffU );
	}

	pgm_debug ("effective IPv6 network address after subnet mask: %s",
		pgm_inet_ntop(AF_INET6, in6, s2, sizeof(s2)));
	return 0;

default_none:
	memset (in6, 0xff, sizeof(*in6));	/* equivalent to IN6ADDR_NONE */
	return -1;
}

/* sockaddr_in6 version of pgm_inet6_network, we use sockaddr in order to
 * resolve and keep the scope identifier.
 */

PGM_GNUC_INTERNAL
int
pgm_sa6_network (
	const char*	     restrict s,		/* NULL terminated */
	struct sockaddr_in6* restrict sa6
	)
{
	const char	*p = s;
	char		 s2[INET6_ADDRSTRLEN];
	char		*p2 = s2;
	unsigned	 val = 0;
	struct addrinfo	 hints = {
		.ai_family	= AF_INET6,
		.ai_socktype	= SOCK_STREAM,		/* not really */
		.ai_protocol	= IPPROTO_TCP,		/* not really */
		.ai_flags	= AI_NUMERICHOST
	}, *result = NULL;

	pgm_return_val_if_fail (NULL != s,   -1);
	pgm_return_val_if_fail (NULL != sa6, -1);

	pgm_debug ("pgm_sa6_network (s:\"%s\" sa6:%p)",
		 s, (const void*)sa6);

/* getaddrinfo cannot parse IPv6 addresses with subnet declarations, so
 * chop them off.
 */
	p = s;
	while (*p) {
		if (*p == '/') break;
		*p2++ = *p++;
	}
	if (*p == 0) {
		if (0 == getaddrinfo (s, NULL, &hints, &result)) {
			memcpy (sa6, result->ai_addr, result->ai_addrlen);
			freeaddrinfo (result);
			return 0;
		}
		pgm_debug ("getaddrinfo(AF_INET6) failed on '%s'", s);
		goto default_none;
	}

	*p2 = 0;
	pgm_debug ("net part %s", s2);
	if (0 != getaddrinfo (s2, NULL, &hints, &result)) {
		pgm_debug ("getaddrinfo(AF_INET) failed parsing network part '%s'", s2);
		goto default_none;
	}

	memcpy (sa6, result->ai_addr, result->ai_addrlen);
	freeaddrinfo (result);

#ifdef INET_NETWORK_DEBUG
	char sdebug[INET6_ADDRSTRLEN];
	pgm_sockaddr_ntop ((const struct sockaddr*)sa6, sdebug, sizeof(sdebug));
	pgm_debug ("IPv6 network address: %s", sdebug);
#endif

	p++;
	while (*p)
	{
		if (isdigit(*p)) {
			val = 10 * val + (*p - '0');
		} else {
			pgm_debug ("failed parsing subnet size due to character '%c'", *p);
			goto default_none;
		}
		p++;
	}
	if (val == 0 || val > 128) {
		pgm_debug ("subnet size invalid (%d)", val);
		goto default_none;
	}
	pgm_debug ("subnet size %i", val);

/* zero out host bits */
	const unsigned suffix_length = 128 - val;
	for (int i = suffix_length, j = 15; i > 0; i -= 8, --j)
	{
		sa6->sin6_addr.s6_addr[ j ] &= i >= 8 ? 0x00 : (unsigned)(( 0xffU << i ) & 0xffU );
	}

#ifdef INET_NETWORK_DEBUG
	pgm_sockaddr_ntop ((const struct sockaddr*)sa6, sdebug, sizeof(sdebug));
	pgm_debug ("effective IPv6 network address after subnet mask: %s", sdebug);
#endif
	return 0;

default_none:
	memset (sa6, 0, sizeof(*sa6));
	sa6->sin6_family = AF_INET6;
	memset (&sa6->sin6_addr, 0xff, sizeof(struct in6_addr));	/* equivalent to IN6ADDR_NONE */
	return -1;
}

/* create an internet address from network & host.
 *
 * expect compiler warnings on return type due to compatibility with inet_makeaddr.
 */

PGM_GNUC_INTERNAL
struct in_addr
pgm_inet_makeaddr (
	uint32_t	net,
	uint32_t	host
	)
{
	uint32_t addr;

	if (net < 128)
		addr = (net << IN_CLASSA_NSHIFT) | (host & IN_CLASSA_HOST);
	else if (net < 65536)
		addr = (net << IN_CLASSB_NSHIFT) | (host & IN_CLASSB_HOST);
	else if (net < 16777216UL)
		addr = (net << IN_CLASSC_NSHIFT) | (host & IN_CLASSC_HOST);
	else
		addr = net | host;
	addr = htonl (addr);
	return (*(struct in_addr*)&addr);
}

/* eof */

