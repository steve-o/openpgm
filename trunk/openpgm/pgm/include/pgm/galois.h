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

#ifndef __PGM_GALOIS_H__
#define __PGM_GALOIS_H__

#include <stdint.h>
#include <glib.h>


/* 8 bit wide galois field integer: GF(2⁸) */
typedef uint8_t pgm_gf8_t;

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

#ifdef CONFIG_GALOIS_MUL_LUT
extern const pgm_gf8_t pgm_gftable[PGM_GF_NO_ELEMENTS * PGM_GF_NO_ELEMENTS];
#endif


G_BEGIN_DECLS


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
	if (G_UNLIKELY( !(a && b) )) {
		return 0;
	}

#ifdef CONFIG_GALOIS_MUL_LUT
	return pgm_gftable[ (uint16_t)a << 8 | (uint16_t)b ];
#else
	unsigned sum = pgm_gflog[ a ] + pgm_gflog[ b ];
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
	if (G_UNLIKELY( !a )) {
		return 0;
	}

	int sum = pgm_gflog[ a ] - pgm_gflog[ b ];
	return sum < 0 ? pgm_gfantilog[ sum + PGM_GF_MAX ] : pgm_gfantilog[ sum ];
}

G_END_DECLS

#endif /* __PGM_GALOIS_H__ */
