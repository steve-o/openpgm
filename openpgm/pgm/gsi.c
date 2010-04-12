/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * global session ID helper functions.
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

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <glib.h>

#ifdef G_OS_UNIX
#	include <netdb.h>
#	include <netinet/in.h>
#else
#	include <ws2tcpip.h>
#endif

#include "pgm/messages.h"
#include "pgm/md5.h"
#include "pgm/gsi.h"
#include "pgm/rand.h"


//#define GSI_DEBUG


/* locals */


/* create a GSI based on md5 of a user provided data block.
 *
 * returns TRUE on success, returns FALSE on error and sets error appropriately,
 */

gboolean
pgm_gsi_create_from_data (
	pgm_gsi_t*	gsi,
	const guchar*	data,
	const gsize	length
	)
{
	pgm_return_val_if_fail (NULL != gsi, FALSE);
	pgm_return_val_if_fail (NULL != data, FALSE);
	pgm_return_val_if_fail (length > 1, FALSE);

#ifdef CONFIG_HAVE_GLIB_CHECKSUM
	GChecksum* checksum = g_checksum_new (G_CHECKSUM_MD5);
	pgm_return_val_if_fail (NULL != checksum, FALSE);
	g_checksum_update (checksum, data, length);
	memcpy (gsi, g_checksum_get_string (checksum) + 10, 6);
	g_checksum_free (checksum);
#else
	struct md5_ctx ctx;
	char resblock[16];
	_md5_init_ctx (&ctx);
	_md5_process_bytes (&ctx, data, length);
	_md5_finish_ctx (&ctx, resblock);
	memcpy (gsi, resblock + 10, 6);
#endif
	return TRUE;
}

gboolean
pgm_gsi_create_from_string (
	pgm_gsi_t*	gsi,
	const gchar*	str,
	gssize		length
	)
{
	pgm_return_val_if_fail (NULL != gsi, FALSE);
	pgm_return_val_if_fail (NULL != str, FALSE);

	if (length < 0)
		length = strlen (str);

	return pgm_gsi_create_from_data (gsi, (const guchar*)str, length);
}

/* create a global session ID as recommended by the PGM draft specification using
 * low order 48 bits of md5 of the hostname.
 *
 * returns TRUE on success, returns FALSE on error and sets error appropriately,
 */

gboolean
pgm_gsi_create_from_hostname (
	pgm_gsi_t*	gsi,
	pgm_error_t**	error
	)
{
	pgm_return_val_if_fail (NULL != gsi, FALSE);

	char hostname[NI_MAXHOST];
	int retval = gethostname (hostname, sizeof(hostname));
	if (0 != retval) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_errno (errno),
			     _("Resolving hostname: %s"),
#ifdef G_OS_UNIX
			     strerror (errno)
#else
			     wsa_strerror (WSAGetLastError())
#endif
				);
		return FALSE;
	}

	return pgm_gsi_create_from_string (gsi, hostname, -1);
}

/* create a global session ID based on the IP address.
 *
 * returns TRUE on succcess, returns FALSE on error and sets error.
 */

gboolean
pgm_gsi_create_from_addr (
	pgm_gsi_t*	gsi,
	pgm_error_t**	error
	)
{
	char hostname[NI_MAXHOST];
	struct addrinfo hints, *res = NULL;

	pgm_return_val_if_fail (NULL != gsi, FALSE);

	int retval = gethostname (hostname, sizeof(hostname));
	if (0 != retval) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_errno (errno),
			     _("Resolving hostname: %s"),
#ifdef G_OS_UNIX
			     strerror (errno)
#else
			     wsa_strerror (WSAGetLastError())
#endif
				);
		return FALSE;
	}
	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_ADDRCONFIG;
	retval = getaddrinfo (hostname, NULL, &hints, &res);
	if (0 != retval) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_eai_errno (retval, errno),
			     _("Resolving hostname address: %s"),
			     gai_strerror (retval));
		return FALSE;
	}
	memcpy (gsi, &((struct sockaddr_in*)(res->ai_addr))->sin_addr, sizeof(struct in_addr));
	freeaddrinfo (res);
	guint16 random = pgm_random_int_range (0, UINT16_MAX);
	memcpy ((guint8*)gsi + sizeof(struct in_addr), &random, sizeof(random));
	return TRUE;
}

/* re-entrant form of pgm_gsi_print()
 *
 * returns number of bytes written to buffer on success, returns -1 on
 * invalid parameters.
 */

int
pgm_gsi_print_r (
	const pgm_gsi_t*	gsi,
	char*			buf,
	gsize			bufsize
	)
{
	const guint8* src = (const guint8*)gsi;

	pgm_return_val_if_fail (NULL != gsi, -1);
	pgm_return_val_if_fail (NULL != buf, -1);
	pgm_return_val_if_fail (bufsize > 0, -1);

	return snprintf (buf, bufsize, "%i.%i.%i.%i.%i.%i",
			src[0], src[1], src[2], src[3], src[4], src[5]);
}

/* transform GSI to ASCII string form.
 *
 * on success, returns pointer to ASCII string.  on error, returns NULL.
 */

gchar*
pgm_gsi_print (
	const pgm_gsi_t*	gsi
	)
{
	static char buf[PGM_GSISTRLEN];

	pgm_return_val_if_fail (NULL != gsi, NULL);

	pgm_gsi_print_r (gsi, buf, sizeof(buf));
	return buf;
}

/* compare two global session identifier GSI values and return TRUE if they are equal
 */

gint
pgm_gsi_equal (
        gconstpointer   v,
        gconstpointer   v2
        )
{
/* pre-conditions */
	pgm_assert (v);
	pgm_assert (v2);

        return memcmp (v, v2, 6 * sizeof(guint8)) == 0;
}

/* eof */
