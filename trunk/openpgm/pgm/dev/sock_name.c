/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Test retrieving bound socket name:
 *
 * http://support.microsoft.com/kb/129065
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


#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <glib.h>


/* globals */

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
	puts ("sock_name");

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
	signal(SIGHUP, SIG_IGN);

	int sock = socket (AF_INET, SOCK_RAW, 113);
//	int sock = socket (AF_INET, SOCK_DGRAM, 0);
	assert (sock != 1);

	struct sockaddr_in addr;
	memset (&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons (9000);
	addr.sin_addr.s_addr = htonl (INADDR_ANY);
	int e = bind (sock, (struct sockaddr*)&addr, sizeof(addr));
	assert (e == 0);

	struct sockaddr_storage name;
	memset (&name, 0, sizeof(name));
	socklen_t namelen = sizeof(name);
	e = getsockname (sock, (struct sockaddr*)&name, &namelen);
	assert (e == 0);

	printf ("name %s:%u\n", inet_ntoa (((struct sockaddr_in*)&name)->sin_addr), 
				(unsigned)g_ntohs( ((struct sockaddr_in*)&name)->sin_port) );

	puts ("finished.");
	return 0;
}

/* eof */
