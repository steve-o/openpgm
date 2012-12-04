/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM engine environment, i.e. bare minimum to get anything running.
 * - WinSock on Windows,
 * - Thread API to detect #cores,
 * - Logging API for global lock and debug flags,
 * - Memory API for debug flags,
 * - PRNG API for global lock on global generator,
 * - Timing API for calibration, device locking as appropriate,
 * - PGM protocol# resolution,
 * - Lock on global list of PGM sockets.
 *
 * Copyright (c) 2006-2011 Miru Limited.
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

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#ifndef _BSD_SOURCE
#	define _BSD_SOURCE	1	/* getprotobyname_r */
#endif

#ifndef _WIN32
#	include <netdb.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/engine.h>
#include <impl/mem.h>
#include <impl/socket.h>
#include <pgm/engine.h>
#include <pgm/version.h>


//#define ENGINE_DEBUG


/* globals */
int			pgm_ipproto_pgm PGM_GNUC_READ_MOSTLY = IPPROTO_PGM;

#ifdef _WIN32
LPFN_WSARECVMSG		pgm_WSARecvMsg PGM_GNUC_READ_MOSTLY = NULL;
#endif

#ifdef PGM_DEBUG
unsigned		pgm_loss_rate PGM_GNUC_READ_MOSTLY = 0;
#endif

/* locals */
static bool		pgm_is_supported = FALSE;
static volatile uint32_t pgm_ref_count	 = 0;

#ifdef _WIN32
#	ifndef WSAID_WSARECVMSG
/* http://cvs.winehq.org/cvsweb/wine/include/mswsock.h */
#		define WSAID_WSARECVMSG {0xf689d7c8,0x6f1f,0x436b,{0x8a,0x53,0xe5,0x4f,0xe3,0x51,0xc3,0x22}}
#	endif
#endif


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
	if (pgm_atomic_exchange_and_add32 (&pgm_ref_count, 1) > 0)
		return TRUE;

/* initialise dependent modules */
	pgm_messages_init();

	pgm_minor ("OpenPGM %d.%d.%d%s%s%s %s %s %s %s",
			pgm_major_version, pgm_minor_version, pgm_micro_version,
			pgm_build_revision ? " (" : "", pgm_build_revision ? pgm_build_revision : "", pgm_build_revision ? ")" : "",
			pgm_build_date, pgm_build_time, pgm_build_system, pgm_build_machine);

	pgm_thread_init();
	pgm_mem_init();
	pgm_rand_init();

#ifdef _WIN32
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

/* Find WSARecvMsg API.  Available in Windows XP and Wine 1.3.
 */
	if (NULL == pgm_WSARecvMsg) {
		GUID WSARecvMsg_GUID = WSAID_WSARECVMSG;
		DWORD cbBytesReturned;
		const SOCKET sock = socket (AF_INET, SOCK_DGRAM, 0);
		if (INVALID_SOCKET == sock) {
			WSACleanup();
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_ENGINE,
				       PGM_ERROR_FAILED,
				       _("Cannot open socket."));
			goto err_shutdown;
		}
		if (SOCKET_ERROR == WSAIoctl (sock,
					      SIO_GET_EXTENSION_FUNCTION_POINTER,
					      &WSARecvMsg_GUID, sizeof(WSARecvMsg_GUID),
					      &pgm_WSARecvMsg, sizeof(pgm_WSARecvMsg),
					      &cbBytesReturned,
					      NULL,
					      NULL))
		{
			closesocket (sock);
			WSACleanup();
			pgm_set_error (error,
				       PGM_ERROR_DOMAIN_ENGINE,
				       PGM_ERROR_FAILED,
				       _("WSARecvMsg function not found, available in Windows XP or Wine 1.3."));
			goto err_shutdown;
		}
		pgm_debug ("Retrieved address of WSARecvMsg.");
		closesocket (sock);
	}
#endif /* _WIN32 */

/* find PGM protocol id overriding default value, use first value from NIS */
	const struct pgm_protoent_t *proto = pgm_getprotobyname ("pgm");
	if (proto != NULL) {
		if (proto->p_proto != pgm_ipproto_pgm) {
			pgm_minor (_("Setting PGM protocol number to %i from the protocols database."),
				proto->p_proto);
			pgm_ipproto_pgm = proto->p_proto;
		}
	}

/* ensure timing enabled */
	pgm_error_t* sub_error = NULL;
	if (!pgm_time_init (&sub_error)) {
		if (sub_error)
			pgm_propagate_error (error, sub_error);
#ifdef _WIN32
		WSACleanup();
#endif
		goto err_shutdown;
	}

/* receiver simulated loss rate */
#ifdef PGM_DEBUG
	char* env;
	size_t envlen;

	const errno_t err = pgm_dupenv_s (&env, &envlen, "PGM_LOSS_RATE");
	if (0 == err && envlen > 0) {
		const int loss_rate = atoi (env);
		if (loss_rate > 0 && loss_rate <= 100) {
			pgm_loss_rate = loss_rate;
			pgm_minor (_("Setting PGM packet loss rate to %i%%."), pgm_loss_rate);
		}
		pgm_free (env);
	}
#endif

/* create global sock list lock */
	pgm_rwlock_init (&pgm_sock_list_lock);

	pgm_is_supported = TRUE;
	return TRUE;

err_shutdown:
	pgm_rand_shutdown();
	pgm_mem_shutdown();
	pgm_thread_shutdown();
	pgm_messages_shutdown();
	pgm_atomic_dec32 (&pgm_ref_count);
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
/* cannot use pgm_return_val_if_fail() as logging may not be started */
	if (0 == pgm_atomic_read32 (&pgm_ref_count))
		return FALSE;

	if (pgm_atomic_exchange_and_add32 (&pgm_ref_count, (uint32_t)-1) != 1)
		return TRUE;

	pgm_is_supported = FALSE;

/* destroy all open socks */
	while (pgm_sock_list) {
		pgm_close ((pgm_sock_t*)pgm_sock_list->data, FALSE);
	}

	pgm_rwlock_free (&pgm_sock_list_lock);

	pgm_time_shutdown();

#ifdef _WIN32
	WSACleanup();
#endif

	pgm_rand_shutdown();
	pgm_mem_shutdown();
	pgm_thread_shutdown();
	pgm_messages_shutdown();
	return TRUE;
}

/* helper to drop out of setuid 0 after creating PGM sockets
 */
void
pgm_drop_superuser (void)
{
#ifndef _WIN32
	if (0 == getuid()) {
		setuid((gid_t)65534);
		setgid((uid_t)65534);
	}
#endif
}

/* eof */
