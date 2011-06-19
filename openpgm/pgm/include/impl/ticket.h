/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * Ticket spinlocks per Jonathan Corbet on LKML and Leslie Lamport's
 * Bakery algorithm.
 *
 * NB: CMPXCHG requires 80486 microprocessor.
 *
 * Copyright (c) 2011 Miru Limited.
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
#ifndef __PGM_IMPL_TICKET_H__
#define __PGM_IMPL_TICKET_H__

typedef union pgm_ticket_t pgm_ticket_t;

#if defined( __sun )
#	include <atomic.h>
#elif defined( __APPLE__ )
#	include <libkern/OSAtomic.h>
#elif defined( _WIN32 )
#	define VC_EXTRALEAN
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	if defined( _MSC_VER )
/* not implemented in MinGW */
#		include <intrin.h>
#	endif
#else
#	include <pthread.h>
#	include <unistd.h>
#endif
#include <pgm/types.h>
#include <pgm/atomic.h>
#include <impl/thread.h>

PGM_BEGIN_DECLS

/* Byte alignment for CAS friendly unions.
 * NB: Solaris and OpenSolaris don't support #pragma pack(push) even on x86.
 */
#if defined( __GNUC__ ) && !defined( __sun ) && !defined( __CYGWIN__ )
#	pragma pack(push)
#endif
#pragma pack(1)

union pgm_ticket_t {
#if defined( _WIN64 )
	volatile uint64_t	pgm_tkt_data64;
	struct {
		volatile uint32_t	pgm_un_ticket;
		volatile uint32_t	pgm_un_user;
	} pgm_un;
#else
	volatile uint32_t	pgm_tkt_data32;
	struct {
		volatile uint16_t	pgm_un_ticket;
		volatile uint16_t	pgm_un_user;
	} pgm_un;
#endif
};

#define pgm_tkt_ticket	pgm_un.pgm_un_ticket
#define pgm_tkt_user	pgm_un.pgm_un_user

#if defined( __GNUC__ ) && !defined( __sun ) && !defined( __CYGWIN__ )
#	pragma pack(pop)
#else
#	pragma pack()
#endif

/* additional required atomic ops */

/* 32-bit word CAS, returns TRUE if swap occurred.
 *
 *	if (*atomic == oldval) {
 *		*atomic = newval;
 *		return TRUE;
 *	}
 *	return FALSE;
 *
 * Sun Studio on x86 GCC-compatible assembler not implemented.
 */

static inline
bool
pgm_atomic_compare_and_exchange32 (
	volatile uint32_t*	atomic,
	const uint32_t		newval,
	const uint32_t		oldval
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
/* GCC assembler */
	uint8_t result;
	__asm__ volatile ("lock; cmpxchgl %2, %0\n\t"
			  "setz %1\n\t"
			: "+m" (*atomic), "=q" (result)
			: "ir" (newval),  "a" (oldval)
			: "memory", "cc"  );
	return (bool)result;
#elif defined( __SUNPRO_C ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
/* GCC-compatible assembler */
	uint8_t result;
	__asm__ volatile ("lock; cmpxchgl %2, %0\n\t"
			  "setz %1\n\t"
			: "+m" (*atomic), "=q" (result)
			: "r" (newval),  "a" (oldval)
			: "memory", "cc"  );
	return (bool)result;
#elif defined( __sun )
/* Solaris intrinsic */
	const uint32_t original = atomic_cas_32 (atomic, oldval, newval);
	return (oldval == original);
#elif defined( __APPLE__ )
/* Darwin intrinsic */
	return OSAtomicCompareAndSwap32Barrier ((int32_t)oldval, (int32_t)newval, (volatile int32_t*)atomic);
#elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
/* GCC 4.0.1 intrinsic */
	return __sync_bool_compare_and_swap (atomic, oldval, newval);
#elif defined( _WIN32 )
/* Windows intrinsic */
	const uint32_t original = _InterlockedCompareExchange ((volatile LONG*)atomic, newval, oldval);
	return (oldval == original);
#endif
}

#if defined( _WIN64 )
/* returns TRUE if swap occurred
 */

static inline
bool
pgm_atomic_compare_and_exchange64 (
	volatile uint64_t*	atomic,
	const uint64_t		newval,
	const uint64_t		oldval
	)
{
/* Windows intrinsic */
	const uint64_t original = _InterlockedCompareExchange64 ((volatile LONGLONG*)atomic, newval, oldval);
	return (oldval == original);
}

/* returns original atomic value
 */

static inline
uint32_t
pgm_atomic_fetch_and_inc32 (
	volatile uint32_t*	atomic
	)
{
	const uint32_t nv = _InterlockedIncrement ((volatile LONG*)atomic);
	return nv - 1;
}

/* 64-bit word load
 */

static inline
uint64_t
pgm_atomic_read64 (
	const volatile uint64_t* atomic
	)
{
	return *atomic;
}

#else
/* 16-bit word addition.
 */

static inline
void
pgm_atomic_add16 (
	volatile uint16_t*	atomic,
	const uint16_t		val
	)
{
#	if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ volatile ("lock; addw %1, %0"
			: "=m" (*atomic)
			: "ir" (val), "m" (*atomic)
			: "memory", "cc"  );
#	elif defined( __SUNPRO_C ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ volatile ("lock; addw %0, %1"
			:
			: "r" (val), "m" (*atomic)
			: "memory", "cc"  );
#	elif defined( __sun )
	atomic_add_16 (atomic, val);
#	elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
/* interchangable with __sync_fetch_and_add () */
	__sync_add_and_fetch (atomic, val);
#	elif defined( __APPLE__ )
#		error "There is no OSAtomicAdd16Barrier() on Darwin."
#	elif defined( _WIN32 )
/* there is no _InterlockedExchangeAdd16() */
	_ReadWriteBarrier();
	__asm {
		mov ecx, atomic
		mov ax, val
		lock add ax, word ptr [ecx]
	}
	_ReadWriteBarrier();
#	endif
}

/* 16-bit word addition returning original atomic value.
 */

static inline
uint16_t
pgm_atomic_fetch_and_add16 (
	volatile uint16_t*	atomic,
	const uint16_t		val
	)
{
#	if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	uint16_t result;
	__asm__ volatile ("lock; xaddw %0, %1"
			: "=r" (result), "=m" (*atomic)
			: "0" (val), "m" (*atomic)
			: "memory", "cc"  );
	return result;
#	elif defined( __SUNPRO_C ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	uint16_t result = val;
	__asm__ volatile ("lock; xaddw %0, %1"
			: "+r" (result)
			: "m" (*atomic)
			: "memory", "cc"  );
	return result;
#	elif defined( __sun )
	const uint16_t nv = atomic_add_16_nv (atomic, val);
	return nv - val;
#	elif defined( __GNUC__ ) && ( __GNUC__ * 100 + __GNUC_MINOR__ >= 401 )
	return __sync_fetch_and_add (atomic, val);
#	elif defined( __APPLE__ )
#		error "There is no OSAtomicAdd16Barrier() on Darwin."
#	elif defined( _WIN32 )
/* there is no _InterlockedExchangeAdd16() */
	uint16_t result;
	_ReadWriteBarrier();
	__asm {
		mov ecx, atomic
		mov ax, val
		lock xadd word ptr [ecx], ax
		mov result, ax
	}
	_ReadWriteBarrier();
	return result;
#	endif
}

/* 16-bit word increment.
 */

static inline
void
pgm_atomic_inc16 (
	volatile uint16_t*	atomic
	)
{
#	if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ volatile ("lock; incw %0"
			: "+m" (*atomic)
			:
			: "memory", "cc"  );
#	elif defined( __SUNPRO_C ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ volatile ("lock; incw %0"
			: "+m" (*atomic)
			:
			: "memory", "cc"  );
#	elif defined( __sun )
	atomic_inc_16 (atomic);
#	elif defined( _WIN32 )
/* _InterlockedIncrement16() operates on 32-bit boundaries */
	_ReadWriteBarrier();
	__asm {
		mov ecx, atomic
		lock inc word ptr [ecx]
	}
	_ReadWriteBarrier();
#	else
/* there is no OSAtomicIncrement16Barrier() on Darwin. */
	pgm_atomic_add16 (atomic, 1);
#	endif
}

/* 16-bit word increment returning original atomic value.
 */

static inline
uint16_t
pgm_atomic_fetch_and_inc16 (
	volatile uint16_t*	atomic
	)
{
/* _InterlockedIncrement16() operates on 32-bit boundaries.
 * there is no OSAtomicIncrement16Barrier() on Darwin.
 * there is no xincw instruction on x86.
 */
	return pgm_atomic_fetch_and_add16 (atomic, 1);
}
#endif /* !_WIN64 */


/* ticket spinlocks */

static inline void pgm_ticket_init (pgm_ticket_t* ticket) {
#ifdef _WIN64
	ticket->pgm_tkt_data64 = 0;
#else
	ticket->pgm_tkt_data32 = 0;
#endif
}

static inline void pgm_ticket_free (pgm_ticket_t* ticket) {
/* nop */
	(void)ticket;
}

static inline bool pgm_ticket_trylock (pgm_ticket_t* ticket) {
#ifdef _WIN64
	const uint32_t user = ticket->pgm_tkt_user;
#else
	const uint16_t user = ticket->pgm_tkt_user;
#endif
	pgm_ticket_t exchange, comparand;
	comparand.pgm_tkt_user = comparand.pgm_tkt_ticket = exchange.pgm_tkt_ticket = user;
	exchange.pgm_tkt_user = user + 1;
#ifdef _WIN64
	return pgm_atomic_compare_and_exchange64 (&ticket->pgm_tkt_data64, exchange.pgm_tkt_data64, comparand.pgm_tkt_data64);
#else
	return pgm_atomic_compare_and_exchange32 (&ticket->pgm_tkt_data32, exchange.pgm_tkt_data32, comparand.pgm_tkt_data32);
#endif
}

static inline void pgm_ticket_lock (pgm_ticket_t* ticket) {
#ifdef _WIN64
	const uint32_t user = pgm_atomic_fetch_and_inc32 (&ticket->pgm_tkt_user);
#else
	const uint16_t user = pgm_atomic_fetch_and_inc16 (&ticket->pgm_tkt_user);
#endif
#if defined( _WIN32 ) || defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
	unsigned spins = 0;
	while (ticket->pgm_tkt_ticket != user)
		if (!pgm_smp_system || (++spins > PGM_ADAPTIVE_MUTEX_SPINCOUNT))
#	ifdef _WIN32
			SwitchToThread();
#	else
			sched_yield();
#	endif
		else		/* hyper-threading pause */
#	ifdef _MSC_VER
			YieldProcessor();
#	else
			__asm volatile ("pause" ::: "memory");
#	endif
#else
	while (ticket->pgm_tkt_ticket != user)
		sched_yield();
#endif
}

static inline void pgm_ticket_unlock (pgm_ticket_t* ticket) {
#ifdef _WIN64
	pgm_atomic_inc32 (&ticket->pgm_tkt_ticket);
#else
	pgm_atomic_inc16 (&ticket->pgm_tkt_ticket);
#endif
}

static inline bool pgm_ticket_is_unlocked (pgm_ticket_t* ticket) {
	pgm_ticket_t copy;
#ifdef _WIN64
	copy.pgm_tkt_data64 = pgm_atomic_read64 (&ticket->pgm_tkt_data64);
#else
	copy.pgm_tkt_data32 = pgm_atomic_read32 (&ticket->pgm_tkt_data32);
#endif
	return (copy.pgm_tkt_ticket == copy.pgm_tkt_user);
}

PGM_END_DECLS

#endif /* __PGM_IMPL_TICKET_H__ */
