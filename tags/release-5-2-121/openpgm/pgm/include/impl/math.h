/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Shared math routines.
 *
 * Copyright (c) 2006-2010 Miru Limited.
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
#ifndef __PGM_IMPL_MATH_H__
#define __PGM_IMPL_MATH_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

/* fast log base 2 of power of 2
 */

static inline unsigned pgm_power2_log2 (unsigned) PGM_GNUC_CONST;

static inline
unsigned
pgm_power2_log2 (
	unsigned	v
	)
{
	static const unsigned int b[] = { 0xAAAAAAAA, 0xCCCCCCCC, 0xF0F0F0F0, 0xFF00FF00, 0xFFFF0000 };
	unsigned int r = (v & b[0]) != 0;
/* C89 version */
	unsigned i;
	for (i = 4; i > 0; i--)
		r |= ((v & b[i]) != 0) << i;
	return r;
}

/* nearest power of 2
 */

static inline size_t pgm_nearest_power (size_t, size_t) PGM_GNUC_CONST;

static inline
size_t
pgm_nearest_power (
	size_t		b,
	size_t		v
	)
{
	if (v > (SIZE_MAX/2))
		return SIZE_MAX;
	while (b < v)
		b <<= 1;
	return b;
}

unsigned pgm_spaced_primes_closest (unsigned) PGM_GNUC_PURE;

PGM_END_DECLS

#endif /* __PGM_IMPL_MATH_H__ */
