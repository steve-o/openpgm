/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * 32-bit atomic operations.  A complex mix of inline assembler and compiler
 * intrinsics.  Native x86 code uses fetch-and-add instruction which is proven
 * faster than Solaris intrinsics that all use compare-and-swap (CAS):
 * https://blogs.oracle.com/dave/entry/atomic_fetch_and_add_vs
 *
 * AMD Opteron revision E memory-barrier bug is ignored as obsolete hardware.
 * https://bugzilla.kernel.org/show_bug.cgi?id=11305
 *
 * NB: XADD requires 80486 microprocessor.
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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_ATOMIC_H__
#define __PGM_ATOMIC_H__

#if defined( __sun )
#	include <atomic.h>
#elif defined( _AIX ) && ( !defined( __GNUC__ ) ||  (__GNUC__ * 100 + __GNUC_MINOR__ < 401 ))
#	include <sys/atomic_op.h>
#elif defined( __APPLE__ )
#	include <libkern/OSAtomic.h>
#elif defined( __NetBSD__ )
#	include <sys/atomic.h>
#elif defined( _MSC_VER )
/* not implemented in MinGW */
#	include <intrin.h>
#endif
#include <pgm/types.h>


/* 32-bit word addition returning original atomic value.
 *
 * 	uint32_t tmp = *atomic;
 * 	*atomic += val;
 * 	return tmp;
 */

static inline
uint32_t
pgm_atomic_exchange_and_add32 (
	volatile uint32_t*	atomic,
	const uint32_t		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
/* GCC assembler */
	uint32_t result;
	__asm__ volatile ("lock; xaddl %0, %1"
		        : "=r" (result), "=m" (*atomic)
		        : "0" (val), "m" (*atomic)
		        : "memory", "cc"  );
	return result;
#elif (defined( __SUNPRO_C ) || defined( __SUNPRO_CC )) && (defined( __i386 ) || defined( __amd64 ))
/* GCC-compatible assembler */
	uint32_t result = val;
	__asm__ volatile ("lock; xaddl %0, %1"
		       :: "r" (result), "m" (*atomic)  );
	return result;
#elif defined( __sun ) || defined(__NetBSD__)
/* Solaris and NetBSD intrinsic */
	const uint32_t nv = atomic_add_32_nv (atomic, (int32_t)val);
	return nv - val;
#elif defined( __APPLE__ )
/* Darwin intrinsic */
	const uint32_t nv = OSAtomicAdd32Barrier ((int32_t)val, (volatile int32_t*)atomic);
	return nv - val;
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
/* GCC 4.0.1 intrinsic */
	return __sync_fetch_and_add (atomic, val);
#elif defined( _AIX )
	return fetch_and_add((atomic_p)atomic, val);
#elif defined( _WIN32 )
/* Windows intrinsic */
	return _InterlockedExchangeAdd ((volatile LONG*)atomic, val);
#else
#	error "No supported atomic operations for this platform."
#endif
}

/* 32-bit word addition.
 *
 * 	*atomic += val;
 */

static inline
void
pgm_atomic_add32 (
	volatile uint32_t*	atomic,
	const uint32_t		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ volatile ("lock; addl %1, %0"
		        : "=m" (*atomic)
		        : "ir" (val), "m" (*atomic)
		        : "memory", "cc"  );
#elif (defined( __SUNPRO_C ) || defined( __SUNPRO_CC )) && (defined( __i386 ) || defined( __amd64 ))
	__asm__ volatile ("lock; addl %1, %0"
		       :: "r" (val), "m" (*atomic)  );
#elif defined( __sun ) || defined( __NetBSD__ )
	atomic_add_32 (atomic, (int32_t)val);
#elif defined( __APPLE__ )
	OSAtomicAdd32Barrier ((int32_t)val, (volatile int32_t*)atomic);
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
/* interchangable with __sync_fetch_and_add () */
	__sync_add_and_fetch (atomic, val);
#elif defined( _AIX )
	fetch_and_add((atomic_p)atomic, val);
#elif defined( _WIN32 )
	_InterlockedExchangeAdd ((volatile LONG*)atomic, val);
#endif
}

/* 32-bit word increment.
 *
 * 	*atomic++;
 *
 * Per Intel's rule #33: 
 * http://www.intel.com/content/dam/doc/manual/64-ia-32-architectures-optimization-manual.pdf
 *
 * INC and DEC instructions should be replaced with ADD and SUB because false dependencies
 * are created on earlier instructions that set flags.
 */

static inline
void
pgm_atomic_inc32 (
	volatile uint32_t*	atomic
	)
{
#if defined( __GNUC__ ) && (defined( __i386__ ) || defined( __x86_64__ ))
	__asm__ volatile ("lock; addl $1, %0"
		        : "+m" (*atomic)
		        :
		        : "memory", "cc"  );
#elif (defined( __SUNPRO_C ) || defined( __SUNPRO_CC )) && (defined( __i386 ) || defined( __amd64 ))
	__asm__ volatile ("lock; addl $1, %0"
		       :: "m" (*atomic)  );
#elif defined( __sun ) || defined( __NetBSD__ )
	atomic_inc_32 (atomic);
#elif defined( __APPLE__ )
	OSAtomicIncrement32Barrier ((volatile int32_t*)atomic);
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
	pgm_atomic_add32 (atomic, 1);
#elif defined( _AIX )
	fetch_and_add((atomic_p)atomic, 1);
#elif defined( _WIN32 )
	_InterlockedIncrement ((volatile LONG*)atomic);
#endif
}

/* 32-bit word decrement.
 *
 * 	*atomic--;
 */

static inline
void
pgm_atomic_dec32 (
	volatile uint32_t*	atomic
	)
{
#if defined( __GNUC__ ) && (defined( __i386__ ) || defined( __x86_64__ ))
	__asm__ volatile ("lock; subl $1, %0"
		        : "+m" (*atomic)
		        :
		        : "memory", "cc"  );
#elif (defined( __SUNPRO_C ) || defined( __SUNPRO_CC )) && (defined( __i386 ) || defined( __amd64 ))
	__asm__ volatile ("lock; subl $1, %0"
		       :: "m" (*atomic)  );
#elif defined( __sun ) || defined( __NetBSD__ )
	atomic_dec_32 (atomic);
#elif defined( __APPLE__ )
	OSAtomicDecrement32Barrier ((volatile int32_t*)atomic);
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
	pgm_atomic_add32 (atomic, (uint32_t)-1);
#elif defined( _AIX )
	fetch_and_add((atomic_p)atomic, -1);
#elif defined( _WIN32 )
	_InterlockedDecrement ((volatile LONG*)atomic);
#endif
}

/* 32-bit word load 
 */

static inline
uint32_t
pgm_atomic_read32 (
	const volatile uint32_t* atomic
	)
{
	return *atomic;
}

/* 32-bit word store
 */

static inline
void
pgm_atomic_write32 (
	volatile uint32_t*	atomic,
	const uint32_t		val
	)
{
	*atomic = val;
}

#endif /* __PGM_ATOMIC_H__ */
