/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test various network specifications.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <glib.h>

#include "pgm/backtrace.h"
#include "pgm/if.h"


#include "../if.c"


/* globals */
static const char *tests[] = 
		{
		 "",
		 "eth0",
		 ";",
		 "eth0;",
		 "127",					/* network address */
		 "127.0.0.0;",
		 "127.x.x.x;",
		 "127.0.0.0/8;",
		 "127/8;",
		 "127.1/8;",
		 "127/8;",
		 "127.0.0.1;",
		 ";127.0.0.1",
		 "localhost",
		 ";ALL-SYSTEMS.MCAST.NET",		/* should resolve to 224.0.0.1 */
		 "224.0.0.1",
		 "224.0.0.1;",
		 ";;",
		 ";;;",
		 "eth0;;",
		 ";224.0.0.1;",
		 "eth0;224.0.0.1;",
		 ";224.0.0.1;225.0.0.1",
		 "eth0;224.0.0.1;225.0.0.1" ,
		 ";;224.0.0.1",

		 ";;224.0.0.1,225.0.0.1",
		 ";224.0.0.1,225.0.0.1;",
		 "eth0,eth1;",
		 "eth0,eth1;224.0.0.1,225.0.0.1;226.0.0.1",

		 "::1/128;",
		 "eth0;ff03::3",

		 NULL 
		};


G_GNUC_NORETURN static void
usage (const char* bin)
{
	fprintf (stderr, "Usage: %s [options]\n", bin);
	exit (1);
}

int
main (
	int	argc,
	char   *argv[]
	)
{
	puts ("testif");

/* parse program arguments */
	const char* binary_name = strrchr (argv[0], '/');
	int c;
	while ((c = getopt (argc, argv, "h")) != -1)
	{
		switch (c) {

		case 'h':
		case '?': usage (binary_name);
		}
	}

/* setup signal handlers */
	signal (SIGSEGV, on_sigsegv);
	signal(SIGHUP, SIG_IGN);

	for (const char** p = tests; *p; p++)
	{
		GList *recv_list = NULL, *send_list = NULL;

		printf ("#%i: [%s]\n", (int)( p - tests ), *p);
		int retval = pgm_if_parse_network (*p, AF_UNSPEC, &recv_list, &send_list);

		if (retval == 0)
		{
			char s[INET6_ADDRSTRLEN];
			char s2[INET6_ADDRSTRLEN];
			int i = 0;
/* receive entity */
			while (recv_list)
			{
				struct sockaddr* source = &((struct group_source_req*)recv_list->data)->gsr_source;
				struct sockaddr* group  = &((struct group_source_req*)recv_list->data)->gsr_group;

				inet_ntop (source->sa_family, pgm_sockaddr_addr(source), s, sizeof(s));
				inet_ntop (group->sa_family, pgm_sockaddr_addr(group), s2, sizeof(s2));
				printf ("receive #%i: %s;%s\n", i+1, s, s2);
				i++;

				g_free(recv_list->data);
				recv_list = g_list_delete_link (recv_list, recv_list);
			}
/* send entity */
			i = 0;
			while (send_list)
			{
				struct sockaddr* source = &((struct group_source_req*)send_list->data)->gsr_source;
				struct sockaddr* group  = &((struct group_source_req*)send_list->data)->gsr_group;

				inet_ntop (source->sa_family, pgm_sockaddr_addr(source), s, sizeof(s));
				inet_ntop (group->sa_family, pgm_sockaddr_addr(group), s2, sizeof(s2));
				printf ("send #%i: %s;%s\n", i+1, s, s2);
				i++;

				g_free(send_list->data);
				send_list = g_list_delete_link (send_list, send_list);
			}
		}

		printf ("\nret value %i.\n\n", retval);
	}

	puts ("finished.");
	return 0;
}

/* eof */
