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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_ERROR_H__
#define __PGM_ERROR_H__

typedef struct pgm_error_t pgm_error_t;

#include <pgm/types.h>

PGM_BEGIN_DECLS

/* error domains */
enum
{
	PGM_ERROR_DOMAIN_IF,		/* interface and host */
	PGM_ERROR_DOMAIN_PACKET,
	PGM_ERROR_DOMAIN_RECV,
	PGM_ERROR_DOMAIN_TIME,
	PGM_ERROR_DOMAIN_SOCKET,
	PGM_ERROR_DOMAIN_ENGINE,
	PGM_ERROR_DOMAIN_HTTP,
	PGM_ERROR_DOMAIN_SNMP
};

/* error codes */
enum
{
	/* Derived from errno, eai_errno, etc */
	PGM_ERROR_ADDRFAMILY,		/* EAI_ADDRFAMILY */
	PGM_ERROR_AFNOSUPPORT,		/* EAI_FAMILY */
	PGM_ERROR_AGAIN,
	PGM_ERROR_BADE,			/* ERROR_INVALID_DATA */
	PGM_ERROR_BADF,
	PGM_ERROR_BOUNDS,		/* sequence out-of-bounds */
	PGM_ERROR_CKSUM,		/* pkt cksum incorrect */
	PGM_ERROR_CONNRESET,
	PGM_ERROR_FAIL,			/* EAI_FAIL */
	PGM_ERROR_FAULT,
	PGM_ERROR_INPROGRESS,		/* WSAEINPROGRESS */
	PGM_ERROR_INTR,
	PGM_ERROR_INVAL,
	PGM_ERROR_MFILE,
	PGM_ERROR_NFILE,
	PGM_ERROR_NOBUFS,		/* ERROR_BUFFER_OVERFLOW */
	PGM_ERROR_NODATA,		/* EAI_NODATA */
	PGM_ERROR_NODEV,
	PGM_ERROR_NOENT,
	PGM_ERROR_NOMEM,
	PGM_ERROR_NONAME,		/* EAI_NONAME */
	PGM_ERROR_NONET,
	PGM_ERROR_NOPROTOOPT,
	PGM_ERROR_NOSYS,		/* ERROR_NOT_SUPPORTED */
	PGM_ERROR_NOTUNIQ,
	PGM_ERROR_NXIO,
	PGM_ERROR_PERM,
	PGM_ERROR_PROCLIM,		/* WSAEPROCLIM */
	PGM_ERROR_PROTO,
	PGM_ERROR_RANGE,
	PGM_ERROR_SERVICE,		/* EAI_SERVICE */
	PGM_ERROR_SOCKTNOSUPPORT,	/* EAI_SOCKTYPE */
	PGM_ERROR_SYSNOTAREADY,		/* WSASYSNOTAREADY */
	PGM_ERROR_SYSTEM,		/* EAI_SYSTEM */
	PGM_ERROR_VERNOTSUPPORTED,	/* WSAVERNOTSUPPORTED */
	PGM_ERROR_XDEV,

	PGM_ERROR_FAILED		/* generic error */
};

struct pgm_error_t
{
	int		domain;
	int		code;
	char*		message;
};

void pgm_error_free (pgm_error_t*);
void pgm_set_error (pgm_error_t**restrict, const int, const int, const char*restrict, ...) PGM_GNUC_PRINTF (4, 5);
void pgm_propagate_error (pgm_error_t**restrict, pgm_error_t*restrict);
void pgm_clear_error (pgm_error_t**);
void pgm_prefix_error (pgm_error_t**restrict, const char*restrict, ...) PGM_GNUC_PRINTF (2, 3);

int pgm_error_from_errno (const int) PGM_GNUC_CONST;
int pgm_error_from_h_errno (const int) PGM_GNUC_CONST;
int pgm_error_from_eai_errno (const int, const int) PGM_GNUC_CONST;
int pgm_error_from_wsa_errno (const int) PGM_GNUC_CONST;
int pgm_error_from_win_errno (const int) PGM_GNUC_CONST;

PGM_END_DECLS

#endif /* __PGM_ERROR_H__ */
