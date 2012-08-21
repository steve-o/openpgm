/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * stdint.h for Win32 & Win64, but not IA64.
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
#ifndef __PGM_WININT_H__
#define __PGM_WININT_H__

/* Microsoft Visual Studio 2010 conflict error message */
#if defined(_MSC_VER) && (_MSC_VER >= 1600) && defined(_STDINT)
#	error "pgm/winint.h conflicts with stdint.h, define __PGM_WININT_H__ to prevent inclusion."
#endif

#include <limits.h>

/* 7.18.1.1  Exact-width integer types */
typedef signed   __int8		int8_t;
typedef unsigned __int8		uint8_t;
typedef signed   __int16	int16_t;
typedef unsigned __int16	uint16_t;
typedef signed   __int32	int32_t;
typedef unsigned __int32	uint32_t;
typedef signed   __int64	int64_t;
typedef unsigned __int64	uint64_t;

/* 7.18.1.2  Minimum-width integer types */
typedef int8_t			int_least8_t;
typedef uint8_t			uint_least8_t;
typedef int16_t			int_least16_t;
typedef uint16_t		uint_least16_t;
typedef int32_t			int_least32_t;
typedef uint32_t		uint_least32_t;
typedef int64_t			int_least64_t;
typedef uint64_t		uint_least64_t;

/* 7.18.1.3  Fastest minimum-width integer types 
 * Not actually guaranteed to be fastest for all purposes
 * Values match Microsoft Visual C++ 2010, not all equal bit sizes. 
 */
typedef int8_t			int_fast8_t;
typedef uint8_t			uint_fast8_t;
typedef int32_t			int_fast16_t;
typedef uint32_t		uint_fast16_t;
typedef int32_t			int_fast32_t;
typedef uint32_t		uint_fast32_t;
typedef int64_t			int_fast64_t;
typedef uint64_t		uint_fast64_t;

/* 7.18.1.5  Greatest-width integer types */
typedef int64_t			intmax_t;
typedef uint64_t		uintmax_t;

/* 7.18.2  Limits of specified-width integer types */
#if !defined ( __cplusplus) || defined (__STDC_LIMIT_MACROS)

/* 7.18.2.1  Limits of exact-width integer types */
#define INT8_MIN		((int8_t)_I8_MIN)
#define INT8_MAX		_I8_MAX
#define INT16_MIN		((int16_t)_I16_MIN)
#define INT16_MAX		_I16_MAX
#define INT32_MIN		((int32_t)_I32_MIN)
#define INT32_MAX		_I32_MAX
#define INT64_MIN		((int64_t)_I64_MIN)
#define INT64_MAX		_I64_MAX
#define UINT8_MAX		_UI8_MAX
#define UINT16_MAX		_UI16_MAX
#define UINT32_MAX		_UI32_MAX
#define UINT64_MAX		_UI64_MAX

/* 7.18.2.2  Limits of minimum-width integer types */
#define INT_LEAST8_MIN		INT8_MIN
#define INT_LEAST16_MIN		INT16_MIN
#define INT_LEAST32_MIN		INT32_MIN
#define INT_LEAST64_MIN		INT64_MIN

#define INT_LEAST8_MAX		INT8_MAX
#define INT_LEAST16_MAX		INT16_MAX
#define INT_LEAST32_MAX		INT32_MAX
#define INT_LEAST64_MAX		INT64_MAX

#define UINT_LEAST8_MAX		UINT8_MAX
#define UINT_LEAST16_MAX	UINT16_MAX
#define UINT_LEAST32_MAX	UINT32_MAX
#define UINT_LEAST64_MAX	UINT64_MAX

/* 7.18.2.3  Limits of fastest minimum-width integer types */
#define INT_FAST8_MIN		INT8_MIN
/* NB: Microsoft does not reflect the actual types data range for fast16 = 32-bit integer */
#define INT_FAST16_MIN		INT32_MIN
#define INT_FAST32_MIN		INT32_MIN
#define INT_FAST64_MIN		INT64_MIN

#define INT_FAST8_MAX		INT8_MAX
#define INT_FAST16_MAX		INT32_MAX
#define INT_FAST32_MAX		INT32_MAX
#define INT_FAST64_MAX		INT64_MAX

#define UINT_FAST8_MAX		UINT8_MAX
#define UINT_FAST16_MAX		UINT32_MAX
#define UINT_FAST32_MAX		UINT32_MAX
#define UINT_FAST64_MAX		UINT64_MAX

/* 7.18.2.4  Limits of integer types capable of holding
    object pointers */
#ifdef _WIN64
#	define INTPTR_MIN		INT64_MIN
#	define INTPTR_MAX		INT64_MAX
#	define UINTPTR_MAX		UINT64_MAX
#else
#	define INTPTR_MIN		INT32_MIN
#	define INTPTR_MAX		INT32_MAX
#	define UINTPTR_MAX		UINT32_MAX
#endif

/* 7.18.2.5  Limits of greatest-width integer types */
#define INTMAX_MIN		INT64_MIN
#define INTMAX_MAX		INT64_MAX
#define UINTMAX_MAX		UINT64_MAX

/* 7.18.3  Limits of other integer types */
#ifdef _WIN64
#	define PTRDIFF_MIN		INT64_MIN
#	define PTRDIFF_MAX		INT64_MAX
#else
#	define PTRDIFF_MIN		INT32_MIN
#	define PTRDIFF_MAX		INT32_MAX
#endif

#define SIG_ATOMIC_MIN		INT_MIN
#define SIG_ATOMIC_MAX		INT_MAX

#ifndef SIZE_MAX
#	ifdef _WIN64
#		define SIZE_MAX			UINT64_MAX
#	else
#		define SIZE_MAX			UINT32_MAX
#	endif
#endif

#ifndef WCHAR_MIN  /* also in wchar.h */
#	define WCHAR_MIN		0U
#	define WCHAR_MAX		UINT16_MAX
#endif

/*
 * wint_t is unsigned short for compatibility with MS runtime
 */
#define WINT_MIN		0U
#define WINT_MAX		UINT16_MAX

#endif /* !defined ( __cplusplus) || defined __STDC_LIMIT_MACROS */


/* 7.18.4  Macros for integer constants */
#if !defined ( __cplusplus) || defined (__STDC_CONSTANT_MACROS)

/* 7.18.4.1  Macros for minimum-width integer constants

    Accoding to Douglas Gwyn <gwyn@arl.mil>:
        "This spec was changed in ISO/IEC 9899:1999 TC1; in ISO/IEC
        9899:1999 as initially published, the expansion was required
        to be an integer constant of precisely matching type, which
        is impossible to accomplish for the shorter types on most
        platforms, because C99 provides no standard way to designate
        an integer constant with width less than that of type int.
        TC1 changed this to require just an integer constant
        *expression* with *promoted* type."

        The trick used here is from Clive D W Feather.
*/

#define INT8_C(val)		(INT_LEAST8_MAX-INT_LEAST8_MAX+(val))
#define INT16_C(val)		(INT_LEAST16_MAX-INT_LEAST16_MAX+(val))
#define INT32_C(val)		(INT_LEAST32_MAX-INT_LEAST32_MAX+(val))
/*  The 'trick' doesn't work in C89 for long long because, without
    suffix, (val) will be evaluated as int, not intmax_t */
#define INT64_C(val)		val##LL

#define UINT8_C(val)		(val)
#define UINT16_C(val)		(val)
#define UINT32_C(val)		(val##U)
#define UINT64_C(val)		val##ULL

/* 7.18.4.2  Macros for greatest-width integer constants */
#define INTMAX_C(val)		val##LL
#define UINTMAX_C(val)		val##ULL

#endif  /* !defined ( __cplusplus) || defined __STDC_CONSTANT_MACROS */

#endif /* __PGM_WININT_H__ */
