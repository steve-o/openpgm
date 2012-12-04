/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable string manipulation functions.
 *
 * Copyright (c) 2010-2011 Miru Limited.
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

#if ( defined( HAVE_VASPRINTF ) || defined( HAVE_STPCPY ) ) && !defined( _GNU_SOURCE )
#	define _GNU_SOURCE	/* vasprintf, stpcpy */
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <impl/framework.h>


//#define STRING_DEBUG

/* Return copy of string, must be freed with pgm_free().
 */

PGM_GNUC_INTERNAL
char*
pgm_strdup (
	const char*	str
	)
{
	char	*new_str;
	size_t	 length;

	if (PGM_LIKELY (NULL != str))
	{
		length = strlen (str) + 1;
		new_str = pgm_malloc (length);
		memcpy (new_str, str, length);
	}
	else
		new_str = NULL;

	return new_str;
}

/* Calculates the maximum space needed to store the output of the sprintf() function.
 */

PGM_GNUC_INTERNAL
int
pgm_printf_string_upper_bound (
	const char*	format,
	va_list		args
	)
{
/* MinGW family supports vsnprintf and so limit platform separation to MSVC. */
#ifdef _MSC_VER
	return _vscprintf (format, args) + 1;
#else
	char c;
	return vsnprintf (&c, 1, format, args) + 1;
#endif
}

/* memory must be freed with pgm_free()
 */

PGM_GNUC_INTERNAL
int
pgm_vasprintf (
	char**	    restrict string,
	const char* restrict format,
	va_list		     args
	)
{
	int len;

	pgm_return_val_if_fail (string != NULL, -1);

#ifdef HAVE_VASPRINTF
	char *strp;
	len = vasprintf (&strp, format, args);
	if (len < 0) {
		*string = NULL;
	} else {
		*string = pgm_strdup (strp);
		free (strp);
	}
#else
#	ifdef _MSC_VER
/* can only copy on assignment, pointer to stack frame */
	va_list args2 = args;
#	else
	va_list args2;
	va_copy (args2, args);
#	endif
	len = pgm_printf_string_upper_bound (format, args);
	*string = pgm_malloc (len);
	len = pgm_vsnprintf_s (*string, len, _TRUNCATE, format, args2);
	va_end (args2);
#endif
	return len;
}

PGM_GNUC_INTERNAL
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

#ifdef HAVE_STPCPY
	return stpcpy (dest, src);
#else
	char		*d = dest;
	const char	*s = src;

	do {
		*d++ = *s;
	} while (*s++ != '\0');
	return d - 1;
#endif
}

PGM_GNUC_INTERNAL
char*
pgm_strconcat (
	const char*	src,
	...
	)
{
	size_t	 len;     
	va_list	 args;
	char	*dest, *s, *to;

	if (!src)
		return NULL;

	len = 1 + strlen (src);
	va_start (args, src);
	s = va_arg (args, char*);
	while (s) {
		len += strlen (s);
		s = va_arg (args, char*);
	}
	va_end (args);

	dest = pgm_malloc (len);

	to = pgm_stpcpy (dest, src);
	va_start (args, src);
	s = va_arg (args, char*);
	while (s) {
		to = pgm_stpcpy (to, s);
		s = va_arg (args, char*);
	}
	va_end (args);

	return dest;
}

/* Split a string with delimiter, result must be freed with pgm_strfreev().
 */

PGM_GNUC_INTERNAL
char**
pgm_strsplit (
	const char* restrict string,
	const char* restrict delimiter,
	int		     max_tokens
	)
{
	pgm_slist_t	 *string_list = NULL, *slist;
	char		**str_array, *s;
	unsigned	  n = 0;
	const char	 *remainder;

	pgm_return_val_if_fail (string != NULL, NULL);
	pgm_return_val_if_fail (delimiter != NULL, NULL);
	pgm_return_val_if_fail (delimiter[0] != '\0', NULL);

	if (max_tokens < 1)
		max_tokens = INT_MAX;

	remainder = string;
	s = strstr (remainder, delimiter);
	if (s)
	{
		const size_t delimiter_len = strlen (delimiter);   

		while (--max_tokens && s)
		{
			const size_t len = s - remainder;
			char* new_string = pgm_malloc (len + 1);

			pgm_strncpy_s (new_string, len + 1, remainder, len);
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

PGM_GNUC_INTERNAL
void
pgm_strfreev (
	char**		str_array
	)
{
	if (PGM_LIKELY (NULL != str_array))
	{
		for (unsigned i = 0; str_array[i] != NULL; i++)
			pgm_free (str_array[i]);

		pgm_free (str_array);
	}
}

/* resize dynamic string
 */

static
void
pgm_string_maybe_expand (
	pgm_string_t*	string,
	size_t		len
	)
{
	if ((string->len + len) >= string->allocated_len)
	{
		string->allocated_len = pgm_nearest_power (1, string->len + len + 1);
		string->str	      = pgm_realloc (string->str, string->allocated_len);
	}
}

/* val may not be a part of string
 */

static
pgm_string_t*
pgm_string_insert_len (
	pgm_string_t* restrict string,
	ssize_t		       pos,
	const char*   restrict val,
	ssize_t		       len
	)
{
	pgm_return_val_if_fail (NULL != string, NULL);
	pgm_return_val_if_fail (NULL != val, string);

	if (len < 0)
		len = strlen (val);

	if (pos < 0)
		pos = string->len;
	else
		pgm_return_val_if_fail ((size_t)pos <= string->len, string);

	pgm_string_maybe_expand (string, len);
		
	if ((size_t)pos < string->len)
		memmove (string->str + pos + len, string->str + pos, string->len - pos);

	if (len == 1)
		string->str[pos] = *val;
	else
		memcpy (string->str + pos, val, len);
	string->len += len;
	string->str[string->len] = 0;
	return string;
}

static
pgm_string_t*
pgm_string_insert_c (
	pgm_string_t*	string,
	ssize_t		pos,
	char		c
	)
{
	pgm_return_val_if_fail (NULL != string, NULL);

	if (pos < 0)
		pos = string->len;
	else
		pgm_return_val_if_fail ((size_t)pos <= string->len, string);

	pgm_string_maybe_expand (string, 1);

	if ((size_t)pos < string->len)
		memmove (string->str + pos + 1, string->str + pos, string->len - pos);

	string->str[pos] = c;
	string->len ++;
	string->str[string->len] = '\0';
	return string;
}

static
pgm_string_t*
pgm_string_append_len (
	pgm_string_t* restrict string,
	const char*   restrict val,
	size_t		       len
	)
{
	pgm_return_val_if_fail (NULL != string, NULL);
	pgm_return_val_if_fail (NULL != val, string);

	return pgm_string_insert_len (string, -1, val, len);
}

/* create new dynamic string
 */

static
pgm_string_t*
pgm_string_sized_new (
	size_t		init_size
	)
{
	pgm_string_t* string;

	string			= pgm_new (pgm_string_t, 1);
	string->allocated_len	= 0;
	string->len		= 0;
	string->str		= NULL;
	pgm_string_maybe_expand (string, MAX(init_size, 2));
	string->str[0]		= '\0';
	return string;
}

PGM_GNUC_INTERNAL
pgm_string_t*
pgm_string_new (
	const char*	init
	)
{
	pgm_string_t* string;

	if (NULL == init || '\0' == *init)
		string = pgm_string_sized_new (2);
	else
	{
		const size_t len = strlen (init);
		string = pgm_string_sized_new (len + 2);
		pgm_string_append_len (string, init, len);
	}
	return string;
}

/* free dynamic string, optionally just the wrapper object
 */

PGM_GNUC_INTERNAL
char*
pgm_string_free (
	pgm_string_t*	string,
	bool		free_segment
	)
{
	char* segment;

	pgm_return_val_if_fail (NULL != string, NULL);

	if (free_segment) {
		pgm_free (string->str);
		segment = NULL;
	} else
		segment = string->str;

	pgm_free (string);
	return segment;
}

static
pgm_string_t*
pgm_string_truncate (
	pgm_string_t* restrict string,
	size_t		       len
	)
{
	pgm_return_val_if_fail (NULL != string, NULL);

	string->len = MIN (len, string->len);
	string->str[ string->len ] = '\0';

	return string;
}

PGM_GNUC_INTERNAL
pgm_string_t*
pgm_string_append (
	pgm_string_t* restrict string,
	const char*   restrict val
	)
{
	pgm_return_val_if_fail (NULL != string, NULL);
	pgm_return_val_if_fail (NULL != val, string);

	return pgm_string_insert_len (string, -1, val, -1);
}

PGM_GNUC_INTERNAL
pgm_string_t*
pgm_string_append_c (
	pgm_string_t*	string,
	char		c
	)
{
	pgm_return_val_if_fail (NULL != string, NULL);

	return pgm_string_insert_c (string, -1, c);
}

static void pgm_string_append_vprintf (pgm_string_t*restrict, const char*restrict, va_list) PGM_GNUC_PRINTF(2, 0);

static
void
pgm_string_append_vprintf (
	pgm_string_t* restrict string,
	const char*   restrict format,
	va_list		       args
	)
{
	char	*buf;
	int	 len;

	pgm_return_if_fail (NULL != string);
	pgm_return_if_fail (NULL != format);

	len = pgm_vasprintf (&buf, format, args);
	if (len >= 0) {
		pgm_string_maybe_expand (string, len);
		memcpy (string->str + string->len, buf, len + 1);
		string->len += len;
		pgm_free (buf);
	}
}

PGM_GNUC_INTERNAL
void
pgm_string_printf (
	pgm_string_t* restrict string,
	const char*   restrict format,
	...
	)
{
	va_list args;

	pgm_string_truncate (string, 0);

	va_start (args, format);
	pgm_string_append_vprintf (string, format, args);
	va_end (args);
}

PGM_GNUC_INTERNAL
void
pgm_string_append_printf (
	pgm_string_t* restrict string,
	const char*   restrict format,
	...
	)
{
	va_list args;

	va_start (args, format);
	pgm_string_append_vprintf (string, format, args);
	va_end (args);
}

/* eof */
