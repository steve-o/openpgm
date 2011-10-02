/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Portable advanced error reporting.  In addition to a basic error code
 * this module provides a domain code and a textual description of the error.
 * Text is localised per gettext() configuration and hence is generally
 * limited to one language per process and an application restart is required
 * to read updated catalogue translations.
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
#ifndef _WIN32
#	include <netdb.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>


//#define ERROR_DEBUG


#define ERROR_OVERWRITTEN_WARNING "pgm_error_t set over the top of a previous pgm_error_t or uninitialized memory.\n" \
               "This indicates a bug. You must ensure an error is NULL before it's set.\n" \
               "The overwriting error message was: %s"

static pgm_error_t* pgm_error_new_valist (const int, const int, const char*, va_list) PGM_GNUC_PRINTF(3, 0);
static void pgm_error_add_prefix (char**restrict, const char*restrict, va_list) PGM_GNUC_PRINTF(2, 0);


static
pgm_error_t*
pgm_error_new_valist (
	const int		error_domain,
	const int		error_code,
	const char*		format,
	va_list			args
	)
{
	pgm_error_t *error = pgm_new (pgm_error_t, 1);
	error->domain  = error_domain;
	error->code    = error_code;
	error->message = pgm_strdup_vprintf (format, args);
	return error;
}

void
pgm_error_free (
	pgm_error_t*	error
	)
{
	pgm_return_if_fail (error != NULL);
	pgm_free (error->message);
	pgm_free (error);
}

void
pgm_set_error (
	pgm_error_t** restrict	err,
	const int		error_domain,
	const int		error_code,
	const char*   restrict	format,
	...
	)
{
	pgm_error_t *new_err;
	va_list args;

	if (NULL == err)
		return;

	va_start (args, format);
	new_err = pgm_error_new_valist (error_domain, error_code, format, args);
	va_end (args);

	if (NULL == *err)
		*err = new_err;
	else
		pgm_warn (_(ERROR_OVERWRITTEN_WARNING), new_err->message); 
}

void
pgm_propagate_error (
	pgm_error_t** restrict	dest,
	pgm_error_t*  restrict	src
	)
{
	pgm_return_if_fail (src != NULL);
 
	if (NULL == dest) {
		if (src)
			pgm_error_free (src);
		return;
	} else {
		if (NULL != *dest) {
			pgm_warn (_(ERROR_OVERWRITTEN_WARNING), src->message);
		} else {
			*dest = src;
		}
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

static
void
pgm_error_add_prefix (
	char**	    restrict	string,
	const char* restrict	format,
	va_list			ap
	)
{
	char* prefix = pgm_strdup_vprintf (format, ap);
	char* oldstring = *string;
	*string = pgm_strconcat (prefix, oldstring, NULL);
	pgm_free (oldstring);
	pgm_free (prefix);
}

void
pgm_prefix_error (
	pgm_error_t** restrict	err,
	const char*   restrict	format,
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

/* error from libc.
 */

int
pgm_error_from_errno (
	const int	from_errno
	)
{
	switch (from_errno) {
#ifdef EAFNOSUPPORT
	case EAFNOSUPPORT:
		return PGM_ERROR_AFNOSUPPORT;
		break;
#endif

#ifdef EAGAIN
	case EAGAIN:
		return PGM_ERROR_AGAIN;
		break;
#endif

#ifdef EBADF
	case EBADF:
		return PGM_ERROR_BADF;
		break;
#endif

#ifdef ECONNRESET
	case ECONNRESET:
		return PGM_ERROR_CONNRESET;
		break;
#endif

#ifdef EFAULT
	case EFAULT:
		return PGM_ERROR_FAULT;
		break;
#endif

#ifdef EINTR
	case EINTR:
		return PGM_ERROR_INTR;
		break;
#endif

#ifdef EINVAL
	case EINVAL:
		return PGM_ERROR_INVAL;
		break;
#endif

#ifdef EMFILE
	case EMFILE:
		return PGM_ERROR_MFILE;
		break;
#endif

#ifdef ENFILE
	case ENFILE:
		return PGM_ERROR_NFILE;
		break;
#endif

#ifdef ENODEV
	case ENODEV:
		return PGM_ERROR_NODEV;
		break;
#endif

#ifdef ENOENT
	case ENOENT:
		return PGM_ERROR_NOENT;
		break;
#endif

#ifdef ENOMEM
	case ENOMEM:
		return PGM_ERROR_NOMEM;
		break;
#endif

#ifdef ENONET
	case ENONET:
		return PGM_ERROR_NONET;
		break;
#endif

#ifdef ENOPROTOOPT
	case ENOPROTOOPT:
		return PGM_ERROR_NOPROTOOPT;
		break;
#endif

#ifdef ENOTUNIQ
	case ENOTUNIQ:
		return PGM_ERROR_NOTUNIQ;
		break;
#endif

#ifdef ENXIO
	case ENXIO:
		return PGM_ERROR_NXIO;
		break;
#endif

#ifdef EPERM
	case EPERM:
		return PGM_ERROR_PERM;
		break;
#endif

#ifdef EPROTO
	case EPROTO:
		return PGM_ERROR_PROTO;
		break;
#endif

#ifdef ERANGE
	case ERANGE:
		return PGM_ERROR_RANGE;
		break;
#endif

#ifdef EXDEV
	case EXDEV:
		return PGM_ERROR_XDEV;
		break;
#endif

	default :
		return PGM_ERROR_FAILED;
		break;
	}
}

/* h_errno from gethostbyname.
 */

int
pgm_error_from_h_errno (
	const int	from_h_errno
	)
{
	switch (from_h_errno) {
#ifdef HOST_NOT_FOUND
	case HOST_NOT_FOUND:
		return PGM_ERROR_NONAME;
		break;
#endif

#ifdef TRY_AGAIN
	case TRY_AGAIN:
		return PGM_ERROR_AGAIN;
		break;
#endif

#ifdef NO_RECOVERY
	case NO_RECOVERY:
		return PGM_ERROR_FAIL;
		break;
#endif

#ifdef NO_DATA
	case NO_DATA:
		return PGM_ERROR_NODATA;
		break;
#endif

	default:
		return PGM_ERROR_FAILED;
		break;
	}
}

/* errno must be preserved before calling to catch correct error
 * status with EAI_SYSTEM.
 */

int
pgm_error_from_eai_errno (
	const int	from_eai_errno,
#ifdef EAI_SYSTEM
	const int	from_errno
#else
	PGM_GNUC_UNUSED const int from_errno
#endif
	)
{
	switch (from_eai_errno) {
#ifdef EAI_ADDRFAMILY
	case EAI_ADDRFAMILY:
		return PGM_ERROR_ADDRFAMILY;
		break;
#endif

#ifdef EAI_AGAIN
	case EAI_AGAIN:
		return PGM_ERROR_AGAIN;
		break;
#endif

#ifdef EAI_BADFLAGS
	case EAI_BADFLAGS:
		return PGM_ERROR_INVAL;
		break;
#endif

#ifdef EAI_FAIL
	case EAI_FAIL:
		return PGM_ERROR_FAIL;
		break;
#endif

#ifdef EAI_FAMILY
	case EAI_FAMILY:
		return PGM_ERROR_AFNOSUPPORT;
		break;
#endif

#ifdef EAI_MEMORY
	case EAI_MEMORY:
		return PGM_ERROR_NOMEM;
		break;
#endif

#ifdef EAI_NODATA
	case EAI_NODATA:
		return PGM_ERROR_NODATA;
		break;
#endif

#if defined(EAI_NONAME) && EAI_NONAME != EAI_NODATA
	case EAI_NONAME:
		return PGM_ERROR_NONAME;
		break;
#endif

#ifdef EAI_SERVICE
	case EAI_SERVICE:
		return PGM_ERROR_SERVICE;
		break;
#endif

#ifdef EAI_SOCKTYPE
	case EAI_SOCKTYPE:
		return PGM_ERROR_SOCKTNOSUPPORT;
		break;
#endif

#ifdef EAI_SYSTEM
	case EAI_SYSTEM:
		return pgm_error_from_errno (from_errno);
		break;
#endif

	default :
		return PGM_ERROR_FAILED;
		break;
	}
}

/* from WSAGetLastError()
 */

int
pgm_error_from_wsa_errno (
	const int	from_wsa_errno
        )
{
	switch (from_wsa_errno) {
#ifdef WSAEINVAL
	case WSAEINVAL:
		return PGM_ERROR_INVAL;
		break;
#endif
#ifdef WSAEMFILE
	case WSAEMFILE:
		return PGM_ERROR_MFILE;
		break;
#endif
#ifdef WSA_NOT_ENOUGH_MEMORY
	case WSA_NOT_ENOUGH_MEMORY:
		return PGM_ERROR_NOMEM;
		break;
#endif
#ifdef WSAENOPROTOOPT
	case WSAENOPROTOOPT:
		return PGM_ERROR_NOPROTOOPT;
		break;
#endif
#ifdef WSAECONNRESET
	case WSAECONNRESET:
		return PGM_ERROR_CONNRESET;
		break;
#endif

	default :
		return PGM_ERROR_FAILED;
		break;
	}
}

/* from Last-Error codes, i.e. Windows non-WinSock and non-DOS errors.
 */

int
pgm_error_from_win_errno (
	const int	from_win_errno
        )
{
	switch (from_win_errno) {
#ifdef ERROR_ADDRESS_NOT_ASSOCIATED
	case ERROR_ADDRESS_NOT_ASSOCIATED:
		return PGM_ERROR_NODATA;
		break;
#endif

#ifdef ERROR_BUFFER_OVERFLOW
	case ERROR_BUFFER_OVERFLOW:
		return PGM_ERROR_NOBUFS;
		break;
#endif

#ifdef ERROR_INVALID_DATA
	case ERROR_INVALID_DATA:
		return PGM_ERROR_BADE;
		break;
#endif

#ifdef ERROR_INSUFFICIENT_BUFFER
	case ERROR_INSUFFICIENT_BUFFER:
		return PGM_ERROR_NOMEM;
		break;
#endif

#ifdef ERROR_INVALID_PARAMETER
	case ERROR_INVALID_PARAMETER:
		return PGM_ERROR_INVAL;
		break;
#endif

#ifdef ERROR_NOT_ENOUGH_MEMORY
	case ERROR_NOT_ENOUGH_MEMORY:
		return PGM_ERROR_NOMEM;
		break;
#endif

#ifdef ERROR_NO_DATA
	case ERROR_NO_DATA:
		return PGM_ERROR_NODATA;
		break;
#endif

#ifdef ERROR_NOT_SUPPORTED
	case ERROR_NOT_SUPPORTED:
		return PGM_ERROR_NOSYS;
		break;
#endif

	default :
		return PGM_ERROR_FAILED;
		break;
	}
}

/* eof */
