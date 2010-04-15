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

char*
pgm_strdup (
	const char*	str
	)
{
	char* new_str;
	size_t length;

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

int
pgm_printf_string_upper_bound (
	const char*	format,
	va_list		args
	)
{
	char c;
	return vsnprintf (&c, 1, format, args) + 1;
}

int
pgm_vasprintf (
	char**	    restrict string,
	const char* restrict format,
	va_list		     args
	)
{
	pgm_return_val_if_fail (string != NULL, -1);
#ifdef CONFIG_HAVE_VASPRINTF
	const int len = vasprintf (string, format, args);
	if (len < 0)
		*string = NULL;
#else
	va_list args2;
	G_VA_COPY (args2, args);
	*string = pgm_malloc (pgm_printf_string_upper_bound (format, args));
/* NB: must be able to handle NULL args, fails on GCC */
	const int len = vsprintf (*string, format, args);
	va_end (args2);
#endif
	return len;
}

char*
pgm_strdup_vprintf (
	const char*	format,
	va_list		args
	)
{
	char *string = NULL;
	pgm_vasprintf (&string, format, args);
	return string;
}

static
char*
pgm_stpcpy (
	char*	    restrict dest,
	const char* restrict src
	)
{
	pgm_return_val_if_fail (dest != NULL, NULL);
	pgm_return_val_if_fail (src != NULL, NULL);
#ifdef CONFIG_HAVE_STPCPY
	return stpcpy (dest, src);
#else
	char *d = dest;
	const char *s = src;
	do {
		*d++ = *s;
	} while (*s++ != '\0');
	return d - 1;
#endif
}

char*
pgm_strconcat (
	const char*	string1,
	...
	)
{
	size_t	l;     
	va_list args;
	char*	s;
	char*	concat;
	char*	ptr;

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

char**
pgm_strsplit (
	const char*	string,
	const char*	delimiter,
	int		max_tokens
	)
{
	pgm_slist_t *string_list = NULL, *slist;
	char **str_array, *s;
	unsigned n = 0;
	const char *remainder;

	pgm_return_val_if_fail (string != NULL, NULL);
	pgm_return_val_if_fail (delimiter != NULL, NULL);
	pgm_return_val_if_fail (delimiter[0] != '\0', NULL);

	if (max_tokens < 1)
		max_tokens = G_MAXINT;

	remainder = string;
	s = strstr (remainder, delimiter);
	if (s)
	{
		const size_t delimiter_len = strlen (delimiter);   

		while (--max_tokens && s)
		{
			const size_t len = s - remainder;
			char *new_string = pgm_new (char, len + 1);
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
		string_list = pgm_slist_prepend (string_list, pgm_strdup (remainder));
	}

	str_array = pgm_new (char*, n + 1);
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
	char**		str_array
	)
{
	if (G_LIKELY (str_array))
	{
		for (unsigned i = 0; str_array[i] != NULL; i++)
			pgm_free (str_array[i]);

		pgm_free (str_array);
	}
}

/* eof */
