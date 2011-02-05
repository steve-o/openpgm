/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * Ticket spinlocks per Jonathan Corbet on LKML and Leslie Lamport's
 * Bakery algorithm.  Read-write version per David Howell on LKML derived
 * from Joseph Seigh at IBM.
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
typedef union pgm_rwticket_t pgm_rwticket_t;

#ifndef _WIN32
#	include <pthread.h>
#	include <unistd.h>
#else
#	define VC_EXTRALEAN
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif
#include <pgm/types.h>
#include <impl/thread.h>

PGM_BEGIN_DECLS

/* Byte alignment for packet memory maps.
 * NB: Solaris and OpenSolaris don't support #pragma pack(push) even on x86.
 */
#if defined( __GNUC__ ) && !defined( __sun )
#	pragma pack(push)
#endif
#pragma pack(1)

union pgm_ticket_t {
#if defined( _WIN32 )
	volatile LONG		pgm_tkt_data32;
	struct {
		volatile SHORT		pgm_un_ticket;
		volatile SHORT		pgm_un_user;
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


union pgm_rwticket_t {
#if defined( _WIN32 )
	volatile LONG		pgm_rwtkt_data32;
	struct {
		union {
			volatile SHORT		pgm_un2_data16;
			struct {
				volatile CHAR		pgm_un3_write;
				volatile CHAR		pgm_un3_read;
			} pgm_un3;
		} pgm_un2;
		volatile CHAR		pgm_un_user;
	} pgm_un;
#else
	volatile uint32_t	pgm_rwtkt_data32;
	struct {
		union {
			volatile uint16_t	pgm_un2_data16;
			struct {
				volatile uint8_t	pgm_un3_write;
				volatile uint8_t	pgm_un3_read;
			} pgm_un3;
		} pgm_un2;
		volatile uint8_t	pgm_un_user;
	} pgm_un;
#endif
};

#define pgm_rwtkt_data16	pgm_un.pgm_un2.pgm_un2_data16
#define pgm_rwtkt_write		pgm_un.pgm_un2.pgm_un3.pgm_un3_write
#define pgm_rwtkt_read		pgm_un.pgm_un2.pgm_un3.pgm_un3_read
#define pgm_rwtkt_user		pgm_un.pgm_un_user

#if defined( __GNUC__ ) && !defined( __sun )
#	pragma pack(pop)
#else
#	pragma pack()
#endif


static inline void pgm_ticket_init (pgm_ticket_t* ticket) {
	ticket->pgm_tkt_data32 = 0;
}

static inline void pgm_ticket_free (pgm_ticket_t* ticket) {
/* nop */
	(void)ticket;
}

static inline bool pgm_ticket_trylock (pgm_ticket_t* ticket) {
	const uint16_t user = ticket->pgm_tkt_user;
	pgm_ticket_t exchange, comparand;
	comparand.pgm_tkt_user = comparand.pgm_tkt_ticket = exchange.pgm_tkt_ticket = user;
	exchange.pgm_tkt_user = user + 1;
#ifdef _WIN32
	const LONG previous = _InterlockedCompareExchange (&ticket->pgm_tkt_data32, exchange.pgm_tkt_data32, comparand.pgm_tkt_data32);
#else   /* GCC atomics */
	const uint32_t previous = __sync_val_compare_and_swap (&ticket->pgm_tkt_data32, comparand.pgm_tkt_data32, exchange.pgm_tkt_data32);
#endif
	return (comparand.pgm_tkt_data32 == previous);
}

#ifdef _WIN32
static inline char _InterlockedExchangeAdd8 (volatile char* const Addend, const char Value) {
	char result;
	__asm volatile (  "lock\n\t"
			  "xaddb %0, %1"
			: "=r" (result), "=m" (*Addend)
			: "0" (Value), "m" (*Addend)
			: "memory", "cc"  );
	return result;
}

static inline short _InterlockedExchangeAdd16 (volatile short* const Addend, const short Value) {
	short result;
	__asm volatile (  "lock\n\t"
			  "xaddw %0, %1"
			: "=r" (result), "=m" (*Addend)
			: "0" (Value), "m" (*Addend)
			: "memory", "cc"  );
	return result;
}

static inline char _InterlockedIncrement8 (volatile char* const Addend) {
	return _InterlockedExchangeAdd8 (Addend, 1);
}
#endif

static inline void pgm_ticket_lock (pgm_ticket_t* ticket) {
#ifdef _WIN32
	const uint16_t user = _InterlockedIncrement16 (&ticket->pgm_tkt_user);
	unsigned spins = 0;
	while (ticket->pgm_tkt_ticket != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			SwitchToThread();
		else
			YieldProcessor();
#elif defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
/* GCC atomics */
	const uint16_t user = __sync_fetch_and_add (&ticket->pgm_tkt_user, 1);
	unsigned spins = 0;
	while (ticket->pgm_tkt_ticket != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			sched_yield();
		else
			__asm volatile ("pause" ::: "memory");	/* hyper-threading pause */
#else
	const uint16_t user = __sync_fetch_and_add (&ticket->pgm_tkt_user, 1);
	while (ticket->pgm_tkt_ticket != user)
		sched_yield();
#endif
}

static inline void pgm_ticket_unlock (pgm_ticket_t* ticket) {
#ifdef _WIN32
	_InterlockedIncrement16 (&ticket->pgm_tkt_ticket);
#else
/* interchangable with __sync_fetch_and_add () */
	__sync_add_and_fetch (&ticket->pgm_tkt_ticket, 1);
#endif
}


static inline void pgm_rwticket_init (pgm_rwticket_t* rwticket) {
	rwticket->pgm_rwtkt_data32 = 0;
}

static inline void pgm_rwticket_free (pgm_rwticket_t* rwticket) {
/* nop */
	(void)rwticket;
}

static inline void pgm_rwticket_reader_lock (pgm_rwticket_t* rwticket) {
#ifdef _WIN32
	const uint8_t user = _InterlockedIncrement8 (&rwticket->pgm_rwtkt_user);
	unsigned spins = 0;
	while (rwticket->pgm_rwtkt_read != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			SwitchToThread();
		else
			YieldProcessor();
	_InterlockedIncrement8 (&rwticket->pgm_rwtkt_read);
#elif defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
	const uint8_t user = __sync_fetch_and_add (&rwticket->pgm_rwtkt_user, 1);
	unsigned spins = 0;
	while (rwticket->pgm_rwtkt_read != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			sched_yield();
		else
			__asm volatile ("pause" ::: "memory");
	__sync_add_and_fetch (&rwticket->pgm_rwtkt_read, 1);
#else
	const uint8_t user = __sync_fetch_and_add (&rwticket->pgm_rwtkt_user, 1);
	while (rwticket->pgm_rwtkt_read != user)
		sched_yield();
	__sync_add_and_fetch (&rwticket->pgm_rwtkt_read, 1);
#endif
}

static inline bool pgm_rwticket_reader_trylock (pgm_rwticket_t* rwticket) {
	const uint8_t user = rwticket->pgm_rwtkt_user;
	pgm_rwticket_t exchange, comparand;
#ifdef _WIN32
	LONG previous;
#else
	uint32_t previous;
#endif
	exchange.pgm_rwtkt_write = comparand.pgm_rwtkt_write = rwticket->pgm_rwtkt_write;
	comparand.pgm_rwtkt_user = comparand.pgm_rwtkt_read = user;
	exchange.pgm_rwtkt_user = exchange.pgm_rwtkt_read = user + 1;
#ifdef _WIN32
	previous = _InterlockedCompareExchange (&rwticket->pgm_rwtkt_data32, exchange.pgm_rwtkt_data32, comparand.pgm_rwtkt_data32);
#else
	previous = __sync_val_compare_and_swap (&rwticket->pgm_rwtkt_data32, comparand.pgm_rwtkt_data32, exchange.pgm_rwtkt_data32);
#endif
	return (comparand.pgm_rwtkt_data32 == previous);
}

static inline void pgm_rwticket_reader_unlock(pgm_rwticket_t* rwticket) {
#ifdef _WIN32
	_InterlockedIncrement8 (&rwticket->pgm_rwtkt_write);
#else
	__sync_add_and_fetch (&rwticket->pgm_rwtkt_write, 1);
#endif
}

static inline void pgm_rwticket_writer_lock (pgm_rwticket_t* rwticket) {
#ifdef _WIN32
	const uint8_t user = _InterlockedIncrement8 (&rwticket->pgm_rwtkt_user);
	unsigned spins = 0;
	while (rwticket->pgm_rwtkt_write != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			SwitchToThread();
		else
			YieldProcessor();
#elif defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
	const uint8_t user = __sync_fetch_and_add (&rwticket->pgm_rwtkt_user, 1);
	unsigned spins = 0;
	while (rwticket->pgm_rwtkt_write != user)
		if (!pgm_smp_system || 0 == (++spins % 200))
			sched_yield();
		else
			__asm volatile ("pause" ::: "memory");
#else
	const uint8_t user = __sync_fetch_and_add (&rwticket->pgm_rwtkt_user, 1);
	while (rwticket->pgm_rwtkt_write != user)
		sched_yield();
#endif
}

static inline bool pgm_rwticket_writer_trylock (pgm_rwticket_t* rwticket) {
	const uint8_t user = rwticket->pgm_rwtkt_user;
	pgm_rwticket_t exchange, comparand;
#ifdef _WIN32
	LONG previous;
#else
	uint32_t previous;
#endif
	exchange.pgm_rwtkt_read = comparand.pgm_rwtkt_read = rwticket->pgm_rwtkt_read;
	exchange.pgm_rwtkt_write = comparand.pgm_rwtkt_user = comparand.pgm_rwtkt_write = user;
	exchange.pgm_rwtkt_user = user + 1;
#ifdef _WIN32
	previous = _InterlockedCompareExchange (&rwticket->pgm_rwtkt_data32, exchange.pgm_rwtkt_data32, comparand.pgm_rwtkt_data32);
#else
	previous = __sync_val_compare_and_swap (&rwticket->pgm_rwtkt_data32, comparand.pgm_rwtkt_data32, exchange.pgm_rwtkt_data32);
#endif
	return (comparand.pgm_rwtkt_data32 == previous);
}

/* requires 16-bit atomic read and write */

static inline void pgm_rwticket_writer_unlock (pgm_rwticket_t* rwticket) {
	pgm_rwticket_t t;
	t.pgm_rwtkt_data16 = rwticket->pgm_rwtkt_data16;
	t.pgm_rwtkt_write++;
	t.pgm_rwtkt_read++;
	rwticket->pgm_rwtkt_data16 = t.pgm_rwtkt_data16;
}

PGM_END_DECLS

#endif /* __PGM_IMPL_TICKET_H__ */
