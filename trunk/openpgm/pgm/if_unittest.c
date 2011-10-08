/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for network interface declaration parsing.
 *
 * CAUTION: Assumes host is IPv4 by default for AF_UNSPEC
 *
 * Copyright (c) 2009-2010 Miru Limited.
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

/* IFF_UP */
#ifndef _BSD_SOURCE
#	define _BSD_SOURCE	1
#endif
/* GNU gai_strerror_r */
#ifndef _GNU_SOURCE
#	define _GNU_SOURCE	1
#endif

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#	include <netdb.h>
#	include <net/if.h>
#	include <sys/socket.h>
#	include <sys/types.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
#include <glib.h>
#include <check.h>
#include <pgm/types.h>
#include <pgm/macros.h>


/* mock state */

struct mock_host_t {
	struct sockaddr_storage	address;
	char*			canonical_hostname;
	char*			alias;
};

struct mock_network_t {
	char*			name;
	struct sockaddr_storage	number;
	char**			aliases;
};

struct mock_interface_t {
	unsigned int		index;
	char*			name;
	unsigned int		flags;
	struct sockaddr_storage	addr;
	struct sockaddr_storage	netmask;
};

static GList *mock_hosts = NULL, *mock_networks = NULL, *mock_interfaces = NULL;

#define MOCK_HOSTNAME		"kiku"
#define MOCK_HOSTNAME6		"ip6-kiku"		/* ping6 doesn't work on fe80:: */
#define MOCK_NETWORK		"private"		/* /etc/networks */
#define MOCK_NETWORK6		"ip6-private"
#define MOCK_PGM_NETWORK	"pgm-private"
#define MOCK_PGM_NETWORK6	"ip6-pgm-private"
#define MOCK_INTERFACE		"eth0"
#define MOCK_INTERFACE_INDEX	2
#define MOCK_ADDRESS		"10.6.28.33"
#define MOCK_GROUP		((long) 0xefc00001) /* 239.192.0.1 */
#define MOCK_GROUP6_INIT	{ { { 0xff,8,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }	/* ff08::1 */
static const struct in6_addr mock_group6_addr = MOCK_GROUP6_INIT;
#define MOCK_ADDRESS6		"2002:dce8:d28e::33"
#define MOCK_ADDRESS6_INIT	{ { { 0x20,2,0xdc,0xe8,0xd2,0x8e,0,0,0,0,0,0,0,0,0,0x33 } } }
static const struct in6_addr mock_address6_addr = MOCK_ADDRESS6_INIT;

static int mock_family =	0;
static char* mock_kiku =	MOCK_HOSTNAME;
static char* mock_localhost =	"localhost";
static char* mock_invalid =	"invalid.invalid";		/* RFC 2606 */
static char* mock_toolong =	"abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij12345"; /* 65 */
static char* mock_hostname =	NULL;

struct pgm_ifaddrs_t;
struct pgm_error_t;

bool mock_pgm_getifaddrs (struct pgm_ifaddrs_t**, struct pgm_error_t**);
void mock_pgm_freeifaddrs (struct pgm_ifaddrs_t*);
unsigned mock_pgm_if_nametoindex (const sa_family_t, const char*);
char* mock_if_indextoname (unsigned int, char*);
int mock_getnameinfo (const struct sockaddr*, socklen_t, char*, size_t, char*, size_t, int);
int mock_getaddrinfo (const char*, const char*, const struct addrinfo*, struct addrinfo**);
void mock_freeaddrinfo (struct addrinfo*);
int mock_gethostname (char*, size_t);
struct pgm_netent_t* mock_pgm_getnetbyname (const char*);
PGM_GNUC_INTERNAL bool mock_pgm_if_getnodeaddr (const sa_family_t, struct sockaddr*restrict, const socklen_t, struct pgm_error_t**restrict);


#define pgm_getifaddrs		mock_pgm_getifaddrs
#define pgm_freeifaddrs		mock_pgm_freeifaddrs
#define pgm_if_nametoindex	mock_pgm_if_nametoindex
#define if_indextoname		mock_if_indextoname
#define getnameinfo		mock_getnameinfo
#define getaddrinfo		mock_getaddrinfo
#define freeaddrinfo		mock_freeaddrinfo
#define gethostname		mock_gethostname
#define pgm_getnetbyname	mock_pgm_getnetbyname
#define pgm_if_getnodeaddr	mock_pgm_if_getnodeaddr


#define IF_DEBUG
#include "if.c"


static
gpointer
create_host (
	const char*	address,
	const char*	canonical_hostname,
	const char*	alias
	)
{
	struct mock_host_t* new_host;

	g_assert (address);
	g_assert (canonical_hostname);

	new_host = g_slice_alloc0 (sizeof(struct mock_host_t));
	g_assert (pgm_sockaddr_pton (address, (struct sockaddr*)&new_host->address));
	new_host->canonical_hostname = g_strdup (canonical_hostname);
	new_host->alias = alias ? g_strdup (alias) : NULL;

	return new_host;
}

static
gpointer
create_network (
	const char*	name,
	const char*	number
	)
{
	struct mock_network_t* new_network;

	g_assert (name);
	g_assert (number);

	new_network = g_slice_alloc0 (sizeof(struct mock_network_t));
	new_network->name = g_strdup (name);
	g_assert (pgm_sockaddr_pton (number, (struct sockaddr*)&new_network->number));

	return new_network;
}

static
gpointer
create_interface (
	const unsigned	index,
	const char*	name,
	const char*	flags
	)
{
	struct mock_interface_t* new_interface;

	g_assert (name);
	g_assert (flags);

	new_interface = g_slice_alloc0 (sizeof(struct mock_interface_t));
	new_interface->index = index;
	new_interface->name = g_strdup (name);

	struct sockaddr_in* sin = (gpointer)&new_interface->addr;
	struct sockaddr_in6* sin6 = (gpointer)&new_interface->addr;

	gchar** tokens = g_strsplit (flags, ",", 0);
	for (guint i = 0; tokens[i]; i++)
	{
		if (strcmp (tokens[i], "up") == 0)
			new_interface->flags |= IFF_UP;
		else if (strcmp (tokens[i], "down") == 0)
			new_interface->flags |= 0;
		else if (strcmp (tokens[i], "loop") == 0)
			new_interface->flags |= IFF_LOOPBACK;
		else if (strcmp (tokens[i], "broadcast") == 0)
			new_interface->flags |= IFF_BROADCAST;
		else if (strcmp (tokens[i], "multicast") == 0)
			new_interface->flags |= IFF_MULTICAST;
		else if (strncmp (tokens[i], "ip=", strlen("ip=")) == 0) {
			const char* addr = tokens[i] + strlen("ip=");
			g_assert (pgm_sockaddr_pton (addr, (struct sockaddr*)&new_interface->addr));
		}
		else if (strncmp (tokens[i], "netmask=", strlen("netmask=")) == 0) {
			const char* addr = tokens[i] + strlen("netmask=");
			g_assert (pgm_sockaddr_pton (addr, (struct sockaddr*)&new_interface->netmask));
		}
		else if (strncmp (tokens[i], "scope=", strlen("scope=")) == 0) {
			const char* scope = tokens[i] + strlen("scope=");
			g_assert (AF_INET6 == ((struct sockaddr*)&new_interface->addr)->sa_family);
			((struct sockaddr_in6*)&new_interface->addr)->sin6_scope_id = atoi (scope);
		}
		else
			g_error ("parsing failed for flag %s%s%s",
				tokens[i] ? "\"" : "", tokens[i] ? tokens[i] : "(null)", tokens[i] ? "\"" : "");
	}
			
	g_strfreev (tokens);
	return new_interface;
}

#define APPEND_HOST2(a,b,c)	\
		do { \
			gpointer data = create_host ((a), (b), (c)); \
			g_assert (data); \
			mock_hosts = g_list_append (mock_hosts, data); \
			g_assert (mock_hosts); g_assert (mock_hosts->data); \
		} while (0)
#define APPEND_HOST(a,b)	APPEND_HOST2((a),(b),NULL)
#define APPEND_NETWORK(a,b)	\
		do { \
			gpointer data = create_network ((a), (b)); \
			g_assert (data); \
			mock_networks = g_list_append (mock_networks, data); \
			g_assert (mock_networks); g_assert (mock_networks->data); \
		} while (0)
#define APPEND_INTERFACE(a,b,c)	\
		do { \
			gpointer data = create_interface ((a), (b), (c)); \
			g_assert (data); \
			mock_interfaces = g_list_append (mock_interfaces, data); \
			g_assert (mock_interfaces); g_assert (mock_interfaces->data); \
		} while (0)
static
void
mock_setup_net (void)
{
	mock_hostname = mock_kiku;

	APPEND_HOST (	"127.0.0.1",		"localhost");
	APPEND_HOST2(	"10.6.28.33",		"kiku.hk.miru.hk",	"kiku");
	APPEND_HOST2(	"2002:dce8:d28e::33",	"ip6-kiku",		"kiku");
	APPEND_HOST2(	"172.12.90.1",		"mi-hee.ko.miru.hk",	"mi-hee");
	APPEND_HOST2(	"::1",			"ip6-localhost",	"ip6-loopback");
	APPEND_HOST (	"239.192.0.1",		"PGM.MCAST.NET");
	APPEND_HOST (	"ff08::1",		"IP6-PGM.MCAST.NET");

	APPEND_NETWORK(	"loopback",	"127.0.0.0");
	APPEND_NETWORK(	"private",	"10.6.28.0");
	APPEND_NETWORK(	"private2",	"172.16.90.0");
#ifndef HAVE_GETNETENT
	APPEND_NETWORK( "pgm-private",	"239.192.0.1");
	APPEND_NETWORK(	"ip6-private",	"2002:dce8:d28e::");
	APPEND_NETWORK( "ip6-pgm-private","ff08::1");
#endif

	APPEND_INTERFACE(	1,	"lo",	"up,loop");
	APPEND_INTERFACE(	2,	"eth0",	"up,broadcast,multicast");
	APPEND_INTERFACE(	3,	"eth1",	"down,broadcast,multicast");
	APPEND_INTERFACE(	1,	"lo",	"up,loop,ip=127.0.0.1,netmask=255.0.0.0");
	APPEND_INTERFACE(	2,	"eth0",	"up,broadcast,multicast,ip=10.6.28.33,netmask=255.255.255.0");
	APPEND_INTERFACE(	1,	"lo",	"up,loop,ip=::1,netmask=ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff,scope=0");
	APPEND_INTERFACE(	2,	"eth0",	"up,broadcast,multicast,ip=2002:dce8:d28e::33,netmask=ffff:ffff:ffff:ffff::0,scope=0");
	APPEND_INTERFACE(	2,	"eth0",	"up,broadcast,multicast,ip=fe80::214:5eff:febd:6dda,netmask=ffff:ffff:ffff:ffff::0,scope=2");
}

static
void
mock_teardown_net (void)
{
	GList* list;

/* rollback APPEND_HOST */
	list = mock_hosts;
	while (list) {
		struct mock_host_t* host = list->data;
		g_free (host->canonical_hostname);
		host->canonical_hostname = NULL;
		if (host->alias) {
			g_free (host->alias);
			host->alias = NULL;
		}
		g_slice_free1 (sizeof(struct mock_host_t), host);
		list->data = NULL;
		list = list->next;
	}
	g_list_free (mock_hosts);
	mock_hosts = NULL;

/* rollback APPEND_NETWORK */
	list = mock_networks;
	while (list) {
		struct mock_network_t* network = list->data;
		g_free (network->name);
		network->name = NULL;
		g_slice_free1 (sizeof(struct mock_network_t), network);
		list->data = NULL;
		list = list->next;
	}
	g_list_free (mock_networks);
	mock_networks = NULL;

/* rollback APPEND_INTERFACE */
	list = mock_interfaces;
	while (list) {
		struct mock_interface_t* interface_ = list->data;
		g_free (interface_->name);
		interface_->name = NULL;
		g_slice_free1 (sizeof(struct mock_interface_t), interface_);
		list->data = NULL;
		list = list->next;
	}
	g_list_free (mock_interfaces);
	mock_interfaces = NULL;

	mock_hostname = NULL;
}

/* mock functions for external references */

size_t
pgm_transport_pkt_offset2 (
        const bool                      can_fragment,
        const bool                      use_pgmcc
        )
{
        return 0;
}

PGM_GNUC_INTERNAL
int
pgm_get_nprocs (void)
{
	return 1;
}

bool
mock_pgm_getifaddrs (
	struct pgm_ifaddrs_t**	ifap,
	pgm_error_t**		err
	)
{
	if (NULL == ifap) {
		return -1;
	}

	g_debug ("mock_getifaddrs (ifap:%p err:%p)", (gpointer)ifap, (gpointer)err);

	GList* list = mock_interfaces;
	int n = g_list_length (list);
	struct pgm_ifaddrs_t* ifa = calloc (n, sizeof(struct pgm_ifaddrs_t));
	struct pgm_ifaddrs_t* ift = ifa;
	while (list) {
		struct mock_interface_t* interface_ = list->data;
		ift->ifa_addr = (gpointer)&interface_->addr;
		ift->ifa_name = interface_->name;
		ift->ifa_flags = interface_->flags;
		ift->ifa_netmask = (gpointer)&interface_->netmask;
		list = list->next;
		if (list) {
			ift->ifa_next = ift + 1;
			ift = ift->ifa_next;
		}
	}

	*ifap = ifa;
	return TRUE;
}

void
mock_pgm_freeifaddrs (
	struct pgm_ifaddrs_t*		ifa
	)
{
	free (ifa);
}

unsigned
mock_pgm_if_nametoindex (
	const sa_family_t	iffamily,
	const char*		ifname
	)
{
	GList* list = mock_interfaces;
	while (list) {
		const struct mock_interface_t* interface_ = list->data;
		if (0 == strcmp (ifname, interface_->name))
			return interface_->index;
		list = list->next;
	}
	return 0;
}

char*
mock_if_indextoname (
	unsigned		ifindex,
	char*			ifname
	)
{
	GList* list = mock_interfaces;
	while (list) {
		const struct mock_interface_t* interface_ = list->data;
		if (interface_->index == ifindex) {
			strcpy (ifname, interface_->name);
			return ifname;
		}
		list = list->next;
	}
	errno = ENXIO;
	return NULL;
}

int
mock_getnameinfo (
	const struct sockaddr*	sa,
	socklen_t		salen,
	char*			host,
	size_t			hostlen,
	char*			serv,
	size_t			servlen,
	int			flags
	)
{
	if ((0 == hostlen && 0 == servlen) ||
            (NULL == host && NULL == serv))
		return EAI_NONAME;

	if (flags & NI_NUMERICHOST && flags & NI_NAMEREQD)
		return EAI_BADFLAGS;

/* pre-conditions */
	g_assert (NULL != host);
	g_assert (hostlen > 0);
	g_assert (NULL == serv);
	g_assert (0 == servlen);

	const int sa_family = sa->sa_family;

	if (AF_INET == sa_family)
		g_assert (sizeof(struct sockaddr_in) == salen);
	else {
		g_assert (AF_INET6 == sa_family);
		g_assert (sizeof(struct sockaddr_in6) == salen);
	}

	if (!(flags & NI_NUMERICHOST))
	{
		GList* list = mock_hosts;
		while (list) {
			const struct mock_host_t* _host = list->data;
			const int host_family = ((struct sockaddr*)&_host->address)->sa_family;
			const size_t host_len = AF_INET == host_family ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

			if (host_family == sa_family &&
			    host_len == salen &&
			    0 == memcmp (sa, &_host->address, salen))
			{
				if (hostlen < (1 + strlen(_host->canonical_hostname)))
#ifdef EAI_OVERFLOW
					return EAI_OVERFLOW;
#else
					return EAI_MEMORY;
#endif
				strncpy (host, _host->canonical_hostname, hostlen);
				return 0;
			}
			list = list->next;
		}

		if (flags & NI_NAMEREQD)
			return EAI_NONAME;
	}

	if (AF_INET == sa_family) 
		pgm_inet_ntop (sa_family, &((const struct sockaddr_in*)sa)->sin_addr, host, hostlen);
	else {
		const unsigned scope = ((const struct sockaddr_in6*)sa)->sin6_scope_id;
		pgm_inet_ntop (sa_family, &((const struct sockaddr_in6*)sa)->sin6_addr, host, hostlen);
		if (scope) {
			char buffer[1+IF_NAMESIZE];
			strcat (host, "%");
			strcat (host, mock_if_indextoname (scope, buffer));
		}
	}
	return 0;
}

int
mock_getaddrinfo (
	const char*		node,
	const char*		service,
	const struct addrinfo*	hints,
	struct addrinfo**	res
	)
{
#ifdef AI_V4MAPPED
	const int ai_flags  = hints ? hints->ai_flags  : (AI_V4MAPPED | AI_ADDRCONFIG);
#else
	const int ai_flags  = hints ? hints->ai_flags  : (AI_ADDRCONFIG);
#endif
	const int ai_family = hints ? hints->ai_family : AF_UNSPEC;
	GList* list;
	struct sockaddr_storage addr;

	if (NULL == node && NULL == service)
		return EAI_NONAME;

/* pre-conditions */
	g_assert (NULL != node);
	g_assert (NULL == service);
	g_assert (!(ai_flags & AI_CANONNAME));
#ifdef AI_NUMERICSERV
	g_assert (!(ai_flags & AI_NUMERICSERV));
#endif
#ifdef AI_V4MAPPED
	g_assert (!(ai_flags & AI_V4MAPPED));
#endif

	g_message ("mock_getaddrinfo (node:%s%s%s service:%s%s%s hints:%p res:%p)",
		node ? "\"" : "", node ? node : "(null)", node ? "\"" : "",
		service ? "\"" : "", service ? service : "(null)", service ? "\"" : "",
		(gpointer)hints,
		(gpointer)res);

	gboolean has_ip4_config;
	gboolean has_ip6_config;

	if (hints && hints->ai_flags & AI_ADDRCONFIG)
	{
		has_ip4_config = has_ip6_config = FALSE;
		list = mock_interfaces;
		while (list) {
			const struct mock_interface_t* interface_ = list->data;
			if (AF_INET == ((struct sockaddr*)&interface_->addr)->sa_family)
				has_ip4_config = TRUE;
			else if (AF_INET6 == ((struct sockaddr*)&interface_->addr)->sa_family)
				has_ip6_config = TRUE;
			if (has_ip4_config && has_ip6_config)
				break;
			list = list->next;
		}
	} else {
		has_ip4_config = has_ip6_config = TRUE;
	}

	if (ai_flags & AI_NUMERICHOST) {
		pgm_sockaddr_pton (node, (struct sockaddr*)&addr);
	}
	list = mock_hosts;
	while (list) {
		struct mock_host_t* host = list->data;
		const int host_family = ((struct sockaddr*)&host->address)->sa_family;
		if (((strcmp (host->canonical_hostname, node) == 0) ||
		     (host->alias && strcmp (host->alias, node) == 0) || 
		     (ai_flags & AI_NUMERICHOST &&
		      0 == pgm_sockaddr_cmp ((struct sockaddr*)&addr, (struct sockaddr*)&host->address)))
		     &&
		    (host_family == ai_family || AF_UNSPEC == ai_family) &&
		    ((AF_INET == host_family && has_ip4_config) || (AF_INET6 == host_family && has_ip6_config)))
		{
			struct addrinfo* ai = malloc (sizeof(struct addrinfo));
			memset (ai, 0, sizeof(struct addrinfo));
			ai->ai_family = host_family;
			ai->ai_addrlen = AF_INET == host_family ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
			ai->ai_addr = (gpointer)&host->address;
			*res = ai;
			return 0;
		}
		list = list->next;
	}
	return EAI_NONAME;
}

void
mock_freeaddrinfo (
	struct addrinfo*	res
	)
{
	free (res);
}

int
mock_gethostname (
	char*			name,
	size_t			len
	)
{
	if (NULL == name) {
		errno = EFAULT;
		return -1;
	}
	if (len < 0) {
		errno = EINVAL;
		return -1;
	}
	if (len < (1 + strlen (mock_hostname))) {
		errno = ENAMETOOLONG;
		return -1;
	}
/* force an error */
	if (mock_hostname == mock_toolong) {
		errno = ENAMETOOLONG;
		return -1;
	}
	strncpy (name, mock_hostname, len);
	if (len > 0)
		name[len - 1] = '\0';
	return 0;
}

struct pgm_netent_t*
mock_pgm_getnetbyname (
	const char*		name
	)
{
	static struct pgm_netent_t ne;
	GList* list = mock_networks;

	if (NULL == name)
		return NULL;

	while (list) {
		const struct mock_network_t* network = list->data;
		if (strcmp (network->name, name) == 0) {
			ne.n_name	= network->name;
			ne.n_aliases	= network->aliases;
			if (AF_INET == network->number.ss_family) {
				struct sockaddr_in sa;
				memset (&sa, 0, sizeof (sa));
				sa.sin_family = network->number.ss_family;
				sa.sin_addr.s_addr = g_ntohl (((struct sockaddr_in*)&network->number)->sin_addr.s_addr);
				memcpy (&ne.n_net, &sa, sizeof (sa));
			} else {
				struct sockaddr_in6 sa6;
				memset (&sa6, 0, sizeof (sa6));
				sa6.sin6_family = network->number.ss_family;
				sa6.sin6_addr = ((struct sockaddr_in6*)&network->number)->sin6_addr;
				memcpy (&ne.n_net, &sa6, sizeof (sa6));
			}
			return &ne;
		}
		list = list->next;
	}
	return NULL;
}

PGM_GNUC_INTERNAL
bool
mock_pgm_if_getnodeaddr (
	const sa_family_t	family,
	struct sockaddr*	addr,
	const socklen_t		cnt,
	pgm_error_t**		error
	)
{
	switch (family) {
	case AF_UNSPEC:
	case AF_INET:
		((struct sockaddr*)addr)->sa_family = AF_INET;
		((struct sockaddr_in*)addr)->sin_addr.s_addr = inet_addr(MOCK_ADDRESS);
		break;
	case AF_INET6:
		((struct sockaddr*)addr)->sa_family = AF_INET6;
		((struct sockaddr_in6*)addr)->sin6_addr = mock_address6_addr;
		break;
	default:
		g_assert_not_reached();
	}
	return TRUE;
}

/* following tests will use AF_UNSPEC address family */

static
void
mock_setup_unspec (void)
{
	mock_family = AF_UNSPEC;
}

/* following tests will use AF_INET address family */

static
void
mock_setup_ip4 (void)
{
	mock_family = AF_INET;
}

/* following tests will use AF_INET6 address family */

static
void
mock_setup_ip6 (void)
{
	mock_family = AF_INET6;
}


/* return 0 if gsr multicast group does not match the default PGM group for
 * the address family, return -1 on no match.
 */

static
gboolean
match_default_group (
	const int			ai_family,
	const struct group_source_req*	gsr
	)
{
	const struct sockaddr_in sa_default = {
		.sin_family	 = AF_INET,
		.sin_addr.s_addr = g_htonl (MOCK_GROUP)
	};
	const struct sockaddr_in6 sa6_default = {
		.sin6_family	 = AF_INET6,
		.sin6_addr	 = MOCK_GROUP6_INIT
	};
	gboolean is_match = FALSE;

	switch (ai_family) {
	case AF_UNSPEC:
	case AF_INET:
		is_match = (0 == pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_group, (const struct sockaddr*)&sa_default));
		if (!is_match) {
			char addr1[INET6_ADDRSTRLEN], addr2[INET6_ADDRSTRLEN];
			pgm_sockaddr_ntop ((struct sockaddr*)&gsr->gsr_group, addr1, sizeof(addr1));
			pgm_sockaddr_ntop ((struct sockaddr*)&sa_default, addr2, sizeof(addr2));
			g_message ("FALSE == cmp(%s%s%s, default-group %s%s%s)",
				addr1 ? "\"" : "", addr1 ? addr1 : "(null)", addr1 ? "\"" : "",
				addr2 ? "\"" : "", addr2 ? addr2 : "(null)", addr2 ? "\"" : "");
		}
		break;
	case AF_INET6:
		is_match = (0 == pgm_sockaddr_cmp ((struct sockaddr*)&gsr->gsr_group, (const struct sockaddr*)&sa6_default));
		if (!is_match) {
			char addr1[INET6_ADDRSTRLEN], addr2[INET6_ADDRSTRLEN];
			pgm_sockaddr_ntop ((struct sockaddr*)&gsr->gsr_group, addr1, sizeof(addr1));
			pgm_sockaddr_ntop ((struct sockaddr*)&sa6_default, addr2, sizeof(addr2));
			g_message ("FALSE == cmp(%s%s%s, default-group %s%s%s)",
				addr1 ? "\"" : "", addr1 ? addr1 : "(null)", addr1 ? "\"" : "",
				addr2 ? "\"" : "", addr2 ? addr2 : "(null)", addr2 ? "\"" : "");
		}
	default:
		break;
	}
	return is_match;
}

/* return 0 if gsr source inteface does not match the INADDR_ANY reserved
 * address, return -1 on no match.
 */

static
int
match_default_source (
	const int			ai_family,
	const struct group_source_req*	gsr
	)
{
	if (0 != gsr->gsr_interface)
		return FALSE;

/* ASM: source == group */
	return (0 == pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_group, (const struct sockaddr*)&gsr->gsr_source));
}

/* return 0 if gsr source interface does not match the hosts default interface,
 * return -1 on mismatch
 */

static
int
match_default_interface (
	const int			ai_family,
	const struct group_source_req*	gsr
	)
{
	if (MOCK_INTERFACE_INDEX != gsr->gsr_interface)
		return FALSE;

/* ASM: source == group */
	return (0 == pgm_sockaddr_cmp ((const struct sockaddr*)&gsr->gsr_group, (const struct sockaddr*)&gsr->gsr_source));
}

/* target:
 *	bool
 *	pgm_getaddrinfo (
 *		const char*				s,
 *		const struct pgm_addrinfo_t* const	hints,
 *		struct pgm_addrinfo_t**			res,
 *		pgm_error_t**				err
 *	)
 */

struct test_case_t {
	const char* ip4;
	const char* ip6;
};

#define IP4_AND_IP6(x)	x, x

static const struct test_case_t cases_001[] = {
	{ 	IP4_AND_IP6("")	},
	{ 	IP4_AND_IP6(";")	},
	{ 	IP4_AND_IP6(";;")	},
	{ "239.192.0.1",			"ff08::1"				},
	{ "239.192.0.1",			"[ff08::1]"				},
	{ ";239.192.0.1",			";ff08::1"				},
	{ ";239.192.0.1",			";[ff08::1]"				},
	{ ";239.192.0.1;239.192.0.1",		";ff08::1;ff08::1"			},
	{ ";239.192.0.1;239.192.0.1",		";[ff08::1];[ff08::1]"			},
	{ "PGM.MCAST.NET",			"IP6-PGM.MCAST.NET"			},
	{ ";PGM.MCAST.NET",			";IP6-PGM.MCAST.NET"			},
	{ ";PGM.MCAST.NET;PGM.MCAST.NET",	";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ ";239.192.0.1;PGM.MCAST.NET",		";ff08::1;IP6-PGM.MCAST.NET"		},
	{ ";239.192.0.1;PGM.MCAST.NET",		";[ff08::1];IP6-PGM.MCAST.NET"		},
	{ ";PGM.MCAST.NET;239.192.0.1",		";IP6-PGM.MCAST.NET;ff08::1"		},
	{ ";PGM.MCAST.NET;239.192.0.1",		";IP6-PGM.MCAST.NET;[ff08::1]"		},
#ifndef HAVE_GETNETENT
	{ "pgm-private",			/* ‡ */ "ip6-pgm-private"			},
	{ ";pgm-private",			/* ‡ */ ";ip6-pgm-private"			},
	{ ";pgm-private;pgm-private",		/* ‡ */ ";ip6-pgm-private;ip6-pgm-private" 	},
	{ ";PGM.MCAST.NET;pgm-private",		/* ‡ */ ";IP6-PGM.MCAST.NET;ip6-pgm-private" 	},
	{ ";pgm-private;PGM.MCAST.NET",		/* ‡ */ ";ip6-pgm-private;IP6-PGM.MCAST.NET" 	},
	{ ";239.192.0.1;pgm-private",		/* ‡ */ ";ff08::1;ip6-pgm-private" 		},
	{ ";239.192.0.1;pgm-private",		/* ‡ */ ";[ff08::1];ip6-pgm-private" 		},
	{ ";pgm-private;239.192.0.1",		/* ‡ */ ";ip6-pgm-private;ff08::1" 		},
	{ ";pgm-private;239.192.0.1",		/* ‡ */ ";ip6-pgm-private;[ff08::1]" 		},
#endif
};

START_TEST (test_parse_transport_pass_001)
{
	fail_unless (mock_family == AF_UNSPEC || mock_family == AF_INET || mock_family == AF_INET6, "invalid mock address family");

	const char* s = (mock_family == AF_INET6) ? cases_001[_i].ip6 : cases_001[_i].ip4;
	struct pgm_addrinfo_t hints = {
		.ai_family	= mock_family
	}, *res = NULL;
	pgm_error_t* err = NULL;

	g_message ("%i: test_parse_transport_001(%s, %s%s%s)",
		   _i,
		   (mock_family == AF_INET6) ? "AF_INET6" : ( (mock_family == AF_INET) ? "AF_INET" : "AF_UNSPEC" ),
		   s ? "\"" : "", s ? s : "(null)", s ? "\"" : "");

/* ‡ Linux does not support IPv6 /etc/networks so IPv6 entries appear as 255.255.255.255 and
 *   pgm_if_parse_transport will fail.
 */
#ifdef HAVE_GETNETENT
	if (NULL != strstr (s, MOCK_NETWORK6) || NULL != strstr (s, MOCK_PGM_NETWORK6))
	{
		g_message ("IPv6 exception, /etc/networks not supported on this platform.");
		return;
	}
#endif

	gboolean retval = pgm_getaddrinfo (s, &hints, &res, &err);
	if (!retval) {
		g_message ("pgm_getaddrinfo: %s",
			(err && err->message) ? err->message : "(null)");
	}
	fail_unless (TRUE == retval, "pgm_getaddrinfo failed");
	fail_if     (NULL == res, "no result");
	fail_unless (NULL == err, "error raised");

	fail_unless (1 == res->ai_recv_addrs_len, "not exactly one receive address");
	fail_unless (match_default_group (mock_family, &res->ai_recv_addrs[0]), "receive address not match default group");
	fail_unless (match_default_source (mock_family, &res->ai_recv_addrs[0]), "receive address not match default source");
	fail_unless (1 == res->ai_send_addrs_len, "not exactly one send address");
	fail_unless (match_default_group (mock_family, &res->ai_send_addrs[0]), "send address not match default group");
	fail_unless (match_default_source (mock_family, &res->ai_send_addrs[0]), "send address not match default source");
	pgm_freeaddrinfo (res);
}
END_TEST

/* interface name
 *
 * pre-condition: interface defined to match running host
 * 		  ipv4 and ipv6 hostnames are different, otherwise "<hostname>" tests might go unexpected.
 */

static const struct test_case_t cases_002[] = {
	{ MOCK_INTERFACE,				/* † */ MOCK_INTERFACE		},
	{ MOCK_INTERFACE ";",				/* † */ MOCK_INTERFACE ";"		},
	{ MOCK_INTERFACE ";;",				/* † */ MOCK_INTERFACE ";;"	},
	{ MOCK_INTERFACE ";239.192.0.1",		/* † */ MOCK_INTERFACE ";ff08::1"			},
	{ MOCK_INTERFACE ";239.192.0.1",		/* † */ MOCK_INTERFACE ";[ff08::1]"			},
	{ MOCK_INTERFACE ";239.192.0.1;239.192.0.1",	/* † */ MOCK_INTERFACE ";ff08::1;ff08::1"		},
	{ MOCK_INTERFACE ";239.192.0.1;239.192.0.1",	/* † */ MOCK_INTERFACE ";[ff08::1];[ff08::1]"		},
	{ MOCK_INTERFACE ";PGM.MCAST.NET",		/* † */ MOCK_INTERFACE ";IP6-PGM.MCAST.NET"	},
	{ MOCK_INTERFACE ";PGM.MCAST.NET;PGM.MCAST.NET",/* † */ MOCK_INTERFACE ";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ MOCK_INTERFACE ";239.192.0.1;PGM.MCAST.NET",	/* † */ MOCK_INTERFACE ";ff08::1;IP6-PGM.MCAST.NET"	},
	{ MOCK_INTERFACE ";239.192.0.1;PGM.MCAST.NET",	/* † */ MOCK_INTERFACE ";[ff08::1];IP6-PGM.MCAST.NET"	},
	{ MOCK_INTERFACE ";PGM.MCAST.NET;239.192.0.1",	/* † */	MOCK_INTERFACE ";IP6-PGM.MCAST.NET;ff08::1"	},
	{ MOCK_INTERFACE ";PGM.MCAST.NET;239.192.0.1",	/* † */	MOCK_INTERFACE ";IP6-PGM.MCAST.NET;[ff08::1]"	},
#ifndef HAVE_GETNETENT
	{ MOCK_INTERFACE ";pgm-private",		/* ‡ */ MOCK_INTERFACE ";ip6-pgm-private" },
	{ MOCK_INTERFACE ";pgm-private;pgm-private",	/* ‡ */ MOCK_INTERFACE ";ip6-pgm-private;ip6-pgm-private" },
#endif
	{ MOCK_ADDRESS,					MOCK_ADDRESS6			},
	{ MOCK_ADDRESS,					"[" MOCK_ADDRESS6 "]"		},
	{ MOCK_ADDRESS ";",				MOCK_ADDRESS6 ";"		},
	{ MOCK_ADDRESS ";",				"[" MOCK_ADDRESS6 "];"		},
	{ MOCK_ADDRESS ";;",				MOCK_ADDRESS6 ";;"		},
	{ MOCK_ADDRESS ";;",				"[" MOCK_ADDRESS6 "];;"		},
	{ MOCK_ADDRESS ";239.192.0.1",			MOCK_ADDRESS6 ";ff08::1"			},
	{ MOCK_ADDRESS ";239.192.0.1",			"[" MOCK_ADDRESS6 "];[ff08::1]"			},
	{ MOCK_ADDRESS ";239.192.0.1;239.192.0.1",	MOCK_ADDRESS6 ";ff08::1;ff08::1"		},
	{ MOCK_ADDRESS ";239.192.0.1;239.192.0.1",	"[" MOCK_ADDRESS6 "];[ff08::1];[ff08::1]"		},
	{ MOCK_ADDRESS ";PGM.MCAST.NET",		MOCK_ADDRESS6 ";IP6-PGM.MCAST.NET"	},
	{ MOCK_ADDRESS ";PGM.MCAST.NET",		"[" MOCK_ADDRESS6 "];IP6-PGM.MCAST.NET"	},
	{ MOCK_ADDRESS ";PGM.MCAST.NET;PGM.MCAST.NET",	MOCK_ADDRESS6 ";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ MOCK_ADDRESS ";PGM.MCAST.NET;PGM.MCAST.NET",	"[" MOCK_ADDRESS6 "];IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ MOCK_ADDRESS ";239.192.0.1;PGM.MCAST.NET",	MOCK_ADDRESS6 ";ff08::1;IP6-PGM.MCAST.NET"	},
	{ MOCK_ADDRESS ";239.192.0.1;PGM.MCAST.NET",	"[" MOCK_ADDRESS6 "];[ff08::1];IP6-PGM.MCAST.NET"	},
	{ MOCK_ADDRESS ";PGM.MCAST.NET;239.192.0.1",	MOCK_ADDRESS6 ";IP6-PGM.MCAST.NET;ff08::1"	},
	{ MOCK_ADDRESS ";PGM.MCAST.NET;239.192.0.1",	"[" MOCK_ADDRESS6 "];IP6-PGM.MCAST.NET;[ff08::1]"	},
#ifndef HAVE_GETNETENT
	{ MOCK_ADDRESS ";pgm-private",			MOCK_ADDRESS6 ";ip6-pgm-private" },
	{ MOCK_ADDRESS ";pgm-private",			"[" MOCK_ADDRESS6 "];ip6-pgm-private" },
	{ MOCK_ADDRESS ";pgm-private;pgm-private",	MOCK_ADDRESS6 ";ip6-pgm-private;ip6-pgm-private" },
	{ MOCK_ADDRESS ";pgm-private;pgm-private",	"[" MOCK_ADDRESS6 "];ip6-pgm-private;ip6-pgm-private" },
	{ MOCK_NETWORK,					/* ‡ */ MOCK_NETWORK6			},
	{ MOCK_NETWORK ";",				/* ‡ */ MOCK_NETWORK6 ";"		},
	{ MOCK_NETWORK ";;",				/* ‡ */ MOCK_NETWORK6 ";;"		},
	{ MOCK_NETWORK ";239.192.0.1",			/* ‡ */ MOCK_NETWORK6 ";ff08::1"			},
	{ MOCK_NETWORK ";239.192.0.1",			/* ‡ */ MOCK_NETWORK6 ";[ff08::1]"			},
	{ MOCK_NETWORK ";239.192.0.1;239.192.0.1",	/* ‡ */ MOCK_NETWORK6 ";ff08::1;ff08::1"		},
	{ MOCK_NETWORK ";239.192.0.1;239.192.0.1",	/* ‡ */ MOCK_NETWORK6 ";[ff08::1];[ff08::1]"		},
	{ MOCK_NETWORK ";PGM.MCAST.NET",		/* ‡ */ MOCK_NETWORK6 ";IP6-PGM.MCAST.NET"	},
	{ MOCK_NETWORK ";PGM.MCAST.NET;PGM.MCAST.NET",	/* ‡ */ MOCK_NETWORK6 ";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ MOCK_NETWORK ";239.192.0.1;PGM.MCAST.NET",	/* ‡ */ MOCK_NETWORK6 ";ff08::1;IP6-PGM.MCAST.NET"	},
	{ MOCK_NETWORK ";239.192.0.1;PGM.MCAST.NET",	/* ‡ */ MOCK_NETWORK6 ";[ff08::1];IP6-PGM.MCAST.NET"	},
	{ MOCK_NETWORK ";PGM.MCAST.NET;239.192.0.1",	/* ‡ */ MOCK_NETWORK6 ";IP6-PGM.MCAST.NET;ff08::1"	},
	{ MOCK_NETWORK ";PGM.MCAST.NET;239.192.0.1",	/* ‡ */ MOCK_NETWORK6 ";IP6-PGM.MCAST.NET;[ff08::1]"	},
	{ MOCK_NETWORK ";pgm-private",			/* ‡ */ MOCK_NETWORK6 ";ip6-pgm-private" },
	{ MOCK_NETWORK ";pgm-private;pgm-private",	/* ‡ */ MOCK_NETWORK6 ";ip6-pgm-private;ip6-pgm-private" },
#endif
	{ MOCK_HOSTNAME,				MOCK_HOSTNAME6			},
	{ MOCK_HOSTNAME ";",				MOCK_HOSTNAME6 ";"		},
	{ MOCK_HOSTNAME ";;",				MOCK_HOSTNAME6 ";;"		},
	{ MOCK_HOSTNAME ";239.192.0.1",			MOCK_HOSTNAME6 ";ff08::1"		},
	{ MOCK_HOSTNAME ";239.192.0.1",			MOCK_HOSTNAME6 ";[ff08::1]"		},
	{ MOCK_HOSTNAME ";239.192.0.1;239.192.0.1",	MOCK_HOSTNAME6 ";ff08::1;ff08::1"	},
	{ MOCK_HOSTNAME ";239.192.0.1;239.192.0.1",	MOCK_HOSTNAME6 ";[ff08::1];[ff08::1]"	},
	{ MOCK_HOSTNAME ";PGM.MCAST.NET",		MOCK_HOSTNAME6 ";IP6-PGM.MCAST.NET" },
	{ MOCK_HOSTNAME ";PGM.MCAST.NET;PGM.MCAST.NET",	MOCK_HOSTNAME6 ";IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET" },
	{ MOCK_HOSTNAME ";239.192.0.1;PGM.MCAST.NET",	MOCK_HOSTNAME6 ";ff08::1;IP6-PGM.MCAST.NET" },
	{ MOCK_HOSTNAME ";239.192.0.1;PGM.MCAST.NET",	MOCK_HOSTNAME6 ";[ff08::1];IP6-PGM.MCAST.NET" },
	{ MOCK_HOSTNAME ";PGM.MCAST.NET;239.192.0.1",	MOCK_HOSTNAME6 ";IP6-PGM.MCAST.NET;ff08::1" },
	{ MOCK_HOSTNAME ";PGM.MCAST.NET;239.192.0.1",	MOCK_HOSTNAME6 ";IP6-PGM.MCAST.NET;[ff08::1]" },
#ifndef HAVE_GETNETENT
	{ MOCK_HOSTNAME ";pgm-private",			MOCK_HOSTNAME6 ";ip6-pgm-private" },
	{ MOCK_HOSTNAME ";pgm-private;pgm-private",	MOCK_HOSTNAME6 ";ip6-pgm-private;ip6-pgm-private" },
#endif
};

START_TEST (test_parse_transport_pass_002)
{
	fail_unless (mock_family == AF_UNSPEC || mock_family == AF_INET || mock_family == AF_INET6, "invalid mock address family");

	const char* s = (mock_family == AF_INET6) ? cases_002[_i].ip6 : cases_002[_i].ip4;
	struct pgm_addrinfo_t hints = {
		.ai_family	= mock_family
	}, *res = NULL;
	pgm_error_t* err = NULL;

	g_message ("%i: test_parse_transport_002(%s, %s%s%s)",
		   _i,
		   (mock_family == AF_INET6) ? "AF_INET6" : ( (mock_family == AF_INET) ? "AF_INET" : "AF_UNSPEC" ),
		   s ? "\"" : "", s ? s : "(null)", s ? "\"" : "");

/* ‡ Linux does not support IPv6 /etc/networks so IPv6 entries appear as 255.255.255.255 and
 *   pgm_if_parse_transport will fail.
 */
#ifdef HAVE_GETNETENT
	if (NULL != strstr (s, MOCK_NETWORK6) || NULL != strstr (s, MOCK_PGM_NETWORK6))
	{
		g_message ("IPv6 exception, /etc/networks not supported on this platform.");
		return;
	}
#endif

/* † Multiple scoped IPv6 interfaces match a simple interface name network parameter and so
 *   pgm-if_parse_transport will fail finding multiple matching interfaces
 */
	if (AF_INET6 == mock_family && 0 == strncmp (s, MOCK_INTERFACE, strlen (MOCK_INTERFACE)))
	{
		g_message ("IPv6 exception, multiple scoped addresses on one interface");
		fail_unless (FALSE == pgm_getaddrinfo (s, &hints, &res, &err), "pgm_getaddrinfo failed");
		fail_unless (NULL == res, "unexpected result");
		fail_if     (NULL == err, "error not raised");
		fail_unless (PGM_ERROR_NOTUNIQ == err->code, "interfaces not found unique");
		return;
	}

	fail_unless (TRUE == pgm_getaddrinfo (s, &hints, &res, &err), "pgm_getaddrinfo failed");
	fail_unless (1 == res->ai_recv_addrs_len, "not exactly one receive address");
	fail_unless (match_default_group     (mock_family, &res->ai_recv_addrs[0]), "receive address not match default group");
	fail_unless (match_default_interface (mock_family, &res->ai_recv_addrs[0]), "receive address not match default interface");
	fail_unless (1 == res->ai_send_addrs_len, "not exactly one send address");
	fail_unless (match_default_group     (mock_family, &res->ai_send_addrs[0]), "send address not match default group");
	fail_unless (match_default_interface (mock_family, &res->ai_send_addrs[0]), "send address not match default interface");
	pgm_freeaddrinfo (res);
}
END_TEST

/* network to node address in bits, 8-32
 *
 * e.g. 127.0.0.1/16
 */

static const struct test_case_t cases_003[] = {
	{ MOCK_ADDRESS "/24",				MOCK_ADDRESS6 "/64"				},
	{ MOCK_ADDRESS "/24;",				MOCK_ADDRESS6 "/64;"				},
	{ MOCK_ADDRESS "/24;;",				MOCK_ADDRESS6 "/64;;"				},
	{ MOCK_ADDRESS "/24;239.192.0.1",		MOCK_ADDRESS6 "/64;ff08::1"			},
	{ MOCK_ADDRESS "/24;239.192.0.1",		MOCK_ADDRESS6 "/64;[ff08::1]"			},
	{ MOCK_ADDRESS "/24;239.192.0.1;239.192.0.1",	MOCK_ADDRESS6 "/64;ff08::1;ff08::1"		},
	{ MOCK_ADDRESS "/24;239.192.0.1;239.192.0.1",	MOCK_ADDRESS6 "/64;[ff08::1];[ff08::1]"		},
	{ MOCK_ADDRESS "/24;PGM.MCAST.NET",		MOCK_ADDRESS6 "/64;IP6-PGM.MCAST.NET"		},
	{ MOCK_ADDRESS "/24;PGM.MCAST.NET;PGM.MCAST.NET",MOCK_ADDRESS6 "/64;IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
	{ MOCK_ADDRESS "/24;239.192.0.1;PGM.MCAST.NET",	MOCK_ADDRESS6 "/64;ff08::1;IP6-PGM.MCAST.NET"	},
	{ MOCK_ADDRESS "/24;239.192.0.1;PGM.MCAST.NET",	MOCK_ADDRESS6 "/64;[ff08::1];IP6-PGM.MCAST.NET"	},
	{ MOCK_ADDRESS "/24;PGM.MCAST.NET;239.192.0.1",	MOCK_ADDRESS6 "/64;IP6-PGM.MCAST.NET;ff08::1"	},
	{ MOCK_ADDRESS "/24;PGM.MCAST.NET;239.192.0.1",	MOCK_ADDRESS6 "/64;IP6-PGM.MCAST.NET;[ff08::1]"	},
	{ MOCK_ADDRESS "/24;PGM.MCAST.NET",		MOCK_ADDRESS6 "/64;IP6-PGM.MCAST.NET"		},
	{ MOCK_ADDRESS "/24;PGM.MCAST.NET;PGM.MCAST.NET",MOCK_ADDRESS6 "/64;IP6-PGM.MCAST.NET;IP6-PGM.MCAST.NET"	},
#ifndef HAVE_GETNETENT
	{ MOCK_ADDRESS "/24;pgm-private",		/* ‡ */ MOCK_ADDRESS6 "/64;ip6-pgm-private"			},
	{ MOCK_ADDRESS "/24;pgm-private;pgm-private",	/* ‡ */ MOCK_ADDRESS6 "/64;ip6-pgm-private;ip6-pgm-private"	},
	{ MOCK_ADDRESS "/24;239.192.0.1;pgm-private",	/* ‡ */ MOCK_ADDRESS6 "/64;ff08::1;ip6-pgm-private"		},
	{ MOCK_ADDRESS "/24;239.192.0.1;pgm-private",	/* ‡ */ MOCK_ADDRESS6 "/64;[ff08::1];ip6-pgm-private"		},
	{ MOCK_ADDRESS "/24;pgm-private;239.192.0.1",	/* ‡ */ MOCK_ADDRESS6 "/64;ip6-pgm-private;ff08::1"		},
	{ MOCK_ADDRESS "/24;pgm-private;239.192.0.1",	/* ‡ */ MOCK_ADDRESS6 "/64;ip6-pgm-private;[ff08::1]"		},
	{ MOCK_ADDRESS "/24;PGM.MCAST.NET;pgm-private",	/* ‡ */ MOCK_ADDRESS6 "/64;IP6-PGM.MCAST.NET;ip6-pgm-private"	},
	{ MOCK_ADDRESS "/24;pgm-private;PGM.MCAST.NET",	/* ‡ */ MOCK_ADDRESS6 "/64;ip6-pgm-private;IP6-PGM.MCAST.NET"	},
#endif
};

START_TEST (test_parse_transport_pass_003)
{
	fail_unless (mock_family == AF_UNSPEC || mock_family == AF_INET || mock_family == AF_INET6, "invalid mock address family");

	const char* s = (mock_family == AF_INET6) ? cases_003[_i].ip6 : cases_003[_i].ip4;
	struct pgm_addrinfo_t hints = {
		.ai_family	= mock_family
	}, *res = NULL;
	pgm_error_t* err = NULL;

	g_message ("%i: test_parse_transport_003(%s, %s%s%s)",
		   _i,
		   (mock_family == AF_INET6) ? "AF_INET6" : ( (mock_family == AF_INET) ? "AF_INET" : "AF_UNSPEC" ),
		   s ? "\"" : "", s ? s : "(null)", s ? "\"" : "");

/* ‡ Linux does not support IPv6 /etc/networks so IPv6 entries appear as 255.255.255.255 and
 *   pgm_if_parse_transport will fail.
 */
#ifdef HAVE_GETNETENT
	if (NULL != strstr (s, MOCK_NETWORK6) || NULL != strstr (s, MOCK_PGM_NETWORK6))
	{
		g_message ("IPv6 exception, /etc/networks not supported on this platform.");
		return;
	}
#endif

	gboolean retval = pgm_getaddrinfo (s, &hints, &res, &err);
	if (!retval) {
		g_message ("pgm_getaddrinfo: %s",
			(err && err->message) ? err->message : "(null)");
	}
	fail_unless (TRUE == retval, "pgm_getaddrinfo failed");
	fail_unless (1 == res->ai_recv_addrs_len, "not exactly one receive address");
	fail_unless (match_default_group     (mock_family, &res->ai_recv_addrs[0]), "receive address not match default group");
	fail_unless (match_default_interface (mock_family, &res->ai_recv_addrs[0]), "receive address not match default interface");
	fail_unless (1 == res->ai_send_addrs_len, "not exactly one send address");
	fail_unless (match_default_group     (mock_family, &res->ai_send_addrs[0]), "send address not match default group");
	fail_unless (match_default_interface (mock_family, &res->ai_send_addrs[0]), "send address not match default interface");
	pgm_freeaddrinfo (res);
}
END_TEST

/* asymmetric groups
 */

START_TEST (test_parse_transport_pass_004)
{
	fail_unless (mock_family == AF_UNSPEC || mock_family == AF_INET || mock_family == AF_INET6, "invalid mock address family");

	const char* s = (mock_family == AF_INET6) ? ";ff08::1;ff08::2"
				       /* AF_INET */: ";239.192.56.1;239.192.56.2";
	struct pgm_addrinfo_t hints = {
		.ai_family	= mock_family
	}, *res = NULL;
	pgm_error_t* err = NULL;
	struct sockaddr_storage addr;

	fail_unless (TRUE == pgm_getaddrinfo (s, &hints, &res, &err), "get_transport_info failed");
	fail_unless (1 == res->ai_recv_addrs_len, "not exactly one receive address");
	fail_unless (1 == res->ai_send_addrs_len, "not exactly one send address");
	if (mock_family == AF_INET6)
	{
		pgm_inet_pton (AF_INET6, "ff08::1", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr*)&addr)->sa_family = mock_family;
		((struct sockaddr_in6*)&addr)->sin6_port = 0;
		((struct sockaddr_in6*)&addr)->sin6_flowinfo = 0;
		((struct sockaddr_in6*)&addr)->sin6_scope_id = 0;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_recv_addrs[0].gsr_group, (struct sockaddr*)&addr), "group not match");
		pgm_inet_pton (AF_INET6, "ff08::2", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr*)&addr)->sa_family = mock_family;
		((struct sockaddr_in6*)&addr)->sin6_port = 0;
		((struct sockaddr_in6*)&addr)->sin6_flowinfo = 0;
		((struct sockaddr_in6*)&addr)->sin6_scope_id = 0;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_send_addrs[0].gsr_group, (struct sockaddr*)&addr), "group not match");
	} else {
		pgm_inet_pton (AF_INET, "239.192.56.1", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr*)&addr)->sa_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_recv_addrs[0].gsr_group, (struct sockaddr*)&addr), "group not match");
		pgm_inet_pton (AF_INET, "239.192.56.2", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr*)&addr)->sa_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_send_addrs[0].gsr_group, (struct sockaddr*)&addr), "group not match");
	}
	fail_unless (match_default_source (mock_family, &res->ai_recv_addrs[0]), "source not match");
	fail_unless (match_default_source (mock_family, &res->ai_send_addrs[0]), "source not match");
	pgm_freeaddrinfo (res);
}
END_TEST

/* multiple receive groups and asymmetric sending
 */

START_TEST (test_parse_transport_pass_005)
{
	fail_unless (mock_family == AF_UNSPEC || mock_family == AF_INET || mock_family == AF_INET6, "invalid mock address family");

	const char* s = (mock_family == AF_INET6) ? ";ff08::1,ff08::2;ff08::3"
				       /* AF_INET */: ";239.192.56.1,239.192.56.2;239.192.56.3";
	struct pgm_addrinfo_t hints = {
		.ai_family	= mock_family
	}, *res = NULL;
	pgm_error_t* err = NULL;
	struct sockaddr_storage addr;

	fail_unless (TRUE == pgm_getaddrinfo (s, &hints, &res, &err), "pgm_getaddrinfo failed");
	fail_unless (2 == res->ai_recv_addrs_len, "not exactly one receive address");
	fail_unless (1 == res->ai_send_addrs_len, "not exactly one send address");
	if (mock_family == AF_INET6)
	{
		pgm_inet_pton (AF_INET6, "ff08::1", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr*)&addr)->sa_family = mock_family;
		((struct sockaddr_in6*)&addr)->sin6_port = 0;
		((struct sockaddr_in6*)&addr)->sin6_flowinfo = 0;
		((struct sockaddr_in6*)&addr)->sin6_scope_id = 0;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_recv_addrs[0].gsr_group, (struct sockaddr*)&addr), "group not match");
		pgm_inet_pton (AF_INET6, "ff08::2", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr*)&addr)->sa_family = mock_family;
		((struct sockaddr_in6*)&addr)->sin6_port = 0;
		((struct sockaddr_in6*)&addr)->sin6_flowinfo = 0;
		((struct sockaddr_in6*)&addr)->sin6_scope_id = 0;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_recv_addrs[1].gsr_group, (struct sockaddr*)&addr), "group not match");
		pgm_inet_pton (AF_INET6, "ff08::3", &((struct sockaddr_in6*)&addr)->sin6_addr);
		((struct sockaddr*)&addr)->sa_family = mock_family;
		((struct sockaddr_in6*)&addr)->sin6_port = 0;
		((struct sockaddr_in6*)&addr)->sin6_flowinfo = 0;
		((struct sockaddr_in6*)&addr)->sin6_scope_id = 0;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_send_addrs[0].gsr_group, (struct sockaddr*)&addr), "group not match");
	} else {
		pgm_inet_pton (AF_INET, "239.192.56.1", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr*)&addr)->sa_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_recv_addrs[0].gsr_group, (struct sockaddr*)&addr), "group not match");
		pgm_inet_pton (AF_INET, "239.192.56.2", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr*)&addr)->sa_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_recv_addrs[1].gsr_group, (struct sockaddr*)&addr), "group not match");
		pgm_inet_pton (AF_INET, "239.192.56.3", &((struct sockaddr_in*)&addr)->sin_addr);
		((struct sockaddr*)&addr)->sa_family = AF_INET;
		fail_unless (0 == pgm_sockaddr_cmp ((struct sockaddr*)&res->ai_send_addrs[0].gsr_group, (struct sockaddr*)&addr), "group not match");
	}
	fail_unless (match_default_source (mock_family, &res->ai_recv_addrs[0]), "source not match");
	fail_unless (match_default_source (mock_family, &res->ai_send_addrs[0]), "source not match");
	pgm_freeaddrinfo (res);
}
END_TEST


/* too many interfaces
 */
START_TEST (test_parse_transport_fail_001)
{
	const char* s = "eth0,lo;;;";
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_UNSPEC
	}, *res = NULL;
	pgm_error_t* err = NULL;

	fail_unless (FALSE == pgm_getaddrinfo (s, &hints, &res, &err), "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* invalid characters, or simply just bogus
 */
START_TEST (test_parse_transport_fail_002)
{
        const char* s = "!@#$%^&*()";
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_UNSPEC
	}, *res = NULL;
	pgm_error_t* err = NULL;

	fail_unless (FALSE == pgm_getaddrinfo (s, &hints, &res, &err), "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* too many groups
 */
START_TEST (test_parse_transport_fail_003)
{
        const char* s = ";239.192.0.1,239.192.0.2,239.192.0.3,239.192.0.4,239.192.0.5,239.192.0.6,239.192.0.7,239.192.0.8,239.192.0.9,239.192.0.10,239.192.0.11,239.192.0.12,239.192.0.13,239.192.0.14,239.192.0.15,239.192.0.16,239.192.0.17,239.192.0.18,239.192.0.19,239.192.0.20;239.192.0.21";
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_UNSPEC
	}, *res = NULL;
	pgm_error_t* err = NULL;

	fail_unless (FALSE == pgm_getaddrinfo (s, &hints, &res, &err), "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* too many receiver groups in asymmetric pairing
 */
START_TEST (test_parse_transport_fail_004)
{
        const char* s = ";239.192.0.1,239.192.0.2,239.192.0.3,239.192.0.4,239.192.0.5,239.192.0.6,239.192.0.7,239.192.0.8,239.192.0.9,239.192.0.10,239.192.0.11,239.192.0.12,239.192.0.13,239.192.0.14,239.192.0.15,239.192.0.16,239.192.0.17,239.192.0.18,239.192.0.19,239.192.0.20,239.192.0.21;239.192.0.22";
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_UNSPEC
	}, *res = NULL;
	pgm_error_t* err = NULL;

	fail_unless (FALSE == pgm_getaddrinfo (s, &hints, &res, &err), "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* null string
 */
START_TEST (test_parse_transport_fail_005)
{
        const char* s = NULL;
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_UNSPEC
	}, *res = NULL;
	pgm_error_t* err = NULL;

	fail_unless (FALSE == pgm_getaddrinfo (s, &hints, &res, &err), "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* invalid address family
 */
START_TEST (test_parse_transport_fail_006)
{
        const char* s = ";";
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_SNA
	}, *res = NULL;
	pgm_error_t* err = NULL;

	fail_unless (FALSE == pgm_getaddrinfo (s, &hints, &res, &err), "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* invalid transport info pointer
 */
START_TEST (test_parse_transport_fail_007)
{
        const char* s = ";";
	pgm_error_t* err = NULL;

	fail_unless (FALSE == pgm_getaddrinfo (s, NULL, NULL, &err), "pgm_getaddrinfo failed");
}
END_TEST

/* invalid interface
 */
START_TEST (test_parse_transport_fail_008)
{
	const char* s = "qe0;";
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_UNSPEC
	}, *res = NULL;
	pgm_error_t* err = NULL;

	gboolean retval = pgm_getaddrinfo (s, &hints, &res, &err);
	if (!retval) {
		g_message ("pgm_getaddrinfo: %s", err ? err->message : "(null)");
	}
	fail_unless (FALSE == retval, "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* non-existing interface IP address
 */
START_TEST (test_parse_transport_fail_009)
{
	const char* s = "172.16.90.1;";
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_UNSPEC
	}, *res = NULL;
	pgm_error_t* err = NULL;

	gboolean retval = pgm_getaddrinfo (s, &hints, &res, &err);
	if (!retval) {
		g_message ("pgm_getaddrinfo: %s",
			(err && err->message) ? err->message : "(null)");
	}
	fail_unless (FALSE == retval, "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* non-existing network name address
 */
START_TEST (test_parse_transport_fail_010)
{
	const char* s = "private2;";
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_UNSPEC
	}, *res = NULL;
	pgm_error_t* err = NULL;

	gboolean retval = pgm_getaddrinfo (s, &hints, &res, &err);
	if (!retval) {
		g_message ("pgm_getaddrinfo: %s",
			(err && err->message) ? err->message : "(null)");
	}
	fail_unless (FALSE == retval, "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* non-existing host name interface
 */
START_TEST (test_parse_transport_fail_011)
{
	const char* s = "mi-hee.ko.miru.hk;";
	struct pgm_addrinfo_t hints = {
		.ai_family	= AF_UNSPEC
	}, *res = NULL;
	pgm_error_t* err = NULL;

	gboolean retval = pgm_getaddrinfo (s, &hints, &res, &err);
	if (!retval) {
		g_message ("pgm_getaddrinfo: %s",
			(err && err->message) ? err->message : "(null)");
	}
	fail_unless (FALSE == retval, "pgm_getaddrinfo failed");
	fail_unless (NULL == res, "unexpected result");
}
END_TEST

/* target:
 *	pgm_if_print_all (void)
 */

START_TEST (test_print_all_pass_001)
{
	pgm_if_print_all ();
}
END_TEST


/* target:
 * 	bool
 * 	is_in_net (
 * 		const struct in_addr*	addr,		-- in host byte order
 * 		const struct in_addr*	netaddr,
 * 		const struct in_addr*	netmask
 * 		)
 */

struct test_case_net_t {
        const char* addr;
        const char* netaddr;
        const char* netmask;
	const gboolean answer;
};

static const struct test_case_net_t cases_004[] = {
	{ "127.0.0.1",		"127.0.0.1",	"255.0.0.0",		TRUE		},
	{ "127.0.0.1",		"127.0.0.1",	"255.255.0.0",		TRUE		},
	{ "127.0.0.1",		"127.0.0.1",	"255.255.255.0",	TRUE		},
	{ "127.0.0.1",		"127.0.0.1",	"255.255.255.255",	TRUE		},
	{ "127.0.0.1",		"127.0.0.0",	"255.0.0.0",		TRUE		},
	{ "127.0.0.1",		"127.0.0.0",	"255.255.0.0",		TRUE		},
	{ "127.0.0.1",		"127.0.0.0",	"255.255.255.0",	TRUE		},
	{ "127.0.0.1",		"127.0.0.0",	"255.255.255.255",	FALSE		},
	{ "172.15.1.1",		"172.16.0.0",	"255.240.0.0",		FALSE		},
	{ "172.16.1.1",		"172.16.0.0",	"255.240.0.0",		TRUE		},
	{ "172.18.1.1",		"172.16.0.0",	"255.240.0.0",		TRUE		},
	{ "172.31.1.1",		"172.16.0.0",	"255.240.0.0",		TRUE		},
	{ "172.32.1.1",		"172.16.0.0",	"255.240.0.0",		FALSE		},
};

START_TEST (test_is_in_net_pass_001)
{
	struct in_addr addr, netaddr, netmask;
	fail_unless (pgm_inet_pton (AF_INET, cases_004[_i].addr,    &addr), "inet_pton failed");
	fail_unless (pgm_inet_pton (AF_INET, cases_004[_i].netaddr, &netaddr), "inet_pton failed");
	fail_unless (pgm_inet_pton (AF_INET, cases_004[_i].netmask, &netmask), "inet_pton failed");
	const gboolean answer =		     cases_004[_i].answer;

	addr.s_addr    = g_ntohl (addr.s_addr);
	netaddr.s_addr = g_ntohl (netaddr.s_addr);
	netmask.s_addr = g_ntohl (netmask.s_addr);
	gboolean result = is_in_net (&addr, &netaddr, &netmask);

	g_message ("result %s (%s)",
		result ? "TRUE" : "FALSE",
		answer ? "TRUE" : "FALSE");

	fail_unless (answer == result, "unexpected result");
}
END_TEST

static const struct test_case_net_t cases_005[] = {
	{ "::1",			"::1",			"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",		TRUE		},
	{ "fe80::203:baff:fe4e:6cc8",	"fe80::",		"ffff:0000:0000:0000:0000:0000:0000:0000",		TRUE		},
	{ "2002:dec8:d28e::36",		"2002:dec8:d28e::",	"ffff:ffff:ffff:0000:0000:0000:0000:0000",		TRUE		},
	{ "2002:dec8:d28e::36",		"2002:dafa:939:0::",	"ffff:ffff:ffff:ffff:0000:0000:0000:0000",		FALSE		},
};

START_TEST (test_is_in_net6_pass_001)
{
	struct in6_addr addr, netaddr, netmask;
	fail_unless (pgm_inet_pton (AF_INET6, cases_005[_i].addr,    &addr), "inet_pton failed");
	fail_unless (pgm_inet_pton (AF_INET6, cases_005[_i].netaddr, &netaddr), "inet_pton failed");
	fail_unless (pgm_inet_pton (AF_INET6, cases_005[_i].netmask, &netmask), "inet_pton failed");
	const gboolean answer =		      cases_005[_i].answer;

	gboolean result = is_in_net6 (&addr, &netaddr, &netmask);

	g_message ("result %s (%s)",
		result ? "TRUE" : "FALSE",
		answer ? "TRUE" : "FALSE");

	fail_unless (answer == result, "unexpected result");
}
END_TEST



static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_is_in_net = tcase_create ("is_in_net");
	suite_add_tcase (s, tc_is_in_net);
	tcase_add_checked_fixture (tc_is_in_net, mock_setup_net, mock_teardown_net);
	tcase_add_checked_fixture (tc_is_in_net, mock_setup_unspec, NULL);
	tcase_add_loop_test (tc_is_in_net, test_is_in_net_pass_001, 0, G_N_ELEMENTS(cases_004));

	TCase* tc_is_in_net6 = tcase_create ("is_in_net6");
	suite_add_tcase (s, tc_is_in_net6);
	tcase_add_checked_fixture (tc_is_in_net6, mock_setup_net, mock_teardown_net);
	tcase_add_checked_fixture (tc_is_in_net6, mock_setup_unspec, NULL);
	tcase_add_loop_test (tc_is_in_net6, test_is_in_net6_pass_001, 0, G_N_ELEMENTS(cases_005));

	TCase* tc_print_all = tcase_create ("print-all");
	tcase_add_checked_fixture (tc_print_all, mock_setup_net, mock_teardown_net);
	suite_add_tcase (s, tc_print_all);
	tcase_add_test (tc_print_all, test_print_all_pass_001);

	return s;
}

/* three variations of all parse-transport tests, one for each valid
 * address family value: AF_UNSPEC, AF_INET, AF_INET6. 
 */

static
Suite*
make_unspec_suite (void)
{
	Suite* s;

	s = suite_create ("AF_UNSPEC");

/* unspecified address family, ai_family == AF_UNSPEC */
	TCase* tc_parse_transport_unspec = tcase_create ("parse_transport/unspec");
	suite_add_tcase (s, tc_parse_transport_unspec);
	tcase_add_checked_fixture (tc_parse_transport_unspec, mock_setup_net, mock_teardown_net);
	tcase_add_checked_fixture (tc_parse_transport_unspec, mock_setup_unspec, NULL);
	tcase_add_loop_test (tc_parse_transport_unspec, test_parse_transport_pass_001, 0, G_N_ELEMENTS(cases_001));
	tcase_add_loop_test (tc_parse_transport_unspec, test_parse_transport_pass_002, 0, G_N_ELEMENTS(cases_002));
	tcase_add_loop_test (tc_parse_transport_unspec, test_parse_transport_pass_003, 0, G_N_ELEMENTS(cases_003));
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_pass_004);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_pass_005);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_001);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_002);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_003);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_004);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_005);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_006);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_007);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_008);
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_009);
#ifndef _WIN32
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_010);
#endif
	tcase_add_test (tc_parse_transport_unspec, test_parse_transport_fail_011);

	return s;
}

static
Suite*
make_af_inet_suite (void)
{
	Suite* s;

	s = suite_create ("AF_INET");

/* IP version 4, ai_family = AF_INET */
	TCase* tc_parse_transport_ip4 = tcase_create ("parse_transport/af_inet");
	suite_add_tcase (s, tc_parse_transport_ip4);
	tcase_add_checked_fixture (tc_parse_transport_ip4, mock_setup_net, mock_teardown_net);
	tcase_add_checked_fixture (tc_parse_transport_ip4, mock_setup_ip4, NULL);
	tcase_add_loop_test (tc_parse_transport_ip4, test_parse_transport_pass_001, 0, G_N_ELEMENTS(cases_001));
	tcase_add_loop_test (tc_parse_transport_ip4, test_parse_transport_pass_002, 0, G_N_ELEMENTS(cases_002));
	tcase_add_loop_test (tc_parse_transport_ip4, test_parse_transport_pass_003, 0, G_N_ELEMENTS(cases_003));
	tcase_add_test (tc_parse_transport_ip4, test_parse_transport_pass_004);
	tcase_add_test (tc_parse_transport_ip4, test_parse_transport_pass_005);

	return s;
}

static
Suite*
make_af_inet6_suite (void)
{
	Suite* s;

	s = suite_create ("AF_INET6");

/* IP version 6, ai_family = AF_INET6 */
	TCase* tc_parse_transport_ip6 = tcase_create ("parse_transport/af_inet6");
	suite_add_tcase (s, tc_parse_transport_ip6);
	tcase_add_checked_fixture (tc_parse_transport_ip6, mock_setup_net, mock_teardown_net);
	tcase_add_checked_fixture (tc_parse_transport_ip6, mock_setup_ip6, NULL);
	tcase_add_loop_test (tc_parse_transport_ip6, test_parse_transport_pass_001, 0, G_N_ELEMENTS(cases_001));
	tcase_add_loop_test (tc_parse_transport_ip6, test_parse_transport_pass_002, 0, G_N_ELEMENTS(cases_002));
	tcase_add_loop_test (tc_parse_transport_ip6, test_parse_transport_pass_003, 0, G_N_ELEMENTS(cases_003));
	tcase_add_test (tc_parse_transport_ip6, test_parse_transport_pass_004);
	tcase_add_test (tc_parse_transport_ip6, test_parse_transport_pass_005);

	TCase* tc_print_all = tcase_create ("print-all");
	tcase_add_checked_fixture (tc_print_all, mock_setup_net, mock_teardown_net);
	suite_add_tcase (s, tc_print_all);
	tcase_add_test (tc_print_all, test_print_all_pass_001);

	return s;
}

static
Suite*
make_master_suite (void)
{
	Suite* s = suite_create ("Master");
	return s;
}

int
main (void)
{
#ifdef _WIN32
	WORD wVersionRequested = MAKEWORD (2, 2);
	WSADATA wsaData;
	g_assert (0 == WSAStartup (wVersionRequested, &wsaData));
	g_assert (LOBYTE (wsaData.wVersion) == 2 && HIBYTE (wsaData.wVersion) == 2);
#endif
	pgm_messages_init();
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_add_suite (sr, make_unspec_suite ());
	srunner_add_suite (sr, make_af_inet_suite ());
	srunner_add_suite (sr, make_af_inet6_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	pgm_messages_shutdown();
#ifdef _WIN32
	WSACleanup();
#endif
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
