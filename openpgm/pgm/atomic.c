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

#include <glib.h>

#include "pgm/atomic.h"

//#define ATOMIC_DEBUG

#ifndef ATOMIC_DEBUG
#define g_trace(...)		while (0)
#else
#define g_trace(...)		g_debug(__VA_ARGS__)
#endif


static GStaticMutex g_atomic_mutex = G_STATIC_MUTEX_INIT;


gint
g_atomic_int_exchange_and_add (
	volatile gint*		atomic,
	const gint		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	gint result;
	__asm__ __volatile__ ("lock; xaddl %0,%1"
			      : "=r" (result), "=m" (*atomic) 
			      : "0" (val), "m" (*atomic));
	return result;
#elif defined( __GNUC__ ) && defined( __sparc__ )
	volatile gint32 *_atomic asm("g1") = atomic;
	gint32 temp asm("o4");
	gint32 result asm("o5");
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
	gint result;
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
	gint result;
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
	unsigned long temp;
	gint result;
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
	const gint nv = atomic_add_32_nv (atomic, val);
	return = nv - val;
#elif defined(G_OS_WIN32)
	return InterlockedExchangeAdd (atomic, val);
#else
	gint result;
	g_mutex_lock (g_atomic_mutex);
	result = *atomic;
	*atomic += val;
	g_mutex_unlock (g_atomic_mutex);
	return result;
#endif
}

/* 32-bit integer ops.
 */
void
pgm_atomic_int32_add (
	volatile gint32*	atomic,
	const gint32		val
	)
{
#if defined( __GNUC__ ) && ( defined( __i386__ ) || defined( __x86_64__ ) )
	__asm__ __volatile__ ("lock; addl %1,%0"
			      : "=m" (*atomic)
			      : "ir" (val), "m" (*atomic));
#elif defined( __GNUC__ ) && defined( __sparc__ )
	volatile gint32 *_atomic asm("g1") = atomic;
	gint32 temp asm("o4");
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
	gint32 temp;
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
	gint32 temp;
	__asm__ __volatile__ ("1:  lwarx   %0,0,%3\n"
			      "    add     %0,%2,%0\n"
			      "    stwcx.  %0,0,%3\n"
			      "    bne-    1b\n"
			      : "=&r" (temp), "=m" (*atomic)
			      : "r" (val), "r" (atomic), "m" (*atomic)
			      : "cc");
#elif defined( __GNUC__ ) && defined( __arm__ )
	unsigned long temp, temp2;
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
	g_mutex_lock (g_atomic_mutex);
	*atomic += val;
	g_mutex_unlock (g_atomic_mutex);
#endif
}

gint32
pgm_atomic_int32_get (
	const volatile gint32*	atomic
	)
{
	return *atomic;
}

void
pgm_atomic_int32_set (
	volatile gint32*	atomic,
	const gint32		newval
	)
{
#if defined( __GNUC__ ) && defined( __arm__ )
	unsigned long temp;
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
	g_mutex_lock (g_atomic_mutex);
	*atomic = newval;
	g_mutex_unlock (g_atomic_mutex);
#endif
}

/* eof */
