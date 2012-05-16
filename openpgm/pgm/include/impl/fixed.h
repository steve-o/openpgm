/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * 8-bit and 16-bit shift fixed point math
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
#       error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_FIXED_H__
#define __PGM_IMPL_FIXED_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

static inline uint_fast32_t pgm_fp8 (unsigned) PGM_GNUC_CONST;
static inline uint_fast32_t pgm_fp16 (unsigned) PGM_GNUC_CONST;
static inline unsigned pgm_fp8tou (uint_fast32_t) PGM_GNUC_CONST;
static inline unsigned pgm_fp16tou (uint_fast32_t) PGM_GNUC_CONST;
static inline uint_fast32_t pgm_fp8mul (uint_fast32_t, uint_fast32_t) PGM_GNUC_CONST;
static inline uint_fast32_t pgm_fp16mul (uint_fast32_t, uint_fast32_t) PGM_GNUC_CONST;
static inline uint_fast32_t pgm_fp8div (uint_fast32_t, uint_fast32_t) PGM_GNUC_CONST;
static inline uint_fast32_t pgm_fp16div (uint_fast32_t, uint_fast32_t) PGM_GNUC_CONST;
static inline uint_fast32_t pgm_fp16pow (uint_fast32_t, uint_fast32_t) PGM_GNUC_CONST;

static inline
uint_fast32_t
pgm_fp8 (
	unsigned	v
	)
{
	return (uint32_t)(v << 8);
}

static inline
uint_fast32_t
pgm_fp16 (
	unsigned	v
	)
{
	return (uint_fast32_t)(v << 16);
}

static inline
unsigned
pgm_fp8tou (
	uint_fast32_t	f
	)
{
	return (f + (1 << 7)) >> 8;
}

static inline
unsigned
pgm_fp16tou (
	uint_fast32_t	f
	)
{
	return (f + (1 << 15)) >> 16;
}

static inline
uint_fast32_t
pgm_fp8mul (
	uint_fast32_t	a,
	uint_fast32_t	b
	)
{
	return ( a * b + 128 ) >> 8;
}

static inline
uint_fast32_t
pgm_fp16mul (
	uint_fast32_t	a,
	uint_fast32_t	b
	)
{
	return ( a * b + 32768 ) >> 16;
}

static inline
uint_fast32_t
pgm_fp8div (
	uint_fast32_t	a,
	uint_fast32_t	b
	)
{
	return ( ( (a << 9) / b ) + 1 ) / 2;
}

static inline
uint_fast32_t
pgm_fp16div (
	uint_fast32_t	a,
	uint_fast32_t	b
	)
{
	return ( ( (a << 17) / b ) + 1 ) / 2;
}

static inline
uint_fast32_t
pgm_fp16pow (
	uint_fast32_t	x,
	uint_fast32_t	y
	)
{
	uint_fast32_t result = pgm_fp16 (1);
/* C89 version */
	uint_fast32_t i;
	for (i = x;
	     y;
	     y >>= 1)
	{
		if (y & 1)
			result = (result * i + 32768) >> 16;
		i = (i * i + 32768) >> 16;
	}
	return result;
}

PGM_END_DECLS

#endif /* __PGM_IMPL_FIXED_H__ */
