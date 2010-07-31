/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * struct sockaddr functions independent of in or in6.
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

#if !defined (__PGM_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#ifndef __PGM_SOCKADDR_H__
#define __PGM_SOCKADDR_H__

#ifndef _WIN32
#	include <sys/socket.h>
#endif
#include <pgm/types.h>

PGM_BEGIN_DECLS

/* fallback values where not directly supported */
#ifndef MSG_DONTWAIT
#	define MSG_DONTWAIT		0
#endif
#ifndef MSG_ERRQUEUE
#	define MSG_ERRQUEUE		0x2000
#endif
#if !defined(EAFNOSUPPORT) && defined(WSAEAFNOSUPPORT)
#	define EAFNOSUPPORT		WSAEAFNOSUPPORT
#endif
#if !defined(CONFIG_HAVE_GROUP_REQ)
/* sections 5 and 8.2 of RFC 3768: Multicast group request */
struct group_req
{
	uint32_t		gr_interface;	/* interface index */
	struct sockaddr_storage	gr_group;	/* group address */
};

struct group_source_req
{
	uint32_t		gsr_interface;	/* interface index */
	struct sockaddr_storage	gsr_group;	/* group address */
	struct sockaddr_storage	gsr_source;	/* group source */
};
#endif /* !CONFIG_HAVE_GROUP_REQ */

sa_family_t pgm_sockaddr_family (const struct sockaddr* sa);
uint16_t pgm_sockaddr_port (const struct sockaddr* sa);
socklen_t pgm_sockaddr_len (const struct sockaddr* sa);
socklen_t pgm_sockaddr_storage_len (const struct sockaddr_storage* ss);
uint32_t pgm_sockaddr_scope_id (const struct sockaddr* sa);
int pgm_sockaddr_ntop (const struct sockaddr*restrict sa, char*restrict dst, size_t ulen);
int pgm_sockaddr_pton (const char*restrict src, struct sockaddr*restrict dst);
int pgm_sockaddr_is_addr_multicast (const struct sockaddr* sa);
int pgm_sockaddr_cmp (const struct sockaddr*restrict sa1, const struct sockaddr*restrict sa2);
int pgm_sockaddr_hdrincl (const int s, const sa_family_t sa_family, const bool v);
int pgm_sockaddr_pktinfo (const int s, const sa_family_t sa_family, const bool v);
int pgm_sockaddr_router_alert (const int s, const sa_family_t sa_family, const bool v);
int pgm_sockaddr_tos (const int s, const sa_family_t sa_family, const int tos);
int pgm_sockaddr_join_group (const int s, const sa_family_t sa_family, const struct group_req* gr);
int pgm_sockaddr_join_source_group (const int s, const sa_family_t sa_family, const struct group_source_req* gsr);
int pgm_sockaddr_multicast_if (int s, const struct sockaddr* address, unsigned ifindex);
int pgm_sockaddr_multicast_loop (const int s, const sa_family_t sa_family, const bool v);
int pgm_sockaddr_multicast_hops (const int s, const sa_family_t sa_family, const unsigned hops);
void pgm_sockaddr_nonblocking (const int s, const bool v);

const char* pgm_inet_ntop (int af, const void*restrict src, char*restrict dst, socklen_t size);
int pgm_inet_pton (int af, const char*restrict src, void*restrict dst);

int pgm_nla_to_sockaddr (const void*restrict nla, struct sockaddr*restrict sa);
int pgm_sockaddr_to_nla (const struct sockaddr*restrict sa, void*restrict nla);

PGM_END_DECLS

#endif /* __PGM_SOCKADDR_H__ */
