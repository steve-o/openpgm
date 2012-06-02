/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Portable implementation of inet_lnaof.
 *
 * Copyright (c) 2006-2012 Miru Limited.
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

#ifndef _WIN32
#	include <sys/types.h>
#	include <sys/socket.h>
#endif
#include <impl/framework.h>
#include <impl/inet_lnaof.h>


//#define INET_LNAOF_DEBUG

/* Equivalent to the description of inet_lnaof(), extract the host
 * address out of an IP address.
 * 	hostaddr = inet_lnaof(addr)
 *
 * nb: lnaof = local network address of
 *
 * returns TRUE if host address is defined, FALSE if only network address.
 */
bool
pgm_inet_lnaof (
	struct in_addr* restrict	dst,	/* host byte order */
	const struct in_addr* restrict	src,
	const struct in_addr* restrict	netmask
	)
{
	bool has_lna = FALSE;

	pgm_assert (NULL != dst);
	pgm_assert (NULL != src);
	pgm_assert (NULL != netmask);

	dst->s_addr = src->s_addr & netmask->s_addr;
	has_lna = (0 != (src->s_addr & ~netmask->s_addr));

	return has_lna;
}

bool
pgm_inet6_lnaof (
	struct in6_addr* restrict	dst,
	const struct in6_addr* restrict	src,
	const struct in6_addr* restrict	netmask
	)
{
	bool has_lna = FALSE;

	pgm_assert (NULL != dst);
	pgm_assert (NULL != src);
	pgm_assert (NULL != netmask);

	for (unsigned i = 0; i < 16; i++) {
		dst->s6_addr[i] = src->s6_addr[i] & netmask->s6_addr[i];
		has_lna |= (0 != (src->s6_addr[i] & !netmask->s6_addr[i]));
	}

	return has_lna;
}

/* eof */
