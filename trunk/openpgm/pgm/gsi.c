/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * global session ID helper functions.
 *
 * Copyright (c) 2006-2011 Miru Limited.
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
#include <errno.h>
#include <stdio.h>
#ifndef _WIN32
#	include <netdb.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>


//#define GSI_DEBUG


/* create a GSI based on md5 of a user provided data block.
 *
 * returns TRUE on success, returns FALSE on error and sets error appropriately,
 */

bool
pgm_gsi_create_from_data (
	pgm_gsi_t*     restrict	gsi,
	const uint8_t* restrict	data,
	const size_t		length
	)
{
	pgm_return_val_if_fail (NULL != gsi, FALSE);
	pgm_return_val_if_fail (NULL != data, FALSE);
	pgm_return_val_if_fail (length > 1, FALSE);

	struct pgm_md5_t ctx;
	char resblock[16];
	pgm_md5_init_ctx (&ctx);
	pgm_md5_process_bytes (&ctx, data, length);
	pgm_md5_finish_ctx (&ctx, resblock);
	memcpy (gsi, resblock + 10, 6);
	return TRUE;
}

bool
pgm_gsi_create_from_string (
	pgm_gsi_t*  restrict gsi,
	const char* restrict str,
	ssize_t	     	     length		/* -1 for NULL terminated */
	)
{
	pgm_return_val_if_fail (NULL != gsi, FALSE);
	pgm_return_val_if_fail (NULL != str, FALSE);

	if (length < 0)
		length = strlen (str);

	return pgm_gsi_create_from_data (gsi, (const uint8_t*)str, length);
}

/* create a global session ID as recommended by the PGM draft specification using
 * low order 48 bits of md5 of the hostname.
 *
 * returns TRUE on success, returns FALSE on error and sets error appropriately,
 */

bool
pgm_gsi_create_from_hostname (
	pgm_gsi_t*    restrict gsi,
	pgm_error_t** restrict error
	)
{
	pgm_return_val_if_fail (NULL != gsi, FALSE);

/* POSIX gethostname silently fails if the buffer is too short.  We use NI_MAXHOST
 * as the highest common denominator, at 1025 bytes including terminating null byte.
 *
 * WinSock namespace providers have a 256 byte limit (MSDN), DNS names are limited to
 * 63 bytes per component, and 15 bytes for NetBIOS names (MAX_COMPUTERNAME_LENGTH).
 *   http://msdn.microsoft.com/en-us/library/ms738527(v=VS.85).aspx
 *   http://msdn.microsoft.com/en-us/library/ms724220(VS.85).aspx
 * SUSv2 guarantees 255 bytes (excluding terminating null byte).
 * POSIX.1-2001 guarantees HOST_NAME_MAX, on Linux is defined to 64.
 */
	char hostname[NI_MAXHOST];
	int retval = gethostname (hostname, sizeof (hostname));
	if (0 != retval) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_sock_errno (save_errno),
			     _("Resolving hostname: %s"),
			     pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno)
				);
		return FALSE;
	}

/* force a trailing null byte */
	hostname[NI_MAXHOST - 1] = '\0';

	return pgm_gsi_create_from_string (gsi, hostname, -1);
}

/* create a global session ID based on the IP address.
 *
 * returns TRUE on succcess, returns FALSE on error and sets error.
 */

bool
pgm_gsi_create_from_addr (
	pgm_gsi_t*    restrict gsi,
	pgm_error_t** restrict error
	)
{
	char hostname[NI_MAXHOST];
	struct addrinfo hints, *res = NULL;

	pgm_return_val_if_fail (NULL != gsi, FALSE);

	int retval = gethostname (hostname, sizeof(hostname));
	if (0 != retval) {
		const int save_errno = pgm_get_last_sock_error();
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_sock_errno (save_errno),
			     _("Resolving hostname: %s"),
			     pgm_sock_strerror_s (errbuf, sizeof (errbuf), save_errno)
				);
		return FALSE;
	}
	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_ADDRCONFIG;
	retval = getaddrinfo (hostname, NULL, &hints, &res);
	if (0 != retval) {
		char errbuf[1024];
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_IF,
			     pgm_error_from_eai_errno (retval, errno),
			     _("Resolving hostname address: %s"),
			     pgm_gai_strerror_s (errbuf, sizeof (errbuf), retval));
		return FALSE;
	}
/* NB: getaddrinfo may return multiple addresses, one per interface & family, only the first
 * return result is used.  The sorting order of the list defined by RFC 3484 and /etc/gai.conf
 */
	memcpy (gsi, &((struct sockaddr_in*)(res->ai_addr))->sin_addr, sizeof(struct in_addr));
	freeaddrinfo (res);
	const uint16_t random_val = pgm_random_int_range (0, UINT16_MAX);
	memcpy ((uint8_t*)gsi + sizeof(struct in_addr), &random_val, sizeof(random_val));
	return TRUE;
}

/* re-entrant form of pgm_gsi_print()
 *
 * returns number of bytes written to buffer on success, returns -1 on
 * invalid parameters.
 */

int
pgm_gsi_print_r (
	const pgm_gsi_t* restrict gsi,
	char*	         restrict buf,
	const size_t		  bufsize
	)
{
	const uint8_t* restrict src = (const uint8_t* restrict)gsi;

	pgm_return_val_if_fail (NULL != gsi, -1);
	pgm_return_val_if_fail (NULL != buf, -1);
	pgm_return_val_if_fail (bufsize > 0, -1);

	return pgm_snprintf_s (buf, bufsize, _TRUNCATE, "%u.%u.%u.%u.%u.%u",
				src[0], src[1], src[2], src[3], src[4], src[5]);
}

/* transform GSI to ASCII string form.
 *
 * on success, returns pointer to ASCII string.  on error, returns NULL.
 */

char*
pgm_gsi_print (
	const pgm_gsi_t*	gsi
	)
{
	static char buf[PGM_GSISTRLEN];

	pgm_return_val_if_fail (NULL != gsi, NULL);

	pgm_gsi_print_r (gsi, buf, sizeof(buf));
	return buf;
}

/* compare two global session identifier GSI values and return TRUE if they are equal
 */

bool
pgm_gsi_equal (
        const void* restrict	p1,
        const void* restrict	p2
        )
{
#ifdef __cplusplus
	union {
		pgm_gsi_t	gsi;
		uint16_t	s[3];
	} _u1, _u2, *u1 = &_u1, *u2 = &_u2;
	memcpy (&_u1.gsi, p1, sizeof (pgm_gsi_t));
	memcpy (&_u2.gsi, p2, sizeof (pgm_gsi_t));
#else
	const union {
		pgm_gsi_t	gsi;
		uint16_t	s[3];
	} *u1 = p1, *u2 = p2;
#endif

/* pre-conditions */
	pgm_assert (NULL != p1);
	pgm_assert (NULL != p2);

	return (u1->s[0] == u2->s[0] && u1->s[1] == u2->s[1] && u1->s[2] == u2->s[2]);
}

/* eof */
