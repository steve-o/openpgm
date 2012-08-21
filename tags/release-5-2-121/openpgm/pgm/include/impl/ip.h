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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#       error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_IP_H__
#define __PGM_IMPL_IP_H__

#ifndef _WIN32
#	include <netinet/in.h>
#	include <sys/param.h>
#endif
#include <pgm/types.h>

PGM_BEGIN_DECLS

/* Byte alignment for packet memory maps.
 * NB: Solaris and OpenSolaris don't support #pragma pack(push) even on x86.
 */
#if defined( __GNUC__ ) && !defined( __sun ) && !defined( __CYGWIN__ )
#	pragma pack(push)
#endif
#pragma pack(1)

/* RFC 791 */

/* nb: first four bytes are forced bitfields for win32 "feature" */
struct pgm_ip
{
#if (defined( __sun ) && defined( _BIT_FIELDS_LTOH )) || (!defined( __sun ) && __BYTE_ORDER == __LITTLE_ENDIAN)
	unsigned 	ip_hl:4;		/* header length */
	unsigned 	ip_v:4;			/* version */
#else
	unsigned 	ip_v:4;			/* version */
	unsigned 	ip_hl:4;		/* header length */
#endif
	unsigned 	ip_tos:8;		/* type of service */
	unsigned 	ip_len:16;		/* total length */
	uint16_t	ip_id;			/* identification */
	uint16_t	ip_off;			/* fragment offset field */
	uint8_t		ip_ttl;			/* time to live */
	uint8_t		ip_p;			/* protocol */
	uint16_t	ip_sum;			/* checksum */
	struct in_addr	ip_src, ip_dst;		/* source and dest address */
};

PGM_STATIC_ASSERT(sizeof(struct pgm_ip) == 20);

/* RFC 2460 */
#ifdef ip6_vfc
#	undef ip6_vfc
#endif
#ifdef ip6_plen
#	undef ip6_plen
#endif
#ifdef ip6_nxt
#	undef ip6_nxt
#endif
#ifdef ip6_hops
#	undef ip6_hops
#endif
struct pgm_ip6_hdr
{
	uint32_t	ip6_vfc;		/* version:4, traffic class:8, flow label:20 */
	uint16_t	ip6_plen;		/* payload length: packet length - 40 */
	uint8_t		ip6_nxt;		/* next header type */
	uint8_t		ip6_hops;		/* hop limit */
	struct in6_addr	ip6_src, ip6_dst;	/* source and dest address */
};

PGM_STATIC_ASSERT(sizeof(struct pgm_ip6_hdr) == 40);

#define PGM_IPOPT_EOL		0	/* end of option list */
#define PGM_IPOPT_NOP		1	/* no operation */
#define PGM_IPOPT_RR		7	/* record packet route */
#define PGM_IPOPT_TS		68	/* timestamp */
#define PGM_IPOPT_SECURITY	130	/* provide s, c, h, tcc */
#define PGM_IPOPT_LSRR		131	/* loose source route */
#define PGM_IPOPT_ESO		133
#define PGM_IPOPT_CIPSO		134
#define PGM_IPOPT_SATID		136	/* satnet id */
#define PGM_IPOPT_SSRR		137	/* strict source route */
#define PGM_IPOPT_RA		148	/* router alert */

/* RFC 768 */
struct pgm_udphdr
{
	in_port_t	uh_sport;		/* source port */
	in_port_t	uh_dport;		/* destination port */
	uint16_t	uh_ulen;		/* udp length */
	uint16_t	uh_sum;			/* udp checksum */
};

PGM_STATIC_ASSERT(sizeof(struct pgm_udphdr) == 8);

#if defined( __GNUC__ ) && !defined( __sun ) && !defined( __CYGWIN__ )
#	pragma pack(pop)
#else
#	pragma pack()
#endif

PGM_END_DECLS

#endif /* __PGM_IMPL_IP_H__ */
