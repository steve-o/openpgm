/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for network interface declaration parsing.
 *
 * Copyright (c) 2009 Miru Limited.
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
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <glib.h>
#include <check.h>

#include "pgm/ip.h"
#include "pgm/sockaddr.h"
#include "pgm/getifaddrs.h"


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
static char* mock_kiku = "kiku";
static char* mock_localhost = "localhost";
static char* mock_invalid = "invalid.invalid";		/* RFC 2606 */
static char* mock_toolong = "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij12345"; /* 65 */
static char* mock_hostname = NULL;



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
	g_assert (pgm_sockaddr_pton (address, &new_host->address));
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
	g_assert (pgm_sockaddr_pton (number, &new_network->number));

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
			g_assert (pgm_sockaddr_pton (addr, &new_interface->addr));
		}
		else if (strncmp (tokens[i], "netmask=", strlen("netmask=")) == 0) {
			const char* addr = tokens[i] + strlen("netmask=");
			g_assert (pgm_sockaddr_pton (addr, &new_interface->netmask));
		}
		else if (strncmp (tokens[i], "scope=", strlen("scope=")) == 0) {
			const char* scope = tokens[i] + strlen("scope=");
			g_assert (AF_INET6 == ((struct sockaddr*)&new_interface->addr)->sa_family);
			((struct sockaddr_in6*)&new_interface->addr)->sin6_scope_id = atoi (scope);
		}
		else
			g_error ("parsing failed for flag \"%s\"", tokens[i]);
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
mock_setup (void)
{
	mock_hostname = mock_kiku;

	APPEND_HOST (	"127.0.0.1",		"localhost");
	APPEND_HOST2(	"10.6.28.33",		"kiki.hk.miru.hk",	"kiku");
	APPEND_HOST2(	"2002:dce8:d28e::33",	"ip6-kiku",		"kiku");
	APPEND_HOST2(	"::1",			"ip6-localhost",	"ip6-loopback");
	APPEND_HOST (	"239.192.0.1",		"PGM.MCAST.NET");
	APPEND_HOST (	"ff08::1",		"IP6-PGM.MCAST.NET");

	APPEND_NETWORK(	"loopback",	"127.0.0.0");
	APPEND_NETWORK(	"private",	"10.6.28.0");
#ifdef CONFIG_HAVE_IP6_NETWORKS
	APPEND_NETWORK(	"ip6-private",	"2002:dce8:d28e:0:0:0");
#endif

	APPEND_INTERFACE(	1,	"lo",	"up,loop");
	APPEND_INTERFACE(	2,	"eth0",	"up,broadcast,multicast");
	APPEND_INTERFACE(	3,	"eth1",	"down,broadcast,multicast");
	APPEND_INTERFACE(	1,	"lo",	"up,loop,ip=127.0.0.1,netmask=255.0.0.0");
	APPEND_INTERFACE(	2,	"eth0",	"up,broadcast,multicast,ip=10.6.28.31,netmask=255.255.255.0");
	APPEND_INTERFACE(	1,	"lo",	"up,loop,ip=::1,netmask=ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff,scope=0");
	APPEND_INTERFACE(	2,	"eth0",	"up,broadcast,multicast,ip=2002:dce8:d28e::31,netmask=ffff:ffff:ffff:ffff::0,scope=0");
	APPEND_INTERFACE(	2,	"eth0",	"up,broadcast,multicast,ip=fe80::214:5eff:febd:6dda,netmask=ffff:ffff:ffff:ffff::0,scope=2");
}

static
void
mock_teardown (void)
{
	GList* list;

	list = mock_hosts;
	while (list) {
		struct mock_host_t* host = list->data;
		g_free (host->canonical_hostname);
		if (host->alias)
			g_free (host->alias);
		g_slice_free1 (sizeof(struct mock_host_t), host);
		list = list->next;
	}
	g_list_free (mock_hosts);

	list = mock_networks;
	while (list) {
		struct mock_network_t* network = list->data;
		g_free (network->name);
		g_slice_free1 (sizeof(struct mock_network_t), network);
		list = list->next;
	}
	g_list_free (mock_networks);

	list = mock_interfaces;
	while (list) {
		struct mock_interface_t* interface = list->data;
		g_free (interface->name);
		g_slice_free1 (sizeof(struct mock_interface_t), interface);
		list = list->next;
	}
	g_list_free (mock_interfaces);
}


/* mock functions for external references */

static 
int
mock_getifaddrs (
	struct ifaddrs**	ifap
	)
{
	if (NULL == ifap) {
		errno = EINVAL;
		return -1;
	}

	GList* list = mock_interfaces;
	int n = g_list_length (list);
	struct ifaddrs* ifa = malloc (n * sizeof(struct ifaddrs));
	memset (ifa, 0, n * sizeof(struct ifaddrs));
	struct ifaddrs* ift = ifa;
	while (list) {
		struct mock_interface_t* interface = list->data;
		ift->ifa_addr = (gpointer)&interface->addr;
		ift->ifa_name = interface->name;
		ift->ifa_flags = interface->flags;
		ift->ifa_netmask = (gpointer)&interface->netmask;
		list = list->next;
		if (list) {
			ift->ifa_next = ift + 1;
			ift = ift->ifa_next;
		}
	}

	*ifap = ifa;

	return 0;
}

static
void
mock_freeifaddrs (
	struct ifaddrs*		ifa
	)
{
	free (ifa);
}

static
unsigned int
mock_if_nametoindex (
	const char*		ifname
	)
{
	GList* list = mock_interfaces;
	while (list) {
		const struct mock_interface_t* interface = list->data;
		if (0 == strcmp (ifname, interface->name))
			return interface->index;
		list = list->next;
	}
	return 0;
}

static
char*
mock_if_indextoname (
	unsigned int		ifindex,
	char*			ifname
	)
{
	GList* list = mock_interfaces;
	while (list) {
		const struct mock_interface_t* interface = list->data;
		if (interface->index == ifindex) {
			strcpy (ifname, interface->name);
			return ifname;
		}
		list = list->next;
	}
	errno = ENXIO;
	return NULL;
}

static
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
					return EAI_OVERFLOW;
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
			strcat (host, ":");
			strcat (host, mock_if_indextoname (scope, buffer));
		}
	}
	return 0;
}

static
int
mock_getaddrinfo (
	const char*		node,
	const char*		service,
	const struct addrinfo*	hints,
	struct addrinfo**	res
	)
{
	const int ai_flags  = hints ? hints->ai_flags  : (AI_V4MAPPED | AI_ADDRCONFIG);
	const int ai_family = hints ? hints->ai_family : AF_UNSPEC;
	GList* list;

	if (NULL == node && NULL == service)
		return EAI_NONAME;

/* pre-conditions */
	g_assert (NULL != node);
	g_assert (NULL == service);
	g_assert (!(ai_flags & AI_CANONNAME));
	g_assert (!(ai_flags & AI_NUMERICHOST));
	g_assert (!(ai_flags & AI_NUMERICSERV));
	g_assert (!(ai_flags & AI_V4MAPPED));

	gboolean has_ip4_config;
	gboolean has_ip6_config;

	if (hints && hints->ai_flags & AI_ADDRCONFIG)
	{
		has_ip4_config = has_ip6_config = FALSE;
		list = mock_interfaces;
		while (list) {
			const struct mock_interface_t* interface = list->data;
			if (AF_INET == ((struct sockaddr*)&interface->addr)->sa_family)
				has_ip4_config = TRUE;
			else if (AF_INET6 == ((struct sockaddr*)&interface->addr)->sa_family)
				has_ip6_config = TRUE;
			if (has_ip4_config && has_ip6_config)
				break;
			list = list->next;
		}
	} else {
		has_ip4_config = has_ip6_config = TRUE;
	}

	list = mock_hosts;
	while (list) {
		struct mock_host_t* host = list->data;
		const int host_family = ((struct sockaddr*)&host->address)->sa_family;
		if (strcmp (host->canonical_hostname, node) == 0 &&
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

static
void
mock_freeaddrinfo (
	struct addrinfo*	res
	)
{
	free (res);
}

static
int
mock_gethostname (
	char*			name,
	size_t			len
	)
{
	if (NULL == name)
		return EFAULT;
	if (len < 0)
		return EINVAL;
	if (len < (1 + strlen (mock_hostname)))
		return ENAMETOOLONG;
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

static
struct netent*
mock_getnetbyname (
	const char*		name
	)
{
	static struct netent ne;
	GList* list = mock_networks;

	if (NULL == name)
		return NULL;

	while (list) {
		const struct mock_network_t* network = list->data;
		if (strcmp (network->name, name) == 0) {
			ne.n_name	= network->name;
			ne.n_aliases	= network->aliases;
			ne.n_addrtype	= AF_INET;
			ne.n_net	= ((struct sockaddr_in*)&network->number)->sin_addr.s_addr;
			return &ne;
		}
		list = list->next;
	}
	return NULL;
}

#define getifaddrs	mock_getifaddrs
#define freeifaddrs	mock_freeifaddrs
#define if_nametoindex	mock_if_nametoindex
#define if_indextoname	mock_if_indextoname
#define getnameinfo	mock_getnameinfo
#define getaddrinfo	mock_getaddrinfo
#define freeaddrinfo	mock_freeaddrinfo
#define gethostname	mock_gethostname
#define getnetbyname	mock_getnetbyname

#define IF_DEBUG
#include "if.c"


/* target:
 *	int
 *	pgm_if_parse_transport (
 *		const char*			s,
 *		int				ai_family,
 *		struct group_source_req*	recv_gsr,
 *		gsize*				recv_len,
 *		struct group_source_req*	send_gsr
 *	)
 */

START_TEST (test_parse_transport_pass_001)
{
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


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_parse_transport = tcase_create ("parse-transport");
	tcase_add_checked_fixture (tc_parse_transport, mock_setup, mock_teardown);
	suite_add_tcase (s, tc_parse_transport);
	tcase_add_test (tc_parse_transport, test_parse_transport_pass_001);

	TCase* tc_print_all = tcase_create ("print-all");
	tcase_add_checked_fixture (tc_print_all, mock_setup, mock_teardown);
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
	SRunner* sr = srunner_create (make_master_suite ());
	srunner_add_suite (sr, make_test_suite ());
	srunner_run_all (sr, CK_ENV);
	int number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* eof */
