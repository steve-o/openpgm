/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable string manipulation functions.
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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "pgm/messages.h"
#include "pgm/mem.h"
#include "pgm/string.h"
#include "pgm/slist.h"


//#define STRING_DEBUG

#ifndef STRING_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* Return copy of string, must be freed with pgm_free().
 */

gchar*
pgm_strdup (
	const gchar*	str
	)
{
	gchar* new_str;
	gsize length;

	if (G_LIKELY (str))
	{
		length = strlen (str) + 1;
		new_str = pgm_new (char, length);
		memcpy (new_str, str, length);
	}
	else
		new_str = NULL;

	return new_str;
}

/* Calculates the maximum space needed to store the output of the sprintf() function.
 */

gsize
pgm_printf_string_upper_bound (
	const gchar*	format,
	va_list		args
	)
{
	gchar c;
	return vsnprintf (&c, 1, format, args) + 1;
}

gint
pgm_vasprintf (
	gchar**		string,
	gchar const*	format,
	va_list		args
	)
{
	pgm_return_val_if_fail (string != NULL, -1);
#ifdef CONFIG_HAVE_VASPRINTF
	const gint len = vasprintf (string, format, args);
	if (len < 0)
		*string = NULL;
#else
	va_list args2;
	G_VA_COPY (args2, args);
	*string = pgm_malloc (pgm_printf_string_upper_bound (format, args));
/* NB: must be able to handle NULL args, fails on GCC */
	const gint len = vsprintf (*string, format, args);
	va_end (args2);
#endif
	return len;
}

gchar*
pgm_strdup_vprintf (
	const gchar*	format,
	va_list		args
	)
{
	gchar *string = NULL;
	pgm_vasprintf (&string, format, args);
	return string;
}

static
gchar*
pgm_stpcpy (
	gchar*		dest,
	const gchar*	src
	)
{
	pgm_return_val_if_fail (dest != NULL, NULL);
	pgm_return_val_if_fail (src != NULL, NULL);
#ifdef CONFIG_HAVE_STPCPY
	return stpcpy (dest, src);
#else
	gchar *d = dest;
	const gchar *s = src;
	do {
		*d++ = *s;
	} while (*s++ != '\0');
	return d - 1;
#endif
}

gchar*
pgm_strconcat (
	const gchar*	string1,
	...
	)
{
	gsize	l;     
	va_list args;
	gchar*	s;
	gchar*	concat;
	gchar*	ptr;

	if (!string1)
		return NULL;

	l = 1 + strlen (string1);
	va_start (args, string1);
	s = va_arg (args, gchar*);
	while (s) {
		l += strlen (s);
		s = va_arg (args, gchar*);
	}
	va_end (args);

	concat = pgm_malloc (l);
	ptr = concat;

	ptr = pgm_stpcpy (ptr, string1);
	va_start (args, string1);
	s = va_arg (args, gchar*);
	while (s) {
		ptr = pgm_stpcpy (ptr, s);
		s = va_arg (args, gchar*);
	}
	va_end (args);

	return concat;
}

/* Split a string with delimiter, result must be freed with pgm_strfreev().
 */

gchar**
pgm_strsplit (
	const gchar*	string,
	const gchar*	delimiter,
	gint		max_tokens
	)
{
	pgm_slist_t *string_list = NULL, *slist;
	gchar **str_array, *s;
	guint n = 0;
	const gchar *remainder;

	pgm_return_val_if_fail (string != NULL, NULL);
	pgm_return_val_if_fail (delimiter != NULL, NULL);
	pgm_return_val_if_fail (delimiter[0] != '\0', NULL);

	if (max_tokens < 1)
		max_tokens = G_MAXINT;

	remainder = string;
	s = strstr (remainder, delimiter);
	if (s)
	{
		const gsize delimiter_len = strlen (delimiter);   

		while (--max_tokens && s)
		{
			const gsize len = s - remainder;
			gchar *new_string = g_new (gchar, len + 1);
			strncpy (new_string, remainder, len);
			new_string[len] = 0;
			string_list = pgm_slist_prepend (string_list, new_string);
			n++;
			remainder = s + delimiter_len;
			s = strstr (remainder, delimiter);
		}
	}
	if (*string)
	{
		n++;
		string_list = pgm_slist_prepend (string_list, g_strdup (remainder));
	}

	str_array = pgm_new (gchar*, n + 1);
	str_array[n--] = NULL;
	for (slist = string_list; slist; slist = slist->next)
		str_array[n--] = slist->data;

	pgm_slist_free (string_list);

	return str_array;
}

/* Free a NULL-terminated array of strings, such as created by pgm_strsplit
 */

void
pgm_strfreev (
	gchar**		str_array
	)
{
	if (G_LIKELY (str_array))
	{
		for (int i = 0; str_array[i] != NULL; i++)
			pgm_free (str_array[i]);

		pgm_free (str_array);
	}
}

/* eof */
