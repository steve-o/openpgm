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

#include <errno.h>
#ifdef CONFIG_HAVE_GETIFADDRS
#	include <ifaddrs.h>
#endif
#if defined( sun )
#	include <sys/sockio.h>
#endif
#include <pgm/i18n.h>
#include <pgm/framework.h>


//#define GETIFADDRS_DEBUG

/* locals */
struct _pgm_ifaddrs
{
	struct pgm_ifaddrs		_ifa;
	char				_name[IF_NAMESIZE];
	struct sockaddr_storage		_addr;
	struct sockaddr_storage		_netmask;
};

/* returns TRUE on success setting ifap to a linked list of system interfaces,
 * returns FALSE on failure and sets error appropriately.
 */

#ifdef SIOCGLIFCONF
static
bool
_pgm_getlifaddrs (
	struct pgm_ifaddrs**	ifap,
	pgm_error_t**		error
	)
{
	const int sock = socket (AF_INET, SOCK_DGRAM, 0);
	if (-1 == sock) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("Opening IPv4 datagram socket: %s"),
				strerror (errno));
		return FALSE;
	}

/* process IPv6 interfaces */
	const int sock6 = socket (AF_INET6, SOCK_DGRAM, 0);
	if (-1 == sock6) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("Opening IPv6 datagram socket: %s"),
				strerror (errno));
		close (sock);
		return FALSE;
	}

	struct lifnum lifn;
again:
	lifn.lifn_family = AF_INET;
	lifn.lifn_flags  = 0;
	if (-1 == ioctl (sock, SIOCGLIFNUM, &lifn)) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("SIOCGLIFNUM failed on IPv4 socket: %s"),
				strerror (errno));
		close (sock);
		close (sock6);
		return FALSE;
	}
	unsigned if_count = lifn.lifn_count;
	pgm_debug ("ioctl(AF_INET, SIOCGLIFNUM) = %d", lifn.lifn_count);

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
	if (-1 == ioctl (sock, SIOCGLIFCONF, &lifc)) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("SIOCGLIFCONF failed on IPv4 socket: %s"),
				strerror (errno));
		close (sock);
		close (sock6);
		return FALSE;
	}
	pgm_debug ("ioctl(AF_INET, SIOCGLIFCONF) = %d (%d)", lifc.lifc_len, (int)(lifc.lifc_len / sizeof(struct lifreq)));

/* repeat everything for IPv6 */
	lifn.lifn_family = AF_INET6;
	lifn.lifn_flags  = 0;
	if (-1 == ioctl (sock6, SIOCGLIFNUM, &lifn)) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("SIOCGLIFNUM failed on IPv6 socket: %s"),
				strerror (errno));
		close (sock);
		close (sock6);
		return FALSE;
	}
	if_count += lifn.lifn_count;
	pgm_debug ("ioctl(AF_INET6, SIOCGLIFNUM) = %d", lifn.lifn_count);

	lifn.lifn_count += 4;

	lifc6.lifc_family = AF_INET6;
	lifc6.lifc_flags  = 0;
	lifc6.lifc_len     = lifn.lifn_count * sizeof(struct lifreq);
	lifc6.lifc_buf     = alloca (lifc6.lifc_len);
	if (-1 == ioctl (sock6, SIOCGLIFCONF, &lifc6)) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("SIOCGLIFCONF failed on IPv6 socket: %s"),
				strerror (errno));
		close (sock);
		close (sock6);
		return FALSE;
	}
	pgm_debug ("ioctl(AF_INET6, SIOCGLIFCONF) = %d (%d)", lifc6.lifc_len, (int)(lifc6.lifc_len / sizeof(struct lifreq)));

	unsigned nlifr = (lifc.lifc_len + lifc6.lifc_len) / sizeof(struct lifreq);
	if (nlifr > if_count)
		goto again;

/* alloc a contiguous block for entire list */
	struct _pgm_ifaddrs* ifa = calloc (nlifr, sizeof (struct _pgm_ifaddrs));
	pgm_assert (NULL != ifa);

	struct _pgm_ifaddrs* ift = ifa;
	struct lifreq* lifr      = lifc.lifc_req;
	struct lifreq* lifr_end  = (struct lifreq *)&lifc.lifc_buf[lifc.lifc_len];

	pgm_assert (IF_NAMESIZE >= LIFNAMSIZ);

	while (lifr < lifr_end)
	{
/* name */
		pgm_debug ("AF_INET/name: %s", lifr->lifr_name ? lifr->lifr_name : "(null)");
		ift->_ifa.ifa_name = ift->_name;
		strncpy (ift->_ifa.ifa_name, lifr->lifr_name, LIFNAMSIZ);
		ift->_ifa.ifa_name[LIFNAMSIZ - 1] = 0;

/* flags */
		if (-1 != ioctl (sock, SIOCGLIFFLAGS, lifr)) {
			ift->_ifa.ifa_flags = lifr->lifr_flags;
		} else {
			pgm_warn (_("SIOCGLIFFLAGS failed on interface %s%s%s"),
				lifr->lifr_name ? "\"" : "", lifr->lifr_name ? lifr->lifr_name : "(null)", lifr->lifr_name ? "\"" : "");
		}

/* address */
		if (-1 != ioctl (sock, SIOCGLIFADDR, lifr)) {
			ift->_ifa.ifa_addr = (void*)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &lifr->lifr_addr, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_addr));
		} else {
			pgm_warn (_("SIOCGLIFADDR failed on interface %s%s%s"),
				lifr->lifr_name ? "\"" : "", lifr->lifr_name ? lifr->lifr_name : "(null)", lifr->lifr_name ? "\"" : "");
		}

/* netmask */
		if (-1 != ioctl (sock, SIOCGLIFNETMASK, lifr)) {
			ift->_ifa.ifa_netmask = (void*)&ift->_netmask;
#		ifdef CONFIG_HAVE_IFR_NETMASK
			memcpy (ift->_ifa.ifa_netmask, &lifr->lifr_netmask, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_netmask));
#		else
			memcpy (ift->_ifa.ifa_netmask, &lifr->lifr_addr, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_addr));
#		endif
		} else {
			pgm_warn (_("SIOCGLIFNETMASK failed on interface %s%s%s"),
				lifr->lifr_name ? "\"" : "", lifr->lifr_name ? lifr->lifr_name : "(null)", lifr->lifr_name ? "\"" : "");
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

/* name */
		pgm_debug ("AF_INET6/name: %s", lifr->lifr_name ? lifr->lifr_name : "(null)");
		ift->_ifa.ifa_name = ift->_name;
		strncpy (ift->_ifa.ifa_name, lifr->lifr_name, sizeof(lifr->lifr_name));
		ift->_ifa.ifa_name[sizeof(lifr->lifr_name) - 1] = 0;

/* flags */
		if (-1 != ioctl (sock6, SIOCGLIFFLAGS, lifr)) {
			ift->_ifa.ifa_flags = lifr->lifr_flags;
		} else {
			pgm_warn (_("SIOCGLIFFLAGS failed on interface %s%s%s"),
				lifr->lifr_name ? "\"" : "", lifr->lifr_name ? lifr->lifr_name : "(null)", lifr->lifr_name ? "\"" : "");
		}

/* address */
		if (-1 != ioctl (sock6, SIOCGLIFADDR, lifr)) {
			ift->_ifa.ifa_addr = (void*)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &lifr->lifr_addr, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_addr));
		} else {
			pgm_warn (_("SIOCGLIFADDR failed on interface %s%s%s"),
				lifr->lifr_name ? "\"" : "", lifr->lifr_name ? lifr->lifr_name : "(null)", lifr->lifr_name ? "\"" : "");
		}

/* netmask */
		if (ioctl (sock6, SIOCGLIFNETMASK, lifr) != -1) {
			ift->_ifa.ifa_netmask = (void*)&ift->_netmask;
#		ifdef CONFIG_HAVE_IFR_NETMASK
			memcpy (ift->_ifa.ifa_netmask, &lifr->lifr_netmask, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_netmask));
#		else
			memcpy (ift->_ifa.ifa_netmask, &lifr->lifr_addr, pgm_sockaddr_len((struct sockaddr*)&lifr->lifr_addr));
#		endif
		} else {
			pgm_warn (_("SIOCGLIFNETMASK failed on interface %s%s%s"),
				lifr->lifr_name ? "\"" : "", lifr->lifr_name ? lifr->lifr_name : "(null)", lifr->lifr_name ? "\"" : "");
		}

		++lifr;
	}

	if (-1 == close (sock6))
		pgm_warn (_("Closing IPv6 socket failed: %s"), strerror(errno));
	if (-1 == close (sock))
		pgm_warn (_("Closing IPv4 socket failed: %s"), strerror(errno));

	*ifap = (struct pgm_ifaddrs*)ifa;
	return TRUE;
}
#endif /* SIOCGLIFCONF */

#ifdef SIOCGIFCONF
static
bool
_pgm_getifaddrs (
	struct pgm_ifaddrs**	ifap,
	pgm_error_t**		error
	)
{
	const int sock = socket (AF_INET, SOCK_DGRAM, 0);
	if (-1 == sock) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("Opening IPv4 datagram socket: %s"),
				strerror (errno));
		return FALSE;
	}

/* process IPv4 interfaces */
	char buf[1024];
	struct ifconf ifc;
	ifc.ifc_buf = buf;
	ifc.ifc_len = sizeof(buf);
	if (-1 == ioctl (sock, SIOCGIFCONF, &ifc)) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("SIOCGIFCONF failed on IPv4 socket: %s"),
				strerror (errno));
		close (sock);
		return FALSE;
	}
	int if_count = ifc.ifc_len / sizeof(struct ifreq);

#	ifdef CONFIG_HAVE_IPV6_SIOCGIFADDR
/* process IPv6 interfaces */
	const int sock6 = socket (AF_INET6, SOCK_DGRAM, 0);
	if (-1 == sock6) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("Opening IPv6 datagram socket: %s"),
				strerror (errno));
		close (sock);
		return FALSE;
	}

	char buf6[1024];
	struct ifconf ifc6;
	ifc6.ifc_buf = buf6;
	ifc6.ifc_len = sizeof(buf6);
	if (-1 == ioctl (sock6, SIOCGIFCONF, &ifc6)) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("SIOCGIFCONF failed on IPv6 socket: %s"),
				strerror (errno));
		close (sock);
		close (sock6);
		return FALSE;
	}
	if_count += ifc6.ifc_len / sizeof(struct ifreq);
#	endif /* CONFIG_HAVE_IPV6_SIOCGIFADDR */

/* alloc a contiguous block for entire list */
	struct _pgm_ifaddrs* ifa = pgm_new0 (struct _pgm_ifaddrs, if_count);
	struct _pgm_ifaddrs* ift = ifa;
	struct ifreq *ifr  = ifc.ifc_req;
	struct ifreq *ifr_end = (struct ifreq *)&ifc.ifc_buf[ifc.ifc_len];

	pgm_assert (IF_NAMESIZE >= sizeof(ifr->ifr_name));

	while (ifr < ifr_end)
	{
/* name */
		pgm_debug ("AF_INET/name:%s", ifr->ifr_name ? ifr->ifr_name : "(null)");
		ift->_ifa.ifa_name = ift->_name;
		strncpy (ift->_ifa.ifa_name, ifr->ifr_name, sizeof(ifr->ifr_name));
		ift->_ifa.ifa_name[sizeof(ifr->ifr_name) - 1] = 0;

/* flags */
		if (-1 != ioctl (sock, SIOCGIFFLAGS, ifr)) {
			ift->_ifa.ifa_flags = ifr->ifr_flags;
		} else {
			pgm_warn (_("SIOCGIFFLAGS failed on interface %s%s%s"),
				ifr->ifr_name ? "\"" : "", ifr->ifr_name ? ifr->ifr_name : "(null)", ifr->ifr_name ? "\"" : "");
		}

/* address */
		if (-1 != ioctl (sock, SIOCGIFADDR, ifr)) {
			ift->_ifa.ifa_addr = (void*)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
		} else {
			pgm_warn (_("SIOCGIFADDR failed on interface %s%s%s"),
				ifr->ifr_name ? "\"" : "", ifr->ifr_name ? ifr->ifr_name : "(null)", ifr->ifr_name ? "\"" : "");
		}

/* netmask */
		if (-1 != ioctl (sock, SIOCGIFNETMASK, ifr)) {
			ift->_ifa.ifa_netmask = (void*)&ift->_netmask;
#	ifdef CONFIG_HAVE_IFR_NETMASK
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_netmask, pgm_sockaddr_len(&ifr->ifr_netmask));
#	else
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
#	endif
		} else {
			pgm_warn (_("SIOCGIFNETMASK failed on interface %s%s%s"),
				ifr->ifr_name ? "\"" : "", ifr->ifr_name ? ifr->ifr_name : "(null)", ifr->ifr_name ? "\"" : "");
		}

		++ifr;
		if (ifr < ifr_end) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}
	}

#	ifdef CONFIG_HAVE_IPV6_SIOCGIFADDR
/* repeat everything for IPv6 */
	ifr  = ifc6.ifc_req;
	ifr_end = (struct ifreq *)&ifc6.ifc_buf[ifc6.ifc_len];

	while (ifr < ifr_end)
	{
		if (ift != ifa) {
			ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
			ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
		}

/* name */
		pgm_debug ("AF_INET6/name:%s", ifr->ifr_name ? ifr->ifr_name : "(null)");
		ift->_ifa.ifa_name = ift->_name;
		strncpy (ift->_ifa.ifa_name, ifr->ifr_name, sizeof(ifr->ifr_name));
		ift->_ifa.ifa_name[sizeof(ifr->ifr_name) - 1] = 0;

/* flags */
		if (-1 != ioctl (sock6, SIOCGIFFLAGS, ifr)) {
			ift->_ifa.ifa_flags = ifr->ifr_flags;
		} else {
			pgm_warn (_("SIOCGIFFLAGS failed on interface %s%s%s"),
				ifr->ifr_name ? "\"" : "", ifr->ifr_name ? ifr->ifr_name : "(null)", ifr->ifr_name ? "\"" : "");
		}

/* address, note this does not work on Linux as struct ifreq is too small for an IPv6 address */
		if (-1 != ioctl (sock6, SIOCGIFADDR, ifr)) {
			ift->_ifa.ifa_addr = (void*)&ift->_addr;
			memcpy (ift->_ifa.ifa_addr, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
		} else {
			pgm_warn (_("SIOCGIFADDR failed on interface %s%s%s"),
				ifr->ifr_name ? "\"" : "", ifr->ifr_name ? ifr->ifr_name : "(null)", ifr->ifr_name ? "\"" : "");
		}

/* netmask */
		if (-1 != ioctl (sock6, SIOCGIFNETMASK, ifr)) {
			ift->_ifa.ifa_netmask = (void*)&ift->_netmask;
#		ifdef CONFIG_HAVE_IFR_NETMASK
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_netmask, pgm_sockaddr_len(&ifr->ifr_netmask));
#		else
			memcpy (ift->_ifa.ifa_netmask, &ifr->ifr_addr, pgm_sockaddr_len(&ifr->ifr_addr));
#		endif
		} else {
			pgm_warn (_("SIOCGIFNETMASK failed on interface %s%s%s"),
				ifr->ifr_name ? "\"" : "", ifr->ifr_name ? ifr->ifr_name : "(null)", ifr->ifr_name ? "\"" : "");
		}

		++ifr;
	}

	if (-1 == close (sock6))
		pgm_warn (_("Closing IPv6 socket failed: %s"), strerror(errno));
#	endif /* CONFIG_HAVE_IPV6_SIOCGIFADDR */

	if (-1 == close (sock))
		pgm_warn (_("Closing IPv4 socket failed: %s"), strerror(errno));

	*ifap = (struct pgm_ifaddrs*)ifa;
	return TRUE;
}
#endif /* SIOCLIFCONF */

#if defined(_WIN32)
static
bool
_pgm_getadaptersinfo (
	struct pgm_ifaddrs**	ifap,
	pgm_error_t**		error
	)
{
	DWORD dwRet;
	LPVOID lpMsgBuf = NULL;
	ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);
	PIP_ADAPTER_INFO pAdapterInfo = NULL;
	PIP_ADAPTER_INFO pAdapter = NULL;

/* loop to handle interfaces coming online causing a buffer overflow
 * between first call to list buffer length and second call to enumerate.
 */
	for (unsigned i = 100; i; i--)
	{
		if (pAdapterInfo) {
			pgm_free (pAdapterInfo);
			pAdapterInfo = NULL;
		}
		pAdapterInfo = (IP_ADAPTER_INFO*)pgm_malloc (ulOutBufLen);
		dwRet = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen);
		if (ERROR_BUFFER_OVERFLOW != dwRet)
			break;
	}

	switch (dwRet) {
	case ERROR_SUCCESS:	/* NO_ERROR */
		break;
	case ERROR_BUFFER_OVERFLOW:
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (ENOBUFS),
				_("GetAdaptersInfo repeatedly failed with ERROR_BUFFER_OVERFLOW."));
		free (pAdapterInfo);
		return FALSE;
	default:
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_adapter_errno (dwRet),
				_("GetAdaptersInfo failed: %s"),
				adapter_strerror (dwRet));
		free (pAdapterInfo);
		return FALSE;
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
	struct _pgm_ifaddrs* ifa = pgm_new0 (struct _pgm_ifaddrs, n);
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
			ift->_ifa.ifa_addr = (void*)&ift->_addr;
			pgm_assert (pgm_sockaddr_pton (pIPAddr->IpAddress.String, ift->_ifa.ifa_addr));

/* name */
			pgm_debug ("name:%s", pAdapter->AdapterName);
			ift->_ifa.ifa_name = ift->_name;
			strncpy (ift->_ifa.ifa_name, pAdapter->AdapterName, IF_NAMESIZE);
			ift->_ifa.ifa_name[IF_NAMESIZE - 1] = 0;

/* flags */
			ift->_ifa.ifa_flags = 0;

/* netmask */
			ift->_ifa.ifa_netmask = (void*)&ift->_netmask;
			pgm_assert (pgm_sockaddr_pton (pIPAddr->IpMask.String, ift->_ifa.ifa_netmask));

/* next */
			if (k++ < (n - 1)) {
				ift->_ifa.ifa_next = (struct pgm_ifaddrs*)(ift + 1);
				ift = (struct _pgm_ifaddrs*)(ift->_ifa.ifa_next);
			}
		}
	}

	free (pAdapterInfo);
	*ifap = (struct pgm_ifaddrs*)ifa;
	return TRUE;
}

static
bool
_pgm_getadaptersaddresses (
	struct pgm_ifaddrs**	ifap,
	pgm_error_t**		error
	)
{
	DWORD dwSize = sizeof(IP_ADAPTER_ADDRESSES), dwRet;
	IP_ADAPTER_ADDRESSES *pAdapterAddresses = NULL, *adapter;

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
						GAA_FLAG_INCLUDE_PREFIX |
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
                pgm_set_error (error,
                                PGM_ERROR_DOMAIN_IF,
                                pgm_error_from_errno (ENOBUFS),
                                _("GetAdaptersAddresses repeatedly failed with ERROR_BUFFER_OVERFLOW."));
                free (pAdapterAddresses);
                return FALSE;
        default:
                pgm_set_error (error,
                                PGM_ERROR_DOMAIN_IF,
                                pgm_error_from_adapter_errno (dwRet),
                                _("GetAdaptersAddresses failed: %s"),
                                adapter_strerror (dwRet));
                free (pAdapterAddresses);
                return FALSE;
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
	struct _pgm_ifaddrs* ifa = pgm_new0 (struct _pgm_ifaddrs, n);
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
			ift->_ifa.ifa_addr = (void*)&ift->_addr;
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
			ift->_ifa.ifa_netmask = (void*)&ift->_netmask;

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
					pgm_warn (_("IPv4 adapter %s prefix length is 0, overriding to 32."), adapter->AdapterName);
					prefixLength = 32;
				}
				((struct sockaddr_in*)ift->_ifa.ifa_netmask)->sin_addr.s_addr = g_htonl( 0xffffffffU << ( 32 - prefixLength ) );
				break;

			case AF_INET6:
				if (0 == prefixLength) {
					pgm_warn (_("IPv6 adapter %s prefix length is 0, overriding to 128."), adapter->AdapterName);
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
	*ifap = (struct pgm_ifaddrs*)ifa;
	return TRUE;
}
#endif /* G_OS_WIN */

/* returns TRUE on success setting ifap to a linked list of system interfaces,
 * returns FALSE on failure and sets error appropriately.
 */

bool
pgm_getifaddrs (
	struct pgm_ifaddrs** restrict ifap,
	pgm_error_t**	     restrict error
	)
{
	pgm_assert (NULL != ifap);

	pgm_debug ("pgm_getifaddrs (ifap:%p error:%p)",
		(void*)ifap, (void*)error);

#ifdef CONFIG_HAVE_GETIFADDRS
	const int e = getifaddrs ((struct ifaddrs**)ifap);
	if (-1 == e) {
		pgm_set_error (error,
				PGM_ERROR_DOMAIN_IF,
				pgm_error_from_errno (errno),
				_("getifaddrs failed: %s"),
				strerror (errno));
		return FALSE;
	}
	return TRUE;
#elif defined(CONFIG_TARGET_WINE)
	return _pgm_getadaptersinfo (ifap, error);
#elif defined(G_OS_WIN)
	return _pgm_getadaptersaddresses (ifap, error);
#elif defined(SIOCGLIFCONF)
	return _pgm_getlifaddrs (ifap, error);
#elif defined(SIOCGIFCONF)
	return _pgm_getifaddrs (ifap, error);
#else
#	error "Unsupported interface enumeration on this platform."
#endif /* !CONFIG_HAVE_GETIFADDRS */
}

void
pgm_freeifaddrs (
	struct pgm_ifaddrs*	ifa
	)
{
	pgm_return_if_fail (NULL != ifa);

#ifdef CONFIG_HAVE_GETIFADDRS
	freeifaddrs ((struct ifaddrs*)ifa);
#else
	pgm_free (ifa);
#endif
}

/* eof */
