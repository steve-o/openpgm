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

#ifdef CONFIG_HAVE_VASPRINTF
#	define _GNU_SOURCE
#endif
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>	/* _GNU_SOURCE for vasprintf */
#include <string.h>
#include <pgm/framework.h>


//#define STRING_DEBUG

/* Return copy of string, must be freed with pgm_free().
 */

char*
pgm_strdup (
	const char*	str
	)
{
	char* new_str;
	size_t length;

	if (PGM_LIKELY (NULL != str))
	{
		length = strlen (str) + 1;
		new_str = malloc (length);
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

/* memory must be freed with free()
 */

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
	va_copy (args2, args);
	*string = malloc (pgm_printf_string_upper_bound (format, args));
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
	s = va_arg (args, char*);
	while (s) {
		l += strlen (s);
		s = va_arg (args, char*);
	}
	va_end (args);

	concat = malloc (l);
	ptr = concat;

	ptr = pgm_stpcpy (ptr, string1);
	va_start (args, string1);
	s = va_arg (args, char*);
	while (s) {
		ptr = pgm_stpcpy (ptr, s);
		s = va_arg (args, char*);
	}
	va_end (args);

	return concat;
}

/* Split a string with delimiter, result must be freed with pgm_strfreev().
 */

char**
pgm_strsplit (
	const char* restrict string,
	const char* restrict delimiter,
	int		     max_tokens
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
		max_tokens = INT_MAX;

	remainder = string;
	s = strstr (remainder, delimiter);
	if (s)
	{
		const size_t delimiter_len = strlen (delimiter);   

		while (--max_tokens && s)
		{
			const size_t len = s - remainder;
			char *new_string = malloc (len + 1);
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
	if (PGM_LIKELY (NULL != str_array))
	{
		for (unsigned i = 0; str_array[i] != NULL; i++)
			free (str_array[i]);

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
	pgm_string_t*	string,
	const char*	val,
	size_t		len
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
	pgm_string_t* string = pgm_new (pgm_string_t, 1);
	string->allocated_len	= 0;
	string->len		= 0;
	string->str		= NULL;
	pgm_string_maybe_expand (string, MAX(init_size, 2));
	string->str[0]		= '\0';
	return string;
}

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
	char *buf;
	int len;

	pgm_return_if_fail (NULL != string);
	pgm_return_if_fail (NULL != format);

	len = pgm_vasprintf (&buf, format, args);
	if (len >= 0) {
		pgm_string_maybe_expand (string, len);
		memcpy (string->str + string->len, buf, len + 1);
		string->len += len;
		free (buf);
	}
}

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
