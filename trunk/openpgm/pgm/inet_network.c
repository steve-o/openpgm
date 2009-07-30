/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable implementations of inet_network and inet_network6.
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
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <glib.h>

#include "pgm/inet_network.h"
#include "pgm/ip.h"

//#define INET_NETWORK_DEBUG

#ifndef INET_NETWORK_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* 127		=> 127.0.0.0
 * 127.1/8	=> 127.0.0.0
 */

int
_pgm_inet_network (
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
_pgm_inet6_network (
	const char* s,		/* NULL terminated */
	struct in6_addr* in6
	)
{
	g_return_val_if_fail (s != NULL, -EINVAL);

	g_trace ("if_inet6_network (\"%s\")", s);

/* inet_pton cannot parse IPv6 addresses with subnet declarations, so
 * chop them off.
 *
 * as we are dealing with network addresses IPv6 zone indices are not important
 * so we can use the inet_xtoy functions.
 */
	char s2[INET6_ADDRSTRLEN];
	const char *p = s;
	char* p2 = s2;
	const char *e = p + strlen(s);
	while (*p) {
		if (*p == '/') break;
		*p2++ = *p++;
	}
	if (p == e) {
		if (pgm_inet_pton (AF_INET6, s, in6)) return 0;
		g_trace ("inet_pton failed");
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}

	*p2 = 0;
//	g_trace ("net part %s", s2);
	if (!pgm_inet_pton (AF_INET6, s2, in6)) {
		g_trace ("inet_pton failed parsing network part %s", s2);
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}

#ifdef IF_DEBUG
	char sdebug[INET6_ADDRSTRLEN];
	g_trace ("IPv6 network address: %s", pgm_inet_ntop(AF_INET6, in6, sdebug, sizeof(sdebug)));
#endif

	p++;
	int val = 0;
	while (p < e)
	{
		if (isdigit(*p)) {
			val = 10 * val + (*p - '0');
		} else {
			g_trace ("failed parsing subnet size due to character '%c'", *p);
			memcpy (in6, &in6addr_any, sizeof(in6addr_any));
			return -1;
		}
		p++;
	}
	if (val == 0 || val > 128) {
		g_trace ("subnet size invalid (%d)", val);
		memcpy (in6, &in6addr_any, sizeof(in6addr_any));
		return -1;
	}
	g_trace ("subnet size %i", val);

/* zero out host bits */
	while (val < 128) {
		int byte_index = val / 8;
		int bit_index  = val % 8;

		in6->s6_addr[byte_index] &= ~(1 << bit_index);
		val++;
	}

	g_trace ("effective IPv6 network address after subnet mask: %s", pgm_inet_ntop(AF_INET6, in6, s2, sizeof(s2)));

	return 0;
}

/* eof */

