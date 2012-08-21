/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Interface index to interface name function.  Defined as part of RFC2553
 * for IPv6 basic socket extensions, but also available for IPv4 addresses
 * on many platforms.
 *
 * Copyright (c) 2006-2011 Miru Limited.
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
#ifdef _WIN32
#	include <ws2tcpip.h>
#	include <iphlpapi.h>
#endif
#include <impl/framework.h>


//#define INDEXTONAME_DEBUG

PGM_GNUC_INTERNAL
char*
pgm_if_indextoname (
	unsigned int		ifindex,
	char*			ifname
        )
{
#if !defined( _WIN32 )
/* Vista+ implements if_indextoname for IPv6 */
	return if_indextoname (ifindex, ifname);
#else
/* Windows maintains a few different numbers for each interface, the
 * number returned by GetIfEntry has shown to be the same as that 
 * determined by GetAdaptersAddresses and GetAdaptersInfo.
 */
	pgm_return_val_if_fail (NULL != ifname, NULL);

	MIB_IFROW ifRow = { .dwIndex = ifindex };
	const DWORD dwRetval = GetIfEntry (&ifRow);
	if (NO_ERROR != dwRetval)
		return NULL;
	strcpy (ifname, (char*)ifRow.wszName);
	return ifname;
#endif /* _WIN32 */
}

/* eof */
