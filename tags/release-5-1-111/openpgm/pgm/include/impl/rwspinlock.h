/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * read-write spinlock.
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
#ifndef __PGM_IMPL_RWSPINLOCK_H__
#define __PGM_IMPL_RWSPINLOCK_H__

typedef struct pgm_rwspinlock_t pgm_rwspinlock_t;

#include <pgm/types.h>
#include <impl/ticket.h>

PGM_BEGIN_DECLS

struct pgm_rwspinlock_t {
	pgm_ticket_t		lock;
	volatile uint32_t	readers;
};


/* read-write lock */

static inline void pgm_rwspinlock_init (pgm_rwspinlock_t* rwspinlock) {
	pgm_ticket_init (&rwspinlock->lock);
	rwspinlock->readers = 0;
}

static inline void pgm_rwspinlock_free (pgm_rwspinlock_t* rwspinlock) {
	pgm_ticket_free (&rwspinlock->lock);
}

static inline void pgm_rwspinlock_reader_lock (pgm_rwspinlock_t* rwspinlock) {
	for (;;) {
#if defined( _WIN32 ) || defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
		unsigned spins = 0;
		while (!pgm_ticket_is_unlocked (&rwspinlock->lock))
			if (!pgm_smp_system || (++spins > PGM_ADAPTIVE_MUTEX_SPINCOUNT))
#	ifdef _WIN32
				SwitchToThread();
#	else
				sched_yield();
#	endif
			else		/* hyper-threading pause */
#	ifdef _MSC_VER
/* not implemented in Mingw */
				YieldProcessor();
#	else
				__asm volatile ("pause" ::: "memory");
#	endif
#else
		while (!pgm_ticket_is_unlocked (&rwspinlock->lock))
			sched_yield();
#endif
/* speculative lock */
		pgm_atomic_inc32 (&rwspinlock->readers);
		if (pgm_ticket_is_unlocked (&rwspinlock->lock))
			return;
		pgm_atomic_dec32 (&rwspinlock->readers);
	}
}

static inline bool pgm_rwspinlock_reader_trylock (pgm_rwspinlock_t* rwspinlock) {
	pgm_atomic_inc32 (&rwspinlock->readers);
	if (pgm_ticket_is_unlocked (&rwspinlock->lock))
		return TRUE;
	pgm_atomic_dec32 (&rwspinlock->readers);
	return FALSE;
}

static inline void pgm_rwspinlock_reader_unlock (pgm_rwspinlock_t* rwspinlock) {
	pgm_atomic_dec32 (&rwspinlock->readers);
}

static inline void pgm_rwspinlock_writer_lock (pgm_rwspinlock_t* rwspinlock) {
#if defined( _WIN32 ) || defined( __i386__ ) || defined( __i386 ) || defined( __x86_64__ ) || defined( __amd64 )
	unsigned spins = 0;
	pgm_ticket_lock (&rwspinlock->lock);
	while (rwspinlock->readers)
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
	pgm_ticket_lock (&rwspinlock->lock);
	while (rwspinlock->readers)
		sched_yield();
#endif
}

static inline bool pgm_rwspinlock_writer_trylock (pgm_rwspinlock_t* rwspinlock) {
	if (rwspinlock->readers)
		return FALSE;
	if (!pgm_ticket_trylock (&rwspinlock->lock))
		return FALSE;
	if (rwspinlock->readers) {
		pgm_ticket_unlock (&rwspinlock->lock);
		return FALSE;
	}
	return TRUE;
}

static inline void pgm_rwspinlock_writer_unlock (pgm_rwspinlock_t* rwspinlock) {
	pgm_ticket_unlock (&rwspinlock->lock);
}

PGM_END_DECLS

#endif /* __PGM_IMPL_RWSPINLOCK_H__ */
