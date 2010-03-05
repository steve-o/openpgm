/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable error reporting.
 *
 * Copyright (c) 2010 Miru Limited.
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

#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <glib.h>

#include "pgm/mem.h"
#include "pgm/string.h"
#include "pgm/error.h"


//#define ERROR_DEBUG

#ifndef ERROR_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


#define ERROR_OVERWRITTEN_WARNING "pgm_error_t set over the top of a previous pgm_error_t or uninitialized memory.\n" \
               "This indicates a bug. You must ensure an error is NULL before it's set.\n" \
               "The overwriting error message was: %s"

static
pgm_error_t*
pgm_error_new_valist (
	pgm_quark_t	domain,
	gint		code,
	const gchar*	format,
	va_list		args
	)
{
	pgm_error_t *error = pgm_new (pgm_error_t, 1);
	error->domain  = domain;
	error->code    = code;
	error->message = g_strdup_vprintf (format, args);
	return error;
}

void
pgm_error_free (
	pgm_error_t*	error
	)
{
	g_return_if_fail (error != NULL);
	pgm_free (error->message);
	pgm_free (error);
}

void
pgm_set_error (
	pgm_error_t**	err,
	pgm_quark_t	domain,
	gint		code,
	const gchar*	format,
	...
	)
{
	pgm_error_t *new;
	va_list args;

	if (NULL == err)
		return;

	va_start (args, format);
	new = pgm_error_new_valist (domain, code, format, args);
	va_end (args);

	if (NULL == *err)
		*err = new;
	else
		g_warning (_(ERROR_OVERWRITTEN_WARNING), new->message); 
}

void
pgm_propagate_error (
	pgm_error_t**	dest,
	pgm_error_t*	src
	)
{
	g_return_if_fail (src != NULL);
 
	if (NULL == dest) {
		if (src)
			pgm_error_free (src);
		return;
	} else {
		if (NULL != *dest)
			g_warning (_(ERROR_OVERWRITTEN_WARNING), src->message);
		else
			*dest = src;
	}
}

void
pgm_clear_error (
	pgm_error_t**	err
	)
{
	if (err && *err) {
		pgm_error_free (*err);
		*err = NULL;
	}
}

static void
pgm_error_add_prefix (
	gchar**		string,
	const gchar*	format,
	va_list		ap
	)
{
	gchar* prefix = pgm_strdup_vprintf (format, ap);
	gchar* oldstring = *string;
	*string = g_strconcat (prefix, oldstring, NULL);
	pgm_free (oldstring);
	pgm_free (prefix);
}

void
pgm_prefix_error (
	pgm_error_t**	err,
	const gchar*	format,
	...
	)
{
	if (err && *err) {
		va_list ap;
		va_start (ap, format);
		pgm_error_add_prefix (&(*err)->message, format, ap);
		va_end (ap);
	}
}


/* eof */
