/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * struct sockaddr functions independent of in or in6.
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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_SOCKADDR_H__
#define __PGM_IMPL_SOCKADDR_H__

#ifndef _WIN32
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netdb.h>
#endif
#if defined( __APPLE__ ) || defined( __FreeBSD__ )
/* incomplete RFC 3678 API support */
#	include <pgm/in.h>
#endif
#include <pgm/types.h>
#include <impl/security.h>
#include <impl/wsastrerror.h>

PGM_BEGIN_DECLS

/* fallback values where not directly supported */
#ifndef MSG_DONTWAIT
#	define MSG_DONTWAIT		0
#endif
#ifndef MSG_ERRQUEUE
#	define MSG_ERRQUEUE		0x2000
#endif

#ifndef _WIN32
#	define SOCKET				int
#	define INVALID_SOCKET			(int)-1
#	define SOCKET_ERROR			(int)-1
#	define PGM_SOCK_EAGAIN			EAGAIN
#	define PGM_SOCK_ECONNRESET		ECONNRESET
#	define PGM_SOCK_EHOSTUNREACH		EHOSTUNREACH
#	define PGM_SOCK_EINTR			EINTR
#	define PGM_SOCK_EINVAL			EINVAL
#	define PGM_SOCK_ENETUNREACH		ENETUNREACH
#	define PGM_SOCK_ENOBUFS			ENOBUFS
#	define closesocket			close
#	define ioctlsocket			ioctl
#	define pgm_error_from_sock_errno	pgm_error_from_errno

static inline
int
pgm_get_last_sock_error (void)
{
	return errno;
}

static inline
void
pgm_set_last_sock_error (int errnum)
{
	errno = errnum;
}

static inline
char*
pgm_sock_strerror_s (char *buffer, size_t size, int errnum)
{
	return pgm_strerror_s (buffer, size, errnum);
}

static inline
char*
pgm_gai_strerror_s (char* buffer, size_t size, int errnum)
{
	pgm_strncpy_s (buffer, size, gai_strerror (errnum), _TRUNCATE);
	return buffer;
}

#else
#	define PGM_SOCK_EAGAIN			WSAEWOULDBLOCK
#	define PGM_SOCK_ECONNRESET		WSAECONNRESET
#	define PGM_SOCK_EHOSTUNREACH		WSAEHOSTUNREACH
#	define PGM_SOCK_EINTR			WSAEINTR
#	define PGM_SOCK_EINVAL			WSAEINVAL
#	define PGM_SOCK_ENETUNREACH		WSAENETUNREACH
#	define PGM_SOCK_ENOBUFS			WSAENOBUFS
#	define pgm_get_last_sock_error()	WSAGetLastError()
#	define pgm_set_last_sock_error(e)	WSASetLastError(e)
#	define pgm_error_from_sock_errno	pgm_error_from_wsa_errno

static inline
char*
pgm_sock_strerror_s (char *buffer, size_t size, int errnum)
{
	pgm_strncpy_s (buffer, size, pgm_wsastrerror (errnum), _TRUNCATE);
	return buffer;
}

static inline
char*
pgm_gai_strerror_s (char* buffer, size_t size, int errnum)
{
/* Windows maps EAI error codes onto WSA error codes */
	return pgm_sock_strerror_s (buffer, size, errnum);
}

#endif

PGM_GNUC_INTERNAL sa_family_t pgm_sockaddr_family (const struct sockaddr* sa);
PGM_GNUC_INTERNAL in_port_t pgm_sockaddr_port (const struct sockaddr* sa);
PGM_GNUC_INTERNAL socklen_t pgm_sockaddr_len (const struct sockaddr* sa);
PGM_GNUC_INTERNAL socklen_t pgm_sockaddr_storage_len (const struct sockaddr_storage* ss);
PGM_GNUC_INTERNAL uint8_t pgm_sockaddr_prefixlen (const struct sockaddr* sa);
PGM_GNUC_INTERNAL uint32_t pgm_sockaddr_scope_id (const struct sockaddr* sa);
PGM_GNUC_INTERNAL int pgm_sockaddr_ntop (const struct sockaddr*restrict sa, char*restrict dst, size_t ulen);
PGM_GNUC_INTERNAL int pgm_sockaddr_pton (const char*restrict src, struct sockaddr*restrict dst);
PGM_GNUC_INTERNAL int pgm_sockaddr_is_addr_multicast (const struct sockaddr* sa);
PGM_GNUC_INTERNAL int pgm_sockaddr_is_addr_unspecified (const struct sockaddr* sa);
PGM_GNUC_INTERNAL int pgm_sockaddr_cmp (const struct sockaddr*restrict sa1, const struct sockaddr*restrict sa2);
PGM_GNUC_INTERNAL int pgm_sockaddr_hdrincl (const SOCKET s, const sa_family_t sa_family, const bool v);
PGM_GNUC_INTERNAL int pgm_sockaddr_pktinfo (const SOCKET s, const sa_family_t sa_family, const bool v);
PGM_GNUC_INTERNAL int pgm_sockaddr_router_alert (const SOCKET s, const sa_family_t sa_family, const bool v);
PGM_GNUC_INTERNAL int pgm_sockaddr_tos (const SOCKET s, const sa_family_t sa_family, const int tos);
PGM_GNUC_INTERNAL int pgm_sockaddr_join_group (const SOCKET s, const sa_family_t sa_family, const struct group_req* gr);
PGM_GNUC_INTERNAL int pgm_sockaddr_leave_group (const SOCKET s, const sa_family_t sa_family, const struct group_req* gr);
PGM_GNUC_INTERNAL int pgm_sockaddr_block_source (const SOCKET s, const sa_family_t sa_family, const struct group_source_req* gsr);
PGM_GNUC_INTERNAL int pgm_sockaddr_unblock_source (const SOCKET s, const sa_family_t sa_family, const struct group_source_req* gsr);
PGM_GNUC_INTERNAL int pgm_sockaddr_join_source_group (const SOCKET s, const sa_family_t sa_family, const struct group_source_req* gsr);
PGM_GNUC_INTERNAL int pgm_sockaddr_leave_source_group (const SOCKET s, const sa_family_t sa_family, const struct group_source_req* gsr);
#ifndef GROUP_FILTER_SIZE
#	define GROUP_FILTER_SIZE(numsrc) (sizeof (struct group_filter)		\
					 - sizeof (struct sockaddr_storage)	\
					 + ((numsrc)				\
					    * sizeof (struct sockaddr_storage)))
#endif
#ifndef IP_MSFILTER_SIZE
#	define IP_MSFILTER_SIZE(numsrc) (sizeof (struct ip_msfilter)		\
					- sizeof (struct in_addr)		\
					+ ((numsrc)				\
					   * sizeof (struct in_addr)))
#endif
PGM_GNUC_INTERNAL int pgm_sockaddr_msfilter (const SOCKET s, const sa_family_t sa_family, const struct group_filter* gf_list);
PGM_GNUC_INTERNAL int pgm_sockaddr_multicast_if (const SOCKET s, const struct sockaddr* address, const unsigned ifindex);
PGM_GNUC_INTERNAL int pgm_sockaddr_multicast_loop (const SOCKET s, const sa_family_t sa_family, const bool v);
PGM_GNUC_INTERNAL int pgm_sockaddr_multicast_hops (const SOCKET s, const sa_family_t sa_family, const unsigned hops);
PGM_GNUC_INTERNAL void pgm_sockaddr_nonblocking (const SOCKET s, const bool v);

PGM_GNUC_INTERNAL const char* pgm_inet_ntop (int af, const void*restrict src, char*restrict dst, socklen_t size);
PGM_GNUC_INTERNAL int pgm_inet_pton (int af, const char*restrict src, void*restrict dst);

PGM_GNUC_INTERNAL int pgm_nla_to_sockaddr (const void*restrict nla, struct sockaddr*restrict sa);
PGM_GNUC_INTERNAL int pgm_sockaddr_to_nla (const struct sockaddr*restrict sa, void*restrict nla);

PGM_END_DECLS

#endif /* __PGM_IMPL_SOCKADDR_H__ */
