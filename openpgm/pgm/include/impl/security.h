/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable security-enhanced CRT functions.
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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#ifndef __PGM_IMPL_SECURITY_H__
#define __PGM_IMPL_SECURITY_H__

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <impl/i18n.h>
#include <impl/string.h>

PGM_BEGIN_DECLS

#ifdef CONFIG_HAVE_FTIME
static inline
errno_t
#	ifndef _WIN32
pgm_ftime_s (struct timeb *timeptr)
#	elif !defined( _MSC_VER )
pgm_ftime_s (struct _timeb *timeptr)
#	else
pgm_ftime_s (struct __timeb64 *timeptr)
#	endif
{
#	ifndef _WIN32
	return ftime (timeptr);
#	elif !defined( _MSC_VER )
	_ftime (timeptr);
	return 0;
#	elif  defined( CONFIG_HAVE_SECURITY_ENHANCED_CRT )
	return _ftime64_s (timeptr);
#	else
	_ftime64 (timeptr);
	return 0;
#	endif
}
#endif /* CONFIG_HAVE_FTIME */

#ifndef _TRUNCATE
#	define _TRUNCATE	(size_t)-1
#endif

static inline
errno_t
pgm_strncpy_s (char *dest, size_t size, const char *src, size_t count)
{
#ifndef CONFIG_HAVE_SECURITY_ENHANCED_CRT
	if (_TRUNCATE == count) {
		strncpy (dest, src, size);
		if (size > 0)
			dest[size - 1] = 0;
		return 0;
	}
	strncpy (dest, src, count + 1);
	dest[count] = 0;
	return 0;
#else
	return strncpy_s (dest, size, src, count);
#endif
}

static inline int pgm_vsnprintf_s (char*, size_t, size_t, const char*, va_list) PGM_GNUC_PRINTF(4, 0);

static inline
int
pgm_vsnprintf_s (char *str, size_t size, size_t count, const char *format, va_list ap)
{
#ifndef _WIN32
	if (_TRUNCATE == count) {
		const int retval = vsnprintf (str, size, format, ap);
		if (size > 0)
			str[size - 1] = 0;
		return retval;
	}
	const int retval = vsnprintf (str, count + 1, format, ap);
	str[count] = 0;
	return retval;
#elif !defined( CONFIG_HAVE_SECURITY_ENHANCED_CRT )
	if (_TRUNCATE == count) {
		const int retval = _vsnprintf (str, size, format, ap);
		if (size > 0)
			str[size - 1] = 0;
		return retval;
	}
	const int retval = _vsnprintf (str, count + 1, format, ap);
	str[count] = 0;
	return retval;
#else
	return _vsnprintf_s (str, size, count, format, ap);
#endif
}

#ifndef CONFIG_HAVE_SECURITY_ENHANCED_CRT
static inline int pgm_snprintf_s (char*, size_t, size_t, const char*, ...) PGM_GNUC_PRINTF(4, 5);
static inline int pgm_sscanf_s (const char*, const char*, ...) PGM_GNUC_SCANF(2, 3);

static inline
int
pgm_snprintf_s (char *str, size_t size, size_t count, const char *format, ...)
{
	va_list ap;
	int retval;

	va_start (ap, format);
	retval = pgm_vsnprintf_s (str, size, count, format, ap);
	va_end (ap);
	return retval;
}

static inline
int
pgm_sscanf_s (const char *buffer, const char *format, ...)
{
	va_list ap;
	int retval;

	va_start (ap, format);
	retval = vsscanf (buffer, format, ap);
	va_end (ap);
	return retval;
}
#else
#	define pgm_snprintf_s		_snprintf_s
#	define pgm_sscanf_s		sscanf_s
#endif /* CONFIG_HAVE_SECURITY_ENHANCED_CRT */

static inline
char*
pgm_strerror_s (char *buffer, size_t size, int errnum)
{
#ifdef CONFIG_HAVE_SECURITY_ENHANCED_CRT
	if (0 != strerror_s (buffer, size, errnum))
		pgm_snprintf_s (buffer, size, _TRUNCATE, _("Unknown error %d"), errnum);
	return buffer;
#elif defined( _WIN32 )
	pgm_strncpy_s (buffer, size, strerror (errnum), _TRUNCATE);
	return buffer;
#elif (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !defined( _GNU_SOURCE )
/* XSI-compliant */
	if (0 != strerror_r (errnum, buffer, size))
		pgm_snprintf_s (buffer, size, _TRUNCATE, _("Unknown error %d"), errnum);
	return buffer;
#else
/* GNU-specific, failure is noted within buffer contents */
	return strerror_r (errnum, buffer, size);
#endif
}

/* Security-only APIs */

static inline
errno_t
pgm_dupenv_s (char **buffer, size_t *size, const char* name)
{
#ifndef CONFIG_HAVE_SECURITY_ENHANCED_CRT
	const char *val = getenv (name);
/* not found */
	if (NULL == val) {
		*buffer = NULL;
		*size = 0;
		return 0;
	}
	*buffer = pgm_strdup (val);
/* out of memory */
	if (NULL == *buffer) {
		*buffer = NULL;
		*size = 0;
		return errno;	/* ENOMEM */
	}
	*size = strlen (*buffer) + 1;
	return 0;
#else
	return _dupenv_s (buffer, count, name);
#endif
}

/* Win32 specific APIs */

#ifdef _WIN32
static inline
errno_t
pgm_wcstombs_s (size_t *retval, char *dest, size_t size, const wchar_t *src, size_t count)
{
#	ifndef CONFIG_HAVE_SECURITY_ENHANCED_CRT
	size_t characters;
	if (_TRUNCATE == count) {
		characters = wcstombs (dest, src, size);
/* may invalidate last multi-byte character */
		if (size > 0)
			dest[size - 1] = 0;
	} else {
		characters = wcstombs (dest, src, count + 1);
		dest[count] = 0;
	}
	if ((size_t)-1 == characters) {
		*retval = 0;
		return errno;
	}
	*retval = characters;
	return 0;
#	else
	return wcstombs_s (retval, dest, size, src, count);
#	endif
}

#endif /* _WIN32 */

PGM_END_DECLS

#endif /* __PGM_IMPL_SECURITY_H__ */
