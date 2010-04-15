/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable implementations of inet_network and inet_network6.
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

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include <glib.h>

#ifdef G_OS_UNIX
#	include <netinet/in.h>
#	include <netinet/ip.h>
#	include <sys/socket.h>
#else
#	include <ws2tcpip.h>
#	include <iphlpapi.h>
#endif

#include "pgm/messages.h"
#include "pgm/inet_network.h"
#include "pgm/sockaddr.h"


//#define INET_NETWORK_DEBUG



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

int					/* return type to match inet_network() */
pgm_inet_network (
	const char*		s,
	struct in_addr*		in
	)
{
	pgm_return_val_if_fail (NULL != s,  -1);
	pgm_return_val_if_fail (NULL != in, -1);

	pgm_debug ("pgm_inet_network (s:\"%s\" in:%p)",
		 s, (gpointer)in);

	const char *p = s;
	unsigned val = 0;
	int shift = 24;

	in->s_addr = INADDR_ANY;

	while (*p)
	{
		if (isdigit (*p)) {
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
			while (*p)
			{
				if (isdigit (*p)) {
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
			const struct in_addr netaddr = { .s_addr = cidr_to_netmask (val) };
#ifdef INET_NETWORK_DEBUG
{
g_trace ("netaddr %s", inet_ntoa (netaddr));
}
#endif
			in->s_addr &= netaddr.s_addr;
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

	in->s_addr |= val << shift;
	return 0;
}

/* Converts a numbers-and-dots notation string into an IPv6 network number.
 *
 * ::1/128	=> 0:0:0:0:0:0:0:1
 * ::1          => 0:0:0:0:0:0:0:1
 * ::1.2.3.4	=> 0:0:0:0:1.2.3.4
 *
 * returns 0 on success, returns -1 on invalid address.
 */

int
pgm_inet6_network (
	const char*		s,		/* NULL terminated */
	struct in6_addr*	in6
	)
{
	pgm_return_val_if_fail (NULL != s,   -1);
	pgm_return_val_if_fail (NULL != in6, -1);

	pgm_debug ("pgm_inet6_network (s:\"%s\" in6:%p)",
		 s, (gpointer)in6);

/* inet_pton cannot parse IPv6 addresses with subnet declarations, so
 * chop them off.
 *
 * as we are dealing with network addresses IPv6 zone indices are not important
 * so we can use the inet_xtoy functions.
 */
	char s2[INET6_ADDRSTRLEN];
	const char *p = s;
	char* p2 = s2;
	while (*p) {
		if (*p == '/') break;
		*p2++ = *p++;
	}
	if (*p == 0) {
		if (pgm_inet_pton (AF_INET6, s, in6)) return 0;
		pgm_debug ("inet_pton failed");
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}

	*p2 = 0;
	pgm_debug ("net part %s", s2);
	if (!pgm_inet_pton (AF_INET6, s2, in6)) {
		pgm_debug ("inet_pton failed parsing network part %s", s2);
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}

#ifdef INET_NETWORK_DEBUG
	char sdebug[INET6_ADDRSTRLEN];
	pgm_debug ("IPv6 network address: %s", pgm_inet_ntop(AF_INET6, in6, sdebug, sizeof(sdebug)));
#endif

	p++;
	unsigned val = 0;
	while (*p)
	{
		if (isdigit(*p)) {
			val = 10 * val + (*p - '0');
		} else {
			pgm_debug ("failed parsing subnet size due to character '%c'", *p);
			memcpy (in6, &in6addr_any, sizeof(in6addr_any));
			return -1;
		}
		p++;
	}
	if (val == 0 || val > 128) {
		pgm_debug ("subnet size invalid (%d)", val);
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}
	pgm_debug ("subnet size %i", val);

/* zero out host bits */
	const unsigned suffix_length = 128 - val;
	for (unsigned i = suffix_length, j = 15; i > 0; i -= 8, --j)
	{
		in6->s6_addr[ j ] &= i >= 8 ? 0x00 : (unsigned)(( 0xffU << i ) & 0xffU );
	}

	pgm_debug ("effective IPv6 network address after subnet mask: %s", pgm_inet_ntop(AF_INET6, in6, s2, sizeof(s2)));

	return 0;
}

/* eof */

