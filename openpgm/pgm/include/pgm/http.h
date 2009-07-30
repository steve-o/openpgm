/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * HTTP administrative interface
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

#ifndef __PGM_HTTP_H__
#define __PGM_HTTP_H__

#include <glib.h>


#define PGM_HTTP_ERROR		pgm_http_error_quark ()

typedef enum
{
	/* Derived from errno */
	PGM_HTTP_ERROR_FAULT,		/* gethostname returned EFAULT */
	PGM_HTTP_ERROR_INVAL,
	PGM_HTTP_ERROR_PERM,
	PGM_HTTP_ERROR_MFILE,
	PGM_HTTP_ERROR_NFILE,
	PGM_HTTP_ERROR_NXIO,
	PGM_HTTP_ERROR_RANGE,
	PGM_HTTP_ERROR_NOENT,
	PGM_HTTP_ERROR_ADDRFAMILY,	/* getaddrinfo return EAI_ADDRFAMILY */
	PGM_HTTP_ERROR_AGAIN,
	PGM_HTTP_ERROR_BADFLAGS,
	PGM_HTTP_ERROR_FAIL,
	PGM_HTTP_ERROR_FAMILY,
	PGM_HTTP_ERROR_MEMORY,
	PGM_HTTP_ERROR_NODATA,
	PGM_HTTP_ERROR_NONAME,
	PGM_HTTP_ERROR_SERVICE,
	PGM_HTTP_ERROR_SOCKTYPE,
	PGM_HTTP_ERROR_SYSTEM,
	PGM_HTTP_ERROR_FAILED
} PGMHTTPError;

G_BEGIN_DECLS

#define PGM_HTTP_DEFAULT_SERVER_PORT	4968

gboolean pgm_http_init (guint16, GError**) G_GNUC_WARN_UNUSED_RESULT;
gboolean pgm_http_shutdown (void);
GQuark pgm_http_error_quark (void);

G_END_DECLS

#endif /* __PGM_SIGNAL_H__ */
