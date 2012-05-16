/*
 * Galois field maths.
 *
 * Copyright (c) 2006-2008 Miru Limited.
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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_GALOIS_H__
#define __PGM_IMPL_GALOIS_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

/* 8 bit wide galois field integer: GF(2⁸) */
#ifdef _MSC_VER
typedef uint8_t pgm_gf8_t;
#else
typedef uint8_t __attribute__((__may_alias__)) pgm_gf8_t;
#endif

/* E denotes the encoding symbol length in bytes.
 * S denotes the symbol size in units of m-bit elements.  When m = 8,
 * then S and E are equal.
 */
#define PGM_GF_ELEMENT_BYTES	sizeof(pgm_gf8_t)

/* m defines the length of the elements in the finite field, in bits.
 * m belongs to {2..16}.
 */
#define PGM_GF_ELEMENT_BITS	( 8 * PGM_GF_ELEMENT_BYTES )

/* q defines the number of elements in the finite field.
 */
#define PGM_GF_NO_ELEMENTS	( 1 << PGM_GF_ELEMENT_BITS )
#define PGM_GF_MAX		( PGM_GF_NO_ELEMENTS - 1 )


extern const pgm_gf8_t pgm_gflog[PGM_GF_NO_ELEMENTS];
extern const pgm_gf8_t pgm_gfantilog[PGM_GF_NO_ELEMENTS];

#ifdef USE_GALOIS_MUL_LUT
extern const pgm_gf8_t pgm_gftable[PGM_GF_NO_ELEMENTS * PGM_GF_NO_ELEMENTS];
#endif

/* In a finite field with characteristic 2, addition and subtraction are
 * identical, and are accomplished using the XOR operator. 
 */
static inline
pgm_gf8_t
pgm_gfadd (
	pgm_gf8_t	a,
	pgm_gf8_t	b
	)
{
	return a ^ b;
}

static inline
pgm_gf8_t
pgm_gfadd_equals (
	pgm_gf8_t	a,
	pgm_gf8_t	b
	)
{
	return a ^= b;
}

static inline
pgm_gf8_t
pgm_gfsub (
	pgm_gf8_t	a,
	pgm_gf8_t	b
	)
{
	return pgm_gfadd (a, b);
}

static inline
pgm_gf8_t
pgm_gfsub_equals (
	pgm_gf8_t	a,
	pgm_gf8_t	b
	)
{
	return pgm_gfadd_equals (a, b);
}

static inline
pgm_gf8_t
pgm_gfmul (
	pgm_gf8_t	a,
	pgm_gf8_t	b
        )
{
	if (PGM_UNLIKELY( !(a && b) )) {
		return 0;
	}

#ifdef USE_GALOIS_MUL_LUT
	return pgm_gftable[ (uint16_t)a << 8 | (uint16_t)b ];
#else
	const unsigned sum = pgm_gflog[ a ] + pgm_gflog[ b ];
	return sum >= PGM_GF_MAX ? pgm_gfantilog[ sum - PGM_GF_MAX ] : pgm_gfantilog[ sum ];
#endif
}

static inline
pgm_gf8_t
pgm_gfdiv (
	pgm_gf8_t	a,
	pgm_gf8_t	b
        )
{
/* C89 version */
	const int sum = pgm_gflog[ a ] - pgm_gflog[ b ];
	if (PGM_UNLIKELY( !a )) {
		return 0;
	}

	return sum < 0 ? pgm_gfantilog[ sum + PGM_GF_MAX ] : pgm_gfantilog[ sum ];
}

PGM_END_DECLS

#endif /* __PGM_IMPL_GALOIS_H__ */
