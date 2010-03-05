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

#include <string.h>

#include <glib.h>

#include "pgm/malloc.h"
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

	g_return_val_if_fail (string != NULL, NULL);
	g_return_val_if_fail (delimiter != NULL, NULL);
	g_return_val_if_fail (delimiter[0] != '\0', NULL);

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
