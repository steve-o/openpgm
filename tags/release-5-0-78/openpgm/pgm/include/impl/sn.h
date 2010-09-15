/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * serial number arithmetic: rfc 1982
 *
 * Copyright (c) 2006-2007 Miru Limited.
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

#ifndef __PGM_IMPL_SN_H__
#define __PGM_IMPL_SN_H__

#include <pgm/types.h>
#include <impl/messages.h>

PGM_BEGIN_DECLS

#define PGM_UINT32_SIGN_BIT		(1UL<<31)
#define PGM_UINT64_SIGN_BIT		(1ULL<<63)

/* declare for GCC attributes */
static inline bool pgm_uint32_lt  (const uint32_t, const uint32_t) PGM_GNUC_CONST;
static inline bool pgm_uint32_lte (const uint32_t, const uint32_t) PGM_GNUC_CONST;
static inline bool pgm_uint32_gt  (const uint32_t, const uint32_t) PGM_GNUC_CONST;
static inline bool pgm_uint32_gte (const uint32_t, const uint32_t) PGM_GNUC_CONST;
static inline bool pgm_uint64_lt  (const uint64_t, const uint64_t) PGM_GNUC_CONST;
static inline bool pgm_uint64_lte (const uint64_t, const uint64_t) PGM_GNUC_CONST;
static inline bool pgm_uint64_gt  (const uint64_t, const uint64_t) PGM_GNUC_CONST;
static inline bool pgm_uint64_gte (const uint64_t, const uint64_t) PGM_GNUC_CONST;

/* 32 bit */
static inline
bool pgm_uint32_lt (
	const uint32_t	s,
	const uint32_t	t
	)
{
	pgm_assert (sizeof(int) >= 4);
	return ((s) - (t)) & PGM_UINT32_SIGN_BIT;
}

static inline
bool
pgm_uint32_lte (
	const uint32_t	s,
	const uint32_t	t
	)
{
	pgm_assert (sizeof(int) >= 4);
	return ((s) == (t)) || ( ((s) - (t)) & PGM_UINT32_SIGN_BIT );
}

static inline
bool
pgm_uint32_gt (
	const uint32_t	s,
	const uint32_t	t
	)
{
	pgm_assert (sizeof(int) >= 4);
	return ((t) - (s)) & PGM_UINT32_SIGN_BIT;
}

static inline
bool
pgm_uint32_gte (
	const uint32_t	s,
	const uint32_t	t
	)
{
	pgm_assert (sizeof(int) >= 4);
	return ((s) == (t)) || ( ((t) - (s)) & PGM_UINT32_SIGN_BIT );
}

/* 64 bit */
static inline
bool
pgm_uint64_lt (
	const uint64_t	s,
	const uint64_t	t
	)
{
	if (sizeof(int) == 4)
	{
/* need to force boolean conversion when int = 32bits */
		return ( ((s) - (t)) & PGM_UINT64_SIGN_BIT ) != 0;
	}
	else
	{
		pgm_assert (sizeof(int) >= 8);
		return ( ((s) - (t)) & PGM_UINT64_SIGN_BIT ) != 0;
	}
}

static inline
bool
pgm_uint64_lte (
	const uint64_t	s,
	const uint64_t	t
	)
{
	if (sizeof(int) == 4)
	{
/* need to force boolean conversion when int = 32bits */
		return	( (s) == (t) )
			||
			( ( ((s) - (t)) & PGM_UINT64_SIGN_BIT ) != 0 );
	}
	else
	{
		pgm_assert (sizeof(int) >= 8);
		return ( ((s) == (t)) || ( ((s) - (t)) & PGM_UINT64_SIGN_BIT ) ) != 0;
	}
}

static inline
bool
pgm_uint64_gt (
	const uint64_t	s,
	const uint64_t	t
	)
{
	if (sizeof(int) == 4)
	{
/* need to force boolean conversion when int = 32bits */
		return ( ((t) - (s)) & PGM_UINT64_SIGN_BIT ) != 0;
	}
	else
	{
		pgm_assert (sizeof(int) >= 8);
		return ( ((t) - (s)) & PGM_UINT64_SIGN_BIT ) != 0;
	}
}

static inline
bool
pgm_uint64_gte (
	const uint64_t	s,
	const uint64_t	t
	)
{
	if (sizeof(int) == 4)
	{
/* need to force boolean conversion when int = 32bits */
		return	( (s) == (t) )
			||
			( ( ((t) - (s)) & PGM_UINT64_SIGN_BIT ) != 0 );
	}
	else
	{
		pgm_assert (sizeof(int) >= 8);
		return ( ((s) == (t)) || ( ((t) - (s)) & PGM_UINT64_SIGN_BIT ) ) != 0;
	}
}

PGM_END_DECLS

#endif /* __PGM_IMPL_SN_H__ */
