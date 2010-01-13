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

#ifndef __PGM_SOCKADDR_H__
#define __PGM_SOCKADDR_H__

#include <glib.h>

#ifdef G_OS_UNIX
#	include <fcntl.h>
#	include <netdb.h>
#	include <string.h>
#	include <unistd.h>
#	include <netinet/in.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#else
#	include <ws2tcpip.h>
#endif

#ifndef MSG_DONTWAIT
#	define MSG_DONTWAIT		0
#endif
#ifndef MSG_ERRQUEUE
#	define MSG_ERRQUEUE		0x2000
#endif
#ifdef G_OS_WIN32
#	define EAFNOSUPPORT		WSAEAFNOSUPPORT
#endif


G_BEGIN_DECLS

gushort pgm_sockaddr_family (const struct sockaddr* sa);
guint16 pgm_sockaddr_port (const struct sockaddr* sa);
socklen_t pgm_sockaddr_len (const struct sockaddr* sa);
socklen_t pgm_sockaddr_storage_len (const struct sockaddr_storage* ss);
guint32 pgm_sockaddr_scope_id (const struct sockaddr* sa);
int pgm_sockaddr_ntop (const struct sockaddr* sa, char* dst, size_t ulen);
int pgm_sockaddr_pton (const char* src, gpointer dst);
int pgm_sockaddr_is_addr_multicast (const struct sockaddr* sa);
int pgm_sockaddr_cmp (const struct sockaddr *sa1, const struct sockaddr *sa2);
int pgm_sockaddr_hdrincl (const int s, const int sa_family, const gboolean v);
int pgm_sockaddr_pktinfo (const int s, const int sa_family, const gboolean v);
int pgm_sockaddr_router_alert (const int s, const int sa_family, const gboolean v);
int pgm_sockaddr_tos (const int s, const int sa_family, const int tos);
int pgm_sockaddr_join_group (const int s, const int sa_family, const struct group_req* gr);
int pgm_sockaddr_join_source_group (const int s, const int sa_family, const struct group_source_req* gsr);
int pgm_sockaddr_multicast_if (int s, const struct sockaddr* address, int ifindex);
int pgm_sockaddr_multicast_loop (const int s, const int sa_family, const gboolean v);
int pgm_sockaddr_multicast_hops (const int s, const int sa_family, const gint hops);
void pgm_sockaddr_nonblocking (const int s, const gboolean v);

const char* pgm_inet_ntop (int af, const void* src, char* dst, socklen_t size);
int pgm_inet_pton (int af, const char* src, void* dst);

int pgm_nla_to_sockaddr (gconstpointer nla, struct sockaddr* sa);
int pgm_sockaddr_to_nla (const struct sockaddr* sa, gpointer nla);

G_END_DECLS

#endif /* __PGM_SOCKADDR_H__ */
