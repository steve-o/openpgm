/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM checksum routines
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
#include <impl/framework.h>


/* locals */

static inline uint16_t do_csum (const void*, uint16_t, uint32_t) PGM_GNUC_PURE;
static uint16_t do_csum_8bit (const void*, uint16_t, uint32_t) PGM_GNUC_PURE;
static uint16_t do_csum_16bit (const void*, uint16_t, uint32_t) PGM_GNUC_PURE;
static uint16_t do_csum_32bit (const void*, uint16_t, uint32_t) PGM_GNUC_PURE;
static uint16_t do_csum_64bit (const void*, uint16_t, uint32_t) PGM_GNUC_PURE;
#if defined(__amd64) || defined(__x86_64__) || defined(_WIN64)
static uint16_t do_csum_vector (const void*, uint16_t, uint32_t) PGM_GNUC_PURE;
#endif


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
		src  = (*buf++) << 8;
/* second byte as least significant */
		src |= (*buf++);
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
do_csumcpy_8bit (
	const void* restrict srcaddr,
	void*       restrict dstaddr,
	uint16_t	     len,
	uint32_t	     csum
	)
{
	uint_fast32_t acc;
	const uint8_t*restrict srcbuf;
	uint8_t*restrict dstbuf;
	uint_fast16_t val16;

	acc = csum;
	srcbuf = (const uint8_t*restrict)srcaddr;
	dstbuf = (uint8_t*restrict)dstaddr;
	while (len > 1) {
/* first byte as most significant */
		val16  = (*dstbuf++ = *srcbuf++) << 8;
/* second byte as least significant */
		val16 |= (*dstbuf++ = *srcbuf++);
		acc += val16;
		len -= 2;
	}
/* trailing odd byte */
	if (len > 0) {
		val16 = (*dstbuf = *srcbuf) << 8;
		acc += val16;
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

	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
	is_odd = ((uintptr_t)buf & 1);
/* align first byte */
	if (PGM_UNLIKELY(is_odd)) {
		((uint8_t*)&remainder)[1] = *buf++;
		len--;
	}
/* 8-byte unrolls */
	count8 = len >> 3;
	while (count8--) {
		acc += ((const uint16_t*)buf)[ 0 ];
		acc += ((const uint16_t*)buf)[ 1 ];
		acc += ((const uint16_t*)buf)[ 2 ];
		acc += ((const uint16_t*)buf)[ 3 ];
		buf  = &buf[ 8 ];
	}
	len %= 8;
/* final 7 bytes */
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
	if (PGM_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return (uint16_t)acc;
}

static
uint16_t
do_csumcpy_16bit (
	const void* restrict srcaddr,
	void*	    restrict dstaddr,
	uint16_t	     len,
	uint32_t	     csum
	)
{
	uint_fast32_t acc;
	const uint8_t*restrict srcbuf;
	uint8_t*restrict dstbuf;
	uint16_t remainder;
	uint_fast16_t count8;
	bool is_odd;

	acc = csum;
	srcbuf = (const uint8_t*restrict)srcaddr;
	dstbuf = (uint8_t*restrict)dstaddr;
	remainder = 0;

	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
	is_odd = ((uintptr_t)srcbuf & 1);
/* align first byte */
	if (PGM_UNLIKELY(is_odd)) {
		((uint8_t*restrict)&remainder)[1] = *dstbuf++ = *srcbuf++;
		len--;
	}
/* 8-byte unrolls, anything larger than 16-byte or less than 8 loses performance */
	count8 = len >> 3;
	while (count8--) {
		acc += ((uint16_t*restrict)dstbuf)[ 0 ] = ((const uint16_t*restrict)srcbuf)[ 0 ];
		acc += ((uint16_t*restrict)dstbuf)[ 1 ] = ((const uint16_t*restrict)srcbuf)[ 1 ];
		acc += ((uint16_t*restrict)dstbuf)[ 2 ] = ((const uint16_t*restrict)srcbuf)[ 2 ];
		acc += ((uint16_t*restrict)dstbuf)[ 3 ] = ((const uint16_t*restrict)srcbuf)[ 3 ];
		srcbuf = &srcbuf[ 8 ];
		dstbuf = &dstbuf[ 8 ];
	}
	len %= 8;
/* final 7 bytes */
	while (len > 1) {
		acc += ((uint16_t*restrict)dstbuf)[ 0 ] = ((const uint16_t*restrict)srcbuf)[ 0 ];
		srcbuf = &srcbuf[ 2 ];
		dstbuf = &dstbuf[ 2 ];
		len -= 2;
	}
/* trailing odd byte */
	if (len > 0) {
		((uint8_t*restrict)&remainder)[0] = *dstbuf = *srcbuf;
	}
	acc += remainder;
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (PGM_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return (uint16_t)acc;
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

	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
	is_odd = ((uintptr_t)buf & 1);
/* align first byte */
	if (PGM_UNLIKELY(is_odd)) {
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
	if (PGM_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return (uint16_t)acc;
}

static
uint16_t
do_csumcpy_32bit (
	const void* restrict srcaddr,
	void*       restrict dstaddr,
	uint16_t	     len,
	uint32_t	     csum
	)
{
	uint_fast32_t acc;
	const uint8_t*restrict srcbuf;
	uint8_t*restrict dstbuf;
	uint16_t remainder;
	uint_fast16_t count;
	bool is_odd;

	acc = csum;
	srcbuf = (const uint8_t*restrict)srcaddr;
	dstbuf = (uint8_t*restrict)dstaddr;
	remainder = 0;

	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
	is_odd = ((uintptr_t)srcbuf & 1);
/* align first byte */
	if (PGM_UNLIKELY(is_odd)) {
		((uint8_t*restrict)&remainder)[1] = *dstbuf++ = *srcbuf++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((uintptr_t)srcbuf & 2) {
			acc += ((uint16_t*restrict)dstbuf)[ 0 ] = ((const uint16_t*restrict)srcbuf)[ 0 ];
			srcbuf = &srcbuf[ 2 ];
			dstbuf = &dstbuf[ 2 ];
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
				acc += ((uint32_t*restrict)dstbuf)[ 0 ] = ((const uint32_t*restrict)srcbuf)[ 0 ];
				carry = ((const uint32_t*restrict)dstbuf)[ 0 ] > acc;
				srcbuf = &srcbuf[ 4 ];
				dstbuf = &dstbuf[ 4 ];
				count--;
			}
			acc += carry;
			acc  = (acc >> 16) + (acc & 0xffff);
		}
		if (len & 2) {
			acc += ((uint16_t*restrict)dstbuf)[ 0 ] = ((const uint16_t*restrict)srcbuf)[ 0 ];
			srcbuf = &srcbuf[ 2 ];
			dstbuf = &dstbuf[ 2 ];
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*restrict)&remainder)[0] = *dstbuf = *srcbuf;
	}
	acc += remainder;
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (PGM_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return (uint16_t)acc;
}

/* best if architecture has native 64-bit words
 */

static
uint16_t
do_csum_64bit (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
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

	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
	is_odd = ((uintptr_t)buf & 1);
/* align first byte */
	if (PGM_UNLIKELY(is_odd)) {
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
	if (PGM_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return (uint16_t)acc;
}

static
uint16_t
do_csumcpy_64bit (
	const void* restrict srcaddr,
	void*	    restrict dstaddr,
	uint16_t	     len,
	uint32_t	     csum
	)
{
	uint_fast64_t acc;
	const uint8_t* restrict srcbuf;
	uint8_t* restrict dstbuf;
	uint16_t remainder;
	uint_fast16_t count;
	bool is_odd;

	acc = csum;
	srcbuf = (const uint8_t*restrict)srcaddr;
	dstbuf = (uint8_t*restrict)dstaddr;
	remainder = 0;

	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
	is_odd = ((uintptr_t)srcbuf & 1);
/* align first byte */
	if (PGM_UNLIKELY(is_odd)) {
		((uint8_t*restrict)&remainder)[1] = *dstbuf++ = *srcbuf++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((uintptr_t)srcbuf & 2) {
			acc += ((uint16_t*restrict)dstbuf)[ 0 ] = ((const uint16_t*restrict)srcbuf)[ 0 ];
			srcbuf = &srcbuf[ 2 ];
			dstbuf = &dstbuf[ 2 ];
			count--;
			len -= 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((uintptr_t)srcbuf & 4) {
				acc += ((uint32_t*restrict)dstbuf)[ 0 ] = ((const uint32_t*restrict)srcbuf)[ 0 ];
				srcbuf = &srcbuf[ 4 ];
				dstbuf = &dstbuf[ 4 ];
				count--;
				len -= 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
/* 64-byte blocks */
				uint_fast64_t carry = 0;
				uint_fast16_t count64 = count >> 3;
				if (count64)
				{
					carry = 0;
					while (count64) {
						acc += carry;
						acc += ((uint64_t*restrict)dstbuf)[ 0 ] = ((const uint64_t*restrict)srcbuf)[ 0 ];
						carry  = ((const uint64_t*restrict)dstbuf)[ 0 ] > acc;
						acc += carry;
						acc += ((uint64_t*restrict)dstbuf)[ 1 ] = ((const uint64_t*restrict)srcbuf)[ 1 ];
						carry  = ((const uint64_t*restrict)dstbuf)[ 1 ] > acc;
						acc += carry;
						acc += ((uint64_t*restrict)dstbuf)[ 2 ] = ((const uint64_t*restrict)srcbuf)[ 2 ];
						carry  = ((const uint64_t*restrict)dstbuf)[ 2 ] > acc;
						acc += carry;
						acc += ((uint64_t*restrict)dstbuf)[ 3 ] = ((const uint64_t*restrict)srcbuf)[ 3 ];
						carry  = ((const uint64_t*restrict)dstbuf)[ 3 ] > acc;
						acc += carry;
						acc += ((uint64_t*restrict)dstbuf)[ 4 ] = ((const uint64_t*restrict)srcbuf)[ 4 ];
						carry  = ((const uint64_t*restrict)dstbuf)[ 4 ] > acc;
						acc += carry;
						acc += ((uint64_t*restrict)dstbuf)[ 5 ] = ((const uint64_t*restrict)srcbuf)[ 5 ];
						carry  = ((const uint64_t*restrict)dstbuf)[ 5 ] > acc;
						acc += carry;
						acc += ((uint64_t*restrict)dstbuf)[ 6 ] = ((const uint64_t*restrict)srcbuf)[ 6 ];
						carry  = ((const uint64_t*restrict)dstbuf)[ 6 ] > acc;
						acc += carry;
						acc += ((uint64_t*restrict)dstbuf)[ 7 ] = ((const uint64_t*restrict)srcbuf)[ 7 ];
						carry  = ((const uint64_t*restrict)dstbuf)[ 7 ] > acc;
						srcbuf = &srcbuf[ 64 ];
						dstbuf = &dstbuf[ 64 ];
						count64--;
					}
					acc += carry;
					acc  = (acc >> 32) + (acc & 0xffffffff);
					count %= 8;
				}

/* last 56 bytes */
				carry = 0;
				while (count) {
					acc += carry;
					acc += ((uint64_t*restrict)dstbuf)[ 0 ] = ((const uint64_t*restrict)srcbuf)[ 0 ];
					carry = ((const uint64_t*restrict)dstbuf)[ 0 ] > acc;
					srcbuf = &srcbuf[ 8 ];
					dstbuf = &dstbuf[ 8 ];
					count--;
				}
				acc += carry;
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				acc += ((uint32_t*restrict)dstbuf)[ 0 ] = ((const uint32_t*restrict)srcbuf)[ 0 ];
				srcbuf = &srcbuf[ 4 ];
				dstbuf = &dstbuf[ 4 ];
			}
		}
		if (len & 2) {
			acc += ((uint16_t*restrict)dstbuf)[ 0 ] = ((const uint16_t*restrict)srcbuf)[ 0 ];
			srcbuf = &srcbuf[ 2 ];
			dstbuf = &dstbuf[ 2 ];
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*restrict)&remainder)[0] = *dstbuf = *srcbuf;
	}
	acc += remainder;
	acc  = (acc >> 32) + (acc & 0xffffffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (PGM_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return (uint16_t)acc;
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

	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
/* align first byte */
	is_odd = ((uintptr_t)buf & 1);
	if (PGM_UNLIKELY(is_odd)) {
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
					__asm__ volatile ("addq %1, %0\n\t"
							  "adcq %2, %0"
					     		: "=r" (acc)
							: "m" (*(const uint64_t*)buf), "r" (carry), "0" (acc)
							: "cc"  );
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
	if (PGM_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return (uint16_t)acc;
}

static
uint16_t
do_csumcpy_vector (
	const void* restrict srcaddr,
	void* restrict       dstaddr,
	uint16_t	     len,
	uint32_t	     csum
	)
{
	uint64_t acc;			/* fixed size for asm */
	const uint8_t*restrict srcbuf;
	uint8_t*restrict dstbuf;
	uint16_t remainder;		/* fixed size for endian swap */
	uint_fast16_t count;
	bool is_odd;

	acc = csum;
	srcbuf = (const uint8_t*restrict)srcaddr;
	dstbuf = (uint8_t*restrict)dstaddr;
	remainder = 0;

	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
/* fill cache line with source buffer, invalidate destination buffer,
 * perversly for testing high temporal locality is better than no locality,
 * whilst in production no locality may be preferred depending on skb re-use.
 */
	pgm_prefetch (srcbuf);
	pgm_prefetchw (dstbuf);
/* align first byte */
	is_odd = ((uintptr_t)srcbuf & 1);
	if (PGM_UNLIKELY(is_odd)) {
		((uint8_t*restrict)&remainder)[1] = *dstbuf++ = *srcbuf++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((uintptr_t)srcbuf & 2) {
			acc += ((uint16_t*restrict)dstbuf)[ 0 ] = ((const uint16_t*restrict)srcbuf)[ 0 ];
			srcbuf = &srcbuf[ 2 ];
			dstbuf = &dstbuf[ 2 ];
			count--;
			len -= 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((uintptr_t)srcbuf & 4) {
				acc += ((uint32_t*restrict)dstbuf)[ 0 ] = ((const uint32_t*restrict)srcbuf)[ 0 ];
				srcbuf = &srcbuf[ 4 ];
				dstbuf = &dstbuf[ 4 ];
				count--;
				len -= 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
/* 64-byte blocks */
				uint64_t carry = 0;
				uint_fast16_t count64 = count >> 3;

				while (count64)
				{
					pgm_prefetch (&srcbuf[ 64 ]);
					pgm_prefetchw (&dstbuf[ 64 ]);
					__asm__ volatile ("movq 0*8(%1), %%r8\n\t"	/* load */
							  "movq 1*8(%1), %%r9\n\t"
							  "movq 2*8(%1), %%r10\n\t"
							  "movq 3*8(%1), %%r11\n\t"
							  "movq 4*8(%1), %%r12\n\t"
							  "movq 5*8(%1), %%r13\n\t"
							  "movq 6*8(%1), %%r14\n\t"
							  "movq 7*8(%1), %%r15\n\t"
							  "adcq %%r8, %0\n\t"		/* checksum */
							  "adcq %%r9, %0\n\t"
							  "adcq %%r10, %0\n\t"
							  "adcq %%r11, %0\n\t"
							  "adcq %%r12, %0\n\t"
							  "adcq %%r13, %0\n\t"
							  "adcq %%r14, %0\n\t"
							  "adcq %%r15, %0\n\t"
							  "adcq %3, %0\n\t"
							  "movq %%r8, 0*8(%2)\n\t"	/* save */
							  "movq %%r9, 1*8(%2)\n\t"
							  "movq %%r10, 2*8(%2)\n\t"
							  "movq %%r11, 3*8(%2)\n\t"
							  "movq %%r12, 4*8(%2)\n\t"
							  "movq %%r13, 5*8(%2)\n\t"
							  "movq %%r14, 6*8(%2)\n\t"
							  "movq %%r15, 7*8(%2)"
							: "=r" (acc)
							: "r" (srcbuf), "r" (dstbuf), "r" (carry), "0" (acc)
							: "cc", "memory", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"  );
					srcbuf = &srcbuf[ 64 ];
					dstbuf = &dstbuf[ 64 ];
					count64--;
				}
				count %= 8;
/* last 56 bytes */
				while (count) {
					__asm__ volatile ("addq %1, %0\n\t"
							  "adcq %2, %0"
							: "=r" (acc)
							: "m" (*(const uint64_t*restrict)srcbuf), "r" (carry), "0" (acc)
							: "cc"  );
					srcbuf  = &srcbuf[ 8 ];
					count--;
				}
				acc += carry;
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				acc += ((uint32_t*restrict)dstbuf)[ 0 ] = ((const uint32_t*restrict)srcbuf)[ 0 ];
				srcbuf = &srcbuf[ 4 ];
				dstbuf = &dstbuf[ 4 ];
			}
		}
		if (len & 2) {
			acc += ((uint16_t*restrict)dstbuf)[ 0 ] = ((const uint16_t*restrict)srcbuf)[ 0 ];
			srcbuf = &srcbuf[ 2 ];
			dstbuf = &dstbuf[ 2 ];
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*restrict)&remainder)[0] = *dstbuf = *srcbuf;
	}
	acc += remainder;
	acc  = (acc >> 32) + (acc & 0xffffffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (PGM_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return (uint16_t)acc;
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
#if   defined( USE_8BIT_CHECKSUM )
	return do_csum_8bit (addr, len, csum);
#elif defined( USE_16BIT_CHECKSUM )
	return do_csum_16bit (addr, len, csum);
#elif defined( USE_32BIT_CHECKSUM )
	return do_csum_32bit (addr, len, csum);
#elif defined( USE_64BIT_CHECKSUM )
	return do_csum_64bit (addr, len, csum);
#elif defined( USE_VECTOR_CHECKSUM )
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

/* Calculate & copy a partial PGM checksum.
 *
 * Optimum performance when src & dst are on same alignment.
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

#if defined( __sparc__ ) || defined( __sparc ) || defined( __sparcv9 )
/* SPARC will not handle destination & source addresses with different alignment */
	memcpy (dst, src, len);
	return pgm_csum_partial (dst, len, csum);
#else
#	if   defined( USE_8BIT_CHECKSUM )
	return do_csumcpy_8bit (src, dst, len, csum);
#	elif defined( USE_16BIT_CHECKSUM )
	return do_csumcpy_16bit (src, dst, len, csum);
#	elif defined( USE_32BIT_CHECKSUM )
	return do_csumcpy_32bit (src, dst, len, csum);
#	elif defined( USE_64BIT_CHECKSUM )
	return do_csumcpy_64bit (src, dst, len, csum);
#	elif defined( USE_VECTOR_CHECKSUM )
	return do_csumcpy_vector (src, dst, len, csum);
#	else
	memcpy (dst, src, len);
	return pgm_csum_partial (dst, len, csum);
#	endif
#endif
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
	return (uint16_t)(csum == 0xffff ? csum : ~csum);
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
