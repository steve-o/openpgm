/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM checksum routines
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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <glib.h>

#include "pgm/messages.h"
#include "pgm/checksum.h"


/* globals */

/* endian independent checksum routine
 */

static
uint16_t
do_csum_8bit (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
	)
{
	uint_fast32_t acc;
	uint16_t src;
	const uint8_t* buf;

	acc = csum;
	buf = (const uint8_t*)addr;
	while (len > 1) {
/* first byte as most significant */
		src = (*buf) << 8;
		buf++;
/* second byte as least significant */
		src |= (*buf);
		buf++;
		acc += src;
		len -= 2;
	}
/* trailing odd byte */
	if (len > 0) {
		src = (*buf) << 8;
		acc += src;
	}
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	return htons ((uint16_t)acc);
}

static
uint16_t
do_csum_16bit (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
	)
{
	uint_fast32_t acc;
	const uint8_t* buf;
	uint16_t remainder;
	uint_fast16_t count8;
	bool is_odd;

	acc = csum;
	buf = (const uint8_t*)addr;
	remainder = 0;

	if (G_UNLIKELY(len == 0))
		return acc;
	is_odd = ((uintptr_t)buf & 1);
/* align first byte */
	if (G_UNLIKELY(is_odd)) {
		((guint8*)&remainder)[1] = *buf++;
		len--;
	}
/* 8-way unrolls */
	count8 = len >> 3;
	if (count8)
	{
		while (count8--) {
			acc += ((const uint16_t*)buf)[ 0 ];
			acc += ((const uint16_t*)buf)[ 1 ];
			acc += ((const uint16_t*)buf)[ 2 ];
			acc += ((const uint16_t*)buf)[ 3 ];
			buf  = &buf[ 8 ];
		}
		len %= 8;
	}
	while (len > 1) {
		acc += ((const uint16_t*)buf)[ 0 ];
		buf  = &buf[ 2 ];
		len -= 2;
	}
/* trailing odd byte */
	if (len > 0) {
		((uint8_t*)&remainder)[0] = *buf;
	}
	acc += remainder;
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (G_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return acc;
}

static
uint16_t
do_csum_32bit (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
	)
{
	uint_fast32_t acc;
	const uint8_t* buf;
	uint16_t remainder;
	uint_fast16_t count;
	bool is_odd;

	acc = csum;
	buf = (const uint8_t*)addr;
	remainder = 0;

	if (G_UNLIKELY(len == 0))
		return acc;
	is_odd = ((uintptr_t)buf & 1);
/* align first byte */
	if (G_UNLIKELY(is_odd)) {
		((uint8_t*)&remainder)[1] = *buf++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((uintptr_t)buf & 2) {
			acc += ((const uint16_t*)buf)[ 0 ];
			buf  = &buf[ 2 ];
			count--;
			len -= 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			uint32_t carry = 0;
			while (count) {
				acc += carry;
				acc += ((const uint32_t*)buf)[ 0 ];
				carry = ((const uint32_t*)buf)[ 0 ] > acc;
				buf  = &buf[ 4 ];
				count--;
			}
			acc += carry;
			acc  = (acc >> 16) + (acc & 0xffff);
		}
		if (len & 2) {
			acc += ((const uint16_t*)buf)[ 0 ];
			buf  = &buf[ 2 ];
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*)&remainder)[0] = *buf;
	}
	acc += remainder;
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (G_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return acc;
}

/* best if architecture has native 64-bit words
 */

static
uint16_t
do_csum_64bit (
	const void*	addr,
	uint16_t	len,
	int		csum
	)
{
	uint_fast64_t acc;
	const uint8_t* buf;
	uint16_t remainder;
	uint_fast16_t count;
	bool is_odd;

	acc = csum;
	buf = (const uint8_t*)addr;
	remainder = 0;

	if (G_UNLIKELY(len == 0))
		return acc;
	is_odd = ((uintptr_t)buf & 1);
/* align first byte */
	if (G_UNLIKELY(is_odd)) {
		((uint8_t*)&remainder)[1] = *buf++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((uintptr_t)buf & 2) {
			acc += ((const uint16_t*)buf)[ 0 ];
			buf  = &buf[ 2 ];
			count--;
			len -= 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((uintptr_t)buf & 4) {
				acc += ((const uint32_t*)buf)[ 0 ];
				buf  = &buf[ 4 ];
				count--;
				len -= 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
				uint_fast64_t carry = 0;
				while (count) {
					acc += carry;
					acc += ((const uint64_t*)buf)[ 0 ];
					carry = ((const uint64_t*)buf)[ 0 ] > acc;
					buf  = &buf[ 8 ];
					count--;
				}
				acc += carry;
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				acc += ((const uint32_t*)buf)[ 0 ];
				buf  = &buf[ 4 ];
			}
		}
		if (len & 2) {
			acc += ((const uint16_t*)buf)[ 0 ];
			buf  = &buf[ 2 ];
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*)&remainder)[0] = *buf;
	}
	acc += remainder;
	acc  = (acc >> 32) + (acc & 0xffffffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (G_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return acc;
}

#if defined(__amd64) || defined(__x86_64__)
/* simd instructions unique to AMD/Intel 64-bit, so always little endian.
 */

static
uint16_t
do_csum_vector (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
	)
{
	uint64_t acc;			/* fixed size for asm */
	const uint8_t* buf;
	uint16_t remainder;		/* fixed size for endian swap */
	uint_fast16_t count;
	bool is_odd;

	acc = csum;
	buf = (const uint8_t*)addr;
	remainder = 0;

	if (G_UNLIKELY(len == 0))
		return acc;
	is_odd = ((uintptr_t)buf & 1);
/* align first byte */
	if (G_UNLIKELY(is_odd)) {
		((uint8_t*)&remainder)[1] = *buf++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((uintptr_t)buf & 2) {
			acc += ((const uint16_t*)buf)[ 0 ];
			buf  = &buf[ 2 ];
			count--;
			len -= 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((uintptr_t)buf & 4) {
				acc += ((const uint32_t*)buf)[ 0 ];
				buf  = &buf[ 4 ];
				count--;
				len -= 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
				uint64_t carry = 0;
				while (count) {
					asm("addq %1, %0 \n\t"
						"adcq %2, %0"
						: "=r" (acc)
						: "m" (*(uint64_t*)buf), "r" (carry), "0" (acc));
					buf  = &buf[ 8 ];
					count--;
				}
				acc += carry;
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				acc += ((const uint32_t*)buf)[ 0 ];
				buf  = &buf[ 4 ];
			}
		}
		if (len & 2) {
			acc += ((const uint16_t*)buf)[ 0 ];
			buf  = &buf[ 2 ];
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*)&remainder)[0] = *buf;
	}
	acc += remainder;
	acc  = (acc >> 32) + (acc & 0xffffffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (G_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return acc;
}

#endif

static inline
uint16_t
do_csum (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
	)
{
#if defined(CONFIG_8BIT_CHECKSUM)
	return do_csum_8bit (addr, len, csum);
#elif defined(CONFIG_16BIT_CHECKSUM)
	return do_csum_16bit (addr, len, csum);
#elif defined(CONFIG_32BIT_CHECKSUM)
	return do_csum_32bit (addr, len, csum);
#elif defined(CONFIG_64BIT_CHECKSUM)
	return do_csum_64bit (addr, len, csum);
#elif defined(CONFIG_VECTOR_CHECKSUM)
	return do_csum_vector (addr, len, csum);
#else
#	error "checksum routine undefined"
#endif
}

/* Calculate an IP header style checksum
 */

uint16_t
pgm_inet_checksum (
	const void*	addr,
	uint16_t	len,
	uint16_t	csum
	)
{
/* pre-conditions */
	pgm_assert (NULL != addr);

	return ~do_csum (addr, len, csum);
}

/* Calculate a partial (unfolded) checksum
 */

uint32_t
pgm_compat_csum_partial (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
	)
{
/* pre-conditions */
	pgm_assert (NULL != addr);

	csum  = (csum >> 16) + (csum & 0xffff);
	csum += do_csum (addr, len, 0);
	csum  = (csum >> 16) + (csum & 0xffff);

	return csum;
}

/* Calculate & copy a partial PGM checksum
 */

uint32_t
pgm_compat_csum_partial_copy (
	const void* restrict src,
	void*	    restrict dst,
	uint16_t	     len,
	uint32_t	     csum
	)
{
/* pre-conditions */
	pgm_assert (NULL != src);
	pgm_assert (NULL != dst);

	memcpy (dst, src, len);
	return pgm_csum_partial (dst, len, csum);
}

/* Fold 32 bit checksum accumulator into 16 bit final value.
 */

uint16_t
pgm_csum_fold (
	uint32_t	csum
	)
{
	csum  = (csum >> 16) + (csum & 0xffff);
	csum += (csum >> 16);

/* handle special case of no checksum */
	return csum == 0xffff ? csum : ~csum;
}

/* Add together two unfolded checksum accumulators
 */

uint32_t
pgm_csum_block_add (
	uint32_t	csum,
	uint32_t	csum2,
	const uint16_t	offset
	)
{
	if (offset & 1)			/* byte magic on odd offset */
		csum2 = ((csum2 & 0xff00ff) << 8) +
			((csum2 >> 8) & 0xff00ff);

	csum += csum2;
	return csum + (csum < csum2);
}

/* eof */
