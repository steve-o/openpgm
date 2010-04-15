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

#include <stdbool.h>
#include <stdint.h>
#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <glib.h>

#ifdef G_OS_UNIX
#	include <netdb.h>
#else
#       include <ws2tcpip.h>
#endif

#include "pgm/messages.h"
#include "pgm/atomic.h"
#include "pgm/pgm.h"
#include "pgm/packet.h"
#include "pgm/time.h"
#include "pgm/thread.h"
#include "pgm/rand.h"


//#define PGM_DEBUG


/* globals */
int pgm_ipproto_pgm = IPPROTO_PGM;


/* locals */
static bool pgm_is_supported = FALSE;
static volatile int32_t pgm_ref_count = 0;


/* startup PGM engine, mainly finding PGM protocol definition, if any from NSS
 *
 * returns TRUE on success, returns FALSE if an error occurred, implying some form of
 * system re-configuration is required to resolve before trying again.
 *
 * NB: Valgrind loves generating errors in getprotobyname().
 */
bool
pgm_init (
	pgm_error_t**	error
	)
{
	if (pgm_atomic_int32_exchange_and_add (&pgm_ref_count, 1) > 0)
		return TRUE;

/* initialise dependent modules */
	pgm_messages_init();

	pgm_minor ("OpenPGM %d.%d.%d%s%s%s %s %s %s",
			pgm_major_version, pgm_minor_version, pgm_micro_version,
			pgm_build_revision ? " (" : "", pgm_build_revision ? pgm_build_revision : "", pgm_build_revision ? ")" : "",
			pgm_build_date, pgm_build_time, pgm_build_platform);

	pgm_atomic_init();
	pgm_thread_init();
	pgm_mem_init();
	pgm_rand_init();

#ifdef G_OS_WIN32
	WORD wVersionRequested = MAKEWORD (2, 2);
	WSADATA wsaData;
	if (WSAStartup (wVersionRequested, &wsaData) != 0)
	{
		const int save_errno = WSAGetLastError ();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_ENGINE,
			     pgm_error_from_wsa_errno (save_errno),
			     _("WSAStartup failure: %s"),
			     pgm_wsastrerror (save_errno));
		goto err_shutdown;
	}

	if (LOBYTE (wsaData.wVersion) != 2 || HIBYTE (wsaData.wVersion) != 2)
	{
		WSACleanup();
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_ENGINE,
			     PGM_ERROR_FAILED,
			     _("WSAStartup failed to provide requested version 2.2."));
		goto err_shutdown;
	}
#endif /* G_OS_WIN32 */

/* find PGM protocol id overriding default value */
#ifdef CONFIG_HAVE_GETPROTOBYNAME_R
	char b[1024];
	struct protoent protobuf, *proto;
	const int e = getprotobyname_r ("pgm", &protobuf, b, sizeof(b), &proto);
	if (e != -1 && proto != NULL) {
		if (proto->p_proto != pgm_ipproto_pgm) {
			pgm_minor (_("Setting PGM protocol number to %i from /etc/protocols."),
				proto->p_proto);
			pgm_ipproto_pgm = proto->p_proto;
		}
	}
#else
	const struct protoent *proto = getprotobyname ("pgm");
	if (proto != NULL) {
		if (proto->p_proto != pgm_ipproto_pgm) {
			pgm_minor (_("Setting PGM protocol number to %i from /etc/protocols."),
				proto->p_proto);
			pgm_ipproto_pgm = proto->p_proto;
		}
	}
#endif

/* ensure timing enabled */
	pgm_error_t* sub_error = NULL;
	if (!pgm_time_init (&sub_error)) {
		if (sub_error)
			pgm_propagate_error (error, sub_error);
		goto err_shutdown;
	}

/* create global transport list lock */
	pgm_rwlock_init (&pgm_transport_list_lock);

	pgm_is_supported = TRUE;
	return TRUE;

err_shutdown:
	pgm_rand_shutdown();
	pgm_mem_shutdown();
	pgm_thread_shutdown();
	pgm_atomic_shutdown();
	pgm_messages_shutdown();
	pgm_atomic_int32_dec (&pgm_ref_count);
	return FALSE;
}

/* returns TRUE if PGM engine has been initialized
 */

bool
pgm_supported (void)
{
	return ( pgm_is_supported == TRUE );
}

/* returns TRUE on success, returns FALSE if an error occurred.
 */

bool
pgm_shutdown (void)
{
	pgm_return_val_if_fail (pgm_atomic_int32_get (&pgm_ref_count) > 0, FALSE);

	if (!pgm_atomic_int32_dec_and_test (&pgm_ref_count))
		return TRUE;

	pgm_is_supported = FALSE;

/* destroy all open transports */
	while (pgm_transport_list) {
		pgm_transport_destroy ((pgm_transport_t*)pgm_transport_list->data, FALSE);
	}

	pgm_time_shutdown();

#ifdef G_OS_WIN32
	WSACleanup();
#endif

	pgm_rand_shutdown();
	pgm_mem_shutdown();
	pgm_thread_shutdown();
	pgm_atomic_shutdown();
	pgm_messages_shutdown();
	return TRUE;
}

/* eof */
