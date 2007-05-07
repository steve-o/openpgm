/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * struct sockaddr functions independent of in or in6.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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

/* Combination of ip_mreq & ipv6_mreq */
struct sock_mreq
{
/* Port number and IP multicast address of group. */
    struct sockaddr	smr_multiaddr;

/* Local IP address of interface. */
    struct sockaddr	smr_interface;
};

G_BEGIN_DECLS


static inline int sockaddr_is_addr_multicast (struct sockaddr *s)
{
    int retval = 0;

    switch (s->sa_family) {
    case AF_INET:
	retval = IN_MULTICAST(g_htonl( ((struct sockaddr_in*)s)->sin_addr.s_addr ));
	break;

    case AF_INET6:
	retval = IN6_IS_ADDR_MULTICAST( &((struct sockaddr_in6*)s)->sin6_addr );
	break;

    default:
	retval = -EINVAL;
	break;
    }

    return retval;
}

static inline int sockaddr_cmp (struct sockaddr *a, struct sockaddr *b)
{
    int retval = 0;

    if (a->sa_family != b->sa_family)
    {
	retval = a->sa_family < b->sa_family ? -1 : 1;
    }
    else
    {
	switch (a->sa_family) {
	case AF_INET:
	    if (((struct sockaddr_in*)a)->sin_addr.s_addr != ((struct sockaddr_in*)b)->sin_addr.s_addr)
	    {
		retval = ((struct sockaddr_in*)a)->sin_addr.s_addr < ((struct sockaddr_in*)b)->sin_addr.s_addr ? -1 : 1;
	    }
	    break;

	case AF_INET6:
	    retval = memcmp (&((struct sockaddr_in6*)a)->sin6_addr, &((struct sockaddr_in6*)b)->sin6_addr, sizeof(struct in6_addr));
	    break;

	default:
	    retval = -EINVAL;
	    break;
	}
    }

    return retval;
}

static inline int sockaddr_hdrincl (int s, int sa_family, gboolean v)
{
    int retval = 0;

    switch (sa_family) {
    case AF_INET:
	retval = setsockopt (s, IPPROTO_IP, IP_HDRINCL, &v, sizeof(v));
	break;

    case AF_INET6:  /* method does not exist */
	break;

    default:
	retval = -EINVAL;
	break;
    }

    return retval;
}

static inline int sockaddr_add_membership (int s, struct sock_mreq* smr)
{
    int retval = 0;

    switch (smr->smr_interface.sa_family) {
    case AF_INET:
	{
	    struct ip_mreq mreq;
	    memset (&mreq, 0, sizeof(mreq));

	    mreq.imr_interface.s_addr = ((struct sockaddr_in*)&smr->smr_interface)->sin_addr.s_addr;
	    mreq.imr_multiaddr.s_addr = ((struct sockaddr_in*)&smr->smr_multiaddr)->sin_addr.s_addr;

	    retval = setsockopt (s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
	}
	break;

    case AF_INET6:
	{
	    struct ipv6_mreq mreq6;
	    memset (&mreq6, 0, sizeof(mreq6));

	    memcpy (&mreq6.ipv6mr_interface, &((struct sockaddr_in6*)&smr->smr_interface)->sin6_addr, sizeof(struct in6_addr));
	    memcpy (&mreq6.ipv6mr_multiaddr, &((struct sockaddr_in6*)&smr->smr_multiaddr)->sin6_addr, sizeof(struct in6_addr));

	    retval = setsockopt (s, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6));
	}
	break;

    default:
	retval = -EINVAL;
	break;
    }

    return retval;
}

static inline int sockaddr_multicast_if (int s, struct sock_mreq* smr)
{
    int retval = 0;

    switch (smr->smr_interface.sa_family) {
    case AF_INET:
	{
	    struct ip_mreq mreq;
	    memset (&mreq, 0, sizeof(mreq));

	    mreq.imr_interface.s_addr = ((struct sockaddr_in*)&smr->smr_interface)->sin_addr.s_addr;
	    mreq.imr_multiaddr.s_addr = ((struct sockaddr_in*)&smr->smr_multiaddr)->sin_addr.s_addr;

	    retval = setsockopt (s, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq));
	}
	break;

    case AF_INET6:
	{
	    struct ipv6_mreq mreq6;
	    memset (&mreq6, 0, sizeof(mreq6));

	    memcpy (&mreq6.ipv6mr_interface, &((struct sockaddr_in6*)&smr->smr_interface)->sin6_addr, sizeof(struct in6_addr));
	    memcpy (&mreq6.ipv6mr_multiaddr, &((struct sockaddr_in6*)&smr->smr_multiaddr)->sin6_addr, sizeof(struct in6_addr));

	    retval = setsockopt (s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &mreq6, sizeof(mreq6));
	}
	break;

    default:
	retval = -EINVAL;
	break;
    }

    return retval;
}

static inline int sockaddr_multicast_loop (int s, int sa_family, gboolean v)
{
    int retval = 0;

    switch (sa_family) {
    case AF_INET:
	retval = setsockopt (s, IPPROTO_IP, IP_MULTICAST_LOOP, &v, sizeof(v));
	break;

    case AF_INET6:
	retval = setsockopt (s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &v, sizeof(v));
	break;

    default:
	retval = -EINVAL;
	break;
    }

    return retval;
}

static inline int sockaddr_multicast_hops (int s, int sa_family, gint hops)
{
    int retval = 0;

    switch (sa_family) {
    case AF_INET:
	retval = setsockopt (s, IPPROTO_IP, IP_MULTICAST_TTL, &hops, sizeof(hops));
	break;

    case AF_INET6:
	retval = setsockopt (s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops));
	break;

    default:
	retval = -EINVAL;
	break;
    }

    return retval;
}


G_END_DECLS

#endif /* __PGM_SOCKADDR_H__ */
