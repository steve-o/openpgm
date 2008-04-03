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

#include <glib.h>

#include "pgm/backtrace.h"
#include "pgm/if.h"


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


static void
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

	for (char** p = (char**)tests; *p; p++)
	{
		struct sockaddr devices[10], receive_groups[10], send_group;

		printf ("#%i: [%s]\n", (int)( p - (char**)tests ), *p);
		int retval = pgm_if_parse_network (*p, AF_UNSPEC, devices, receive_groups, &send_group, 10);

		if (retval == 0)
		{
			char s[INET6_ADDRSTRLEN];
			int i = 0;
			while (devices[i].sa_family) {
				inet_ntop(devices[i].sa_family,
					devices[i].sa_family == AF_INET ?
						(struct sockaddr*)&((struct sockaddr_in*)(&devices[i]))->sin_addr :
						(struct sockaddr*)&((struct sockaddr_in6*)(&devices[i]))->sin6_addr,
					s,
					sizeof(s));
				printf ("device #%i: %s\n", i+1, s);
				i++;
			}
			i = 0;
			while (receive_groups[i].sa_family) {
				inet_ntop(receive_groups[i].sa_family,
					receive_groups[i].sa_family == AF_INET ?
						(struct sockaddr*)&((struct sockaddr_in*)&receive_groups[i])->sin_addr :
						(struct sockaddr*)&((struct sockaddr_in6*)&receive_groups[i])->sin6_addr,
					s,
					sizeof(s));
				printf ("receive_groups #%i: %s\n", i+1, s);
				i++;
			}
			inet_ntop(send_group.sa_family,
				send_group.sa_family == AF_INET ?
					(struct sockaddr*)&((struct sockaddr_in*)&send_group)->sin_addr :
					(struct sockaddr*)&((struct sockaddr_in6*)&send_group)->sin6_addr,
				s,
				sizeof(s));
			printf ("send_group: %s\n", s);
		}

		printf ("\nret value %i.\n\n", retval);
	}

	puts ("finished.");
	return 0;
}

/* eof */
