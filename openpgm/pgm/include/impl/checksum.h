/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM checksum routines
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
#ifndef __PGM_IMPL_CHECKSUM_H__
#define __PGM_IMPL_CHECKSUM_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

uint16_t pgm_inet_checksum (const void*, uint16_t, uint16_t);
uint16_t pgm_csum_fold (uint32_t) PGM_GNUC_CONST;
uint32_t pgm_csum_block_add (uint32_t, uint32_t, const uint16_t) PGM_GNUC_CONST;
uint32_t pgm_compat_csum_partial (const void*, uint16_t, uint32_t);
uint32_t pgm_compat_csum_partial_copy (const void*restrict, void*restrict, uint16_t, uint32_t);

static inline uint32_t add32_with_carry (uint32_t, uint32_t) PGM_GNUC_CONST;

#if defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
static inline uint32_t add32_with_carry (uint32_t a, uint32_t b)
{
	__asm__ ( "addl %2, %0 \n\t"
		  "adcl $0, %0"
		: "=r" (a)			/* output operands */
		: "0" (a), "r" (b));		/* input operands */
	return a;
}
#elif defined( __sparc__ ) || defined( __sparc ) || defined( __sparcv9 )
static inline uint32_t add32_with_carry (uint32_t a, uint32_t b)
{
	__asm__ ( "addcc %2, %0, %0 \n\t"
		  "addx %0, %%g0, %0"
		: "=r" (a)			/* output operands */
		: "0" (a), "r" (b)		/* input operands */
		: "cc");			/* list of clobbered registers */
	return a;
}
#else
static inline uint32_t add32_with_carry (uint32_t a, uint32_t b)
{
	a += b;
	a  = (a >> 16) + (a & 0xffff);
	return a;
}
#endif

#	define pgm_csum_partial            pgm_compat_csum_partial
#	define pgm_csum_partial_copy       pgm_compat_csum_partial_copy

PGM_END_DECLS

#endif /* __PGM_IMPL_CHECKSUM_H__ */

