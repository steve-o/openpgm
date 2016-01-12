/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Pre-processor macros for cross-platform, cross-compiler ice cream.
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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_MACROS_H__
#define __PGM_MACROS_H__

/* NULL, ptrdiff_t, and size_t
 */

#include <stddef.h>


/* GCC function attributes
 * http://gcc.gnu.org/onlinedocs/gcc/Function-Attributes.html
 */

#if !defined(__APPLE__) && !defined(_AIX) && ((__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ >= 96))

/* No side-effects except return value, may follow pointers and read globals */
#	define PGM_GNUC_PURE			__attribute__((__pure__))

/* Returns new memory like malloc() */
#	define PGM_GNUC_MALLOC			__attribute__((__malloc__))

#	define PGM_GNUC_CACHELINE_ALIGNED	__attribute__((__aligned__(SMP_CACHE_BYTES), \
						__section__((".data.cacheline_aligned")))
#	define PGM_GNUC_READ_MOSTLY		__attribute__((__section__(".data.read_mostly")))

#else
#	define PGM_GNUC_PURE
#	define PGM_GNUC_MALLOC
#	define PGM_GNUC_CACHELINE_ALIGNED
#	define PGM_GNUC_READ_MOSTLY
#endif

#if (__GNUC__ >= 4)

/* Variable argument function with NULL terminated list */
#	define PGM_GNUC_NULL_TERMINATED		__attribute__((__sentinel__))
#else
#	define PGM_GNUC_NULL_TERMINATED
#endif

#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)

/* malloc() with xth parameter being size */
#	define PGM_GNUC_ALLOC_SIZE(x)		__attribute__((__alloc_size__(x)))

/* malloc() with xth*yth parameters being size */
#	define PGM_GNUC_ALLOC_SIZE2(x,y)	__attribute__((__alloc_size__(x,y)))
#else
#	define PGM_GNUC_ALLOC_SIZE(x)
#	define PGM_GNUC_ALLOC_SIZE2(x,y)
#endif

#if (__GNUC__ > 2) || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)

/* printf() like function */
#	define PGM_GNUC_PRINTF(format, args)	__attribute__((__format__ (__printf__, format, args)))
#	define PGM_GNUC_SCANF(format, args)	__attribute__((__format__ (__scanf__, format, args)))
#	define PGM_GNUC_FORMAT(format)		__attribute__((__format_arg__ (format)))

/* Function will never return */
#	define PGM_GNUC_NORETURN		__attribute__((__noreturn__))

/* No side-effects except return value, must not follow pointers or read globals */
#	define PGM_GNUC_CONST			__attribute__((__const__))

/* Unused function */
#	define PGM_GNUC_UNUSED			__attribute__((__unused__))

#else /* !__GNUC__ */
#	define PGM_GNUC_PRINTF(format, args)
#	define PGM_GNUC_SCANF(format, args)
#	define PGM_GNUC_FORMAT(format)
#	define PGM_GNUC_NORETURN
#	define PGM_GNUC_CONST
#	define PGM_GNUC_UNUSED
#endif /* !__GNUC__ */

#if (__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)

/* Raise compiler warning if caller ignores return value */
#	define PGM_GNUC_WARN_UNUSED_RESULT	__attribute__((warn_unused_result))

#	ifdef HAVE_DSO_VISIBILITY
/* Hidden visibility */
#		define PGM_GNUC_INTERNAL		__attribute__((visibility("hidden")))
#	else
#		define PGM_GNUC_INTERNAL
#	endif
#else /* !__GNUC__ */
#	define PGM_GNUC_WARN_UNUSED_RESULT
#	if ((defined(__SUNPRO_C) && (__SUNPRO_C >= 0x550)) || (defined(__SUNPRO_CC) && (__SUNPRO_CC >= 0x550))) && defined(HAVE_DSO_VISIBILITY)
#		define PGM_GNUC_INTERNAL		__hidden
#	else
#		define PGM_GNUC_INTERNAL
#	endif
#endif /* __GNUC__ */


/* Compiler time assertions, must be on unique lines in the project */
#define PGM_PASTE_ARGS(identifier1,identifier2) identifier1 ## identifier2
#define PGM_PASTE(identifier1,identifier2)      PGM_PASTE_ARGS (identifier1, identifier2)
#define PGM_STATIC_ASSERT(expr) typedef struct { char compile_time_assertion[(expr) ? 1 : -1]; } PGM_PASTE (_pgm_static_assert_, __LINE__)

/* Function declaration wrappers for C++ */
#if defined(__cplusplus) && !defined(PGM_CPP_COMPILATION)
#	define PGM_BEGIN_DECLS  extern "C" {
#	define PGM_END_DECLS    }
#else
#	define PGM_BEGIN_DECLS
#	define PGM_END_DECLS
#endif

/* Surprisingly still not defined in C99 */
#ifndef	FALSE
#	define	FALSE	(0)
#endif

#ifndef	TRUE
#	define	TRUE	(!FALSE)
#endif

#undef	MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#undef	MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#undef	ABS
#define ABS(a)	   (((a) < 0) ? -(a) : (a))

#undef	CLAMP
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

/* Number of elements */
#define PGM_N_ELEMENTS(arr)		(sizeof (arr) / sizeof ((arr)[0]))

/* Structure offsets */
#if defined(__GNUC__)  && __GNUC__ >= 4
#	define PGM_OFFSETOF(struct_type, member)	(offsetof (struct_type, member))
#else
#	define PGM_OFFSETOF(struct_type, member)	((size_t)((char*)&((struct_type*) 0)->member))
#endif

/* Branch prediction hint */
#if defined(__GNUC__) && (__GNUC__ > 2) && defined(__OPTIMIZE__)
#	define PGM_LIKELY(expr)		__builtin_expect ((expr), 1)
#	define PGM_UNLIKELY(expr)	__builtin_expect ((expr), 0)
#else
#	define PGM_LIKELY(expr)		(expr)
#	define PGM_UNLIKELY(expr)	(expr)
#endif

#endif /* __PGM_MACROS_H__ */
