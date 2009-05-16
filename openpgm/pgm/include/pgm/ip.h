/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * Internet header for protocol version 4, RFC 791.
 *
 * Copyright (c) 1982, 1986, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifndef __PGM_IP_H__
#define __PGM_IP_H__

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#ifndef __PGM_SOCKADDR_H__
#	include "pgm/sockaddr.h"
#endif


/* RFC 791 */
struct pgm_ip
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	unsigned int	ip_hl:4;		/* header length */
	unsigned int	ip_v:4;			/* version */
#elif G_BYTE_ORDER == G_BIG_ENDIAN
	unsigned int	ip_v:4;			/* version */
	unsigned int	ip_hl:4;		/* header length */
#else
#	error unknown ENDIAN type
#endif
	guint8		ip_tos;			/* type of service */
	guint16		ip_len;			/* total length */
	guint16		ip_id;			/* identification */
	guint16		ip_off;			/* fragment offset field */
	guint8		ip_ttl;			/* time to live */
	guint8		ip_p;			/* protocol */
	guint16		ip_sum;			/* checksum */
	struct in_addr	ip_src, ip_dst;		/* source and dest address */
};

/* RFC 2460 */
struct pgm_ip6_hdr
{
	guint32		ip6_vfc;		/* version:4, traffic class:8, flow label:20 */
	guint16		ip6_plen;		/* payload length: packet length - 40 */
	guint8		ip6_nxt;		/* next header type */
	guint8		ip6_hops;		/* hop limit */
	struct in6_addr	ip6_src, ip6_dst;	/* source and dest address */
};

/* RFC 768 */
struct pgm_udphdr
{
	guint16		uh_sport;		/* source port */
	guint16		uh_dport;		/* destination port */
	guint16		uh_ulen;		/* udp length */
	guint16		uh_sum;			/* udp checksum */
};

G_BEGIN_DECLS


/* Note that are sockaddr structure is not passed these functions inherently
 * cannot support IPv6 Zone Indices and hence are rather limited for the
 * link-local scope.
 */
static inline const char* pgm_inet_ntop (int af, const void* src, char* dst, socklen_t size)
{
	g_assert (AF_INET == af || AF_INET6 == af);
	g_assert (NULL != src);
	g_assert (NULL != dst);
	g_assert (size > 0);

	switch (af) {
	case AF_INET:
	{
		struct sockaddr_in sin;
		memset (&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_addr   = *(const struct in_addr*)src;
		getnameinfo ((struct sockaddr*)&sin, sizeof(sin),
			     dst, size,
			     NULL, 0,
			     NI_NUMERICHOST);
		return dst;
	}
	case AF_INET6:
	{
		struct sockaddr_in6 sin6;
		memset (&sin6, 0, sizeof(sin6));
		sin6.sin6_family = AF_INET6;
		sin6.sin6_addr   = *(const struct in6_addr*)src;
		getnameinfo ((struct sockaddr*)&sin6, sizeof(sin6),
			     dst, size,
			     NULL, 0,
			     NI_NUMERICHOST);
		return dst;
	}
	}

	errno = EAFNOSUPPORT;
	return NULL;
}

static inline int pgm_inet_pton (int af, const char* src, void* dst)
{
	g_assert (AF_INET == af || AF_INET6 == af);
	g_assert (NULL != src);
	g_assert (NULL != dst);

	struct addrinfo hints = {
		.ai_family	= af,
		.ai_socktype	= SOCK_STREAM,		/* not really */
		.ai_protocol	= IPPROTO_TCP,		/* not really */
		.ai_flags	= AI_NUMERICHOST
	}, *result;

	int e = getaddrinfo (src, NULL, &hints, &result);
	if (0 != e) {
		return 0;	/* error */
	}

	g_assert (NULL != result->ai_addr);
	switch (pgm_sockaddr_family (result->ai_addr)) {
	case AF_INET:
		g_assert (sizeof(struct sockaddr_in) == pgm_sockaddr_len (result->ai_addr));
		break;

	case AF_INET6:
		g_assert (sizeof(struct sockaddr_in6) == pgm_sockaddr_len (result->ai_addr));
		break;
	}

	memcpy (dst, pgm_sockaddr_addr (result->ai_addr), pgm_sockaddr_addr_len (result->ai_addr));
	freeaddrinfo (result);
	return 1;	/* success */
}

G_END_DECLS

#endif /* __PGM_IP_H__ */
