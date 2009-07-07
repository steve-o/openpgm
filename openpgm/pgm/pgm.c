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

#include <netdb.h>

#include <glib.h>

#include "pgm/packet.h"
#include "pgm/time.h"


#ifndef PGM_DEBUG
#	define g_trace(m,...)		while (0)
#else
#	define g_trace(m,...)		g_debug(__VA_ARGS__)
#endif


/* globals */
int ipproto_pgm = IPPROTO_PGM;


/* startup PGM engine, mainly finding PGM protocol definition, if any from NSS
 *
 * on success, returns 0.
 */

int
pgm_init (void)
{
/* ensure threading enabled */
	if (!g_thread_supported ())
		g_thread_init (NULL);

/* ensure timing enabled */
	if (!pgm_time_supported ())
		pgm_time_init();

/* find PGM protocol id */
// TODO: fix valgrind errors
#ifdef CONFIG_HAVE_GETPROTOBYNAME_R
	char b[1024];
	struct protoent protobuf, *proto;
	int e = getprotobyname_r ("pgm", &protobuf, b, sizeof(b), &proto);
	if (e != -1 && proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			g_trace ("Setting PGM protocol number to %i from /etc/protocols.",
				proto->p_proto);
			ipproto_pgm = proto->p_proto;
		}
	}
#else
	struct protoent *proto = getprotobyname ("pgm");
	if (proto != NULL) {
		if (proto->p_proto != ipproto_pgm) {
			g_trace ("Setting PGM protocol number to %i from /etc/protocols.",
				proto->p_proto);
			ipproto_pgm = proto->p_proto;
		}
	}
#endif

	return 0;
}

/* eof */
