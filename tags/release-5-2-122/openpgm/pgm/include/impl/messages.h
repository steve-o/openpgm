/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic message reporting.
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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_MESSAGES_H__
#define __PGM_IMPL_MESSAGES_H__

#ifdef _MSC_VER
#	include <pgm/wininttypes.h>
#else
#	include <inttypes.h>
#endif
#include <pgm/zinttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <pgm/types.h>
#include <pgm/messages.h>

PGM_BEGIN_DECLS

PGM_GNUC_INTERNAL void pgm__log  (const int, const char*, ...) PGM_GNUC_PRINTF (2, 3);
PGM_GNUC_INTERNAL void pgm__logv (const int, const char*, va_list) PGM_GNUC_PRINTF (2, 0);

#if defined( HAVE_ISO_VARARGS )

/* debug trace level only valid in debug mode */
#	ifdef PGM_DEBUG
#		define pgm_debug(...) \
			do { \
				if (pgm_min_log_level == PGM_LOG_LEVEL_DEBUG) \
					pgm__log (PGM_LOG_LEVEL_DEBUG, __VA_ARGS__); \
			} while (0)
#	else
#		define pgm_debug(...)	while (0)
#	endif /* !PGM_DEBUG */

#	define pgm_trace(r,...) \
			do { \
				if (pgm_min_log_level <= PGM_LOG_LEVEL_TRACE && pgm_log_mask & (r)) \
					pgm__log (PGM_LOG_LEVEL_TRACE, __VA_ARGS__); \
			} while (0)
#	define pgm_minor(...) \
			do { \
				if (pgm_min_log_level <= PGM_LOG_LEVEL_MINOR) \
					pgm__log (PGM_LOG_LEVEL_MINOR, __VA_ARGS__); \
			} while (0)
#	define pgm_info(...) \
			do { \
				if (pgm_min_log_level <= PGM_LOG_LEVEL_NORMAL) \
					pgm__log (PGM_LOG_LEVEL_NORMAL, __VA_ARGS__); \
			} while (0)
#	define pgm_warn(...) \
			do { \
				if (pgm_min_log_level <= PGM_LOG_LEVEL_WARNING) \
					pgm__log (PGM_LOG_LEVEL_WARNING, __VA_ARGS__); \
			} while (0)
#	define pgm_error(...) \
			do { \
				if (pgm_min_log_level <= PGM_LOG_LEVEL_ERROR) \
					pgm__log (PGM_LOG_LEVEL_ERROR, __VA_ARGS__); \
			} while (0)
#	define pgm_fatal(...) \
			do { \
				pgm__log (PGM_LOG_LEVEL_FATAL, __VA_ARGS__); \
			} while (0)

#elif defined( HAVE_GNUC_VARARGS )

#	ifdef PGM_DEBUG
#		define pgm_debug(f...) \
			do { \
				if (pgm_min_log_level == PGM_LOG_LEVEL_DEBUG) \
					pgm__log (PGM_LOG_LEVEL_DEBUG, f); \
			} while (0)
#	else
#		define pgm_debug(f...)	while (0)
#	endif /* !PGM_DEBUG */

#	define pgm_trace(r,f...)	if (pgm_min_log_level <= PGM_LOG_LEVEL_TRACE && pgm_log_mask & (r)) \
					pgm__log (PGM_LOG_LEVEL_TRACE, f)
#	define pgm_minor(f...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_MINOR) pgm__log (PGM_LOG_LEVEL_MINOR, f)
#	define pgm_info(f...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_NORMAL) pgm__log (PGM_LOG_LEVEL_NORMAL, f)
#	define pgm_warn(f...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_WARNING) pgm__log (PGM_LOG_LEVEL_WARNING, f)
#	define pgm_error(f...)		if (pgm_min_log_level <= PGM_LOG_LEVEL_ERROR) pgm__log (PGM_LOG_LEVEL_ERROR, f)
#	define pgm_fatal(f...)		pgm__log (PGM_LOG_LEVEL_FATAL, f)

#else   /* no varargs macros */

/* declare for GCC attributes */
static inline void pgm_debug (const char*, ...) PGM_GNUC_PRINTF (1, 2);
static inline void pgm_trace (const int, const char*, ...) PGM_GNUC_PRINTF (2, 3);
static inline void pgm_minor (const char*, ...) PGM_GNUC_PRINTF (1, 2);
static inline void pgm_info (const char*, ...) PGM_GNUC_PRINTF (1, 2);
static inline void pgm_warn (const char*, ...) PGM_GNUC_PRINTF (1, 2);
static inline void pgm_error (const char*, ...) PGM_GNUC_PRINTF (1, 2);
static inline void pgm_fatal (const char*, ...) PGM_GNUC_PRINTF (1, 2);

static inline void pgm_debug (const char* format, ...) {
	if (PGM_LOG_LEVEL_DEBUG == pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_DEBUG, format, args);
		va_end (args);
	}
}

static inline void pgm_trace (const int role, const char* format, ...) {
	if (PGM_LOG_LEVEL_TRACE >= pgm_min_log_level && pgm_log_mask & role) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_TRACE, format, args);
		va_end (args);
	}
}

static inline void pgm_minor (const char* format, ...) {
	if (PGM_LOG_LEVEL_MINOR >= pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_MINOR, format, args);
		va_end (args);
	}
}

static inline void pgm_info (const char* format, ...) {
	if (PGM_LOG_LEVEL_NORMAL >= pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_NORMAL, format, args);
		va_end (args);
	}
}

static inline void pgm_warn (const char* format, ...) {
	if (PGM_LOG_LEVEL_WARNING >= pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_WARNING, format, args);
		va_end (args);
	}
}

static inline void pgm_error (const char* format, ...) {
	if (PGM_LOG_LEVEL_ERROR >= pgm_min_log_level) {
		va_list args;
		va_start (args, format);
		pgm__logv (PGM_LOG_LEVEL_WARNING, format, args);
		va_end (args);
	}
}

static inline void pgm_fatal (const char* format, ...) {
	va_list args;
	va_start (args, format);
	pgm__logv (PGM_LOG_LEVEL_FATAL, format, args);
	va_end (args);
}

#endif /* varargs */

#define pgm_warn_if_reached() \
	do { \
		pgm_warn ("file %s: line %d (%s): code should not be reached", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__); \
	} while (0)
#define pgm_warn_if_fail(expr) \
	do { \
		if (PGM_LIKELY (expr)); \
		else \
			pgm_warn ("file %s: line %d (%s): runtime check failed: (%s)", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
	} while (0)


#ifdef PGM_DISABLE_ASSERT

#	define pgm_assert(expr)			while (0)
#	define pgm_assert_not_reached()		while (0)
#	define pgm_assert_cmpint(n1, cmp, n2)	while (0)
#	define pgm_assert_cmpuint(n1, cmp, n2)	while (0)

#elif defined(__GNUC__)

#	define pgm_assert(expr) \
	do { \
		if (PGM_LIKELY(expr)); \
		else { \
			pgm_fatal ("file %s: line %d (%s): assertion failed: (%s)", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
			abort (); \
		} \
	} while (0)
#	define pgm_assert_not_reached() \
	do { \
		pgm_fatal ("file %s: line %d (%s): should not be reached", \
			__FILE__, __LINE__, __PRETTY_FUNCTION__); \
		abort (); \
	} while (0)
#	define pgm_assert_cmpint(n1, cmp, n2) \
	do { \
		const int64_t _n1 = (n1), _n2 = (n2); \
		if (PGM_LIKELY(_n1 cmp _n2)); \
		else { \
			pgm_fatal ("file %s: line %d (%s): assertion failed (%s): (%" PRIi64 " %s %" PRIi64 ")", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #n1 " " #cmp " " #n2, _n1, #cmp, _n2); \
			abort (); \
		} \
	} while (0)
#	define pgm_assert_cmpuint(n1, cmp, n2) \
	do { \
		const uint64_t _n1 = (n1), _n2 = (n2); \
		if (PGM_LIKELY(_n1 cmp _n2)); \
		else { \
			pgm_fatal ("file %s: line %d (%s): assertion failed (%s): (%" PRIu64 " %s %" PRIu64 ")", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #n1 " " #cmp " " #n2, _n1, #cmp, _n2); \
			abort (); \
		} \
	} while (0)

#else

#	define pgm_assert(expr) \
	do { \
		if (PGM_LIKELY(expr)); \
		else { \
			pgm_fatal ("file %s: line %d: assertion failed: (%s)", \
				__FILE__, __LINE__, #expr); \
			abort (); \
		} \
	} while (0)
#	define pgm_assert_not_reached() \
	do { \
		pgm_fatal ("file %s: line %d: assertion failed: (%s)", \
			__FILE__, __LINE__); \
		abort (); \
	} while (0)
#	define pgm_assert_cmpint(n1, cmp, n2) \
	do { \
		const int64_t _n1 = (n1), _n2 = (n2); \
		if (PGM_LIKELY(_n1 cmp _n2)); \
		else { \
			pgm_fatal ("file %s: line %d: assertion failed (%s): (%" PRIi64 " %s %" PRIi64 ")", \
				__FILE__, __LINE__, #n1 " " #cmp " " #n2, _n1, #cmp, _n2); \
			abort (); \
		} \
	} while (0)
#	define pgm_assert_cmpuint(n1, cmp, n2) \
	do { \
		const uint64_t _n1 = (n1), _n2 = (n2); \
		if (PGM_LIKELY(_n1 cmp _n2)); \
		else { \
			pgm_fatal ("file %s: line %d: assertion failed (%s): (%" PRIu64 " %s %" PRIu64 ")", \
				__FILE__, __LINE__, #n1 " " #cmp " " #n2, _n1, #cmp, _n2); \
			abort (); \
		} \
	} while (0)

#endif /* !PGM_DISABLE_ASSERT */

#ifdef PGM_DISABLE_CHECKS

#	define pgm_return_if_fail(expr)			while (0)
#	define pgm_return_val_if_fail(expr, val)	while (0)
#	define pgm_return_if_reached()			return
#	define pgm_return_val_if_reached(val)		return (val)

#elif defined(__GNUC__)

#	define pgm_return_if_fail(expr)	\
	do { \
		if (PGM_LIKELY(expr)); \
		else { \
			pgm_warn ("file %s: line %d (%s): assertion `%s' failed", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
			return; \
		} \
	} while (0)
#	define pgm_return_val_if_fail(expr, val) \
	do { \
		if (PGM_LIKELY(expr)); \
		else { \
			pgm_warn ("file %s: line %d (%s): assertion `%s' failed", \
				__FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
			return (val); \
		} \
	} while (0)
#	define pgm_return_if_reached() \
	do { \
		pgm_warn ("file %s: line %d (%s): should not be reached", \
			__FILE__, __LINE__, __PRETTY_FUNCTION__); \
		return; \
	} while (0)
#	define pgm_return_val_if_reached(val) \
	do { \
		pgm_warn ("file %s: line %d (%s): should not be reached", \
			__FILE__, __LINE__, __PRETTY_FUNCTION__); \
		return (val); \
	} while (0)

#else

#	define pgm_return_if_fail(expr)	\
	do { \
		if (PGM_LIKELY(expr)); \
		else { \
			pgm_warn ("file %s: line %d: assertion `%s' failed", \
				__FILE__, __LINE__, #expr); \
			return; \
		} \
	} while (0)
#	define pgm_return_val_if_fail(expr, val) \
	do { \
		if (PGM_LIKELY(expr)); \
		else { \
			pgm_warn ("file %s: line %d: assertion `%s' failed", \
				__FILE__, __LINE__, #expr); \
			return (val); \
		} \
	} while (0)
#	define pgm_return_if_reached() \
	do { \
		pgm_warn ("file %s: line %d): should not be reached", \
			__FILE__, __LINE__); \
		return; \
	} while (0)
#	define pgm_return_val_if_reached(val) \
	do { \
		pgm_warn ("file %s: line %d: should not be reached", \
			__FILE__, __LINE__); \
		return (val); \
	} while (0)

#endif /* !PGM_DISABLE_CHECKS */

PGM_END_DECLS

#endif /* __PGM_IMPL_MESSAGES_H__ */
