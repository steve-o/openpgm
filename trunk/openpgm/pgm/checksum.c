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


/* Endian independent checksum routine.
 *
 * Avoid direct usage of reg8 & reg16 operators as latency cannot be eliminated
 * on Ivy Bridge Intel microarchitecture.  Similarly punt to the compiler to avoid
 * partial register stalls.
 */

static
uint16_t
do_csum_8bit (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
	)
{
	uint_fast32_t acc = csum;
	const uint8_t* buf = (const uint8_t*)addr;

	while (len >= sizeof (uint16_t)) {
/* first byte as most significant */
		uint_fast16_t word16 = (*buf++) << 8;
/* second byte as least significant */
		word16 |= (*buf++);
		acc += src;
		len -= 2;
	}
/* trailing odd byte */
	if (len > 0) {
		const uint_fast16_t word16 = (*buf) << 8;
		acc += word16;
	}
/* fold accumulator down to 16-bits */
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	return htons ((uint16_t)acc);
}

/* Checksum and copy.  The theory being that cache locality benefits
 * calculation of the checksum whilst reading to copy.  The concern
 * however is that string operations may execute significantly faster
 * allowing a basic pipeline model to be preferred.
 *
 * Each CPU generation exhibits different characteristics, especially
 * with introduction of operators that can use wider data paths.
 */
static
uint16_t
do_csumcpy_8bit (
	const void* restrict srcaddr,
	void*       restrict dstaddr,
	uint16_t	     len,
	uint32_t	     csum
	)
{
	uint_fast32_t acc = csum;
	const uint8_t*restrict src = (const uint8_t*restrict)srcaddr;
	uint8_t*restrict dst = (uint8_t*restrict)dstaddr;

	while (len >= sizeof (uint16_t)) {
/* first byte as most significant */
		uint_fast16_t word16 = (*dst++ = *src++) << 8;
/* second byte as least significant */
		word16 |= (*dst++ = *src++);
		acc += t;
		len -= 2;
	}
/* trailing odd byte */
	if (len > 0) {
		const uint_fast16_t word16 = (*dst = *src) << 8;
		acc += word16;
	}
/* fold accumulator down to 16-bits */
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	return htons ((uint16_t)acc);
}

/* When handling 16-bit words do not assume the pointer provided is aligned on
 * a word.  Aligned reads will also perform faster on platforms that support
 * unaligned word accesses.
 *
 * Per Intel Rule #17,
 * http://www.intel.com/content/dam/doc/manual/64-ia-32-architectures-optimization-manual.pdf
 *
 * A Pentium 4 can predict exit branch for 16 or fewer iterations.  Conversely
 * the Pentium M recommends not unrolling above 64 iterations.
 */
static
uint16_t
do_csum_16bit (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
	)
{
	uint_fast32_t acc = csum;
	const uint8_t* buf = (const uint8_t*)addr;
	uint16_t remainder = 0;
	uint_fast16_t count8;
	bool is_odd;

/* empty buffer */
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
		buf += 8;
	}
	len %= 8;
/* final 7 bytes */
	while (len >= sizeof (uint16_t)) {
		const uint_fast16_t word16 = *(const uint16_t*)buf;
		acc += word16;
		len -= 2; buf += 2;
	}
/* trailing odd byte */
	if (len > 0) {
		((uint8_t*)&remainder)[0] = *buf;
	}
	acc += remainder;
/* fold accumulator down to 16-bits */
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
	uint_fast32_t acc = csum;
	const uint8_t*restrict src = (const uint8_t*restrict)srcaddr;
	uint8_t*restrict dst = (uint8_t*restrict)dstaddr;
	uint16_t remainder = 0;
	uint_fast16_t count8;
	bool is_odd;

/* empty buffer */
	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
	is_odd = ((uintptr_t)srcbuf & 1);
/* align first byte */
	if (PGM_UNLIKELY(is_odd)) {
		((uint8_t*restrict)&remainder)[1] = *dst++ = *src++;
		len--;
	}
/* 8-byte unrolls, anything larger than 16-byte or less than 8 loses performance */
	count8 = len >> 3;
	while (count8--) {
		acc += ((uint16_t*restrict)dst)[ 0 ] = ((const uint16_t*restrict)src)[ 0 ];
		acc += ((uint16_t*restrict)dst)[ 1 ] = ((const uint16_t*restrict)src)[ 1 ];
		acc += ((uint16_t*restrict)dst)[ 2 ] = ((const uint16_t*restrict)src)[ 2 ];
		acc += ((uint16_t*restrict)dst)[ 3 ] = ((const uint16_t*restrict)src)[ 3 ];
		src += 8; dst += 8;
	}
	len %= 8;
/* final 7 bytes */
	while (len >= sizeof (uint16_t)) {
		const uint_fast16_t word16 = *(uint16_t*restrict)dst = *(const uint16_t*restrict)src;
		acc += word16;
		len -= 2; src += 2; dst += 2;
	}
/* trailing odd byte */
	if (len > 0) {
		((uint8_t*restrict)&remainder)[0] = *dst = *src;
	}
	acc += remainder;
/* fold accumulator down to 16-bits */
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
	uint_fast32_t acc = csum;
	const uint8_t* buf = (const uint8_t*)addr;
	uint16_t remainder = 0;
	uint_fast16_t count;
	bool is_odd;

/* empty buffer */
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
			const uint_fast16_t t = *(const uint16_t*)buf;
			acc += t;
			count--; len -= 2; buf += 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			while (count) {
				const uint_fast32_t word32 = *(const uint32_t*)buf;
				acc += word32;
/* detect carry without native adc operator */
				if (acc < word32) acc++;
				count--; buf += 4;
			}
			acc  = (acc >> 16) + (acc & 0xffff);
		}
		if (len & 2) {
			const uint_fast32_t t = *(const uint32_t*)buf;
			acc += t;
			buf += 2;
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*)&remainder)[0] = *buf;
	}
	acc += remainder;
/* fold accumulator down to 16-bits */
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
	uint_fast32_t acc = csum;
	const uint8_t*restrict src = (const uint8_t*restrict)srcaddr;
	uint8_t*restrict dst = (uint8_t*restrict)dstaddr;
	uint16_t remainder = 0;
	uint_fast16_t count;
	bool is_odd;

/* empty buffer */
	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
	is_odd = ((uintptr_t)src & 1);
/* align first byte */
	if (PGM_UNLIKELY(is_odd)) {
		((uint8_t*restrict)&remainder)[1] = *dst++ = *src++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((uintptr_t)src & 2) {
			const uint_fast16_t word16 = *(uint16_t*restrict)dst = *(const uint16_t*restrict)src;
			acc += word16;
			count--; len -= 2; src += 2; dst += 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			while (count) {
				const uint_fast32_t word32 = *(const uint32_t*restrict)src;
				*(uint32_t*restrict)dst = word32;
				acc += word32;
				if (acc < word32) acc++;
				count--; src += 4; dst += 4;
			}
			acc  = (acc >> 16) + (acc & 0xffff);
		}
		if (len & 2) {
			const uint_fast16_t word16 = *(uint16_t*restrict)dst = *(const uint16_t*restrict)src;
			acc += word16;
			src += 2; dst += 2;
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*restrict)&remainder)[0] = *dst = *src;
	}
	acc += remainder;
/* fold accumulator down to 16-bits */
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
	uint_fast64_t acc = csum;
	const uint8_t* buf = (const uint8_t*)addr;
	uint16_t remainder = 0;
	uint_fast16_t count;
	bool is_odd;

/* empty buffer */
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
			const uint_fast16_t word16 = *(const uint16_t*)buf;
			acc += word16;
			count--; len -= 2; buf += 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((uintptr_t)buf & 4) {
				const uint_fast32_t word32 = *(const uint32_t*)buf;
				acc += word32;
				count--; len -= 4; buf += 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
				while (count) {
					const uint_fast64_t word64 = *(const uint64_t*)buf;
					acc += word64;
					if (acc < word64) acc++;
					count--; buf += 8;
				}
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				const uint_fast32_t word32 = *(const uint32_t*)buf;
				acc += word32;
				buf += 4;
			}
		}
		if (len & 2) {
			const uint_fast16_t word16 = *(const uint16_t*)buf;
			acc += word16;
			buf += 2;
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*)&remainder)[0] = *buf;
	}
	acc += remainder;
/* fold accumulator down to 16-bits */
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
	uint_fast64_t acc = csum;
	const uint8_t* restrict src = (const uint8_t*restrict)srcaddr;
	uint8_t* restrict dstbuf = (uint8_t*restrict)dstaddr;
	uint16_t remainder = 0;
	uint_fast16_t count;
	bool is_odd;

/* empty buffer */
	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
	is_odd = ((uintptr_t)src & 1);
/* align first byte */
	if (PGM_UNLIKELY(is_odd)) {
		((uint8_t*restrict)&remainder)[1] = *dst++ = *src++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((uintptr_t)srcbuf & 2) {
			const uint_fast16_t word16 = *(uint16_t*restrict)dst = *(const uint16_t*restrict)src;
			acc += word16;
			count--; len -= 2; src += 2; dst += 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((uintptr_t)srcbuf & 4) {
				const uint_fast32_t word32 = *(uint32_t*restrict)dst = *(const uint32_t*restrict)src;
				acc += word32;
				count--; len -= 4; src += 4; dst += 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
/* 64-byte blocks */
				uint_fast16_t count64 = count >> 3;
				if (count64)
				{
					while (count64) {
						uint_fast64_t word64;
						word64 = ((uint64_t*restrict)dst)[ 0 ] = ((const uint64_t*restrict)src)[ 0 ];
						acc += word64;
						if (acc < word64) acc++;
						word64 = ((uint64_t*restrict)dst)[ 1 ] = ((const uint64_t*restrict)src)[ 1 ];
						acc += word64;
						if (acc < word64) acc++;
						word64 = ((uint64_t*restrict)dst)[ 2 ] = ((const uint64_t*restrict)src)[ 2 ];
						acc += word64;
						if (acc < word64) acc++;
						word64 = ((uint64_t*restrict)dst)[ 3 ] = ((const uint64_t*restrict)src)[ 3 ];
						acc += word64;
						if (acc < word64) acc++;
						word64 = ((uint64_t*restrict)dst)[ 4 ] = ((const uint64_t*restrict)src)[ 4 ];
						acc += word64;
						if (acc < word64) acc++;
						word64 = ((uint64_t*restrict)dst)[ 5 ] = ((const uint64_t*restrict)src)[ 5 ];
						acc += word64;
						if (acc < word64) acc++;
						word64 = ((uint64_t*restrict)dst)[ 6 ] = ((const uint64_t*restrict)src)[ 6 ];
						acc += word64;
						if (acc < word64) acc++;
						word64 = ((uint64_t*restrict)dst)[ 7 ] = ((const uint64_t*restrict)src)[ 7 ];
						acc += word64;
						if (acc < word64) acc++;
						count64--; src += 64; dst += 64;
					}
					acc  = (acc >> 32) + (acc & 0xffffffff);
					count %= 8;
				}

/* last 56 bytes */
				while (count) {
					const uint64_t word64 = *(uint64_t*restrict)dst = *(const uint64_t*restrict)src;
					acc += word64;
					if (acc < word64) acc++;
					count--; src += 8; dst += 8;
				}
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				const uint32_t word32 = *(uint32_t*restrict)dst = *(const uint32_t*restrict)src;
				acc += word32;
				src += 4; dst += 4;
			}
		}
		if (len & 2) {
			const uint16_t word16 = *(uint16_t*restrict)dst = *(const uint16_t*restrict)src;
			acc += word16;
			src += 2; dst += 2;
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*restrict)&remainder)[0] = *dstbuf = *srcbuf;
	}
	acc += remainder;
/* fold accumulator down to 16-bits */
	acc  = (acc >> 32) + (acc & 0xffffffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc  = (acc >> 16) + (acc & 0xffff);
	acc += (acc >> 16);
	if (PGM_UNLIKELY(is_odd))
		acc = ((acc & 0xff) << 8) | ((acc & 0xff00) >> 8);
	return (uint16_t)acc;
}

#if defined(__amd64) || defined(__x86_64__)
/* SIMD instructions unique to AMD/Intel 64-bit, so always little endian.
 *
 * TODO: TLB priming and prefetch with cache line size (128 bytes).
 * TODO: 8-byte software versus 16-byte hardware prefetch.
 */

static
uint16_t
do_csum_vector (
	const void*	addr,
	uint16_t	len,
	uint32_t	csum
	)
{
	uint64_t acc = csum;		/* fixed size for asm */
	const uint8_t* buf = (const uint8_t*)addr;
	uint16_t remainder = 0;		/* fixed size for endian swap */
	uint_fast16_t count;
	bool is_odd;

/* empty buffer */
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
			const uint16_t word16 = *(const uint16_t*)buf;
			acc += word16;
			count--; len -= 2; buf += 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((uintptr_t)buf & 4) {
				const uint32_t word32 = *(const uint32_t*)buf;
				acc += word32;
				count--; len -= 4; buf += 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
				while (count) {
					uint64_t carry = 0;
					__asm__ volatile ("addq %1, %0\n\t"
							  "adcq %2, %0"
					     		: "=r" (acc)
							: "m" (*(const uint64_t*)buf), "r" (carry), "0" (acc)
							: "cc"  );
					if (carry) acc++;
					count--; buf += 8;
				}
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				const uint32_t word32 = *(const uint32_t*)buf;
				acc += word32;
				buf += 4;
			}
		}
		if (len & 2) {
			const uint16_t word16 = *(const uint16_t*)buf;
			acc += word16;
			buf += 2;
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*)&remainder)[0] = *buf;
	}
	acc += remainder;
/* fold accumulator down to 16-bits */
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
	uint64_t acc = csum;		/* fixed size for asm */
	const uint8_t*restrict src = (const uint8_t*restrict)srcaddr;
	uint8_t*restrict dst = (uint8_t*restrict)dstaddr;
	uint16_t remainder = 0;		/* fixed size for endian swap */
	uint_fast16_t count;
	bool is_odd;

/* empty buffer */
	if (PGM_UNLIKELY(len == 0))
		return (uint16_t)acc;
/* fill cache line with source buffer, invalidate destination buffer,
 * perversly for testing high temporal locality is better than no locality,
 * whilst in production no locality may be preferred depending on skb re-use.
 */
/* align first byte */
	is_odd = ((uintptr_t)src & 1);
	if (PGM_UNLIKELY(is_odd)) {
		((uint8_t*restrict)&remainder)[1] = *dst++ = *src++;
		len--;
	}
/* 16-bit words */
	count = len >> 1;
	if (count)
	{
		if ((uintptr_t)srcbuf & 2) {
			const uint16_t word16 = *(uint16_t*restrict)dst = *(const uint16_t*restrict)src;
			acc += word16;
			count--; len -= 2; src += 2; dst += 2;
		}
/* 32-bit words */
		count >>= 1;
		if (count)
		{
			if ((uintptr_t)src & 4) {
				const uint32_t word32 = *(uint32_t*restrict)dst = *(const uint32_t*restrict)src;
				acc += word32;
				count--; len -= 4; src += 4; dst += 4;
			}
/* 64-bit words */
			count >>= 1;
			if (count)
			{
/* 64-byte blocks */
				uint_fast16_t count64 = count >> 3;

				while (count64)
				{
					uint64_t carry = 0;
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
							: "r" (src), "r" (dst), "r" (carry), "0" (acc)
							: "cc", "memory", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"  );
					if (carry) acc++;
					count64--; src += 64; dst += 64;
				}
				count %= 8;
/* last 56 bytes */
				while (count) {
					uint64_t carry = 0;
					__asm__ volatile ("addq %1, %0\n\t"
							  "adcq %2, %0"
							: "=r" (acc)
							: "m" (*(const uint64_t*restrict)src), "r" (carry), "0" (acc)
							: "cc"  );
					if (carry) acc++;
					count--; src += 8;
				}
				acc  = (acc >> 32) + (acc & 0xffffffff);
			}
			if (len & 4) {
				const uint32_t word32 = *(uint32_t*restrict)dst = *(const uint32_t*restrict)src;
				acc += word32;
				src += 4; dst += 4;
			}
		}
		if (len & 2) {
			const uint16_t word16 = *(uint16_t*restrict)dst = *(const uint16_t*restrict)src;
			acc += word16;
			src += 2; dst += 2;
		}
	}
/* trailing odd byte */
	if (len & 1) {
		((uint8_t*restrict)&remainder)[0] = *dst = *src;
	}
	acc += remainder;
/* fold accumulator down to 16-bits */
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

/* invert to get the ones-complement. */
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
