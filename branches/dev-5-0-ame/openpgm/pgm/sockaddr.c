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

#include <errno.h>
#ifndef _WIN32
#	include <sys/socket.h>
#	include <netdb.h>
#endif
#include <impl/framework.h>


/* FreeBSD */
#ifndef IPV6_ADD_MEMBERSHIP
#	define IPV6_ADD_MEMBERSHIP	IPV6_JOIN_GROUP
#	define IPV6_DROP_MEMBERSHIP	IPV6_LEAVE_GROUP
#endif
/* OpenSolaris differences */
#ifndef MCAST_MSFILTER
#	include <sys/ioctl.h>
#endif
#ifndef SOL_IP
#	define SOL_IP			IPPROTO_IP
#endif
#ifndef SOL_IPV6
#	define SOL_IPV6			IPPROTO_IPV6
#endif
#ifndef IP_MAX_MEMBERSHIPS
#	define IP_MAX_MEMBERSHIPS	20
#endif


sa_family_t
pgm_sockaddr_family (
	const struct sockaddr*	sa
	)
{
	return sa->sa_family;
}

uint16_t
pgm_sockaddr_port (
	const struct sockaddr*	sa
	)
{
	uint16_t sa_port;
	switch (sa->sa_family) {
	case AF_INET: {
		struct sockaddr_in s4;
		memcpy (&s4, sa, sizeof(s4));
		sa_port = s4.sin_port;
		break;
	}

	case AF_INET6: {
		struct sockaddr_in6 s6;
		memcpy (&s6, sa, sizeof(s6));
		sa_port = s6.sin6_port;
		break;
	}

	default:
		sa_port = 0;
		break;
	}
	return sa_port;
}

socklen_t
pgm_sockaddr_len (
	const struct sockaddr*	sa
	)
{
	socklen_t sa_len;
	switch (sa->sa_family) {
	case AF_INET:	sa_len = sizeof(struct sockaddr_in); break;
	case AF_INET6:	sa_len = sizeof(struct sockaddr_in6); break;
	default:	sa_len = 0; break;
	}
	return sa_len;
}

socklen_t
pgm_sockaddr_storage_len (
	const struct sockaddr_storage*	ss
	)
{
	socklen_t ss_len;
	switch (ss->ss_family) {
	case AF_INET:	ss_len = sizeof(struct sockaddr_in); break;
	case AF_INET6:	ss_len = sizeof(struct sockaddr_in6); break;
	default:	ss_len = 0; break;
	}
	return ss_len;
}

uint32_t
pgm_sockaddr_scope_id (
	const struct sockaddr*	sa
	)
{
	uint32_t scope_id;
	if (AF_INET6 == sa->sa_family) {
		struct sockaddr_in6 s6;
		memcpy (&s6, sa, sizeof(s6));
		scope_id = s6.sin6_scope_id;
	} else
		scope_id = 0;
	return scope_id;
}

int
pgm_sockaddr_ntop (
	const struct sockaddr* restrict sa,
	char*		       restrict	host,
	size_t			        hostlen
	)
{
	return getnameinfo (sa, pgm_sockaddr_len (sa),
			    host, hostlen,
			    NULL, 0,
			    NI_NUMERICHOST);
}

int
pgm_sockaddr_pton (
	const char*	 restrict src,
	struct sockaddr* restrict dst		/* will error on wrong size */
	)
{
	struct addrinfo hints = {
		.ai_family	= AF_UNSPEC,
		.ai_socktype	= SOCK_STREAM,		/* not really */
		.ai_protocol	= IPPROTO_TCP,		/* not really */
		.ai_flags	= AI_NUMERICHOST
	}, *result = NULL;
	const int status = getaddrinfo (src, NULL, &hints, &result);
	if (PGM_LIKELY(0 == status)) {
		memcpy (dst, result->ai_addr, result->ai_addrlen);
		freeaddrinfo (result);
		return 1;
	}
	return 0;
}

/* returns tri-state value: 1 if sa is multicast, 0 if sa is not multicast, -1 on error
 */

int
pgm_sockaddr_is_addr_multicast (
	const struct sockaddr*	sa
	)
{
	int retval;

	switch (sa->sa_family) {
	case AF_INET: {
		struct sockaddr_in s4;
		memcpy (&s4, sa, sizeof(s4));
		retval = IN_MULTICAST(ntohl( s4.sin_addr.s_addr ));
		break;
	}

	case AF_INET6: {
		struct sockaddr_in6 s6;
		memcpy (&s6, sa, sizeof(s6));
		retval = IN6_IS_ADDR_MULTICAST( &s6.sin6_addr );
		break;
	}

	default:
		retval = -1;
		break;
	}
	return retval;
}

int
pgm_sockaddr_cmp (
	const struct sockaddr* restrict sa1,
	const struct sockaddr* restrict sa2
	)
{
	int retval = 0;

	if (sa1->sa_family != sa2->sa_family)
		retval = sa1->sa_family < sa2->sa_family ? -1 : 1;
	else {
		switch (sa1->sa_family) {
		case AF_INET: {
			struct sockaddr_in sa1_in, sa2_in;
			memcpy (&sa1_in, sa1, sizeof(sa1_in));
			memcpy (&sa2_in, sa2, sizeof(sa2_in));
			if (sa1_in.sin_addr.s_addr != sa2_in.sin_addr.s_addr)
				retval = sa1_in.sin_addr.s_addr < sa2_in.sin_addr.s_addr ? -1 : 1;
			break;
		}

		case AF_INET6: {
			struct sockaddr_in6 sa1_in6, sa2_in6;
			memcpy (&sa1_in6, sa1, sizeof(sa1_in6));
			memcpy (&sa2_in6, sa2, sizeof(sa2_in6));
			retval = memcmp (&sa1_in6.sin6_addr, &sa2_in6.sin6_addr, sizeof(struct in6_addr));
			if (0 == retval && sa1_in6.sin6_scope_id != sa2_in6.sin6_scope_id)
				retval = sa1_in6.sin6_scope_id < sa2_in6.sin6_scope_id ? -1 : 1;
			break;
		}

		default:
			break;
		}
	}
	return retval;
}

/* IP header included with data.
 *
 * If no error occurs, pgm_sockaddr_hdrincl returns zero.  Otherwise, a value
 * of PGM_SOCKET_ERROR is returned, and a specific error code can be retrieved
 * by calling pgm_sock_errno().
 */

int
pgm_sockaddr_hdrincl (
	const int		s,
	const sa_family_t	sa_family,
	const bool		v
	)
{
	int retval = PGM_SOCKET_ERROR;

	switch (sa_family) {
	case AF_INET: {
#ifndef _WIN32
/* Solaris:ip(7P)  Mentioned but not detailed.
 *
 * Linux:ip(7) "A boolean integer flag is zero when it is false, otherwise
 * true.  If enabled, the user supplies an IP header in front of the user
 * data."  Mentions only send-side, nothing about receive-side.
 * Linux:raw(7) "For receiving the IP header is always included in the packet."
 *
 * FreeBSD,OS X:IP(4) provided by example "int hincl = 1;"
 *
 * Stevens: "IP_HDRINCL has datatype int."
 */
		const int optval = v ? 1 : 0;
#else
		const DWORD optval = v ? 1 : 0;
#endif
		retval = setsockopt (s, IPPROTO_IP, IP_HDRINCL, (const char*)&optval, sizeof(optval));
		break;
	}

	case AF_INET6:  /* method only exists on Win32, just ignore */
		retval = 0;
		break;

	default: break;
	}
	return retval;
}

/* Return destination IP address.
 *
 * If no error occurs, pgm_sockaddr_pktinfo returns zero.  Otherwise, a value
 * of PGM_SOCKET_ERROR is returned, and a specific error code can be retrieved
 * by calling pgm_sock_errno().
 */

int
pgm_sockaddr_pktinfo (
	const int		s,
	const sa_family_t	sa_family,
	const bool		v
	)
{
	int retval = PGM_SOCKET_ERROR;
#ifndef _WIN32
/* Solaris:ip(7P) "The following options take in_pktinfo_t as the parameter"
 * Completely different, although ip6(7P) is a little better, "The following
 * options are boolean switches controlling the reception of ancillary data"
 *
 * Linux:ip(7) "A boolean integer flag is zero when it is false, otherwise
 * true.  The argument is a flag that tells the socket whether the IP_PKTINFO
 * message should be passed or not."
 * Linux:ipv6(7) Not listed, however IPV6_PKTINFO is with "Argument is a pointer
 * to a boolean value in an integer."
 *
 * Absent from FreeBSD & OS X, suggested replacement IP_RECVDSTADDR.
 * OS X:IP6(4) "IPV6_PKTINFO int *"
 *
 * Stevens: "IP_RECVDSTADDR has datatype int."
 */
	const int optval = v ? 1 : 0;
#else
	const DWORD optval = v ? 1 : 0;
#endif

	switch (sa_family) {
	case AF_INET:
#ifdef IP_RECVDSTADDR
		retval = setsockopt (s, IPPROTO_IP, IP_RECVDSTADDR, (const char*)&optval, sizeof(optval));
#else
		retval = setsockopt (s, IPPROTO_IP, IP_PKTINFO, (const char*)&optval, sizeof(optval));
#endif
		break;

	case AF_INET6:
#ifdef IPV6_RECVPKTINFO
		retval = setsockopt (s, IPPROTO_IPV6, IPV6_RECVPKTINFO, (const char*)&optval, sizeof(optval));
#else
		retval = setsockopt (s, IPPROTO_IPV6, IPV6_PKTINFO, (const char*)&optval, sizeof(optval));
#endif
		break;

	default: break;
	}
	return retval;
}

/* Set IP Router Alert option for all outgoing packets.
 *
 * If no error occurs, pgm_sockaddr_router_alert returns zero.  Otherwise, a
 * value of PGM_SOCKET_ERROR is returned, and a specific error code can be
 * retrieved by calling pgm_sock_errno().
 */

int
pgm_sockaddr_router_alert (
	const int		s,
	const sa_family_t	sa_family,
	const bool		v
	)
{
	int retval = PGM_SOCKET_ERROR;
#ifdef CONFIG_IP_ROUTER_ALERT
/* Linux:ip(7) "A boolean integer flag is zero when it is false, otherwise
 * true.  Expects an integer flag."
 * Linux:ipv6(7) "Argument is a pointer to an integer."
 *
 * NB: Doesn't actually perform as expected, maybe optval should be different?
 */
	const int optval = v ? 1 : 0;

	switch (sa_family) {
	case AF_INET:
		retval = setsockopt (s, IPPROTO_IP, IP_ROUTER_ALERT, (const char*)&optval, sizeof(optval));
		break;

	case AF_INET6:
		retval = setsockopt (s, IPPROTO_IPV6, IPV6_ROUTER_ALERT, (const char*)&optval, sizeof(optval));
		break;

	default: break;
	}
#else
#	if defined(CONFIG_HAVE_IPOPTION)
/* NB: struct ipoption is not very portable and requires a lot of additional headers */
	const struct ipoption router_alert = {
		.ipopt_dst  = 0,
		.ipopt_list = { v ? PGM_IPOPT_RA : 0x00, v ? 0x04 : 0x00, 0x00, 0x00 }
	};
#	else
/* manually set the IP option */
	const int ipopt_ra = (PGM_IPOPT_RA << 24) | (0x04 << 16);
	const int router_alert = v ? htonl (ipopt_ra) : 0;
#	endif

	switch (sa_family) {
	case AF_INET:
/* Linux:ip(7) "The maximum option size for IPv4 is 40 bytes."
 */
		retval = setsockopt (s, IPPROTO_IP, IP_OPTIONS, (const char*)&router_alert, sizeof(router_alert));
retval = 0;
		break;

	default: break;
	}
#endif
	return retval;
}

/* Type-of-service and precedence.
 *
 * If no error occurs, pgm_sockaddr_tos returns zero.  Otherwise, a value of
 * PGM_SOCKET_ERROR is returned, and a specific error code can be retrieved by
 * calling pgm_sock_errno().
 */

int
pgm_sockaddr_tos (
	const int		s,
	const sa_family_t	sa_family,
	const int		tos
	)
{
	int retval = PGM_SOCKET_ERROR;

	switch (sa_family) {
	case AF_INET: {
#ifndef _WIN32
/* Solaris:ip(7P) "This option takes an integer argument as its input value."
 *
 * Linux:ip(7) "TOS is a byte."
 *
 * FreeBSD,OS X:IP(4) provided by example "int tos = IPTOS_LOWDELAY;"
 *
 * Stevens: "IP_TOS has datatype int."
 */
		const int optval = tos;
#else
/* IP_TOS only works on Win32 with system override:
 * http://support.microsoft.com/kb/248611
 * TODO: Implement GQoS (IPv4 only), qWAVE QOS is Vista+ only
 */
		const DWORD optval = tos;
#endif
		retval = setsockopt (s, IPPROTO_IP, IP_TOS, (const char*)&optval, sizeof(optval));
		break;
	}

	case AF_INET6:  /* TRAFFIC_CLASS not implemented */
		break;

	default: break;
	}
	return retval;
}

/* Join multicast group.
 * NB: IPV6_JOIN_GROUP == IPV6_ADD_MEMBERSHIP
 *
 * If no error occurs, pgm_sockaddr_join_group returns zero.  Otherwise, a
 * value of PGM_SOCKET_ERROR is returned, and a specific error code can be
 * retrieved by calling pgm_sock_errno().
 */

int
pgm_sockaddr_join_group (
	const int		s,
	const sa_family_t	sa_family,
	const struct group_req*	gr
	)
{
	int retval = PGM_SOCKET_ERROR;
#ifdef CONFIG_HAVE_MCAST_JOIN
/* Solaris:ip(7P) "The following options take a struct ip_mreq_source as the
 * parameter."  Presumably with source field zeroed out.
 * Solaris:ip6(7P) "Takes a struct group_req as the parameter."
 * Different type for each family, however group_req is protocol-independent.
 *
 * Stevens: "MCAST_JOIN_GROUP has datatype group_req{}."
 *
 * RFC3678: Argument type struct group_req
 */
	const int recv_level = (AF_INET == sa_family) ? SOL_IP : SOL_IPV6;
	retval = setsockopt (s, recv_level, MCAST_JOIN_GROUP, gr, sizeof(struct group_req));
#else
	switch (sa_family) {
	case AF_INET: {
/* Solaris:ip(7P) Just mentions "Join a multicast group."
 * No further details provided.
 *
 * Linux:ip(7) "Argument is an ip_mreqn structure.  For compatibility, the old
 * ip_mreq structure (present since Linux 1.2) is still supported."
 *
 * FreeBSD,OS X:IP(4) provided by example "struct ip_mreq mreq;"
 *
 * Stevens: "IP_ADD_MEMBERSHIP has datatype ip_mreq{}."
 *
 * RFC3678: Argument type struct ip_mreq
 */
#ifdef CONFIG_HAVE_IP_MREQN
		struct ip_mreqn mreqn;
		struct sockaddr_in ifaddr;
		memset (&mreqn, 0, sizeof(mreqn));
		mreqn.imr_multiaddr.s_addr = ((const struct sockaddr_in*)&gr->gr_group)->sin_addr.s_addr;
		if (!pgm_if_indextoaddr (gr->gr_interface, AF_INET, 0, (struct sockaddr*)&ifaddr, NULL))
			return -1;
		mreqn.imr_address.s_addr = ifaddr.sin_addr.s_addr;
		mreqn.imr_ifindex = gr->gr_interface;
		retval = setsockopt (s, SOL_IP, IP_ADD_MEMBERSHIP, (const char*)&mreqn, sizeof(mreqn));
#else
		struct ip_mreq mreq;
		struct sockaddr_in ifaddr;
		memset (&mreq, 0, sizeof(mreq));
		mreq.imr_multiaddr.s_addr = ((const struct sockaddr_in*)&gr->gr_group)->sin_addr.s_addr;
		if (!pgm_if_indextoaddr (gr->gr_interface, AF_INET, 0, (struct sockaddr*)&ifaddr, NULL))
			return -1;
		mreq.imr_interface.s_addr = ifaddr.sin_addr.s_addr;
		retval = setsockopt (s, SOL_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
#endif /* !CONFIG_HAVE_IP_MREQN */
		break;
	}

	case AF_INET6: {
/* Solaris:ip6(7P) "Takes a struct ipv6_mreq as the parameter;"
 *
 * Linux:ipv6(7) "Argument is a pointer to a struct ipv6_mreq structure."
 *
 * OS X:IP6(4) "IPV6_JOIN_GROUP struct ipv6_mreq *"
 *
 * Stevens: "IPV6_JOIN_GROUP has datatype ipv6_mreq{}."
 */
		struct ipv6_mreq mreq6;
		memset (&mreq6, 0, sizeof(mreq6));
		mreq6.ipv6mr_multiaddr = ((const struct sockaddr_in6*)&gr->gr_group)->sin6_addr;
		mreq6.ipv6mr_interface = gr->gr_interface;
		retval = setsockopt (s, SOL_IPV6, IPV6_ADD_MEMBERSHIP, (const char*)&mreq6, sizeof(mreq6));
		break;
	}

	default: break;
	}
#endif /* CONFIG_HAVE_MCAST_JOIN */
	return retval;
}

/* leave a joined group
 */

int
pgm_sockaddr_leave_group (
	const int		s,
	const sa_family_t	sa_family,
	const struct group_req*	gr
	)
{
	int retval = PGM_SOCKET_ERROR;
#ifdef CONFIG_HAVE_MCAST_JOIN
	const int recv_level = (AF_INET == sa_family) ? SOL_IP : SOL_IPV6;
	retval = setsockopt (s, recv_level, MCAST_LEAVE_GROUP, gr, sizeof(struct group_req));
#else
	switch (sa_family) {
	case AF_INET: {
#ifdef CONFIG_HAVE_IP_MREQN
		struct ip_mreqn mreqn;
		struct sockaddr_in ifaddr;
		memset (&mreqn, 0, sizeof(mreqn));
		mreqn.imr_multiaddr.s_addr = ((const struct sockaddr_in*)&gr->gr_group)->sin_addr.s_addr;
		if (!pgm_if_indextoaddr (gr->gr_interface, AF_INET, 0, (struct sockaddr*)&ifaddr, NULL))
			return -1;
		mreqn.imr_address.s_addr = ifaddr.sin_addr.s_addr;
		mreqn.imr_ifindex = gr->gr_interface;
		retval = setsockopt (s, SOL_IP, IP_DROP_MEMBERSHIP, (const char*)&mreqn, sizeof(mreqn));
#else
		struct ip_mreq mreq;
		struct sockaddr_in ifaddr;
		memset (&mreq, 0, sizeof(mreq));
		mreq.imr_multiaddr.s_addr = ((const struct sockaddr_in*)&gr->gr_group)->sin_addr.s_addr;
		if (!pgm_if_indextoaddr (gr->gr_interface, AF_INET, 0, (struct sockaddr*)&ifaddr, NULL))
			return -1;
		mreq.imr_interface.s_addr = ifaddr.sin_addr.s_addr;
		retval = setsockopt (s, SOL_IP, IP_DROP_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
#endif /* !CONFIG_HAVE_IP_MREQN */
		break;
	}

	case AF_INET6: {
		struct ipv6_mreq mreq6;
		memset (&mreq6, 0, sizeof(mreq6));
		mreq6.ipv6mr_multiaddr = ((const struct sockaddr_in6*)&gr->gr_group)->sin6_addr;
		mreq6.ipv6mr_interface = gr->gr_interface;
		retval = setsockopt (s, SOL_IPV6, IPV6_DROP_MEMBERSHIP, (const char*)&mreq6, sizeof(mreq6));
		break;
	}

	default: break;
	}
#endif /* CONFIG_HAVE_MCAST_JOIN */
	return retval;
}

/* block either at the NIC or kernel, packets from a particular source
 */

int
pgm_sockaddr_block_source (
	const int			s,
	const sa_family_t		sa_family,
	const struct group_source_req*	gsr
	)
{
	int retval = PGM_SOCKET_ERROR;
#ifdef CONFIG_HAVE_MCAST_JOIN
	const int recv_level = (AF_INET == sa_family) ? SOL_IP : SOL_IPV6;
	retval = setsockopt (s, recv_level, MCAST_BLOCK_SOURCE, gsr, sizeof(struct group_source_req));
#elif defined(IP_BLOCK_SOURCE)
	switch (sa_family) {
	case AF_INET: {
		struct ip_mreq_source mreqs;
		struct sockaddr_in ifaddr;
		memset (&mreqs, 0, sizeof(mreqs));
		mreqs.imr_multiaddr.s_addr = ((const struct sockaddr_in*)&gsr->gsr_group)->sin_addr.s_addr;
		mreqs.imr_sourceaddr.s_addr = ((const struct sockaddr_in*)&gsr->gsr_source)->sin_addr.s_addr;
		pgm_if_indextoaddr (gsr->gsr_interface, AF_INET, 0, (struct sockaddr*)&ifaddr, NULL);
		mreqs.imr_interface.s_addr = ifaddr.sin_addr.s_addr;
		retval = setsockopt (s, SOL_IP, IP_BLOCK_SOURCE, (const char*)&mreqs, sizeof(mreqs));
		break;
	}

	case AF_INET6:
/* No IPv6 API implemented, MCAST_BLOCK_SOURCE should be available instead.
 */
		break;

	default: break;
	}
#endif /* CONFIG_HAVE_MCAST_JOIN */
	return retval;
}

/* unblock a blocked multicast source.
 */

int
pgm_sockaddr_unblock_source (
	const int			s,
	const sa_family_t		sa_family,
	const struct group_source_req*	gsr
	)
{
	int retval = PGM_SOCKET_ERROR;
#ifdef CONFIG_HAVE_MCAST_JOIN
	const int recv_level = (AF_INET == sa_family) ? SOL_IP : SOL_IPV6;
	retval = setsockopt (s, recv_level, MCAST_UNBLOCK_SOURCE, gsr, sizeof(struct group_source_req));
#elif defined(IP_UNBLOCK_SOURCE)
	switch (sa_family) {
	case AF_INET: {
		struct ip_mreq_source mreqs;
		struct sockaddr_in ifaddr;
		memset (&mreqs, 0, sizeof(mreqs));
		mreqs.imr_multiaddr.s_addr = ((const struct sockaddr_in*)&gsr->gsr_group)->sin_addr.s_addr;
		mreqs.imr_sourceaddr.s_addr = ((const struct sockaddr_in*)&gsr->gsr_source)->sin_addr.s_addr;
		pgm_if_indextoaddr (gsr->gsr_interface, AF_INET, 0, (struct sockaddr*)&ifaddr, NULL);
		mreqs.imr_interface.s_addr = ifaddr.sin_addr.s_addr;
		retval = setsockopt (s, SOL_IP, IP_UNBLOCK_SOURCE, (const char*)&mreqs, sizeof(mreqs));
		break;
	}

	case AF_INET6:
/* No IPv6 API implemented, MCAST_UNBLOCK_SOURCE should be available instead.
 */
		break;

	default: break;
	}
#endif /* CONFIG_HAVE_MCAST_JOIN */
	return retval;
}

/* Join source-specific multicast.
 * NB: Silently reverts to ASM if SSM not supported.
 *
 * If no error occurs, pgm_sockaddr_join_source_group returns zero.
 * Otherwise, a value of PGM_SOCKET_ERROR is returned, and a specific error
 * code can be retrieved by calling pgm_sock_errno().
 */

int
pgm_sockaddr_join_source_group (
	const int			s,
	const sa_family_t		sa_family,
	const struct group_source_req*	gsr
	)
{
	int retval = PGM_SOCKET_ERROR;
#ifdef CONFIG_HAVE_MCAST_JOIN
/* Solaris:ip(7P) "The following options take a struct ip_mreq_source as the
 * parameter."
 * Solaris:ip6(7P) "Takes a struct group_source_req as the parameter."
 * Different type for each family, however group_source_req is protocol-
 * independent.
 *
 * Stevens: "MCAST_JOIN_SOURCE_GROUP has datatype group_source_req{}."
 *
 * RFC3678: Argument type struct group_source_req
 */
	const int recv_level = (AF_INET == sa_family) ? SOL_IP : SOL_IPV6;
	retval = setsockopt (s, recv_level, MCAST_JOIN_SOURCE_GROUP, gsr, sizeof(struct group_source_req));
#elif defined(IP_ADD_SOURCE_MEMBERSHIP)
	switch (sa_family) {
	case AF_INET: {
/* Solaris:ip(7P) "The following options take a struct ip_mreq as the
 * parameter."  Incorrect literature wrt RFC.
 *
 * Linux:ip(7) absent.
 *
 * OS X:IP(4) absent.
 *
 * Stevens: "IP_ADD_SOURCE_MEMBERSHIP has datatype ip_mreq_source{}."
 *
 * RFC3678: Argument type struct ip_mreq_source
 */
		struct ip_mreq_source mreqs;
		struct sockaddr_in ifaddr;
		memset (&mreqs, 0, sizeof(mreqs));
		mreqs.imr_multiaddr.s_addr = ((const struct sockaddr_in*)&gsr->gsr_group)->sin_addr.s_addr;
		mreqs.imr_sourceaddr.s_addr = ((const struct sockaddr_in*)&gsr->gsr_source)->sin_addr.s_addr;
		pgm_if_indextoaddr (gsr->gsr_interface, AF_INET, 0, (struct sockaddr*)&ifaddr, NULL);
		mreqs.imr_interface.s_addr = ifaddr.sin_addr.s_addr;
		retval = setsockopt (s, SOL_IP, IP_ADD_SOURCE_MEMBERSHIP, (const char*)&mreqs, sizeof(mreqs));
		break;
	}

	case AF_INET6:
/* No IPv6 API implemented, MCAST_JOIN_SOURCE_GROUP should be available instead.
 */
		retval = pgm_sockaddr_join_group (s, sa_family, (const struct group_req*)gsr);
		break;

	default: break;
	}
#else
	retval = pgm_sockaddr_join_group (s, sa_family, (const struct group_req*)gsr);	
#endif /* CONFIG_HAVE_MCAST_JOIN */
	return retval;
}

/* drop a SSM source
 */

int
pgm_sockaddr_leave_source_group (
	const int			s,
	const sa_family_t		sa_family,
	const struct group_source_req*	gsr
	)
{
	int retval = PGM_SOCKET_ERROR;
#ifdef CONFIG_HAVE_MCAST_JOIN
	const int recv_level = (AF_INET == sa_family) ? SOL_IP : SOL_IPV6;
	retval = setsockopt (s, recv_level, MCAST_LEAVE_SOURCE_GROUP, gsr, sizeof(struct group_source_req));
#elif defined(IP_ADD_SOURCE_MEMBERSHIP)
	switch (sa_family) {
	case AF_INET: {
		struct ip_mreq_source mreqs;
		struct sockaddr_in ifaddr;
		memset (&mreqs, 0, sizeof(mreqs));
		mreqs.imr_multiaddr.s_addr = ((const struct sockaddr_in*)&gsr->gsr_group)->sin_addr.s_addr;
		mreqs.imr_sourceaddr.s_addr = ((const struct sockaddr_in*)&gsr->gsr_source)->sin_addr.s_addr;
		pgm_if_indextoaddr (gsr->gsr_interface, AF_INET, 0, (struct sockaddr*)&ifaddr, NULL);
		mreqs.imr_interface.s_addr = ifaddr.sin_addr.s_addr;
		retval = setsockopt (s, SOL_IP, IP_DROP_SOURCE_MEMBERSHIP, (const char*)&mreqs, sizeof(mreqs));
		break;
	}

	case AF_INET6:
/* No IPv6 API implemented, MCAST_LEAVE_SOURCE_GROUP should be available instead.
 */
		retval = pgm_sockaddr_leave_group (s, sa_family, (const struct group_req*)gsr);
		break;

	default: break;
	}
#else
	retval = pgm_sockaddr_leave_group (s, sa_family, (const struct group_req*)gsr);	
#endif /* CONFIG_HAVE_MCAST_JOIN */
	return retval;
}

#if defined(MCAST_MSFILTER) || defined(SIOCSMSFILTER)
/* Batch block and unblock sources.
 */

int
pgm_sockaddr_msfilter (
	const int			s,
	const sa_family_t		sa_family,
	const struct group_filter*	gf_list
	)
{
	int retval = PGM_SOCKET_ERROR;
#ifdef MCAST_MSFILTER
	const int recv_level = (AF_INET == sa_family) ? SOL_IP : SOL_IPV6;
	const socklen_t len = GROUP_FILTER_SIZE(gf_list->gf_numsrc);
	retval = setsockopt (s, recv_level, MCAST_MSFILTER, (const char*)gf_list, len);
#elif defined(SIOCSMSFILTER)
	retval = ioctl (s, SIOCSMSFILTER, (const char*)gf_list);
#endif
	return retval;
}
#endif /* MCAST_MSFILTER || SIOCSMSFILTER */

/* Specify outgoing interface.
 *
 * If no error occurs, pgm_sockaddr_multicast_if returns zero.  Otherwise, a
 * value of PGM_SOCKET_ERROR is returned, and a specific error code can be
 * retrieved by calling pgm_sock_errno().
 */

int
pgm_sockaddr_multicast_if (
	int			s,
	const struct sockaddr*	address,
	unsigned		ifindex
	)
{
	int retval = PGM_SOCKET_ERROR;

	switch (address->sa_family) {
	case AF_INET: {
/* Solaris:ip(7P) "This option takes a struct in_addr as an argument, and it
 * selects that interface for outgoing IP multicast packets."
 *
 * Linux:ip(7) "Argument is an ip_mreqn or ip_mreq structure similar to
 * IP_ADD_MEMBERSHIP."
 *
 * OS X:IP(4) provided by example "struct in_addr addr;"
 *
 * Stevens: "IP_MULTICAST_IF has datatype struct in_addr{}."
 */
		struct sockaddr_in s4;
		memcpy (&s4, address, sizeof(s4));
		retval = setsockopt (s, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&s4.sin_addr, sizeof(s4.sin_addr));
		break;
	}

	case AF_INET6: {
#ifndef _WIN32
/* Solaris:ip6(7P) "This option takes an integer as an argument; the integer
 * is the interface index of the selected interface."
 *
 * Linux:ipv6(7) "The argument is a pointer to an interface index (see 
 * netdevice(7)) in an integer."
 *
 * OS X:IP6(4) "IPV6_MULTICAST_IF u_int *"
 *
 * Stevens: "IPV6_MULTICAST_IF has datatype u_int."
 */
		const unsigned int optval = ifindex;
#else
		const DWORD optval = ifindex;
#endif
		retval = setsockopt (s, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char*)&optval, sizeof(optval));
		break;
	}

	default: break;
	}
	return retval;
}

/* Specify multicast loop, other applications on the same host may receive
 * outgoing packets.  This does not affect unicast packets such as NAKs.
 *
 * If no error occurs, pgm_sockaddr_multicast_loop returns zero.  Otherwise, a
 * value of PGM_SOCKET_ERROR is returned, and a specific error code can be
 * retrieved by calling pgm_sock_errno().
 */

int
pgm_sockaddr_multicast_loop (
	const int		s,
	const sa_family_t	sa_family,
	const bool		v
	)
{
	int retval = PGM_SOCKET_ERROR;

	switch (sa_family) {
	case AF_INET: {
#ifndef _WIN32
/* Solaris:ip(7P) "Setting the unsigned character argument to 0 causes the
 * opposite behavior, meaning that when multiple zones are present, the
 * datagrams are delivered to all zones except the sending zone."
 *
 * Linux:ip(7) "Sets or reads a boolean integer argument"
 *
 * OS X:IP(4) provided by example "u_char loop;"
 *
 * Stevens: "IP_MULTICAST_LOOP has datatype u_char."
 */
		const unsigned char optval = v ? 1 : 0;
#else
		const DWORD optval = v ? 1 : 0;
#endif
		retval = setsockopt (s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&optval, sizeof(optval));
		break;
	}

	case AF_INET6: {
#ifndef _WIN32
/* Solaris:ip(7P) "Setting the unsigned character argument to 0 will cause the opposite behavior."
 *
 * Linux:ipv6(7) "Argument is a pointer to boolean."
 *
 * OS X:IP6(7) "IPV6_MULTICAST_LOOP u_int *"
 *
 * Stevens: "IPV6_MULTICAST_LOOP has datatype u_int."
 */
		const unsigned int optval = v ? 1 : 0;
#else
		const DWORD optval = v ? 1 : 0;
#endif
		retval = setsockopt (s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (const char*)&optval, sizeof(optval));
		break;
	}

	default: break;
	}
	return retval;
}

/* Specify TTL or outgoing hop limit.
 * NB: Only affects multicast hops, unicast hop-limit is not changed.
 *
 * If no error occurs, pgm_sockaddr_multicast_hops returns zero.  Otherwise, a
 * value of PGM_SOCKET_ERROR is returned, and a specific error code can be
 * retrieved by calling pgm_sock_errno().
 */

int
pgm_sockaddr_multicast_hops (
	const int		s,
	const sa_family_t	sa_family,
	const unsigned		hops
	)
{
	int retval = PGM_SOCKET_ERROR;

	switch (sa_family) {
	case AF_INET: {
#ifndef _WIN32
/* Solaris:ip(7P) "This option takes an unsigned character as an argument."
 *
 * Linux:ip(7) "Argument is an integer."
 *
 * OS X:IP(4) provided by example for SOCK_DGRAM with IP_TTL: "int ttl = 60;",
 * or for SOCK_RAW & SOCK_DGRAM with IP_MULTICAST_TTL: "u_char ttl;"
 *
 * Stevens: "IP_MULTICAST_TTL has datatype u_char."
 */
		const unsigned char optval = hops;
#else
		const DWORD optval = hops;
#endif
		retval = setsockopt (s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&optval, sizeof(optval));
		break;
	}

	case AF_INET6: {
#ifndef _WIN32
/* Solaris:ip6(7P) "This option takes an integer as an argument."
 *
 * Linux:ipv6(7) "Argument is a pointer to an integer."
 *
 * OS X:IP6(7) "IPV6_MULTICAST_HOPS int *"
 *
 * Stevens: "IPV6_MULTICAST_HOPS has datatype int."
 */
		const int optval = hops;
#else
		const DWORD optval = hops;
#endif
		retval = setsockopt (s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char*)&optval, sizeof(optval));
		break;
	}

	default: break;
	}
	return retval;
}

void
pgm_sockaddr_nonblocking (
	const int	s,
	const bool	v
	)
{
#ifndef _WIN32
	int flags = fcntl (s, F_GETFL);
	if (!v) flags &= ~O_NONBLOCK;
	else flags |= O_NONBLOCK;
	fcntl (s, F_SETFL, flags);
#else
	u_long mode = v;
	ioctlsocket (s, FIONBIO, &mode);
#endif
}

/* Note that are sockaddr structure is not passed these functions inherently
 * cannot support IPv6 Zone Indices and hence are rather limited for the
 * link-local scope.
 */
const char*
pgm_inet_ntop (
	int		     af,
	const void* restrict src,
	char*	    restrict dst,
	socklen_t	     size
	)
{
	pgm_assert (AF_INET == af || AF_INET6 == af);
	pgm_assert (NULL != src);
	pgm_assert (NULL != dst);
	pgm_assert (size > 0);

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

int
pgm_inet_pton (
	int		     af,
	const char* restrict src,
	void*	    restrict dst
	)
{
	pgm_assert (AF_INET == af || AF_INET6 == af);
	pgm_assert (NULL != src);
	pgm_assert (NULL != dst);

	struct addrinfo hints = {
		.ai_family	= af,
		.ai_socktype	= SOCK_STREAM,		/* not really */
		.ai_protocol	= IPPROTO_TCP,		/* not really */
		.ai_flags	= AI_NUMERICHOST
	}, *result = NULL;

	const int e = getaddrinfo (src, NULL, &hints, &result);
	if (0 != e) {
		return 0;	/* error */
	}

	pgm_assert (NULL != result->ai_addr);
	pgm_assert (0 != result->ai_addrlen);

	switch (result->ai_addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in s4;
		memcpy (&s4, result->ai_addr, sizeof(s4));
		memcpy (dst, &s4.sin_addr.s_addr, sizeof(struct in_addr));
		break;
	}

	case AF_INET6: {
		struct sockaddr_in6 s6;
		memcpy (&s6, result->ai_addr, sizeof(s6));
		memcpy (dst, &s6.sin6_addr, sizeof(struct in6_addr));
		break;
	}

	default:
		pgm_assert_not_reached();
		break;
	}

	freeaddrinfo (result);
	return 1;	/* success */
}

int
pgm_nla_to_sockaddr (
	const void*	 restrict nla,
	struct sockaddr* restrict sa
	)
{
	uint16_t nla_family;
	int retval = 0;

	memcpy (&nla_family, nla, sizeof(nla_family));
	sa->sa_family = ntohs (nla_family);
	switch (sa->sa_family) {
	case AFI_IP:
		sa->sa_family = AF_INET;
		((struct sockaddr_in*)sa)->sin_addr.s_addr = ((const struct in_addr*)((const char*)nla + sizeof(uint32_t)))->s_addr;
		break;

	case AFI_IP6:
		sa->sa_family = AF_INET6;
		memcpy (&((struct sockaddr_in6*)sa)->sin6_addr, (const struct in6_addr*)((const char*)nla + sizeof(uint32_t)), sizeof(struct in6_addr));
		break;

	default:
		retval = -EINVAL;
		break;
	}

	return retval;
}

int
pgm_sockaddr_to_nla (
	const struct sockaddr* restrict sa,
	void*		       restrict	nla
	)
{
	int retval = 0;

	*(uint16_t*)nla = sa->sa_family;
	*(uint16_t*)((char*)nla + sizeof(uint16_t)) = 0;	/* reserved 16bit space */
	switch (sa->sa_family) {
	case AF_INET:
		*(uint16_t*)nla = htons (AFI_IP);
		((struct in_addr*)((char*)nla + sizeof(uint32_t)))->s_addr = ((const struct sockaddr_in*)sa)->sin_addr.s_addr;
		break;

	case AF_INET6:
		*(uint16_t*)nla = htons (AFI_IP6);
		memcpy ((struct in6_addr*)((char*)nla + sizeof(uint32_t)), &((const struct sockaddr_in6*)sa)->sin6_addr, sizeof(struct in6_addr));
		break;

	default:
		retval = -EINVAL;
		break;
	}

	return retval;
}

/* eof */
