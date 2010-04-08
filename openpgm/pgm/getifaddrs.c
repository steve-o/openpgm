/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable getifaddrs implementation.
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
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
pgm_compat_getifaddrs (
	struct pgm_ifaddrs**	ifap
	)
{
	g_trace ("pgm_getifaddrs (ifap:%p)", (void*)ifap);

#ifdef G_OS_UNIX
	int sock = socket (AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		return -1;
	}

#	ifdef SIOCGLIFCONF

/* process IPv6 interfaces */
	int sock6 = socket (AF_INET6, SOCK_DGRAM, 0);
	if (sock6 < 0) {
		close (sock);
		return -1;
	}

	struct lifnum lifn;
again:
	lifn.lifn_family = AF_INET;
	lifn.lifn_flags  = 0;
	if (ioctl (sock, SIOCGLIFNUM, &lifn) < 0) {
		close (sock);
		close (sock6);
		return -1;
	}
	unsigned if_count = lifn.lifn_count;
	g_trace ("ioctl(AF_INET, SIOCGLIFNUM) = %d", lifn.lifn_count);

/* nb: Sun and Apple code likes to pad the interface count here in case interfaces
 * are coming up between calls,
 */
	lifn.lifn_count += 4;

/* process all interfaces with family-agnostic ioctl, unfortunately it still
 * must be called on each family socket despite what if_tcp(7P) says.
 */
	struct lifconf lifc, lifc6;
	lifc.lifc_family = AF_INET;
	lifc.lifc_flags  = 0;
	lifc.lifc_len    = lifn.lifn_count * sizeof(struct lifreq);
	lifc.lifc_buf    = alloca (lifc.lifc_len);
	if (ioctl (sock, SIOCGLIFCONF, &lifc) < 0) {
		close (sock);
		close (sock6);
		return -1;
	}
	g_trace ("ioctl(AF_INET, SIOCGLIFCONF) = %d (%d)", lifc.lifc_len, lifc.lifc_len / sizeof(struct lifreq));

/* repeat everything for IPv6 */
	lifn.lifn_family = AF_INET6;
	lifn.lifn_flags  = 0;
	if (ioctl (sock6, SIOCGLIFNUM, &lifn) < 0) {
		close (sock);
		close (sock6);
		return -1;
	}
	if_count += lifn.lifn_count;
	g_trace ("ioctl(AF_INET6, SIOCGLIFNUM) = %d", lifn.lifn_count);

	lifn.lifn_count += 4;

	lifc6.lifc_family = AF_INET6;
	lifc6.lifc_flags  = 0;
	lifc6.lifc_len     = lifn.lifn_count * sizeof(struct lifreq);
	lifc6.lifc_buf     = alloca (lifc6.lifc_len);
	if (ioctl (sock6, SIOCGLIFCONF, &lifc6) < 0) {
		close (sock);
		close (sock6);
		return -1;
	}
	g_trace ("ioctl(AF_INET6, SIOCGLIFCONF) = %d (%d)", lifc6.lifc_len, lifc6.lifc_len / sizeof(struct lifreq));

	unsigned nlifr = (lifc.lifc_len + lifc6.lifc_len) / sizeof(struct lifreq);
	if (nlifr > if_count)
		goto again;

/* alloc a contiguous block for entire list */
	struct _pgm_ifaddrs* ifa = calloc (nlifr, sizeof (struct _pgm_ifaddrs));
	g_assert (NULL != ifa);

	struct _pgm_ifaddrs* ift = ifa;
	struct lifreq* lifr      = lifc.lifc_req;
	struct lifreq* lifr_end  = (struct lifreq *)&lifc.lifc_buf[lifc.lifc_len];

	g_assert (IF_NAMESIZE >= LIFNAMSIZ);

	while (lifr < lifr_end)
	{
/* name */
		g_trace ("AF_INET/name: %s", lifr->lifr_name ? lifr->lifr_name : "(null)");
		ift->_ifa.ifa_name = ift->_name;
		strncpy (ift->_ifa.ifa_name, lifr->lifr_name, LIFNAMSIZ);
		ift->_ifa.ifa_name[LIFNAMSIZ - 1] = 0;

/* flags */
		if (ioctl (sock, SIOCGLIFFLAGS, lifr) != -1) {
			ift->_ifa.ifa_flags = lifr->lifr_flags;
		}

/* address */
		if (ioctl (sock, SIOCGLIFADDR, lifr) != -1) {
			ift->_ifa.ifa_addr = (gpointer)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &lifr->lifr_addr, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_addr));
		}

/* netmask */
		if (ioctl (sock, SIOCGLIFNETMASK, lifr) != -1) {
			ift->_ifa.ifa_netmask = (gpointer)&ift->_netmask;
#		ifdef CONFIG_HAVE_IFR_NETMASK
			memcpy (ift->_ifa.ifa_netmask, &lifr->lifr_netmask, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_netmask));
#		else
			memcpy (ift->_ifa.ifa_netmask, &lifr->lifr_addr, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_addr));
#		endif
		}

		++lifr;
		if (lifr < lifr_end) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}
	}

/* repeat everything for IPv6 */
	lifr  = lifc6.lifc_req;
	lifr_end = (struct lifreq *)&lifc6.lifc_buf[lifc6.lifc_len];

	while (lifr < lifr_end)
	{
		if (ift != ifa) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}

/* address */
		if (ioctl (sock6, SIOCGLIFADDR, lifr) != -1) {
			ift->_ifa.ifa_addr = (gpointer)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &lifr->lifr_addr, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_addr));
		}

/* name */
		g_trace ("AF_INET6/name: %s", lifr->lifr_name ? lifr->lifr_name : "(null)");
		ift->_ifa.ifa_name = ift->_name;
		strncpy (ift->_ifa.ifa_name, lifr->lifr_name, sizeof(lifr->lifr_name));
		ift->_ifa.ifa_name[sizeof(lifr->lifr_name) - 1] = 0;

/* flags */
		if (ioctl (sock6, SIOCGLIFFLAGS, lifr) != -1) {
			ift->_ifa.ifa_flags = lifr->lifr_flags;
		}

/* netmask */
		if (ioctl (sock6, SIOCGLIFNETMASK, lifr) != -1) {
			ift->_ifa.ifa_netmask = (gpointer)&ift->_netmask;
#		ifdef CONFIG_HAVE_IFR_NETMASK
			memcpy (ift->_ifa.ifa_netmask, &lifr->lifr_netmask, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_netmask));
#		else
			memcpy (ift->_ifa.ifa_netmask, &lifr->lifr_addr, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_addr));
#		endif
		}

		++lifr;
	}

#	else /* !SIOCGLIFCONF */

/* process IPv4 interfaces */
	char buf[1024];
	struct ifconf ifc;
	ifc.ifc_buf = buf;
	ifc.ifc_len = sizeof(buf);
	if (ioctl (sock, SIOCGIFCONF, &ifc) < 0) {
		close (sock);
		return -1;
	}
	int if_count = ifc.ifc_len / sizeof(struct ifreq);

#		ifdef CONFIG_HAVE_IPV6_SIOCGIFADDR

/* process IPv6 interfaces */
	int sock6 = socket (AF_INET6, SOCK_DGRAM, 0);
	if (sock6 < 0) {
		close (sock);
		return -1;
	}

	char buf6[1024];
	struct ifconf ifc6;
	ifc6.ifc_buf = buf6;
	ifc6.ifc_len = sizeof(buf6);
	if (ioctl (sock6, SIOCGIFCONF, &ifc6) < 0) {
		close (sock);
		close (sock6);
		return -1;
	}
	if_count += ifc6.ifc_len / sizeof(struct ifreq);

#		endif /* CONFIG_HAVE_IPV6_SIOCGIFADDR */

/* alloc a contiguous block for entire list */
	struct _pgm_ifaddrs* ifa = malloc (if_count * sizeof(struct _pgm_ifaddrs));
	struct _pgm_ifaddrs* ift = ifa;
	memset (ifa, 0, if_count * sizeof(struct _pgm_ifaddrs));
	struct ifreq *ifr  = ifc.ifc_req;
	struct ifreq *ifr_end = (struct ifreq *)&ifc.ifc_buf[ifc.ifc_len];

	g_assert (IF_NAMESIZE >= sizeof(ifr->ifr_name));

	while (ifr < ifr_end)
	{
/* address */
		if (ioctl (sock, SIOCGIFADDR, ifr) != -1) {
			ift->_ifa.ifa_addr = (gpointer)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
		}

/* name */
		g_trace ("AF_INET/name:%s", ifr->ifr_name ? ifr->ifr_name : "(null)");
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
#	ifdef CONFIG_HAVE_IFR_NETMASK
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_netmask, pgm_sockaddr_len(&ifr->ifr_netmask));
#	else
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
#	endif
		}

		++ifr;
		if (ifr < ifr_end) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}
	}

#		ifdef CONFIG_HAVE_IPV6_SIOCGIFADDR

/* repeat everything for IPv6 */
	ifr  = ifc6.ifc_req;
	ifr_end = (struct ifreq *)&ifc6.ifc_buf[ifc6.ifc_len];

	while (ifr < ifr_end)
	{
		if (ift != ifa) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}

/* address, note this does not work on Linux as struct ifreq is too small for an IPv6 address */
		if (ioctl (sock6, SIOCGIFADDR, ifr) != -1) {
			ift->_ifa.ifa_addr = (gpointer)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
		}

/* name */
		g_trace ("AF_INET6/name:%s", ifr->ifr_name ? ifr->ifr_name : "(null)");
		ift->_ifa.ifa_name = ift->_name;
		strncpy (ift->_ifa.ifa_name, ifr->ifr_name, sizeof(ifr->ifr_name));
		ift->_ifa.ifa_name[sizeof(ifr->ifr_name) - 1] = 0;

/* flags */
		if (ioctl (sock6, SIOCGIFFLAGS, ifr) != -1) {
			ift->_ifa.ifa_flags = ifr->ifr_flags;
		}

/* netmask */
		if (ioctl (sock6, SIOCGIFNETMASK, ifr) != -1) {
			ift->_ifa.ifa_netmask = (gpointer)&ift->_netmask;
#		ifdef CONFIG_HAVE_IFR_NETMASK
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_netmask, pgm_sockaddr_len(&ifr->ifr_netmask));
#		else
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
#		endif
		}

		++ifr;
	}

	close (sock6);

#		endif /* CONFIG_HAVE_IPV6_SIOCGIFADDR */
#	endif /* !SIOCGLIFCONF */

	close (sock);

#elif defined(CONFIG_TARGET_WINE) /* !G_OS_UNIX */

	DWORD dwRet;
	ULONG ulOutBufLen = sizeof (IP_ADAPTER_INFO);
	PIP_ADAPTER_INFO pAdapterInfo;
	PIP_ADAPTER_INFO pAdapter = NULL;

	pAdapterInfo = (IP_ADAPTER_INFO *) malloc(sizeof (IP_ADAPTER_INFO));
	if (NULL == pAdapterInfo) {
		g_error(_("malloc failed for pAdapterInfo."));
		return -1;
	}
	dwRet = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen);
	if (ERROR_BUFFER_OVERFLOW == dwRet) {
		free(pAdapterInfo);
		pAdapterInfo = (IP_ADAPTER_INFO *) malloc(ulOutBufLen);
		if (NULL == pAdapterInfo) {
			g_error(_("malloc failed for pAdapterInfo on provided buffer size of %ul bytes."), ulOutBufLen);
			return -1;
		}
	}
	dwRet = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen);
	if (NO_ERROR != dwRet) {
		g_error(_("GetAdaptersInfo(2) did not return NO_ERROR."));
		free(pAdapterInfo);
		return -1;
	}

/* count valid adapters */
	int n = 0, k = 0;
	for (pAdapter = pAdapterInfo;
		 pAdapter;
		 pAdapter = pAdapter->Next)
	{
		for (IP_ADDR_STRING *pIPAddr = &pAdapter->IpAddressList;
			 pIPAddr;
			 pIPAddr = pIPAddr->Next)
		{
/* skip null adapters */
			if (strlen (pIPAddr->IpAddress.String) == 0)
				continue;
			++n;
		}
	}

/* contiguous block for adapter list */
	struct _pgm_ifaddrs* ifa = malloc (n * sizeof(struct _pgm_ifaddrs));
	memset (ifa, 0, n * sizeof(struct _pgm_ifaddrs));
	struct _pgm_ifaddrs* ift = ifa;

/* now populate list */
	for (pAdapter = pAdapterInfo;
		 pAdapter;
		 pAdapter = pAdapter->Next)
	{
		for (IP_ADDR_STRING *pIPAddr = &pAdapter->IpAddressList;
			 pIPAddr;
			 pIPAddr = pIPAddr->Next)
		{
/* skip null adapters */
			if (strlen (pIPAddr->IpAddress.String) == 0)
				continue;
/* address */
			ift->_ifa.ifa_addr = (gpointer)&ift->_addr;
			g_assert (pgm_sockaddr_pton (pIPAddr->IpAddress.String, ift->_ifa.ifa_addr));

/* name */
			g_trace ("name:%s", pAdapter->AdapterName);
			ift->_ifa.ifa_name = ift->_name;
			strncpy (ift->_ifa.ifa_name, pAdapter->AdapterName, IF_NAMESIZE);
			ift->_ifa.ifa_name[IF_NAMESIZE - 1] = 0;

/* flags */
			ift->_ifa.ifa_flags = 0;

/* netmask */
			ift->_ifa.ifa_netmask = (gpointer)&ift->_netmask;
			g_assert (pgm_sockaddr_pton (pIPAddr->IpMask.String, ift->_ifa.ifa_netmask));

/* next */
			if (k++ < (n - 1)) {
				ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
				ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
			}
		}
	}

	free(pAdapterInfo);

#else /* !CONFIG_TARGET_WINE */

	DWORD dwSize = 0, dwRet;
	IP_ADAPTER_ADDRESSES *pAdapterAddresses, *adapter;

	dwRet = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_SKIP_MULTICAST, NULL, NULL, &dwSize);
	if (ERROR_BUFFER_OVERFLOW != dwRet) {
		g_error(_("GetAdaptersAddresses did not return ERROR_BUFFER_OVERFLOW."));
		return -1;
	}
	pAdapterAddresses = (IP_ADAPTER_ADDRESSES*)malloc (dwSize);
	if (NULL == pAdapterAddresses) {
		g_error(_("malloc failed for pAdapterAddresses on provided buffer size of %u bytes."), (unsigned)dwSize);
		return -1;
	}
	dwRet = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_SKIP_MULTICAST, NULL, pAdapterAddresses, &dwSize);
	if (ERROR_SUCCESS != dwRet) {
		g_error(_("GetAdaptersAddresses(2) did not return ERROR_SUCCESS."));
		free(pAdapterAddresses);
		return -1;
	}

/* count valid adapters */
	int n = 0, k = 0;
	for (adapter = pAdapterAddresses;
		 adapter;
		 adapter = adapter->Next)
	{
		for (IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress;
			 unicast;
			 unicast = unicast->Next)
		{
/* ensure IP adapter */
			if (AF_INET != unicast->Address.lpSockaddr->sa_family &&
			    AF_INET6 != unicast->Address.lpSockaddr->sa_family)
			{
				continue;
			}

			++n;
		}
	}

/* contiguous block for adapter list */
	struct _pgm_ifaddrs* ifa = malloc (n * sizeof(struct _pgm_ifaddrs));
	memset (ifa, 0, n * sizeof(struct _pgm_ifaddrs));
	struct _pgm_ifaddrs* ift = ifa;

/* now populate list */
	for (adapter = pAdapterAddresses;
		 adapter;
		 adapter = adapter->Next)
	{
		int unicastIndex = 0;
		for (IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress;
			 unicast;
			 unicast = unicast->Next, ++unicastIndex)
		{
/* ensure IP adapter */
			if (AF_INET != unicast->Address.lpSockaddr->sa_family &&
			    AF_INET6 != unicast->Address.lpSockaddr->sa_family)
			{
				continue;
			}

/* address */
			ift->_ifa.ifa_addr = (gpointer)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, unicast->Address.lpSockaddr, unicast->Address.iSockaddrLength);

/* name */
			g_trace ("name:%s", adapter->AdapterName);
			ift->_ifa.ifa_name = ift->_name;
			strncpy (ift->_ifa.ifa_name, adapter->AdapterName, IF_NAMESIZE);
			ift->_ifa.ifa_name[IF_NAMESIZE - 1] = 0;

/* flags */
			ift->_ifa.ifa_flags = 0;
			if (IfOperStatusUp == adapter->OperStatus)
				ift->_ifa.ifa_flags |= IFF_UP;
			if (IF_TYPE_SOFTWARE_LOOPBACK == adapter->IfType)
				ift->_ifa.ifa_flags |= IFF_LOOPBACK;
			if (!(adapter->Flags & IP_ADAPTER_NO_MULTICAST))
				ift->_ifa.ifa_flags |= IFF_MULTICAST;

/* netmask */
			ift->_ifa.ifa_netmask = (gpointer)&ift->_netmask;

/* pre-Vista must hunt for matching prefix in linked list, otherwise use OnLinkPrefixLength */
			int prefixIndex = 0;
			ULONG prefixLength = 0;
			for (IP_ADAPTER_PREFIX *prefix = adapter->FirstPrefix;
				prefix;
				prefix = prefix->Next, ++prefixIndex)
			{
				if (prefixIndex == unicastIndex) {
					prefixLength = prefix->PrefixLength;
					break;
				}
			}

/* map prefix to netmask */
			ift->_ifa.ifa_netmask->sa_family = unicast->Address.lpSockaddr->sa_family;
			switch (unicast->Address.lpSockaddr->sa_family) {
			case AF_INET:
				if (0 == prefixLength) {
					g_warning (_("IPv4 adapter %s prefix length is 0, overriding to 32."), adapter->AdapterName);
					prefixLength = 32;
				}
				((struct sockaddr_in*)ift->_ifa.ifa_netmask)->sin_addr.s_addr = g_htonl( 0xffffffffU << ( 32 - prefixLength ) );
				break;

			case AF_INET6:
				if (0 == prefixLength) {
					g_warning (_("IPv6 adapter %s prefix length is 0, overriding to 128."), adapter->AdapterName);
					prefixLength = 128;
				}
				for (ULONG i = prefixLength, j = 0; i > 0; i -= 8, ++j)
				{
					((struct sockaddr_in6*)ift->_ifa.ifa_netmask)->sin6_addr.s6_addr[ j ] = i >= 8 ? 0xff : (ULONG)(( 0xffU << ( 8 - i ) ) & 0xffU );
				}
				break;
			}

/* next */
			if (k++ < (n - 1)) {
				ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
				ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
			}
		}
	}

	free (pAdapterAddresses);

#endif /* !G_OS_UNIX */

	*ifap = (struct pgm_ifaddrs*)ifa;
	return 0;
}

void
pgm_compat_freeifaddrs (
	struct pgm_ifaddrs*	ifa
	)
{
	free (ifa);
}

/* eof */
