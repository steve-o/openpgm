/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * global session ID helper functions.
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

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "pgm/md5.h"
#include "pgm/gsi.h"

//#define GSI_DEBUG

#ifndef GSI_DEBUG
#	define g_trace(...)		while (0)
#else
#	define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* locals */

static PGMGSIError pgm_gsi_error_from_errno (gint);
static PGMGSIError pgm_gsi_error_from_eai_errno (gint);


/* create a global session ID as recommended by the PGM draft specification using
 * low order 48 bits of md5 of the hostname.
 *
 * returns TRUE on success, returns FALSE on error and sets error appropriately,
 */

gboolean
pgm_gsi_create_from_hostname (
	pgm_gsi_t*	gsi,
	GError**	error
	)
{
	struct md5_ctx ctx;
	char hostname[NI_MAXHOST];
	char resblock[16];

	g_return_val_if_fail (NULL != gsi, FALSE);

	int retval = gethostname (hostname, sizeof(hostname));
	if (0 != retval) {
		g_set_error (error,
			     PGM_GSI_ERROR,
			     pgm_gsi_error_from_errno (errno),
			     _("Resolving hostname: %s"),
			     g_strerror (errno));
		return FALSE;
	}

	md5_init_ctx (&ctx);
	md5_process_bytes (hostname, strlen(hostname), &ctx);
	md5_finish_ctx (&ctx, resblock);

	memcpy (gsi, resblock + 10, 6);
	return TRUE;
}

/* create a global session ID based on the IP address.
 *
 * GLib random API will need warming up before g_random_int_range returns
 * numbers that actually vary.
 *
 * returns TRUE on succcess, returns FALSE on error and sets error.
 */

gboolean
pgm_gsi_create_from_addr (
	pgm_gsi_t*	gsi,
	GError**	error
	)
{
	char hostname[NI_MAXHOST];
	struct addrinfo hints, *res = NULL;

	g_return_val_if_fail (NULL != gsi, FALSE);

	int retval = gethostname (hostname, sizeof(hostname));
	if (0 != retval) {
		g_set_error (error,
			     PGM_GSI_ERROR,
			     pgm_gsi_error_from_errno (errno),
			     _("Resolving hostname: %s"),
			     g_strerror (errno));
		return FALSE;
	}
	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_ADDRCONFIG;
	retval = getaddrinfo (hostname, NULL, &hints, &res);
	if (0 != retval) {
		g_set_error (error,
			     PGM_GSI_ERROR,
			     pgm_gsi_error_from_eai_errno (retval),
			     _("Resolving hostname address: %s"),
			     gai_strerror (retval));
		return FALSE;
	}
	memcpy (gsi, &((struct sockaddr_in*)(res->ai_addr))->sin_addr, sizeof(struct in_addr));
	freeaddrinfo (res);
	guint16 random = g_random_int_range (0, UINT16_MAX);
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

	g_return_val_if_fail (NULL != gsi, -1);
	g_return_val_if_fail (NULL != buf, -1);
	g_return_val_if_fail (bufsize > 0, -1);

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

	g_return_val_if_fail (NULL != gsi, NULL);

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
	g_assert (v);
	g_assert (v2);

        return memcmp (v, v2, 6 * sizeof(guint8)) == 0;
}

GQuark
pgm_gsi_error_quark (void)
{
	return g_quark_from_static_string ("pgm-gsi-error-quark");
}

static
PGMGSIError
pgm_gsi_error_from_errno (
	gint		err_no
	)
{
	switch (err_no) {
#ifdef EFAULT
	case EFAULT:
		return PGM_GSI_ERROR_FAULT;
		break;
#endif

#ifdef EINVAL
	case EINVAL:
		return PGM_GSI_ERROR_INVAL;
		break;
#endif

#ifdef EPERM
	case EPERM:
		return PGM_GSI_ERROR_PERM;
		break;
#endif

	default :
		return PGM_GSI_ERROR_FAILED;
		break;
	}
}

/* errno must be preserved before calling to catch correct error
 * status with EAI_SYSTEM.
 */

static
PGMGSIError
pgm_gsi_error_from_eai_errno (
	gint		err_no
	)
{
	switch (err_no) {
#ifdef EAI_ADDRFAMILY
	case EAI_ADDRFAMILY:
		return PGM_GSI_ERROR_ADDRFAMILY;
		break;
#endif

#ifdef EAI_AGAIN
	case EAI_AGAIN:
		return PGM_GSI_ERROR_AGAIN;
		break;
#endif

#ifdef EAI_BADFLAGS
	case EAI_BADFLAGS:
		return PGM_GSI_ERROR_BADFLAGS;
		break;
#endif

#ifdef EAI_FAIL
	case EAI_FAIL:
		return PGM_GSI_ERROR_FAIL;
		break;
#endif

#ifdef EAI_FAMILY
	case EAI_FAMILY:
		return PGM_GSI_ERROR_FAMILY;
		break;
#endif

#ifdef EAI_MEMORY
	case EAI_MEMORY:
		return PGM_GSI_ERROR_MEMORY;
		break;
#endif

#ifdef EAI_NODATA
	case EAI_NODATA:
		return PGM_GSI_ERROR_NODATA;
		break;
#endif

#ifdef EAI_NONAME
	case EAI_NONAME:
		return PGM_GSI_ERROR_NONAME;
		break;
#endif

#ifdef EAI_SERVICE
	case EAI_SERVICE:
		return PGM_GSI_ERROR_SERVICE;
		break;
#endif

#ifdef EAI_SOCKTYPE
	case EAI_SOCKTYPE:
		return PGM_GSI_ERROR_SOCKTYPE;
		break;
#endif

#ifdef EAI_SYSTEM
	case EAI_SYSTEM:
		return pgm_gsi_error_from_errno (errno);
		break;
#endif

	default :
		return PGM_GSI_ERROR_FAILED;
		break;
	}
}

/* eof */
