/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Windows interface index to interface name function.
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

#ifdef _WIN32
#	include <ws2tcpip.h>
#	include <iphlpapi.h>
#endif
#include <pgm/framework.h>


//#define INDEXTONAME_DEBUG


char*
pgm_if_indextoname (
	unsigned int		ifindex,
	char*			ifname
        )
{
#ifndef _WIN32
	return if_indextoname (ifindex, ifname);
#else
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
