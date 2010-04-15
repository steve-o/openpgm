/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Windows interface name to interface index function.
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

#include <glib.h>

#ifdef G_OS_WIN32
#	include <ws2tcpip.h>
#	include <iphlpapi.h>
#endif

#include "pgm/nametoindex.h"



//#define NAMETOINDEX_DEBUG


#ifdef G_OS_WIN32

/* Retrieve interface index for a specified adapter name.
 *
 * On Windows is IPv4 specific, IPv6 indexes are separate (IP_ADAPTER_ADDRESSES::Ipv6IfIndex)
 *
 * On error returns zero, no errors are defined.
 */

int						/* type matching if_nametoindex() */
pgm_compat_if_nametoindex (
	const int		iffamily,
	const char*		ifname
        )
{
	pgm_return_val_if_fail (NULL != ifname, 0);

#ifdef CONFIG_TARGET_WINE

	pgm_assert (AF_INET6 != iffamily);

	ULONG ifIndex = 0;
	DWORD dwSize = sizeof(MIB_IFTABLE), dwRet;
	MIB_IFTABLE *pIfTable = NULL;
	MIB_IFROW *pIfRow;

	dwRet = GetAdapterIndex ((const LPWSTR)ifname, &ifIndex);
	if (NO_ERROR == dwRet)
		return ifIndex;

/* loop to handle interfaces coming online causing a buffer overflow
 * between first call to list buffer length and second call to enumerate.
 */
	for (unsigned i = 100; i; i--)
	{
		if (pIfTable) {
			pgm_free (pIfTable);
			pIfTable = NULL;
		}
		pIfTable = (MIB_IFTABLE *)pgm_malloc (dwSize);
		dwRet = GetIfTable (pIfTable, &dwSize, FALSE);
		if (ERROR_INSUFFICIENT_BUFFER != dwRet)
			break;
	}

	switch (dwRet) {
	case ERROR_SUCCESS:	/* NO_ERROR */
		break;
	case ERROR_INSUFFICIENT_BUFFER:
		pgm_warn (_("GetIfTable repeatedly failed with ERROR_INSUFFICIENT_BUFFER"));
		pgm_free (pIfTable);
		return 0;
	default:
		pgm_warn (_("GetIfTable failed"));
		pgm_free (pIfTable);
		return 0;
	}

	for (unsigned i = 0; i < pIfTable->dwNumEntries; i++)
	{
		pIfRow = (MIB_IFROW *) & pIfTable->table[i];
		if (0 == strncmp (ifname, pIfRow->bDescr, pIfRow->dwDescrLen)) {
			ifIndex = pIfRow->dwIndex;
			pgm_free (pIfTable);
			return ifIndex;
		}
	}
	pgm_free (pIfTable);

#else /* !CONFIG_TARGET_WINE */

	ULONG ifIndex;
	DWORD dwSize = sizeof(IP_ADAPTER_ADDRESSES), dwRet;
	IP_ADAPTER_ADDRESSES *pAdapterAddresses = NULL, *adapter;

/* first see if GetAdapterIndex is working
 */
	dwRet = GetAdapterIndex ((const LPWSTR)ifname, &ifIndex);
	if (NO_ERROR == dwRet)
		return ifIndex;

/* fallback to finding index via iterating adapter list */

/* loop to handle interfaces coming online causing a buffer overflow
 * between first call to list buffer length and second call to enumerate.
 */
	for (unsigned i = 100; i; i--)
	{
		if (pAdapterAddresses) {
			pgm_free (pAdapterAddresses);
			pAdapterAddresses = NULL;
		}
		pAdapterAddresses = (IP_ADAPTER_ADDRESSES*)pgm_malloc (dwSize);
		dwRet = GetAdaptersAddresses (AF_UNSPEC,
						GAA_FLAG_SKIP_ANYCAST |
						GAA_FLAG_SKIP_DNS_SERVER |
						GAA_FLAG_SKIP_FRIENDLY_NAME |
						GAA_FLAG_SKIP_MULTICAST,
						NULL,
						NULL,
						&dwSize);
		if (ERROR_BUFFER_OVERFLOW != dwRet)
			break;
	}

	switch (dwRet) {
	case ERROR_SUCCESS:
		break;
	case ERROR_BUFFER_OVERFLOW:
		pgm_warn (_("GetAdaptersAddresses repeatedly failed with ERROR_BUFFER_OVERFLOW"));
		pgm_free (pAdapterAddresses);
		return 0;
	default:
		pgm_warn (_("GetAdaptersAddresses failed"));
		pgm_free (pAdapterAddresses);
		return 0;
	}

	for (adapter = pAdapterAddresses;
		adapter;
		adapter = adapter->Next)
	{
		if (0 == strcmp (ifname, adapter->AdapterName)) {
			ifIndex = AF_INET6 == iffamily ? adapter->Ipv6IfIndex : adapter->IfIndex;
			pgm_free (pAdapterAddresses);
			return ifIndex;
		}
	}

	pgm_free (pAdapterAddresses);

#endif
	return 0;
}
#endif /* G_OS_WIN32 */

/* eof */
