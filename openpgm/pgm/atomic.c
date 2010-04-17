/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * 32-bit atomic operations.
 *
 * Copyright (c) 2010 Miru Limited.
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

#include <pgm/framework.h>

//#define ATOMIC_DEBUG


/* globals */

static pgm_mutex_t atomic_mutex;
static volatile int32_t atomic_ref_count = 0;


void
pgm_atomic_init (void)
{
	if (pgm_atomic_int32_exchange_and_add (&atomic_ref_count, 1) > 0)
		return;

	pgm_mutex_init (&atomic_mutex);
}

void
pgm_atomic_shutdown (void)
{
	pgm_return_if_fail (pgm_atomic_int32_get (&atomic_ref_count) > 0);

	if (!pgm_atomic_int32_dec_and_test (&atomic_ref_count))
		return;

	pgm_mutex_free (&atomic_mutex);
}

int32_t
pgm_atomic_int32_exchange_and_add (
	volatile int32_t*	atomic,
	const int32_t		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	int32_t result;
	__asm__ __volatile__ ("lock; xaddl %0,%1"
			      : "=r" (result), "=m" (*atomic) 
			      : "0" (val), "m" (*atomic));
	return result;
#elif defined( __GNUC__ ) && defined( __sparc__ )
	volatile int32_t *_atomic asm("g1") = atomic;
	int32_t temp   asm("o4");
	int32_t result asm("o5");
	__asm__ __volatile__("1:  ld	[%%g1],%%o4\n"
			     "    add	%%o4,%3,%%o5\n"
			     "    cas	[%%g1],%%o4,%%o5\n"
			     "    cmp	%%o4,%%o5\n"
			     "    bne	1b\n"
			     "    add	%%o5,%3,%%o5"
			     : "=&r" (temp), "=&r" (result)
			     : "r" (_atomic), "r" (val)
			     : "memory", "cc");
	return result;
#elif defined( __GNUC__ ) && defined( __ppc__ )
	int32_t result;
	__asm__ __volatile__ ("1:  lwarx   %0,0,%2\n"
			      "    add     %0,%1,%0\n"
			      "    stwcx.  %0,0,%2\n"
			      "    bne-    1b"
#ifdef CONFIG_HAVE_PPC405_ERR77
			      "    dcbt    0,%2;"
#endif
			      "    stwcx.  %0,0,%2\n"
			      "    bne-    1b"
#ifdef CONFIG_HAVE_PPC_SMP
			      "\n  isync"
#endif
			      : "=&r" (result)
			      : "r" (val), "r" (atomic)
			      : "cc", "memory");
	return result;
#elif defined( __GNUC__ ) && defined( __ppc64__ )
	int32_t result;
	__asm__ __volatile__ (
#ifdef CONFIG_HAVE_PPC_SMP
			      "    eieio\n"
#endif
			      "1:  lwarx   %0,0,%2\n"
			      "    add     %0,%1,%0\n"
			      "    stwcx.  %0,0,%2\n"
			      "    bne-    1b"
#ifdef CONFIG_HAVE_PPC_SMP
			      "\n  isync"
#endif
			      : "=&r" (result)
			      : "r" (val), "r" (atomic)
			      : "cc", "memory");
	return result;
#elif defined( __GNUC__ ) && defined( __arm__ )
	int32_t temp, result;
	__asm__ __volatile__ ("@ atomic_add_return\n"
			      "1:  ldrex   %0,[%2]\n"
			      "    add     %0,%0,%3\n"
			      "    strex   %1,%0,[%2]\n"
			      "    teq     %1,#0\n"
			      "    bne     1b"
			      : "=&r" (result), "=&r" (temp)
			      : "r" (atomic), "Ir" (val)
			      : "cc");
	return result;
#elif defined( __GNUC__ )
	return __sync_fetch_and_add (atomic, val);
#elif defined( __SUNPRO_C ) && defined( __sparc__ )
	const int32_t nv = atomic_add_32_nv (atomic, val);
	return = nv - val;
#elif defined(G_OS_WIN32)
	return InterlockedExchangeAdd (atomic, val);
#else
#	error "native interlocked exchange required for module startup"
	int32_t result;
	pgm_mutex_lock (&g_atomic_mutex);
	result = *atomic;
	*atomic += val;
	pgm_mutex_unlock (&g_atomic_mutex);
	return result;
#endif
}

/* 32-bit integer ops.
 */
void
pgm_atomic_int32_add (
	volatile int32_t*	atomic,
	const int32_t		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ __volatile__ ("lock; addl %1,%0"
			      : "=m" (*atomic)
			      : "ir" (val), "m" (*atomic));
#elif defined( __GNUC__ ) && defined( __sparc__ )
	volatile int32_t *_atomic asm("g1") = atomic;
	int32_t temp asm("o4");
	__asm__ __volatile__("1:  ld	[%%g1],%%o4\n"
			     "    add	%%o4,%3,%%o5\n"
			     "    cas	[%%g1],%%o4,%%o5\n"
			     "    cmp	%%o4,%%o5\n"
			     "    bne	1b\n"
			     "    add	%%o5,%3,%%o5"
			     : "=&r" (temp)
			     : "r" (_atomic), "r" (val)
			     : "memory", "cc");
#elif defined( __GNUC__ ) && defined( __ppc__ )
	int32_t temp;
	__asm__ __volatile__ ("1:  lwarx   %0,0,%3\n"
			      "    add     %0,%2,%0\n"
#ifdef CONFIG_HAVE_PPC405_ERR77
			      "    dcbt    0,%3;"
#endif
			      "    stwcx.  %0,0,%3\n"
			      "    bne-    1b\n"
			      : "=&r" (temp), "=m" (*atomic)
			      : "r" (val), "r" (atomic), "m" (*atomic)
			      : "cc");
#elif defined( __GNUC__ ) && defined( __ppc64__ )
	int32_t temp;
	__asm__ __volatile__ ("1:  lwarx   %0,0,%3\n"
			      "    add     %0,%2,%0\n"
			      "    stwcx.  %0,0,%3\n"
			      "    bne-    1b\n"
			      : "=&r" (temp), "=m" (*atomic)
			      : "r" (val), "r" (atomic), "m" (*atomic)
			      : "cc");
#elif defined( __GNUC__ ) && defined( __arm__ )
	int32_t temp, temp2;
	__asm__ __volatile__ ("@ atomic_add\n"
			      "1:  ldrex   %0,[%2]\n"
			      "    add     %0,%0,%3\n"
			      "    strex   %1,%0,[%2]\n"
			      "    teq     %1,#0\n"
			      "    bne     1b"
			      : "=&r" (temp), "=&r" (temp2)
			      : "r" (atomic), "Ir" (i)
			      : "cc");
#elif defined( __GNUC__ )
	__sync_fetch_and_add (atomic, val);
#elif defined( __SUNPRO_C ) && defined( __sparc__ )
	atomic_add_32_nv (atomic, val);
#elif defined(G_OS_WIN32)
	InterlockedExchangeAdd (atomic, val);
#else
	pgm_mutex_lock (&g_atomic_mutex);
	*atomic += val;
	pgm_mutex_unlock (&g_atomic_mutex);
#endif
}

int32_t
pgm_atomic_int32_get (
	const volatile int32_t*	atomic
	)
{
	return *atomic;
}

void
pgm_atomic_int32_set (
	volatile int32_t*	atomic,
	const int32_t		newval
	)
{
#if defined( __GNUC__ ) && defined( __arm__ )
	int32_t temp;
	__asm__ __volatile__ ("@ atomic_set\n"
			      "1:  ldrex   %0,[%1]\n"
			      "    strex   %0,%2,[%1]\n"
			      "    teq     %0,#0\n"
			      "    bne     1b"
			      : "=&r" (temp)
			      : "r" (atomic), "r" (newval)
			      : "cc");
#elif defined( __GNUC__ )
	*atomic = newval;
#elif defined(G_OS_WIN32)
	InterlockedExchange (atomic, newval);
#else
	pgm_mutex_lock (&g_atomic_mutex);
	*atomic = newval;
	pgm_mutex_unlock (&g_atomic_mutex);
#endif
}

/* eof */
