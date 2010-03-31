/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM engine.
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

#include <glib.h>
#include <glib/gi18n-lib.h>

#ifdef G_OS_UNIX
#	include <netdb.h>
#else
#       include <ws2tcpip.h>
#endif

#include "pgm/pgm.h"
#include "pgm/packet.h"
#include "pgm/timep.h"


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
 * returns TRUE on success, returns FALSE if an error occurred, implying some form of
 * system re-configuration is required to resolve before trying again.
 */
// TODO: could wrap initialization with g_once_init_enter/leave.
// TODO: fix valgrind errors in getprotobyname/_r
gboolean
pgm_init (
	GError**	error
	)
{
	if (TRUE == pgm_got_initialized) {
		g_set_error (error,
			     PGM_ENGINE_ERROR,
			     PGM_ENGINE_ERROR_FAILED,
			     _("Engine already initalized."));
		return FALSE;
	}

/* ensure threading enabled */
	if (!g_thread_supported ())
		g_thread_init (NULL);

#ifdef G_OS_WIN32
	WORD wVersionRequested = MAKEWORD (2, 2);
	WSADATA wsaData;
	if (WSAStartup (wVersionRequested, &wsaData) != 0)
	{
		const int save_errno = WSAGetLastError ();
		g_set_error (error,
			     PGM_ENGINE_ERROR,
			     pgm_engine_error_from_wsa_errno (save_errno),
			     _("WSAStartup failure: %s"),
			     pgm_wsastrerror (save_errno));
		return FALSE;
	}

	if (LOBYTE (wsaData.wVersion) != 2 || HIBYTE (wsaData.wVersion) != 2)
	{
		WSACleanup ();
		g_set_error (error,
			     PGM_ENGINE_ERROR,
			     PGM_ENGINE_ERROR_FAILED,
			     _("WSAStartup failed to provide requested version 2.2."));
		return FALSE;
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
	GError* sub_error = NULL;
	if (!pgm_time_supported () &&
	    !pgm_time_init (&sub_error))
	{
		g_propagate_error (error, sub_error);
		return FALSE;
	}

	pgm_got_initialized = TRUE;
	return TRUE;
}

/* returns TRUE if PGM engine has been initialized
 */

gboolean
pgm_supported (void)
{
	return ( pgm_got_initialized == TRUE );
}

/* returns TRUE on success, returns FALSE if an error occurred.
 */

gboolean
pgm_shutdown (void)
{
	g_return_val_if_fail (pgm_supported() == TRUE, FALSE);

/* destroy all open transports */
	while (pgm_transport_list) {
		pgm_transport_destroy (pgm_transport_list->data, FALSE);
	}

	if (pgm_time_supported ())
		pgm_time_shutdown ();

#ifdef G_OS_WIN32
	WSACleanup ();
#endif

	pgm_got_initialized = FALSE;
	return TRUE;
}

GQuark
pgm_engine_error_quark (void)
{
	return g_quark_from_static_string ("pgm-engine-error-quark");
}

PGMEngineError
pgm_engine_error_from_wsa_errno (
	gint            err_no
	)
{
	switch (err_no) {
#ifdef WSASYSNOTAREADY
	case WSASYSNOTAREADY:
		return PGM_ENGINE_ERROR_SYSNOTAREADY;
		break;
#endif
#ifdef WSAVERNOTSUPPORTED
	case WSAVERNOTSUPPORTED:
		return PGM_ENGINE_ERROR_VERNOTSUPPORTED;
		break;
#endif
#ifdef WSAEINPROGRESS
	case WSAEINPROGRESS:
		return PGM_ENGINE_ERROR_INPROGRESS;
		break;
#endif
#ifdef WSAEPROCLIM
	case WSAEPROCLIM:
		return PGM_ENGINE_ERROR_PROCLIM;
		break;
#endif
#ifdef WSAEFAULT
	case WSAEFAULT:
		return PGM_ENGINE_ERROR_FAULT;
		break;
#endif

	default:
		return PGM_ENGINE_ERROR_FAILED;
		break;
	}
}

/* eof */
