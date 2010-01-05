/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM engine.
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

#include <glib.h>

#ifdef G_OS_UNIX
#	include <netdb.h>
#else
#       include <ws2tcpip.h>
#endif

#include "pgm/packet.h"
#include "pgm/time.h"
#include "pgm/transport.h"


#ifndef PGM_DEBUG
#	define g_trace(...)		while (0)
#else
#	define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* globals */
int ipproto_pgm = IPPROTO_PGM;


/* locals */
static gboolean pgm_got_initialized = FALSE;


/* startup PGM engine, mainly finding PGM protocol definition, if any from NSS
 *
 * returns 0 on success, returns -1 if an error occurred, implying some form of
 * system re-configuration is required to resolve before trying again.
 */
// TODO: could wrap initialization with g_once_init_enter/leave.
// TODO: fix valgrind errors in getprotobyname/_r
int
pgm_init (void)
{
	g_return_val_if_fail (pgm_got_initialized == FALSE, -1);
	g_return_val_if_fail (pgm_time_supported() == FALSE, -1);

/* ensure threading enabled */
	if (!g_thread_supported ())
		g_thread_init (NULL);

#ifdef G_OS_WIN32
	WORD wVersionRequested = MAKEWORD (2, 2);
	WSADATA wsaData;
	if (WSAStartup (wVersionRequested, &wsaData) != 0)
		return -1;

	if (LOBYTE (wsaData.wVersion) != 2 || HIBYTE (wsaData.wVersion) != 2)
	{
		WSACleanup ();
		return -1;
	}
#endif /* G_OS_WIN32 */

/* find PGM protocol id overriding default value */
#ifdef CONFIG_HAVE_GETPROTOBYNAME_R
	char b[1024];
	struct protoent protobuf, *proto;
	int e = getprotobyname_r ("pgm", &protobuf, b, sizeof(b), &proto);
	if (e != -1 && proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			g_trace ("setting PGM protocol number to %i from /etc/protocols.",
				proto->p_proto);
			ipproto_pgm = proto->p_proto;
		}
	}
#else
	struct protoent *proto = getprotobyname ("pgm");
	if (proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			g_trace ("setting PGM protocol number to %i from /etc/protocols.",
				proto->p_proto);
			ipproto_pgm = proto->p_proto;
		}
	}
#endif

/* ensure timing enabled */
	if (-1 == pgm_time_init ())
		return -1;

	pgm_got_initialized = TRUE;
	return 0;
}

/* returns TRUE if PGM engine has been initialized
 */

gboolean
pgm_supported (void)
{
	return ( pgm_got_initialized == TRUE );
}

/* returns 0 on success, returns -1 if an error occurred.
 */

int
pgm_shutdown (void)
{
	g_return_val_if_fail (pgm_supported() == TRUE, -1);
	g_return_val_if_fail (pgm_time_supported() == TRUE, -1);

/* destroy all open transports */
	while (pgm_transport_list) {
		pgm_transport_destroy (pgm_transport_list->data, FALSE);
	}

	if (-1 == pgm_time_shutdown ())
		return -1;

#ifdef G_OS_WIN32
	WSACleanup ();
#endif

	pgm_got_initialized = FALSE;
	return 0;
}

/* eof */
