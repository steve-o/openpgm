/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * unit tests for portable interface index to socket address function.
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
#define _BSD_SOURCE	1

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#	include <netdb.h>
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#	include <net/if.h>
#else
#	include <ws2tcpip.h>
#	include <mswsock.h>
#endif
#include <glib.h>
#include <check.h>
#include <pgm/types.h>
#include <pgm/macros.h>


/* mock state */

struct mock_interface_t {
	unsigned int		index;
	char*			name;
	unsigned int		flags;
	struct sockaddr_storage	addr;
	struct sockaddr_storage	netmask;
};

static GList *mock_interfaces = NULL;

struct pgm_ifaddrs_t;
struct pgm_error_t;

static bool mock_pgm_getifaddrs (struct pgm_ifaddrs_t**, struct pgm_error_t**);
static void mock_pgm_freeifaddrs (struct pgm_ifaddrs_t*);
unsigned mock_pgm_if_nametoindex (const sa_family_t, const char*);


#define pgm_getifaddrs		mock_pgm_getifaddrs
#define pgm_freeifaddrs		mock_pgm_freeifaddrs
#define pgm_if_nametoindex	mock_pgm_if_nametoindex

#define INDEXTOADDR_DEBUG
#include "indextoaddr.c"

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
			g_error ("parsing failed for flag \"%s\"", tokens[i]);
	}
			
	g_strfreev (tokens);
	return new_interface;
}

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

	list = mock_interfaces;
	while (list) {
		struct mock_interface_t* interface_ = list->data;
		g_free (interface_->name);
		g_slice_free1 (sizeof(struct mock_interface_t), interface_);
		list = list->next;
	}
	g_list_free (mock_interfaces);
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
		return FALSE;
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

static
void
mock_pgm_freeifaddrs (
	struct pgm_ifaddrs_t*		ifa
	)
{
	g_debug ("mock_freeifaddrs (ifa:%p)", (gpointer)ifa);
	free (ifa);
}

PGM_GNUC_INTERNAL
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


/* target:
 *	bool
 *	pgm_if_indextoaddr (
 *		const unsigned		ifindex,
 *		const sa_family_t	iffamily,
 *		const uint32_t		ifscope,
 *		struct sockaddr*	ifsa,
 *		pgm_error_t**		error
 *	)
 */

START_TEST (test_indextoaddr_pass_001)
{
	char saddr[INET6_ADDRSTRLEN];
	struct sockaddr_storage addr;
	pgm_error_t* err = NULL;
	const unsigned int ifindex = 2;
	fail_unless (TRUE == pgm_if_indextoaddr (ifindex, AF_INET, 0, (struct sockaddr*)&addr, &err), "if_indextoaddr failed");
	pgm_sockaddr_ntop ((struct sockaddr*)&addr, saddr, sizeof(saddr));
	g_message ("index:%d -> %s",
		  ifindex, saddr);
	fail_unless (TRUE == pgm_if_indextoaddr (ifindex, AF_INET6, 0, (struct sockaddr*)&addr, &err), "if_indextoaddr failed");
	pgm_sockaddr_ntop ((struct sockaddr*)&addr, saddr, sizeof(saddr));
	g_message ("index:%d -> %s",
		  ifindex, saddr);
}
END_TEST

START_TEST (test_indextoaddr_fail_001)
{
	pgm_error_t* err = NULL;
	fail_unless (FALSE == pgm_if_indextoaddr (0, 0, 0, NULL, &err), "if_indextoaddr failed");
}
END_TEST


static
Suite*
make_test_suite (void)
{
	Suite* s;

	s = suite_create (__FILE__);

	TCase* tc_indextoaddr = tcase_create ("init-ctx");
	suite_add_tcase (s, tc_indextoaddr);
	tcase_add_checked_fixture (tc_indextoaddr, mock_setup_net, mock_teardown_net);
	tcase_add_test (tc_indextoaddr, test_indextoaddr_pass_001);
	tcase_add_test (tc_indextoaddr, test_indextoaddr_fail_001);
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
