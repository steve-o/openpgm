/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable getifaddrs implementation.
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#ifdef G_OS_UNIX
#	include <net/if.h>
#	include <sys/ioctl.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#else
#	include <ws2tcpip.h>
#	include <iphlpapi.h>
#endif

#include "pgm/sockaddr.h"
#include "pgm/getifaddrs.h"

//#define GETIFADDRS_DEBUG

#ifndef GETIFADDRS_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* locals */
struct _pgm_ifaddrs
{
	struct pgm_ifaddrs		_ifa;
	char				_name[IF_NAMESIZE];
	struct sockaddr_storage		_addr;
	struct sockaddr_storage		_netmask;
};

/* returns 0 on success setting ifap to a linked list of system interfaces,
 * returns -1 on failure.
 */

int
pgm_getifaddrs (
	struct pgm_ifaddrs**	ifap
	)
{
	int sock = socket (AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		return -1;
	}

	int sock6 = socket (AF_INET6, SOCK_DGRAM, 0);
	if (sock6 < 0) {
		close (sock6);
		return -1;
	}

#ifdef G_OS_UNIX
/* get count of interfaces */
	char buf[1024], buf6[1024];
	struct ifconf ifc, ifc6;

	ifc.ifc_buf = buf;
	ifc.ifc_len = sizeof(buf);
	if (ioctl (sock, SIOCGIFCONF, &ifc) < 0) {
		close (sock);
		close (sock6);
		return -1;
	}

	ifc6.ifc_buf = buf6;
	ifc6.ifc_len = sizeof(buf6);
	if (ioctl (sock6, SIOCGIFCONF, &ifc6) < 0) {
		close (sock);
		close (sock6);
		return -1;
	}

/* alloc a contiguous block for entire list */
	int n = (ifc.ifc_len + ifc6.ifc_len) / sizeof(struct ifreq);
	struct _pgm_ifaddrs* ifa = malloc (n * sizeof(struct _pgm_ifaddrs));
	memset (ifa, 0, n * sizeof(struct _pgm_ifaddrs));

/* foreach interface */
	struct ifreq *ifr  = ifc.ifc_req;
	struct ifreq *lifr = (struct ifreq *)&ifc.ifc_buf[ifc.ifc_len];
	struct _pgm_ifaddrs* ift = ifa;

	g_assert (IF_NAMESIZE >= sizeof(ifr->ifr_name));

	while (ifr < lifr)
	{
/* address */
		if (ioctl (sock, SIOCGIFADDR, ifr) != -1) {
			ift->_ifa.ifa_addr = (gpointer)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
		}

/* name */
		ift->_ifa.ifa_name = ift->_name;
		strncpy (ift->_ifa.ifa_name, ifr->ifr_name, sizeof(ifr->ifr_name));
		ift->_ifa.ifa_name[sizeof(ifr->ifr_name) - 1] = 0;

/* flags */
		if (ioctl (sock, SIOCGIFFLAGS, ifr) != -1) {
			ift->_ifa.ifa_flags = ifr->ifr_flags;
		}

/* netmask */
		if (ioctl (sock, SIOCGIFNETMASK, ifr) != -1) {
			ift->_ifa.ifa_netmask = (gpointer)&ift->_netmask;
#ifdef CONFIG_HAVE_IFR_NETMASK
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_netmask, pgm_sockaddr_len(&ifr->ifr_netmask));
#else
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
#endif
		}

		++ifr;
		if (ifr < lifr) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}
	}

#ifdef CONFIG_HAVE_IPV6_SIOCGIFADDR
/* repeat for IPv6 */
	ifr  = ifc6.ifc_req;
	lifr = (struct ifreq *)&ifc6.ifc_buf[ifc6.ifc_len];

	while (ifr < lifr)
	{
		if (ift != ifa) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}

/* address, note this does not work on Linux as struct ifreq is too small for an IPv6 address */
		if (ioctl (sock6, SIOCGIFADDR, ifr) != -1) {
			ift->_ifa.ifa_addr = &ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
		}

/* name */
		ift->_ifa.ifa_name = ift->_name;
		strncpy (ift->_ifa.ifa_name, ifr->ifr_name, sizeof(ifr->ifr_name));
		ift->_ifa.ifa_name[sizeof(ifr->ifr_name) - 1] = 0;

/* flags */
		if (ioctl (sock6, SIOCGIFFLAGS, ifr) != -1) {
			ift->_ifa.ifa_flags = ifr->ifr_flags;
		}

/* netmask */
		if (ioctl (sock6, SIOCGIFNETMASK, ifr) != -1) {
#ifdef CONFIG_HAVE_IFR_NETMASK
			ift->_ifa.ifa_netmask = &ift->_netmask;
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_netmask, pgm_sockaddr_len(&ifr->ifr_netmask));
#else
			ift->_ifa.ifa_netmask = &ift->_addr;
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
#endif
		}

		++ifr;
	}
#endif
#else /* !G_OS_UNIX */
/* get count of interfaces */
	char buf[1024], buf6[1024];
	DWORD bytesReturned, bytesReturned6;

	if (WSAIoctl (	sock,
			SIO_GET_INTERFACE_LIST,		/* control code */
			NULL, 0,			/* input buffer: pointer, size in bytes */
			buf, sizeof(buf),		/* output buffer: pointer, size in bytes */
			&bytesReturned,			/* actual number of bytes output */
			NULL, NULL) < 0)
	{
		close (sock);
		close (sock6);
		return -1;
	}

	if (WSAIoctl (	sock6,
			SIO_GET_INTERFACE_LIST,		/* control code */
			NULL, 0,			/* input buffer: pointer, size in bytes */
			buf6, sizeof(buf6),		/* output buffer: pointer, size in bytes */
			&bytesReturned6,		/* actual number of bytes output */
			NULL, NULL) < 0)
	{
		close (sock);
		close (sock6);
		return -1;
	}

/* guess return structure from size */
	unsigned iilen, iilen6;
	INTERFACE_INFO *ii, *ii6;
	INTERFACE_INFO_EX *iix, *iix6;

/* IPv4 */
	if (0 == bytesReturned % sizeof(INTERFACE_INFO))
	{
		iilen = bytesReturned / sizeof(INTERFACE_INFO);
		ii    = (INTERFACE_INFO*)buf;
		iix   = NULL;
	}
	else
	{
		g_assert (0 == bytesReturned / sizeof(INTERFACE_INFO_EX));
		iilen  = bytesReturned / sizeof(INTERFACE_INFO_EX);
		ii     = NULL;
		iix    = (INTERFACE_INFO_EX*)buf;
	}

/* IPv6 */
	if (0 == bytesReturned6 % sizeof(INTERFACE_INFO))
	{
		iilen6 = bytesReturned6 / sizeof(INTERFACE_INFO);
		ii6    = (INTERFACE_INFO*)buf;
		iix6   = NULL;
	}
	else
	{
		g_assert (0 == bytesReturned6 / sizeof(INTERFACE_INFO_EX));
		iilen6 = bytesReturned6 / sizeof(INTERFACE_INFO_EX);
		ii6    = NULL;
		iix6   = (INTERFACE_INFO_EX*)buf;
	}


/* alloc a contiguous block for entire list */
	unsigned n = iilen + iilen6;
	struct _pgm_ifaddrs* ifa = malloc (n * sizeof(struct _pgm_ifaddrs));
	memset (ifa, 0, n * sizeof(struct _pgm_ifaddrs));

/* foreach interface */
	struct _pgm_ifaddrs* ift = ifa;

	for (unsigned i = 0; i < iilen; i++)
	{
		if (iix)
			g_assert (sizeof(struct sockaddr_in) == iix[i].iiAddress.iSockaddrLength);
/* address */
		ift->_ifa.ifa_addr = &ift->_addr;
		if (ii)
			memcpy (ift->_ifa.ifa_addr, &ii[i].iiAddress.AddressIn, sizeof(struct sockaddr_in));
		else
			memcpy (ift->_ifa.ifa_addr, iix[i].iiAddress.lpSockaddr, iix[i].iiAddress.iSockaddrLength);

/* flags */
		if (ii)
			ift->_ifa.ifa_flags = ii[i].iiFlags;
		else
			ift->_ifa.ifa_flags = iix[i].iiFlags;

/* name */
		ift->_ifa.ifa_name = ift->_name;

/* netmask */
		ift->_ifa.ifa_netmask = &ift->_netmask;
		if (ii)
			memcpy (ift->_ifa.ifa_netmask, &ii[i].iiNetmask.AddressIn, sizeof(struct sockaddr_in));
		else
			memcpy (ift->_ifa.ifa_netmask, iix[i].iiNetmask.lpSockaddr, iix[i].iiNetmask.iSockaddrLength);

		if (ift < &ifa[iilen - 1]) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}
	}

/* repeat for IPv6 */
	for (unsigned i = 0; i < iilen6; i++)
	{
		if (ift != ifa) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}

		if (iix6)
			g_assert (sizeof(struct sockaddr_in6) == iix6[i].iiAddress.iSockaddrLength);
/* address */
		ift->_ifa.ifa_addr = &ift->_addr;
		if (ii6) {
			memcpy (ift->_ifa.ifa_addr, &ii6[i].iiAddress.AddressIn6, sizeof(struct sockaddr_in6_old));
			((struct sockaddr_in6*)ift->_ifa.ifa_addr)->sin6_scope_id = 0;
		} else
			memcpy (ift->_ifa.ifa_addr, iix6[i].iiAddress.lpSockaddr, iix6[i].iiAddress.iSockaddrLength);

/* flags */
		if (ii6)
			ift->_ifa.ifa_flags = ii6[i].iiFlags;
		else
			ift->_ifa.ifa_flags = iix6[i].iiFlags;

/* name */
		ift->_ifa.ifa_name = ift->_name;

/* netmask */
		ift->_ifa.ifa_netmask = &ift->_netmask;
		if (ii6) {
			memcpy (ift->_ifa.ifa_netmask, &ii6[i].iiNetmask.AddressIn6, sizeof(struct sockaddr_in6_old));
			((struct sockaddr_in6*)ift->_ifa.ifa_netmask)->sin6_scope_id = 0;
		} else
			memcpy (ift->_ifa.ifa_netmask, iix6[i].iiNetmask.lpSockaddr, iix6[i].iiNetmask.iSockaddrLength);

		++ift;
	}

/* populate the names via win32 api */
	DWORD dwSize, dwRet;
	IP_ADAPTER_ADDRESSES *pAdapterAddresses, *adapter;

	dwRet = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_SKIP_MULTICAST, NULL, NULL, &dwSize);
	if (ERROR_BUFFER_OVERFLOW != dwRet) {
		perror("GetAdaptersAddresses");
		return -1;
	}
	pAdapterAddresses = (IP_ADAPTER_ADDRESSES*)malloc (dwSize);
	if (NULL == pAdapterAddresses) {
		perror("malloc");
		return -1;
	}
	dwRet = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_SKIP_MULTICAST, NULL, pAdapterAddresses, &dwSize);
	if (ERROR_SUCCESS != dwRet) {
		perror("GetAdaptersAddresses(2)");
		free(pAdapterAddresses);
		return -1;
	}

	unsigned resolved = 0;
	for (adapter = pAdapterAddresses;
		 adapter;
		 adapter = adapter->Next)
	{
		for (IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress;
			 unicast;
			 unicast = unicast->Next)
		{
			struct pgm_ifaddrs* ifi;
			for (ifi = (struct pgm_ifaddrs*)ifa;
				ifi;
				ifi = ifi->ifa_next)
			{
				if (0 == pgm_sockaddr_cmp (unicast->Address.lpSockaddr, ifi->ifa_addr))
				{
					g_assert (IF_NAMESIZE > strlen(adapter->AdapterName));
					strncpy (ifi->ifa_name, adapter->AdapterName, IF_NAMESIZE);
					ifi->ifa_name[IF_NAMESIZE - 1] = 0;
					resolved++;
					break;
				}
			}
		}
	}

	free (pAdapterAddresses);
/* are all adapter names successfully resolved? */
	g_assert (resolved == n);
#endif /* !G_OS_UNIX */

	*ifap = (struct pgm_ifaddrs*)ifa;
	close (sock);
	close (sock6);
	return 0;
}

void
pgm_freeifaddrs (
	struct pgm_ifaddrs*	ifa
	)
{
	free (ifa);
}

/* eof */
