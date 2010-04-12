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

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "pgm/messages.h"
#include "pgm/checksum.h"


/* globals */

/* endian independent checksum routine
 */

static
guint16
do_csum_8bit (
	const void*	addr,
	guint16		len,
	int		csum
	)
{
	guint32 acc;
	guint16 src;
	const guint8* buf;

	acc = csum;
	buf = (const guint8*)addr;
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
	return g_htons ((guint16)acc);
}

static
guint16
do_csum_16bit (
	const void*	addr,
	guint16		len,
	int		csum
	)
{
	guint32 acc;
	const guint8* buf;
	guint16 remainder;
	unsigned count8;
	gboolean is_odd;

	acc = csum;
	buf = (const guint8*)addr;
	remainder = 0;

	if (G_UNLIKELY(len == 0))
		return acc;
	is_odd = ((guint32)buf & 1);
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
			acc += ((const guint16*)buf)[ 0 ];
			acc += ((const guint16*)buf)[ 1 ];
			acc += ((const guint16*)buf)[ 2 ];
			acc += ((const guint16*)buf)[ 3 ];
			buf  = &buf[ 8 ];
		}
		len %= 8;
	}
	while (len > 1) {
		acc += ((const guint16*)buf)[ 0 ];
		buf  = &buf[ 2 ];
		len -= 2;
	}
/* trailing odd byte */
	if (len > 0) {
		((guint8*)&remainder)[0] = *buf;
	}
	acc += remainder;
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (G_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return acc;
}

static
guint16
do_csum_32bit (
	const void*	addr,
	guint16		len,
	int		csum
	)
{
	guint32 acc;
	const guint8* buf;
	guint16 remainder;
	unsigned count;
	gboolean is_odd;

	acc = csum;
	buf = (const guint8*)addr;
	remainder = 0;

	if (G_UNLIKELY(len == 0))
		return acc;
	is_odd = ((guint32)buf & 1);
/* align first byte */
	if (G_UNLIKELY(is_odd)) {
		((guint8*)&remainder)[1] = *buf++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((guint32)buf & 2) {
			acc += ((const guint16*)buf)[ 0 ];
			buf  = &buf[ 2 ];
			count--;
			len -= 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			guint32 carry = 0;
			while (count) {
				acc += carry;
				acc += ((const guint32*)buf)[ 0 ];
				carry = ((const guint32*)buf)[ 0 ] > acc;
				buf  = &buf[ 4 ];
				count--;
			}
			acc += carry;
			acc  = (acc >> 16) + (acc & 0xffff);
		}
		if (len & 2) {
			acc += ((const guint16*)buf)[ 0 ];
			buf  = &buf[ 2 ];
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((guint8*)&remainder)[0] = *buf;
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
guint16
do_csum_64bit (
	const void*	addr,
	guint16		len,
	int		csum
	)
{
	guint64 acc;
	const guint8* buf;
	guint16 remainder;
	unsigned count;
	gboolean is_odd;

	acc = csum;
	buf = (const guint8*)addr;
	remainder = 0;

	if (G_UNLIKELY(len == 0))
		return acc;
	is_odd = ((guint64)buf & 1);
/* align first byte */
	if (G_UNLIKELY(is_odd)) {
		((guint8*)&remainder)[1] = *buf++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((guint64)buf & 2) {
			acc += ((const guint16*)buf)[ 0 ];
			buf  = &buf[ 2 ];
			count--;
			len -= 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((guint64)buf & 4) {
				acc += ((const guint32*)buf)[ 0 ];
				buf  = &buf[ 4 ];
				count--;
				len -= 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
				guint64 carry = 0;
				while (count) {
					acc += carry;
					acc += ((const guint64*)buf)[ 0 ];
					carry = ((const guint64*)buf)[ 0 ] > acc;
					buf  = &buf[ 8 ];
					count--;
				}
				acc += carry;
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				acc += ((const guint32*)buf)[ 0 ];
				buf  = &buf[ 4 ];
			}
		}
		if (len & 2) {
			acc += ((const guint16*)buf)[ 0 ];
			buf  = &buf[ 2 ];
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((guint8*)&remainder)[0] = *buf;
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
/* simd instructions unique to AMD/Intel 64-bit
 */

static
guint16
do_csum_vector (
	const void*	addr,
	guint16		len,
	int		csum
	)
{
	guint64 acc;
	const guint8* buf;
	guint16 remainder;
	unsigned count;
	gboolean is_odd;

	acc = csum;
	buf = (const guint8*)addr;
	remainder = 0;

	if (G_UNLIKELY(len == 0))
		return acc;
	is_odd = ((guint64)buf & 1);
/* align first byte */
	if (G_UNLIKELY(is_odd)) {
		((guint8*)&remainder)[1] = *buf++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((guint64)buf & 2) {
			acc += ((const guint16*)buf)[ 0 ];
			buf  = &buf[ 2 ];
			count--;
			len -= 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((guint64)buf & 4) {
				acc += ((const guint32*)buf)[ 0 ];
				buf  = &buf[ 4 ];
				count--;
				len -= 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
				guint64 carry = 0;
				while (count) {
					asm("addq %1, %0 \n\t"
						"adcq %2, %0"
						: "=r" (acc)
						: "m" (*(guint64*)buf), "r" (carry), "0" (acc));
					buf  = &buf[ 8 ];
					count--;
				}
				acc += carry;
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				acc += ((const guint32*)buf)[ 0 ];
				buf  = &buf[ 4 ];
			}
		}
		if (len & 2) {
			acc += ((const guint16*)buf)[ 0 ];
			buf  = &buf[ 2 ];
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((guint8*)&remainder)[0] = *buf;
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
guint16
do_csum (
	const void*	addr,
	guint16		len,
	int		csum
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

guint16
pgm_inet_checksum (
	const void*	addr,
	guint		len,
	int		csum
	)
{
/* pre-conditions */
	pgm_assert (NULL != addr);

	return ~do_csum (addr, len, csum);
}

/* Calculate a partial (unfolded) checksum
 */

guint32
pgm_compat_csum_partial (
	const void*	addr,
	guint		len,
	guint32		csum
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

guint32
pgm_compat_csum_partial_copy (
	const void*	src,
	void*		dst,
	guint		len,
	guint32		csum
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

guint16
pgm_csum_fold (
	guint32		csum
	)
{
	csum  = (csum >> 16) + (csum & 0xffff);
	csum += (csum >> 16);

/* handle special case of no checksum */
	return csum == 0xffff ? csum : ~csum;
}

/* Add together two unfolded checksum accumulators
 */

guint32
pgm_csum_block_add (
	guint32		csum,
	guint32		csum2,
	guint		offset
	)
{
	if (offset & 1)			/* byte magic on odd offset */
		csum2 = ((csum2 & 0xff00ff) << 8) +
			((csum2 >> 8) & 0xff00ff);

	csum += csum2;
	return csum + (csum < csum2);
}

/* eof */
